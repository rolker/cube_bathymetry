# ADR-0001: Bound long-duration memory and output — lossless tile eviction + incremental publish

## Status

Accepted.

This is the first project-level ADR for `cube_bathymetry`. Numbering restarts at
`0001` per the workspace ADR convention (workspace ADRs and project ADRs are
independent per-repo sequences; see workspace
[ADR-0001](https://github.com/rolker/ros2_agent_workspace/blob/main/docs/decisions/0001-adopt-architecture-decision-records.md)).
In particular this is **not** workspace ADR-0008 ("Follow ROS 2 official
conventions"), which the implementation still honors.

## Context

Two unbounded-growth paths survived the `MapSheet` → `GeoMapSheet` migration
(#21) and caused a real deployment incident — udp_bridge saturation during the
2026-06-10 outing:

- **Problem 1 — RAM grows forever.** `GeoMapSheet::grids_` is a
  `std::map<gggs::GridIndex, std::shared_ptr<GeoGrid>>` that only ever inserts.
  Each resident GGGS grid is a fixed 960×960 ≈ 920k cells. Resident RAM scales
  with total surveyed area for the life of the process; a long survey (or a
  node primed from a large draft store) grows without bound.
- **Problem 2 — published message grows forever.** `publishGrid()` projected
  **every** resident grid into a single monolithic `grid_map_msgs/GridMap` on
  the `grid` topic every ~5 s. Message size and per-cycle projection cost scale
  with surveyed area, so a downlink fine early in a survey saturates later.

`#69` (single-fused-grid store API) is merged (PR#71), so this work targets the
post-#69 layout.

### The data-integrity constraint that shaped the eviction design

Bounding RAM by evicting cold tiles must **never lose survey data** — the
resident grids are the work product (operator directive, 2026-06-26). Two
non-obvious facts make a naive "drop the cold tile" unsafe even if the tile was
saved first:

1. `tile_io::saveTile()` **overwrites** the whole tile file with the grid's
   current finite cells; there is no read-merge. A tile that is evicted (saved
   complete), then revisited and re-accumulated **sparsely**, would on its next
   save overwrite the on-disk file with only the new cells — wiping the
   previously-surveyed cells.
2. The existing reload path (`setPredictedDepthAt`, used by the startup prime)
   seeds only CUBE's slope-correction **prior**, which does **not** round-trip
   through `GeoGrid::values()` (`extractDepthAndUncertainty` returns NaN for a
   node with no settled hypothesis). So a reloaded-but-not-resurveyed cell emits
   NaN and is dropped on the next save.

Fact 1+2 together mean the same partial-revisit data loss already exists
latently **across sessions** in #21's persistence, independent of #70. Eviction
would make it acute (every intra-session evict→revisit), so #70 fixes the root.

## Decision

### 1. Lossless reload of settled CUBE state

Add `Node::seedSettledDepth(depth, uncertainty, params)` — it creates **one**
hypothesis representing a previously-settled cell value:

- `current_estimate = depth`, `number_of_samples = 1`.
- `input_sample_variance = current_variance = predicted_variance =
  (uncertainty / stddev_to_confidence_interval_scale)²`.

This makes the reloaded value **round-trip exactly** through
`extractDepthAndUncertainty` (so it re-emits from `values()` and persists on the
next save), and serves as a **Bayesian prior** that subsequent soundings refine
through the West-Harrison DLM (`Hypothesis::update`). That is the cross-epoch
self-improvement already established as intended in the cube#15 design — not
synthetic-sample pollution, because the seed variance is the stored posterior,
not a fabricated confidence.

**`number_of_samples = 1` (deliberate):** the reloaded value is a single prior
observation; consistent new pings accrete onto the same hypothesis and become
authoritative quickly, and a genuinely changed seabed is followed promptly.
The original sample count is not persisted (the store holds depth / uncertainty
/ timestamp / source only), so a "stickier" higher count would be a fabricated
value — rejected.

Surfaced up the stack as `GeoGrid::setSettledDepthAt` and
`GeoMapSheet::setSettledDepthAt` (mirroring the existing `setPredictedDepthAt`).
Reload does **not** mark the grid dirty — it reproduces already-persisted data;
the next *survey* ping marks it dirty and triggers a complete (reloaded + new)
re-save.

**Reload is applied in both places that recreate a grid from disk:**
- **Startup prime** (`loadIntoSheet` / `primeFromTile`): seeds the settled
  hypothesis (new) **and** keeps seeding the slope-correction prior
  (`setPredictedDepthAt`, unchanged) so existing blunder/slope behavior is
  preserved. This closes the latent cross-session partial-revisit loss.
- **Evicted-tile revisit** (below): the node reloads the single persisted tile
  from disk before inserting the revisiting ping.

Because CUBE state is always complete after reload, the store write stays a
simple whole-tile overwrite — no cell-wise merge-on-save is needed.

### 2. LRU tile eviction, persist-then-drop, persistence-required

`GeoMapSheet` tracks a per-grid last-touch sequence number (bumped on create and
on every sounding insert). It exposes `coldTiles(max_resident)` (the coldest
indices beyond budget, LRU order), `dropTile(index)`, `residentTileCount()`, and
`lastTouchOf(index)`. The **node** orchestrates eviction on the maintenance tick:

- New ROS parameter **`max_resident_tiles`** (default 64) sets the budget.
- Eviction is **enabled only when draft persistence is configured**
  (`draft_dir` non-empty). For each cold tile beyond budget: if it is dirty,
  save it first (complete state — lossless because CUBE state is complete);
  then `dropTile()` and record the index as evicted.
- **Without `draft_dir`, the node does NOT evict** — it leaves the data resident
  and logs a throttled WARN that RAM is unbounded without a configured store.
  Losing data to honor a RAM budget is not an acceptable trade; a deployment
  that needs bounding must configure persistence. (This is the explicit
  resolution to the earlier "evict-anyway-with-data-loss" proposal, which was
  rejected.)

On revisit of an evicted tile, the node loads just that tile from disk
(`loadWindow` over the tile's bounds → a scratch store) and reseeds it via the
settled-depth reload path before the revisiting ping is inserted, so
accumulation continues from the saved state and the eventual re-save is
complete.

### 3. Bounded vessel-centered CA grid replaces the monolithic publish

The `grid` topic keeps its name, type (`grid_map_msgs/GridMap`), frame (`map`),
layers (`elevation`, `uncertainty`), and resolution — only its **extent**
changes from whole-survey to a bounded window:

- New ROS parameter **`ca_window_radius_m`** (default 200.0). `publishCaGrid()`
  looks up the vessel position (`earth` ← `base_link`), selects the resident
  grids whose footprint lies within the radius, and projects only those.
- **Unknown cells stay safe:** cells with no data inside the window are NaN, and
  Nav2 treats them as lethal under the existing `unsurveyed_is_lethal` default —
  the window-boundary safety guarantee is unchanged.
- **Coherence constraint:** the resident budget must be at least the tile span
  of the CA window, else a window tile could be evicted and render as a
  NaN/lethal hole inside the avoidance window (conservative, but it degrades the
  live view). The node computes the window's tile span at `on_configure` and
  WARNs if `max_resident_tiles` is smaller. The default pair (64 tiles, 200 m)
  satisfies the constraint with margin.
- **TF-gap fallback:** if the vessel transform is unavailable, `publishCaGrid()`
  falls back to projecting the full resident set (already bounded by eviction)
  rather than dropping the CA grid — a TF outage must not starve collision
  avoidance.

### 4. Incremental per-tile stream on `~/tiles`

A new `LifecyclePublisher<grid_map_msgs/GridMap>` on **`~/tiles`** (BEST_EFFORT
QoS) emits one message per changed tile per publish cycle, driven by a
publish-dirty set tracked **separately** from the save-dirty set so the two
cadences (publish ~5 s, save `save_interval` s) do not interfere and the set is
cleared by its own consumer regardless of persistence. This is the bounded,
incremental replacement for the monolithic whole-survey emission and the
**interface contract** for the deferred boat→CAMP live-coverage view
(unh_marine_autonomy#86, #250). No in-tree consumer reads `~/tiles` yet;
reconstructing a full-survey display from the stream (e.g. in RViz) is out of
scope here (follow-up).

**Updated 2026-08-25** — "one message per changed tile" is now only the
default. See the *Dirty sub-window publishing* addendum below: the quantized
`~/coverage_tiles` stream can send just the changed cells of a tile
(`publish_dirty_subwindow`, off by default), and the publish-dirty set has
gained per-tile cell bounds to make that possible.

### 5. `clear_grid` service removed (superseded by automatic bounding)

`clear_grid` was the *manual* mitigation for the two problems this ADR now solves
automatically: it reset the whole accumulator to shed RAM and to shrink an
over-large telemetry publish. With resident RAM bounded by eviction and the `grid`
publish bounded to the vessel-centered CA window, that rationale is gone. The
service had **no callers** anywhere in the workspace (no launch file, config, or
other package referenced it), and a lifecycle restart
(deactivate→cleanup→configure) still provides a full reset if one is ever needed.
Keeping it would also leave an inconsistent reset surface — it cleared RAM but not
the on-disk draft, so a post-reset revisit would clobber on-disk tiles piecemeal
(the very partial-revisit loss this ADR otherwise closes). It is therefore
**removed** (service, `clearGrid()`/`clearGridService()`, and the `std_srvs`
dependency) rather than retained.

## Consequences

**Positive:**
- Resident RAM is bounded by `max_resident_tiles`, independent of surveyed area
  or session length, with **no data loss** — evicted tiles are persisted and
  reloaded losslessly on revisit.
- The latent cross-session partial-revisit data loss in #21's persistence is
  closed by the same settled-depth reload mechanism.
- The `grid` (CA) message is bounded by `ca_window_radius_m`, fixing the
  udp_bridge saturation without changing the topic contract.
- `~/tiles` establishes the incremental coverage interface for boat→CAMP.

**Negative / trade-offs:**
- Eviction requires persistence: a node run without `draft_dir` does not bound
  RAM (it warns). Acceptable — the alternative is data loss.
- Reload restores the settled depth/uncertainty as a single-sample prior, not
  the full multi-hypothesis / pre-filter / backscatter-sample state (out of
  scope; backscatter is re-derivable per cube#15). A revisited cell continues
  from the persisted best estimate, which is the intended behavior.
  **(Updated by cube#92/#93 — see ADR-0007 § Phase B.2 addendum: the corrected
  backscatter Welford `(n, mean, M2)` IS now spilled and restored losslessly on
  eviction rather than dropped/re-derived; raw per-beam samples are no longer
  kept, so a richer correction requires re-importing the bag.)**
- A misconfiguration where `max_resident_tiles` < CA-window tile span degrades
  the live view (over-lethal holes). Mitigated by the on_configure WARN and a
  default pair chosen to satisfy the constraint.

## References

- Issue: https://github.com/rolker/cube_bathymetry/issues/70
- Builds on #21 (GeoMapSheet migration, durable draft tiles) and #69
  (single-fused-grid store API).
- Reload model aligns with cube#15 (cross-epoch self-improvement).
- Consumer of the deferred `~/tiles` contract: rolker/unh_marine_autonomy#86,
  rolker/unh_marine_autonomy#250.

## Addendum (cube_bathymetry#96) — two-rung seed precedence, `seed_settled`, batch-regen, live-node → survey

unh_marine_autonomy#248 simplified the store taxonomy (`draft`/`processed`/`chart`
→ `survey`/`reference`) and the backscatter cell (3-band Welford sufficient
statistic). This addendum records the CUBE-side decisions #96 layered on top; the
backscatter-format decisions live in the ADR-0007 addendum.

### Live node writes the `survey` layer

The live `cube_bathymetry_node` now persists directly to the **`survey`** layer
(the old `draft` layer is gone; #248 collapsed draft+processed into one). The
boat-side product is bounded/approximate (the eviction-uncertainty artifact noted
in § Negative trade-offs); the off-boat **batch-regen** rebuild overwrites it as
the authoritative surface. The `draft_dir` ROS parameter name is retained (external
interface stability) but its tiles land under `<draft_dir>/survey/`.
Operator-confirmed.

### Two-rung seed precedence + the `seed_settled` contract

The startup-prime (#21) and revisit-reload (#70) paths generalize into a single
**per-tile, first-touch seed precedence** in `ImportAccumulator::seedNewTile`
(reused by the batch-regen gather). When a tile is first touched:

1. **survey** — a `survey/` bathy tile already on disk (a pre-existing store, or a
   tile written earlier this run) is restored with `seed_settled=true`: settled
   depth as a CUBE hypothesis (round-trips through `values()`, refines under new
   soundings) **and** its per-cell backscatter Welford reconstructed from the
   `survey/` backscatter tile (`welfordFromCell`, ADR-0007 addendum). Measured data.
2. **reference** — else a `reference/` prior tile is primed with
   `seed_settled=false`: predicted-surface only (turns the blunder-rejection gate
   on) but **never settled**, so it produces no `values()` output, seeds no
   backscatter, and is **not counted as measured data**. A reference tile at the
   survey level primes cell-for-cell; a **coarser** reference tile (a multi-level
   prior — `loadWindow` returns tiles at any level, keyed by their own level, so the
   same-level lookup misses them) is handled by a level-walk fallback (#115): the
   finest coarser tile containing this survey tile is resampled (nearest-neighbour on
   cell center) onto the fine survey cells, and the fallback level is logged for
   auditability. Trade-off: the widened gate exposes tiles previously ungated
   (reference data only at non-survey levels) to the coarse/shallow-biased-prior
   false-reject mode — legitimate deeper-than-charted returns can be rejected;
   the margin is tunable via the `blunder_*` params. Enforced by
   `test_import_eviction.ReferenceSeedDoesNotAddMeasuredData` (gate-only, not settled)
   and `test_import_eviction.CoarseLevelReferenceSeedRejectsDeepBlunder` (the
   cross-level fallback gates a deep blunder).
3. else **blank**.

`seed_settled` is thus the contract boundary between *measured* (survey: settled +
backscatter) and *prior-only* (reference: gate-only). This replaces the pre-#96
upfront whole-store `loadIntoSheet` of a `--prior` chart, which loaded the entire
prior into the sheet at once and defeated bounded-RAM eviction; the importer flag is
now `--reference-store` and priming is lazy, per tile.

### Batch-regen — the exact rebuild path

The eviction reload restores a *single reseeded hypothesis*, so a tile evicted
mid-disambiguation has a faithful depth **value** but a slightly re-derived depth
**uncertainty** (the § Negative trade-off above). **batch-regen** (`batch_regen_bag`
/ `BatchRegen`) removes even that artifact: it scatters every sounding to a per-tile
on-disk bucket (the batch's influence-radius-expanded window, matching
`addSoundings`' spread — wording updated by cube_bathymetry#104, which fixed the
selection margin from one cell to the soundings' actual influence radius), then
gathers each tile in a single **unbounded** pass over the complete set
of soundings that touch it — so no tile is ever evicted mid-disambiguation and the
output is **bit-exact** vs a whole-survey-in-RAM build (depth, uncertainty, and the
3-band backscatter). RAM is bounded by one tile's soundings, not surveyed area.
This is the authoritative off-boat product; the bounded-RAM `import_bag` remains the
live/streaming path. Enforced by `test_batch_regen` (byte-exact vs a single-pass
unbounded `ImportAccumulator`).

## Addendum — Anti-entropy phase 2: catalog + serving from disk (#106, 2026-07-23)

The original design served TileRequests from RAM-resident grids only and built
the catalog from the resident set, with from-disk catch-up deferred as a
follow-up. That deferral broke the anti-entropy design's paired invariant
(uma ADR-0008 D4): the consumer converges its cache to *exactly the catalog
set* (prune-on-absence), which is only safe when the catalog is complete
against everything the source can serve. With a resident-only catalog, any
survey exceeding `max_resident_tiles` silently dropped evicted coverage from
the catalog, and the consumer's gen-time-gated prune then **deleted** it from
the operator cache — the operator view eroded toward the boat's resident
window (prime suspect for the 2026-07-21 stuck-tiles field symptom, #104).

Phase 2 restores the pair:

1. **The catalog reflects the store, not RAM.** `publishCatalog()` builds from
   the `TileCatalogBuilder` registry, which is seeded from the **whole**
   persisted store at startup (before the prime trim — seeding after the trim
   lost the just-evicted tiles) and bumped on every push. Eviction never
   removes a registry entry: an evicted tile remains advertised because it
   remains servable.

2. **Requests for evicted tiles are served from disk, read-only.**
   `tileRequestCallback()` queues non-resident requested tiles;
   `drainDiskServeQueue()` loads each into a **scratch** store/sheet
   (`loadWindow` → `primeFromTile`), quantizes, publishes, and discards.
   `reloadEvictedTile()` is deliberately NOT reused here: it inserts into the
   live `geo_map_sheet_`, which is correct for the sounding-revisit path but
   would let a bulk catch-up churn the LRU and evict the tiles the survey is
   actively updating. The two paths must stay separate.

3. **Catch-up is paced, bounded, and dedup'd.** The drain queue serves
   `disk_serve_tiles_per_tick` (default 4) per `disk_serve_interval` (default
   0.5 s) — a catch-up throughput knob tuned against the link budget (the
   operator bridge rate-limits `coverage_tiles` to ~2/s) and deliberately
   independent of `catalog_interval`. Queue depth is capped
   (`disk_serve_queue_max_depth`, default 64); overflow is dropped and heals
   via the consumer's next catalog reconcile. Pending work is cleared on
   deactivate (requests are only accepted while ACTIVE).

Without a `draft_dir` nothing changes: no eviction happens, the registry
equals the pushed set, and the drain timer is never created.

Enforced by `test_anti_entropy_disk_serve`: full-catalog-across-eviction,
disk-serve with zero live-sheet churn, cold-consumer convergence, and a
negative control proving a resident-only catalog prunes a warm consumer's
valid coverage. Field validation against the #104 symptom (07-21 bag replay +
cold-CAMP catch-up) is tracked on #104.

## Addendum — Dirty sub-window publishing (`publish_dirty_subwindow`, 2026-08-25)

Section 4 above describes the incremental stream as **per-tile**: the
publish-dirty *set* bounds which tiles are sent, and each one is sent whole.
That is still what happens by default, and it is not enough.

### The measurement

On the boat, `~/coverage_tiles` carries level-11 tiles of 960x960 cells with
three bands — `depth` INT16 plus `uncertainty` and `backscatter` UINT8, so
4 bytes per cell:

| | cells | payload |
|---|---|---|
| whole tile | 921,600 | **3,686,400 B** |

The operator link's whole budget is 1,500,000 B/s, so **one tile message is
2.46x the entire per-second budget** and occupies ~2.5 s of the link at full
cap. At the measured 0.26 tiles/s that is ~958 kB/s of PAYLOAD. Rate-limiting cannot
help: 0.26/s is already slow. The problem is message **size**.

> **Correction (2026-08-26, addendum 2).** The "64% of the budget" this
> paragraph originally drew from that 958 kB/s was wrong, and wrong in the
> direction that matters. udp_bridge zlib-compresses every packet before
> fragmenting it (`packet.cpp`), and a coverage tile is mostly nodata, which
> compresses to almost nothing — so payload bytes are not what the link
> carries. Measured on both 2026-08-25 Appledore bags through this exact
> chain, the whole-tile stream costs **56.0 kB/s** on transit and **85.7 kB/s**
> on station: 4-6% of the budget, not 64%. The replay reproduces the boat's own
> live figure (997 kB/s payload against 962,899 B/s recorded on gabby at
> 08:17), so it is measuring the real stream.
>
> The case for this work is unaffected, but it is a DIFFERENT case, and the
> right one is already in this section: **message size, not mean rate**.
> `Connection::send`'s `can_send()` admits a message only when the WHOLE thing
> fits the instantaneous budget, so the largest messages are exactly the ones a
> congested link stops carrying — coverage_tiles measured 100% dropped on
> 2026-08-05 while /tf and mavros telemetry still got through at ~55%. The
> measured peak whole-tile message is **1,120,845 B compressed**, three
> quarters of the entire per-second budget in one message; sub-windowed, the
> peak is **172,040 B**.

`quantize_tile.cpp` hardcoded `window_col = window_row = 0`,
`window_width = cols`, `window_height = rows`. The wire contract
(`SonarVisualizationTile`, uma#230) has said since it was designed that bands
carry only the dirty sub-window and the consumer patches it in at the offset —
the producer side was simply never built.

### Decision

**1. Track dirty cell bounds, not just the dirty flag.** `GeoGrid::insert`
already evaluates per cell whether the CUBE node accepted the sounding; that
per-cell result now expands a `CellBox` (inclusive min/max row and column)
instead of collapsing straight into the per-grid bool.
`GeoMapSheet::clearPublishDirtyGrids()` resets the boxes of exactly the tiles
it clears, so the tile set and the cell bounds cannot drift. The save cadence
does not touch them, and the seed/reload accessors — which already carry a
no-dirty-mark contract — do not expand them.

A bounding **box**, not a per-cell mask: four `uint16` per tile against a
960x960 bitset, O(1) on the profiler-hot insert path (#63/#107), and the wire
carries a rectangular window anyway, so a mask could not be transmitted without
changing the message. The cost is over-coverage — a diagonal or two-lobed
change set sends unchanged cells inside its hull, degrading to the whole tile
in the worst case, which is exactly today's behaviour and never worse.

`GeoGrid` is the only place that can do this: `GeoMapSheet` sees one bool per
grid, and re-deriving a box from the sounding bounds would duplicate the
influence-radius math the two deliberately share (#104).

**2. `quantizeTileWindow(grid, stamp, window)` packs just that box.**
`width`/`height` still carry the full tile size — the contract requires it —
while the window fields and each band's array cover exactly the box, row-major.
`quantizeTile()` is now that function called with `CellBox::wholeTile()`, so the
full-tile path and the patch path share one implementation and the default
output is byte-for-byte what it was.

Two consequences worth stating:

- The backscatter **auto-range is scoped to the window**. That is what makes
  the whole-tile case byte-identical, and it gives a narrow patch better
  dynamic range — but successive patches to one tile can then carry different
  `scale`/`offset`. A consumer must dequantize on receipt
  (`value = raw*scale + offset`, uma ADR-0008 D1) and store physical values;
  one that stored raw counts against a single per-tile scale would mis-render
  older cells. `depth` and `uncertainty` are fixed-scale and unaffected.
- A window with no finite-depth cell returns `nullopt` rather than an
  all-nodata patch, which would blank cells the consumer already holds.

**3. The heal paths stay whole-tile.** `tileRequestCallback()` and
`drainDiskServeQueue()` are unchanged. They exist to repair a consumer that has
diverged, and a patch cannot repair divergence.

**4. It is off by default.** `publish_dirty_subwindow` (bool, default `false`,
read at configure) gates the whole thing; `false` reproduces the existing
stream exactly. Enabling it logs a WARN.

> **Superseded by addendum 2 (2026-08-26).** The default is now `true`, and the
> consumer prerequisite this section's caution rests on is met at the PRODUCER
> rather than waited for at the consumer. See below.

### What it saves

Cells at ~1 m, publish cadence >= 5 s, boat ~2 m/s, ~40 m swath, plus a
two-cell influence margin each side. Payload only, per tile message:

| case | box | payload | vs full |
|---|---|---|---|
| one cycle, line along a tile axis | 14 x 44 | 2,464 B | 0.07% (1,500x) |
| one cycle, line at 45 deg | 41 x 41 | 6,724 B | 0.18% (550x) |
| a whole survey line crossing the tile in one cycle | 44 x 960 | 168,960 B | 4.6% (22x) |
| a diagonal line crossing the tile corner to corner in one cycle | 960 x 960 | 3,686,400 B | 100% (no saving) |

At the measured 0.26 tiles/s the first row is ~641 B/s against ~958 kB/s today
— coverage stops being the link's dominant cost. The last row is the honest
worst case and the reason the box representation is called out above: the
saving is a function of track geometry, not a constant. `publishDirtyTiles()`
therefore logs the cells actually sent against the whole-tile equivalent
(throttled, 30 s) so the enable/disable decision can be made from a measured
ratio on the survey in hand.

### Why opt-in, and what enabling it requires

The patch-application semantics live in the consumer. **CAMP (camp#121) is not
in this workspace and its implementation could not be verified here** —
uma `docs/sonar_ecosystem.md` records camp#121 / PR camp#139 as merged, but a
docs claim about an out-of-tree repo is not evidence of shipped behaviour, and
a consumer that ignored the window fields would paint a patch across the whole
tile and corrupt the operator's coverage view mid-survey.

The in-tree second consumer, `marine_web_view`'s `coverage_renderer` (uma#345),
*does* implement the patch path — `_on_tile` decodes with the window
dimensions, dequantizes per message, and applies at `(window_row,
window_col)` — and it exposes the second half of the prerequisite:

> Only a whole-tile message advances possession; a partial one updates the
> pixels and leaves the catalog free to re-serve the tile in full.

That is deliberate and correct — it is what stops a lost best-effort patch from
becoming a permanent hole the catalog never re-requests — but it means that
against *that* consumer, enabling `publish_dirty_subwindow` makes every patched
tile stay in `reconcile()`'s `to_request` list and be re-served **in full**
anyway, costing more than leaving the parameter off.

So enabling this requires a consumer that both (a) applies the window and (b)
accounts for patch possession (a per-tile hole map, or an explicit
whole-tile refresh cadence). Until such a consumer is confirmed on the operator
side, `false` is the correct value. The catalog/TileRequest path remains what
recovers a consumer that gets it wrong.

> **Superseded by addendum 2 (2026-08-26).** This paragraph named the answer
> without noticing it did not have to be the consumer's: "an explicit
> whole-tile refresh cadence" is now implemented at the PRODUCER, where it
> works for every consumer at once and waits for none of them.

Enforced by `test_geo_grid` (box accumulation, reset, re-accumulation, and that
rejected soundings and the seed/reload paths never dirty a cell),
`test_geo_map_sheet` (box and set clear together; a save does not consume the
publish bounds), and `test_quantize_tile` (window packing order and offsets,
band length always `window_width * window_height` per dtype across six window
shapes, the wire extent bound, whole-tile equals the full-tile output
byte for byte, clamping, and the empty/no-depth/outside-tile `nullopt` cases),
and `test_coverage_refresh` (the refresh policy: the interval boundary, that
continuous patching cannot postpone its own heal, that a tile's first message
is always whole, the quiet-tile drain, the per-cycle bound and oldest-first
ordering, the backlog signal, and both disable paths).

## Addendum 2 (2026-08-26): the whole-tile refresh queue, and on by default

Addendum 1 left `publish_dirty_subwindow` off, waiting for a consumer that
"accounts for patch possession". Reviewing that decision against BOTH in-tree
consumers showed the wait was unnecessary and, worse, that the gate as written
was the wrong shape: the two consumers fail in OPPOSITE directions, so no
single consumer-side rule satisfies both, and enabling on the strength of one
of them ships a defect to the other.

### Why the consumer gate could not work

| consumer | possession rule | what a bumped-on-patch catalog did to it |
|---|---|---|
| CAMP (`SonarLiveTile`) | advances from ANY message, patch included | held version matched the catalog, so it never re-requested — a dropped patch became a **permanent invisible hole** while anti-entropy reported convergence |
| `marine_web_view` (`coverage_renderer`) | advances only from a WHOLE tile | every patched tile stayed in `reconcile()`'s request list and was re-served in full, unthrottled, every catalog round — **more traffic than the whole-tile stream** sub-windowing replaces |

Addendum 1 saw the second row and treated it as a cost ("costing more than
leaving the parameter off"). It is worse than that: at a 5 s catalog cadence
and a 5 s request interval, with `tileRequestCallback` serving resident tiles
immediately and with no rate limit, it is a sustained re-request loop on the
link this work exists to protect.

### The decision

**Two producer-side changes, and the consumer gate is dropped.**

1. **The catalog version is bumped only on a whole send.** It recovers its
   documented meaning — "at this version you hold the WHOLE tile" — which is
   also what `TileCatalogEntry.msg` has always said it means. The web view's
   held version now equals the catalog and it stops asking; CAMP's runs ahead
   and it does not ask either; and a consumer that MISSES a whole-tile refresh
   falls behind the catalog, so the documented catalog/TileRequest heal fires
   for both.
2. **Every patched tile is re-sent whole within `subwindow_refresh_interval`
   (default 60 s), budget permitting.** This is addendum 1's own "explicit
   whole-tile refresh cadence", moved to the producer, where it works for every
   consumer at once and waits for none of them. It runs on a WALL timer, not
   the ping path: the tiles most needing a heal are the ones the vessel has
   finished with, whose pings have stopped.

`publish_dirty_subwindow` therefore defaults to **`true`**. `false` still
reproduces the previous whole-tile stream byte for byte.

### What it costs, measured

Both 2026-08-25 Appledore bags replayed through this chain with
`import_bag --tile-size-report`, compressed with the same zlib call udp_bridge
makes:

| | transit | on station |
|---|---|---|
| whole-tile stream | 56.0 kB/s | 85.7 kB/s |
| patches + 60 s refresh | 11.2 kB/s | 9.1 kB/s |
| saving | **80%** | **89%** |

Against a 1,500,000 B/s connection shared with telemetry, costmap, video and
TF. Peak single message falls from 1,120,845 B to 172,040 B. A 300 s interval
costs 7.9 and 3.5 kB/s; 30 s costs 15.2 and 16.1.

Note these are the COMPRESSED figures, and that the cell-count ratios in
addendum 1's "What it saves" table are not the same number — see the
correction above. Transit saves less than station work because each tile gets
one fresh diagonal strip, which is the geometry addendum 1 correctly named as
the worst case.

### What is deliberately NOT claimed

- **The heal latency is a target under load, not a bound.** The drain clears at
  most `subwindow_refresh_tiles_per_cycle` tiles per 5 s tick — 24 per minute
  at the defaults. A turn that quiets more tiles than that at once heals them
  oldest-first over longer than the interval. A throttled WARN says so; the
  drain's own saturation ceiling (~73 kB/s) exceeds the 56.0 kB/s stream it
  replaces, so sustained saturation is worse than not sub-windowing at all.
- **A tile evicted from RAM while owing a refresh cannot be healed** and is
  dropped from the debt with a WARN. Rare by construction — a just-patched tile
  is the warmest thing in the sheet and eviction takes the coldest — and the
  durable fix is to serve the refresh from the draft store the way a
  `TileRequest` already is (#106).
- **Fixed-size chunking, which is what #112 actually asks for, is still not
  done.** This bounds the TYPICAL message, not the worst one: the measured
  sub-window peak of 172,040 B is an observation, not a guarantee, and a
  diagonal line's bounding box still degrades toward the full tile. #112 stays
  open on its own terms.

