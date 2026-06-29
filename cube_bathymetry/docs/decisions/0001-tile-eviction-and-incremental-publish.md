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
