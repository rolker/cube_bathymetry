# Plan: import_bag writes one resolution per run — depth-adaptive store levels need the estimation grid decoupled from the store tiling

## Issue

https://github.com/rolker/cube_bathymetry/issues/143

## Context

`marine_bathymetry_store::depthAdaptiveLevel(depth_m)` merged in
[unh_marine_autonomy#372](https://github.com/rolker/unh_marine_autonomy/pull/372)
(policy: requested cell = `0.05 * |depth|`, no floor, clamped to levels 8..14).
**Nothing calls it.** `import_bag` is the writer that must.

Verified in this checkout:

- `src/import_bag_main.cpp:1027` builds **one** `cube::GeoMapSheet(resolution, iho_order)`
  per run; `:1129-1130` pins `accumulator_config.cell_size_m` to
  `geo_map_sheet.nominalCellSizeMeters()`. CUBE's estimation grid and the store's
  native level are therefore the same single value by construction.
- `src/batch_regen_main.cpp:825-827` does the identical pinning for the
  scatter/gather rebuild path.
- The **read side is already mixed-level**: `BathymetryStore::importTiles` documents
  "Tiles may be at heterogeneous levels (multi-level, ADR-0002 §D2)";
  `gggs::GridIndex` carries its own level, so a tile map is level-agnostic;
  `build_depth_overviews` **discovers** the layer's native levels and folds
  native-wins toward the apex (uma#331), with no `--fine-level` precisely because
  "asserting a single one is meaningless for a mixed-level layer".
- `gggs` is a strict quadtree with free functions `gggs::parent(GridIndex)` and
  `gggs::children(GridIndex)` (`marine_autonomy/gggs/index_math.h`), so grids nest
  exactly across levels.
- `BatchRegen` already abstracts sheet construction behind a
  `SheetFactory = std::function<std::unique_ptr<GeoMapSheet>()>` and builds one
  fresh sheet per tile bucket — most of the decoupling machinery already exists.
- **`build_fingerprint.h/cpp` does not exist yet.** ADR-0003 is Accepted but
  unimplemented (`grep build_fingerprint` over `src/`, `include/`, `test/` is
  empty; `batch_regen_main.cpp` has no `--incremental` path). So the ADR-0003
  consequence is a **document amendment before first implementation**, not a
  schema migration of live files. `dirtyL10Tiles()` (ADR-0002, #111 PR1) *does*
  exist and already takes a `store_level` parameter.

## The unit of decision — decided here (issue scope bullet 1)

**Decision: a GGGS quadtree cut. The unit is the emitted tile itself, resolved
top-down from the policy's coarsest level; the decision input is the shallowest
depth within the candidate tile.** `depthAdaptiveLevel(double depth_m)` keeps its
current scalar signature, unchanged — the "unit" question is answered by the
caller's descent, not by the policy function. This matches the signature's existing
documented contract ("by contract the **shallowest** depth in the tile").

Algorithm (`levelPlanFor(min_depth_by_grid, policy)`):

1. Recon builds `shallowest[g]` for every **level-14 grid** `g` the survey touches
   (~54 m of ground; the finest decision granularity).
2. Roll up to level 8 by repeated `gggs::parent()`, taking the min — a shallowest-
   depth pyramid.
3. Descend from each touched level-8 grid: at grid `g` at level `L`, let
   `implied = depthAdaptiveLevel(shallowest[g])`. If `implied <= L`, emit `g` as a
   **leaf at level L**. Else recurse into `gggs::children(g)` at `L+1`. Terminate at
   `L == policy.finest_level`.
4. A grid with no soundings is **skipped, not emitted** — never passed to
   `depthAdaptiveLevel` (its shallowest is NaN, which the policy function throws on
   by design; the header's `@note` names skipping as the expected caller behaviour).

The result is a set of pairwise-disjoint leaves covering the surveyed ground, with
no ancestor/descendant pair — the property every downstream consumer needs.

**Why this and not the alternatives:**

| Candidate | Rejected because |
|---|---|
| Per-tile at a fixed level | Circular: the tile's identity depends on the level being chosen. |
| Per-region at a fixed decision level | To guarantee that every emitted tile nests wholly inside one region, the decision grid must be at `coarsest_level` (8) — a **3.48 km** region. Shallowest-in-region is deliberately shoal-biased, so a single 2 m shoal would drag 3.48 km of ground to level 14 (see storage below: ~256x the level-10 cost). A finer decision grid removes the nesting guarantee, letting one level-8 or level-9 tile straddle regions that chose different levels. |
| Two-pass over the whole survey choosing one global level | Reduces to today's behaviour with extra machinery. |

The quadtree cut is the per-region scheme with the region size chosen *per region*:
it keeps the exact-nesting guarantee (a leaf never straddles a level boundary,
because leaves come from a quadtree cut) while confining a shoal's refinement to
the ~54 m grids that actually contain it.

**Storage consequence, which is why this matters.** Against the real Shoals
`processed` layer at `~/data/world/depths/processed`: 69 native level-10 tiles,
167 MB of `.tif` (2.42 MB/tile observed; 14.7 MB/tile dense — the
`960*960*2*8` figure uma#376 uses). Each level step is 4x for the same ground, so
level 10 -> 14 is **256x**. If the whole Shoals footprint went to level 14 it would
be ~17,700 tiles and ~43 GB at the observed fill (uma#376's ~4.9 GB/km² dense).
The cut makes that a local cost; the recon report (below) makes it a number the
operator sees **before** the import runs, which is the issue's storage-estimate
deliverable.

## Approach

1. **`level_plan.h/cpp` (new)** — `LevelPlan`: the quadtree cut. Built by
   `levelPlanFor(const std::map<gggs::GridIndex, float> & shallowest_by_index,
   const DepthAdaptiveLevelPolicy &)` per the algorithm above. Query API:
   `std::optional<gggs::GridIndex> leafContaining(const gggs::CellIndex &) const`,
   `std::set<uint8_t> levelsIntersecting(const gz4d::BoundsDegrees &) const`,
   `bool isLeaf(const gggs::GridIndex &) const`, `leavesAtLevel(uint8_t)`.
   JSON round-trip (`toJson`/`fromJson`) + `sha256()` over the canonical JSON, for
   the fingerprint and for `--level-plan` reuse. Pure — no ROS, no store I/O,
   mirroring `build_fingerprint`'s intended shape.
2. **Recon phase in `import_bag`** — a first pass that projects+georeferences
   exactly as today but, instead of accumulating CUBE, records
   `min(depth)` per level-14 grid, and spills each projected `GeoSounding` to a
   flat sequential scratch file. Phase 2 replays the spill, so the **expensive
   projection/TF work runs once**. Spill cost ~48 B/sounding (a 10 h M3 run at
   2560 soundings/s is ~4.4 GB of scratch — same order as the eviction spill the
   accumulator already writes).
3. **`--level-plan-out <f>` / `--level-plan <f>`** — recon-only and reuse-a-plan
   modes, so the operator can inspect the estimate and approve before committing a
   multi-hour import. Both print the **plan report**: leaf count and ground area per
   level, projected tile count and bytes (dense worst case and observed-fill
   estimate), and — called out separately — the area that lands **coarser than
   level 10**, i.e. resolution *lost* versus today's stores in >36 m water. That
   loss is inherent to the pinned #369 ladder, not a new decision; it is surfaced
   because a default that reduces what the operator can see must be visible.
4. **`MultiLevelAccumulator` in `store_import.h/cpp`** — owns one
   `{GeoMapSheet, ImportAccumulator}` per level present in the plan, each
   constructed with `cell_size_m = ` that level's `nominalCellSizeMeters()`.
   `addBatch(soundings)` routes the batch to **every** level whose leaves the
   batch's influence-expanded bounds intersect (`levelsIntersecting`), not just the
   home level — the halo rule. This is what preserves the invariant the
   `:1129-1130` comment protects: each accumulator's scratch/reload/seed stores still
   tile identically to *its own* sheet. What is dropped is "one sheet per run", not
   the coupling itself.
5. **`ImportAccumulatorConfig::persist_leaves`** (optional
   `std::shared_ptr<const LevelPlan>`) — a grid that is not a leaf of the plan at
   this accumulator's level is a halo artefact: never persisted, and freely
   droppable under eviction (dropping loses nothing because it is never written).
   Applied in `evictColdTiles`, `finalize`, and `persistResidentTile`.
   Null = today's behaviour.
6. **`import_bag` wiring** — `--depth-adaptive` (off by default; fixed-level stays
   the default and the only `draft`/live behaviour) selects recon + spill +
   `MultiLevelAccumulator`. Policy tunables exposed as
   `--depth-adaptive-scale/-coarsest/-finest` with the #369 defaults, validated
   once at startup (a policy throw is fatal there, per the policy header's note).
7. **`batch_regen` level-awareness (ADR-0002 amendment)** —
   `SheetFactory` becomes `std::function<std::unique_ptr<GeoMapSheet>(gggs::Level)>`;
   scatter routes to plan leaves instead of a single-level `index_sheet_`; the
   gather builds each bucket's sheet at that leaf's own level.
   `regen_config.cell_size_m` becomes the per-leaf level. Add
   `dirtyTiles(sqlite3*, new_bags, const LevelPlan &, sensor_filter)` alongside the
   existing `dirtyL10Tiles`: roll each expanded level-14 footprint tile up via
   `gggs::parent()` until it hits a plan leaf (and, for a footprint tile *coarser*
   than a leaf, descend to every leaf beneath it). The existing
   fixed-level overload is retained for fixed-level stores. ADR-0002's one-L14-tile
   margin survives unchanged and is re-argued in the amendment: `finest_level`
   (14) equals the survey index's footprint level, so **no leaf is ever finer than
   the index**, and the ~54 m margin still dominates the <=3 m influence radius at
   every leaf level.
8. **ADR-0003 amendment** — `schema_version` 1 -> 2; the scalar `cell_size_m` is
   replaced by a `tiling` object:
   `{"mode": "fixed"|"depth_adaptive", "cell_size_m": <float, fixed only>,
   "policy": {"capture_distance_scale", "coarsest_level", "finest_level"},
   "level_plan_sha256": <hex, depth_adaptive only>, "levels_used": [...]}`.
   Staleness rules gain: any change of `mode`, `policy`, or `level_plan_sha256`
   forces a full regen. No migration code is needed — the schema bump alone forces
   a full regen for any pre-existing v1 fingerprint, and none exist on disk because
   the writer is unimplemented.
9. **Mixed-level verification (hard requirement of this PR, not a follow-up)** —
   see Testing.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/level_plan.h` | **New** — `LevelPlan`, `levelPlanFor`, JSON + sha256, plan report. |
| `cube_bathymetry/src/level_plan.cpp` | **New** — quadtree cut, rollup, queries, report. |
| `cube_bathymetry/include/cube_bathymetry/store_import.h` | `MultiLevelAccumulator`; `ImportAccumulatorConfig::persist_leaves`. |
| `cube_bathymetry/src/store_import.cpp` | Leaf-filtered persist/evict; multi-level routing. |
| `cube_bathymetry/src/import_bag_main.cpp` | Recon phase + sounding spill; `--depth-adaptive*`, `--level-plan[-out]`; replace the single sheet/accumulator with the multi-level path when enabled. |
| `cube_bathymetry/include/cube_bathymetry/batch_regen.h` / `src/batch_regen.cpp` | Level-parameterised `SheetFactory`; plan-driven scatter routing and per-leaf gather. |
| `cube_bathymetry/src/batch_regen_main.cpp` | Accept/emit a level plan; drop the fixed `cell_size_m` pin when a plan is in use. |
| `cube_bathymetry/include/cube_bathymetry/survey_index_query.h` / `src/survey_index_query.cpp` | `dirtyTiles(..., const LevelPlan &, ...)` overload. |
| `cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md` | Amendment: level-aware rollup; margin re-argued for mixed leaves. |
| `cube_bathymetry/docs/decisions/0003-staleness-fingerprint.md` | Amendment: schema v2 `tiling` object + staleness rules. |
| `cube_bathymetry/CMakeLists.txt` | New source + new tests. |
| `cube_bathymetry/test/test_level_plan.cpp` | **New** — cut correctness, disjointness, NaN skip, policy throws. |
| `cube_bathymetry/test/test_mixed_level_import.cpp` | **New** — end-to-end mixed-level store + pyramid verification. |
| `cube_bathymetry/test/test_batch_regen.cpp` | Mixed-level scatter/gather + incremental dirty-set cases. |
| `README.md` | `import_bag` / `batch_regen_bag` flag tables; a depth-adaptive section; the fixed-vs-adaptive comparison row. |

## Testing

| Test | Asserts |
|---|---|
| `test_level_plan` | Leaves are pairwise disjoint with no ancestor/descendant pair; a shoal refines only the level-14 grids containing it, not its siblings; a no-data grid is skipped (no throw escapes); an inverted/NaN policy throws at construction. |
| **Single-level equivalence** (`test_mixed_level_import`) | With `coarsest_level == finest_level == 10`, the depth-adaptive path writes a store **byte-identical** to today's fixed-level path over the same synthetic bags. This is the load-bearing regression guard for the decoupling. |
| **Halo/seam** | A sounding within one influence radius of a level boundary contributes to the leaf on *both* sides; a leaf's cells are identical to what a whole-survey fixed-level build at that leaf's level would produce for that leaf. |
| **Mixed-level output** | A synthetic deep-plain-plus-shoal survey writes tiles at >= 2 levels; the store reloads them; every surveyed position resolves to its native leaf value through `BathymetryStore` query. |
| **Pyramid composition** | `buildDepthOverviewPyramid` runs over the mixed `processed` layer without error, discovers >= 2 native levels, writes derived tiles only where no native tile exists, and a level-by-level composite leaves no coverage hole. |
| **batch_regen** | Mixed-level scatter/gather is bit-identical to the equivalent per-leaf fixed-level build; `dirtyTiles(plan)` returns leaves (never ancestors or descendants of leaves) and stays a conservative superset. |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Capture decisions, not just implementations | The unit-of-decision decision and its three rejected alternatives are recorded above, per the operator's instruction to settle it in the plan rather than a separate ADR. The two *existing* ADRs whose decisions this changes (0002, 0003) are amended in this PR. |
| A change includes its consequences | Both consequences the issue review found are in scope (steps 7 and 8), plus two the review did not name: `BatchRegen::SheetFactory`'s single-level signature, and the halo/persist-filter needed to keep seams exact. |
| Test what breaks | The single-level equivalence test makes the decoupling falsifiable; the pyramid test discharges the issue's "needs demonstrating rather than assuming". |
| Human control and transparency | The recon report prints the storage estimate **and the resolution lost below level 10** before any import runs; `--level-plan-out` makes approval an explicit step. |
| Only what's needed | `--depth-adaptive` is opt-in; fixed-level stays the default, live/`draft` untouched, no retroactive reprocess (gated on uma#366). |
| Improve incrementally | Named split point below if the batch_regen rework balloons. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| cube ADR-0001 (tile eviction / incremental publish) | Yes | Eviction semantics are unchanged per accumulator; the only addition is that a non-leaf halo grid is droppable without a persist (it is never written, so the "persist before drop" rule is vacuous for it). Stated in the ADR-0002 amendment's cross-reference. |
| cube ADR-0002 (dirty-tile footprint math) | Yes | Amended: rollup targets plan leaves, not a fixed L10; margin re-argued because `finest_level == index level 14`. |
| cube ADR-0003 (staleness fingerprint) | Yes | Amended: schema v2 `tiling` object replaces scalar `cell_size_m`; no migration code needed (writer unimplemented). |
| cube ADR-0007 (backscatter addendum) | Yes | The backscatter store follows the bathy leaf tiling — one `MbesBackscatterStore` per level, same leaf filter — so the two stores stay cell-aligned. Verified in the mixed-level test. |
| cube ADR-0008 (predicted-surface geometry) | Yes | The reference/chart prior prime already handles a coarser prior via the #115 level-walk fallback; a finer *survey* leaf reading a level-10 reference tile exercises that existing path rather than a new one. Covered by a test case. |
| uma ADR-0010 D9 / uma ADR-0013 (native-wins pyramid) | Yes | Consumed, not changed — the pyramid builder is already level-discovering; the PR demonstrates composition. |
| workspace ADR-0001 (adopt ADRs) | Yes | Satisfied by amending 0002/0003; a new ADR is deliberately not filed (operator decision at the review-issue checkpoint). |
| workspace ADR-0018 (local-first CI) | Yes (routine) | `ci_local.sh` full-scope attestation before merge. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Store may hold mixed native levels | ADR-0003 fingerprint schema | Yes (step 8) |
| Store may hold mixed native levels | ADR-0002 / `dirtyL10Tiles` rollup level | Yes (step 7) |
| `BatchRegen::SheetFactory` signature | `batch_regen_main.cpp`, `test_batch_regen.cpp` | Yes |
| New `import_bag` / `batch_regen_bag` flags | `README.md` flag documentation | Yes |
| A re-import whose plan differs from the store's previous plan | Leaves orphaned tiles at the old level (`importTiles` never clears; `save()` never deletes — the documented additive-merge footgun) | **Partly**: `--depth-adaptive` refuses to write into a store whose fingerprint records a different `level_plan_sha256` unless `--replace-tiling` is given, and the refusal names the fix. A true `--replace` import path stays out of scope (it is the store's pre-existing gap, uma-side). |
| Deep water now writes level 9 (1.81 m cells) where today it writes level 10 (0.91 m cells) | Operator awareness | Yes — the recon report calls this out explicitly. Policy itself is pinned by #369 and not reopened. |
| Read-side fan-out and store residency become reachable | uma#371, uma#376 | No — read-side follow-ups, correctly out of this write-side issue. The recon report's numbers are expressed in uma#376's terms rather than inventing a second accounting. |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `README.md` — the `import_bag` /
  `batch_regen_bag` flag documentation and the bounded-RAM comparison table
  (lines ~104-113) both describe a single-resolution store;
  `docs/decisions/0002-*.md` and `docs/decisions/0003-*.md` amendments (above);
  the `import_bag_main.cpp:1128-1130` and `batch_regen_main.cpp:825-827` comments
  that state the single-resolution coupling as an invariant.
- **Agent-instruction candidates** (proposals only): this repo has **no
  `.agents/README.md`** (checked in both the worktree and the main tree) — the
  package-inventory / verified-parameter guide AGENTS.md expects. Worth its own
  issue; not a side-effect of this work. Also: ADR-0003's `tool_version` staleness
  key reads `package.xml`'s `<version>`, which is `0.0.0` and has never been
  bumped, so that key is inert — worth a follow-up issue, out of scope here.

## Open Questions

- [ ] Spill-vs-reproject for the recon phase: the plan assumes a flat sounding
      spill (~4.4 GB scratch for a 10 h M3 run) to avoid a second projection pass.
      If scratch space is the tighter constraint than time on the target machine,
      the fallback is re-reading the bags in phase 2. Defaulting to the spill with
      a `--recon-reproject` escape hatch unless the operator says otherwise.
- [ ] Whether `--depth-adaptive` should be reachable from
      `unh_echoboats_project11/scripts/build_bathy_store.sh` in this PR or a
      follow-up. Assuming follow-up: that script lives in another repo and is
      already known-stale (its "legacy store" guard).

## Estimated Scope

**Single PR**, ~8 atomic commits in the order of the Approach steps. Named split
point if it balloons: steps 1-6 + 8 + the import-side tests (depth-adaptive
`import_bag` + fingerprint schema) as PR1, and step 7 (batch_regen /
`dirtyTiles` level-awareness) as a stacked PR2. The split is viable because
`batch_regen`'s fixed-level path stays correct for fixed-level stores throughout;
it is *not* preferred, because a store `import_bag` can write but `batch_regen`
cannot incrementally update is exactly the gap the operator ruled in scope.
