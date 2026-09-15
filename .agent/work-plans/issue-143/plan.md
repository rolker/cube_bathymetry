# Plan: import_bag writes one resolution per run — depth-adaptive store levels need the estimation grid decoupled from the store tiling

## Issue

https://github.com/rolker/cube_bathymetry/issues/143

## Revision 3 — what changed and why

Revision 2 was reviewed (`153a634`, changes-requested) and then **re-based in a
design conversation with the operator on 2026-09-14** against Calder's post-CUBE
work. Six points were confirmed by the operator as the basis of this revision:

1. **Parent tiles stay alive under their children.** Refinement never happens
   in place: a coarse tile keeps estimating while finer child tiles are added
   over the part of it the data supports. The parent is the LOD level above the
   children and the native tile for the unrefined remainder.
2. **Two resolutions decide the level**: the resolution the survey *requires*
   (the #369 depth ladder) and the resolution the data *achieves* (Calder's
   level-of-aggregation over a count grid). The store takes the **coarser**;
   the gap is a coverage deficit.
3. **Recon is a count grid plus a per-coarse-tile sounding spill**, not a
   shallowest-depth map. A child created later replays its parent's spill.
4. **Capture distance becomes spacing-aware**: `max(scale·depth, k·spacing)`,
   `k = 0.71`; the hard-coded 0.5 m floor is removed outright. The physical
   floor (horizontal positioning error) moves into the #369 policy
   ([uma#386](https://github.com/rolker/unh_marine_autonomy/issues/386)).
5. **The backscatter store goes level-agnostic first**
   ([uma#383](https://github.com/rolker/unh_marine_autonomy/issues/383)).
6. Minimal ADR-0003 fingerprint, one shared RAM budget, one PR (earlier
   operator decisions, unchanged).

Consequences for the r2 review: must-fix 2 (`persist_leaves` dropping seam
tiles) is dissolved — there is no leaf filter any more. Must-fixes 3 and 4
(`--replace-tiling` clearing a shared layer; store-wide plan hash refusing
disjoint imports) are dissolved by point 1 — coexisting native levels on the
same ground are the model, so a re-import at a different level is not an error
and needs no guard (see Consequences). Must-fix 1 (shared touch clock) and 5
(RAM budget) stand and are addressed. The r2 suggestions taken are listed in
the revision history at the end.

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
  native-wins toward the apex (uma#331). **uma ADR-0011 skips a parent slot that
  already holds a native tile — it never folds children into a native parent**
  (`overview_pyramid.cpp:271-272`). So a native parent kept alive under native
  children shows its own CUBE estimate at that LOD, which is exactly point 1.
- `gggs` is a strict quadtree with free functions `gggs::parent(GridIndex)` and
  `gggs::children(GridIndex)` (`marine_autonomy/gggs/index_math.h`), so grids nest
  exactly across levels. Cells at levels 8/9/10/11/12/13/14 are 3.62 / 1.81 /
  0.91 / 0.45 / 0.23 / 0.11 / 0.057 m; a grid is 960 cells, so a level-14 grid
  is ~54 m of ground and a level-8 grid ~3.5 km.
- **CUBE has two distance gates** (verified in `node.cpp:154`, `grid.cpp:66-106`,
  `geo_grid.cpp:67-125`, `parameters.cpp:72-115`, and Calder's
  `cube_node.c:1831` / `cube_grid.c:1907-1930`): the **spread radius**
  (`Parameters::influenceRadius`) is spacing-aware — floored at one node
  spacing, grown by the sounding's IHO ratio, capped by its 99 % horizontal
  error — and decides which nodes are offered a sounding; the **capture
  distance** at the node is depth-only, `max(0.05·|depth|, 0.5 m)`. The 0.5 is
  a hard-coded literal in both sources. The CUBE User Manual (§3.3
  Gathering, `original_cube/docs/CUBE_User_Manual.doc`) names it
  `Capture_Distance_Minimum`, a shallow-water node-starvation guard, and
  advises lowering it to half the grid spacing for sub-metre grids. Effective
  reach = min of the two gates, so a node at a cell coarser than
  `0.05·depth / 0.71` (about 1.41× the capture radius; the half-diagonal
  criterion this plan adopts) has corners no node captures (holes), and below 10 m depth
  the 0.5 m floor makes levels 12–14 add nodes without adding resolvable
  detail. Both are fixed by point 4.
- `BatchRegen` already abstracts sheet construction behind a
  `SheetFactory = std::function<std::unique_ptr<GeoMapSheet>()>` and builds one
  fresh sheet per tile bucket.
- **`build_fingerprint.h/cpp` does not exist yet.** ADR-0003 is Accepted but
  unimplemented; `batch_regen_main.cpp` has no `--incremental` path.
  `dirtyL10Tiles()` (ADR-0002, #111 PR1) exists and already takes a
  `store_level` parameter.
- `GeoMapSheet::touch_counter_` is **per sheet** (`geo_map_sheet.h:228-231`), so
  last-touch values from two sheets are not comparable (r2 must-fix 1).
- `MbesBackscatterStore` holds one level fixed at construction and its
  `load()` throws on mismatch (`mbes_store.hpp:51`, `tile_io.hpp:93-96`) —
  hence uma#383.

## Prior art this design rests on

- Calder & Rice, *Design and Implementation of an Extensible Variable Resolution
  Bathymetric Estimator*, US Hydro 2011, and *Computationally efficient variable
  resolution depth estimation*, Computers & Geosciences 106:49–59, 2017 (CHRT).
  Mechanism verified from the GPL support code
  (github.com/brian-r-calder/vr-grid-estimator): fixed coarse SuperCells, each
  holding an independently-refined grid; a first pass estimates observation
  density per SuperCell and turns it into a node spacing (≈5 observations per
  node); a second pass re-adds every observation; a sounding within half a node
  spacing of a SuperCell edge is also offered to the neighbour.
- Calder, *Resolution Determination through Level of Aggregation Analysis*,
  US Hydro 2019 (copy at `~/cube/`). A fine count grid at resolution R; the
  level of aggregation at a cell is the smallest box around it holding the
  required observation count (with a blunder allowance); computed with a
  summed-area table, embarrassingly parallel; a coarse W-grid with one refined
  spacing per W-cell taken as the 95–99th percentile of the LoA values inside
  it; per-tile W for large depth ranges. Two sentences drive this plan: the LoA
  "can be readily updated as new data is added to the database, so long as the
  count grid is preserved", and given a *required* resolution as a function of
  depth, the LoA says where the data *achieves* it — a survey-completeness map.

Our GGGS quadtree is the dyadic constriction of CHRT's SuperCell-plus-refined-grid
(the CCOM CHRT page notes that option). The count grid is Calder's; the depth
ladder is the required resolution he evaluates completeness against.

## The unit of decision — decided here (issue scope bullet 1)

**Decision: the unit is the emitted GGGS tile, chosen top-down from the policy's
coarsest level; a tile is emitted at every level from the coarsest down to the
level the ground supports, so parents coexist with their children.** The level
the ground supports is the coarser of two answers, both evaluated per candidate
tile:

- **Required** (depth): `depthAdaptiveLevel(shallowest decision depth in the
  tile)` — the #369 ladder, unchanged in signature. Once uma#386 lands the
  policy also floors the cell at the survey's horizontal positioning error.
- **Achieved** (data): the level whose cell is no finer than the 95th-percentile
  level of aggregation of the count-grid cells inside the tile, i.e. the finest
  spacing at which every node in the tile still gathers `n_req` observations.

Algorithm (`levelPlanFor(count_grid, decision_depth_by_grid, policy)`):

1. Recon fills a **count grid** at level `C` (parameter `--count-level`,
   **default = `finest_level`**, i.e. 14, cell 0.057 m; validated
   `finest_level <= C <= 20`) with one `uint16` count per cell. R is
   deliberately the finest ladder cell itself rather than Calder's "a quarter
   of the finest expected spacing": his quarter smooths a *continuous* spacing
   estimate, whereas ours is quantised to the dyadic ladder, so achieved
   spacing needs only be resolved to the ladder step — and the achieved path can
   only reach level `C` (achieved spacing is `(2λ+1)·R`, `λ >= 0`), which is why
   the default equals the ladder's fine end. Lowering `C` caps the achieved
   level and quarters the count-grid RAM per level (see RAM below).
   Recon also keeps, per **level-14** grid, a **depth histogram** (sparse
   0.25 m bins, width doubling if a grid's spread would exceed 1024 bins); its
   **decision depth** is the `max(1, ceil(0.02·N))`-th shallowest, read off the
   histogram as the shallow edge of the bin the rank falls in (flier guard,
   kept from r2). A bounded "shallowest 64" reservoir stood here until the
   dry-run review: at ~200 k soundings per grid it returned the 64th shallowest
   raw sounding, 9 m above the surface CUBE stored. No min-count guard: a grid too sparse for
   the percentile is also too sparse to *achieve* a fine level, and counts say so.
2. **LoA** per count cell via a summed-area table over each count-grid tile
   (Calder §III.A): the smallest λ with `Σ counts in the (2λ+1)² box ≥ n_req`,
   `n_req` = `--min-obs-per-node` (default 5) inflated by
   `--blunder-allowance` (default 20 %). Achieved spacing = `(2λ+1)·R`.
3. **Touched set, per level.** The influence radius is **level-dependent**:
   `influenceRadius` floors at `distance_scale`, the sheet's node spacing
   (`parameters.cpp:100-115`; 3.62 m at level 8, 0.057 m at level 14), and the
   sheet's selection window adds a one-cell floor at its own level
   (`geo_map_sheet.cpp:62-72`). So there is one touched set **per level L**:
   recon records, per count cell, the maximum *unfloored* spread term of the
   soundings that landed there (`distance_scale·(ratio−1)^(1/2) − max_radius`,
   capped at `max_radius`, before the spacing floor); at plan time each occupied
   count cell is expanded by `max(recorded term, cell_L)` (the level-L floor,
   which also covers the one-cell selection floor) before testing intersection
   with level-L tiles. At a single level this reproduces the window today's
   fixed-level import selects, near-seam neighbours included; at coarser levels
   the floor grows with the cell, so no seam tile is lost through admission.
4. **Decision-depth rollup**: `decision_depth[g]` for a grid coarser than 14 is
   the **minimum over its *touched* children's decision depths** (the shoal-biased, safe
   direction: a shoal anywhere in `g` makes `g` at least as shallow). The
   percentile is computed once, at level 14, from the reservoir; coarse grids
   never pool soundings.
5. **Descent from each touched grid at `coarsest_level`** (a tunable; 8 by
   default). At grid `g` at level `L`:
   `required = depthAdaptiveLevel(decision_depth[g]).level()`;
   `achieved = levelNoFinerThan(p95 of achieved spacing over g)`, where
   `levelNoFinerThan(s)` is the **finest level whose cell is ≥ s** —
   `gggs::Level::fromCellSize(s)` returns the coarsest level at-or-**finer**
   (`gggs/level.h:59-74`, the unsafe direction), so the helper takes
   `fromCellSize(s).level() - 1` unless that level's cell equals `s` exactly,
   clamped to `[coarsest_level, finest_level]` (so a spacing coarser than the
   coarsest cell cannot underflow toward level 0). Where the LoA box for a
   count cell would run past its count tile's edge, the summed-area query is
   evaluated over the stitched 3×3 count-tile neighbourhood; a box that
   saturates even there reads as the coarsest level (the safe direction) and
   is reported;
   `target = min(required, achieved)` in level numbers (the coarser). **Emit
   `g`** (it is estimated and stored at level L regardless). If `target > L`
   and `L < finest_level`, recurse into the **touched** children of `g`.
6. The result is a set of tiles at several levels where **every emitted tile's
   ancestors up to `coarsest_level` are also emitted** — a full quadtree
   prefix, not a cut. With `coarsest_level == finest_level` it is exactly
   today's single-level touched set. Downstream consumers need no
   disjointness: the store already holds overlapping native levels, and the
   overview pyramid skips native parents.

**Why this and not the alternatives:**

| Candidate | Rejected because |
|---|---|
| Per-tile at a fixed level | Circular: the tile's identity depends on the level being chosen. |
| Per-region at a fixed decision level | The decision grid would have to be level 8 (3.5 km) for exact nesting; one 2 m shoal drags 3.5 km of ground to level 14. |
| Quadtree **cut** (r1/r2: leaves only, parents discarded) | Discards the coarse estimate the LOD pyramid needs, forces a leaf filter that drops seam tiles (r2 must-fix 2), and makes any later re-tiling a destructive replace (r2 must-fixes 3/4). Keeping parents costs one extra estimate per level stacked over a point (two or three on a slope) and buys a true estimate at every LOD. |
| Depth-only decision (r2) | Cannot tell "shallow and well sounded" from "shallow and sparse"; needs flier and min-count guards; over-refines sparse shoals into empty fine tiles. Counts answer the sparse case directly. |
| Two-pass over the whole survey choosing one global level | Reduces to today's behaviour with extra machinery. |

**Storage consequence.** Against the real Shoals `processed` layer at
`~/data/world/depths/processed`: 69 native level-10 tiles, 167 MB of `.tif`
(2.42 MB/tile observed; 14.7 MB/tile dense — the `960*960*2*8` figure uma#376
uses). Each level step is 4x for the same ground. Keeping parents adds the
coarser levels on top of the finest: a geometric series, so at most one third
more than the finest level alone. The recon report prints the estimate per level
before the import runs — the issue's storage-estimate deliverable. It also prints
the area where **required is finer than achieved** (the coverage deficit) and
the area landing **coarser than level 10** (resolution lost against today's
stores in >36 m water; inherent to the pinned ladder, surfaced because a default
that reduces what the operator can see must be visible).

**Recon RAM.** Two structures grow with surveyed area. The count grid: a
level-14 count tile is 960² × 2 B = 1.8 MB over ~54 m of ground, ~340 tiles/km²,
so ~630 MB/km² if fully resident (level 13: ~160 MB/km²; level 12: ~40 MB/km²).
Count tiles are plain rasters: the recon keeps them under the same resident-tile
budget and eviction as CUBE tiles (persist to the scratch dir, reload on
revisit), and the summed-area table is built per tile at LoA time. The depth
reservoir: 64 × 4 B + a count per level-14 grid, ~90 KB/km² — negligible. The
plan report states the resident count-tile peak.

**Compute consequence of parents-alive.** Over a shoal the full prefix is every
level from `coarsest_level` to the achieved level — up to seven native levels —
so that ground is CUBE-estimated up to seven times and each ping there routes
into up to seven accumulators; over a plain it is two or three. Import runtime
therefore scales with the *sum over levels* of the area emitted at each, which
the plan report prints as an estimate-count multiplier against the fixed-level
baseline (the same quantity the storage series bounds at ≤ 4/3 of the finest
level — for storage; compute has no such bound because coarse tiles cost the
same per sounding as fine ones).

## Prerequisite (cross-repo)

**[uma#383](https://github.com/rolker/unh_marine_autonomy/issues/383)** —
`MbesBackscatterStore` level-agnostic, mirroring the bathy store's ADR-0002 §D2
amendment (level recovered from the `<level>_<row>_<col>.tif` filename). ADR-0007
keeps the backscatter store cell-aligned with the bathy tiles, so a
multi-level import writes a multi-level backscatter store, which today cannot be
read back. Operator decision: per-level layout there, not a refusal here. It
must merge and `core_ws` be rebuilt before this PR's mixed-level backscatter
test can pass; the level-plan, count-grid, capture and fingerprint commits do
not touch backscatter and proceed immediately.

**[uma#386](https://github.com/rolker/unh_marine_autonomy/issues/386)** — the
finest-cell floor from horizontal positioning error in `DepthAdaptiveLevelPolicy`.
Not a build prerequisite (the policy's default floor is 0), but the plan report
names it so the operator knows the shallow-water floor is a policy setting, not
a CUBE constant, once it lands.

## Approach

1. **Capture distance** (`parameters.h`, `node.cpp:154`, `original_cube` is
   untouched). Replace `max(capture_distance_scale·|depth|, 0.5)` with
   `max(capture_distance_scale·|depth|, capture_spacing_scale·distance_scale)`,
   `capture_spacing_scale` a new `Parameters` field, **default 0.71** (half the
   cell diagonal: every sounding reaches at least one node; the manual's 0.5 is
   node-centric and drops corner soundings — operator decision 2026-09-14).
   The 0.5 m literal is **removed, not made a parameter** (remove obsolete
   features outright). `distance_scale` is already the node spacing
   (`parameters.cpp:115`). This is what lets a parent tile above the ladder
   level estimate without holes, and lets fine shallow tiles stop averaging over
   a 0.5 m circle. Live CUBE inherits the change at its fixed level: at level
   10 the new floor is 0.65 m against 0.5 m today — stated in the README and
   the parameter table; below 10 m depth live estimates gather slightly more.
2. **`count_grid.h/cpp` (new)** — `CountGrid`: sparse map of `gggs::GridIndex`
   (level `C`) → `uint16[960×960]`, `add(lat, lon)`, per-tile summed-area table,
   `levelOfAggregation(cell, n_req)` (bisection over box size, Calder §III.A),
   `achievedSpacingPercentile(gggs::GridIndex coarse, double p)`. Persist/merge
   (`toDir`/`fromDir`, one `.tif` per count tile) so a live-collected count grid
   can seed the offline one later (follow-up, see Open Questions). Pure.
3. **`level_plan.h/cpp` (new)** — `LevelPlan`: the quadtree prefix from the
   algorithm above. Query API: `std::set<gggs::GridIndex> tilesContaining(const
   gggs::CellIndex &) const` (one per emitted level), `std::set<uint8_t>
   levelsIntersecting(const gz4d::BoundsDegrees &) const`, `bool
   isEmitted(const gggs::GridIndex &) const`, `isTouched(const gggs::GridIndex &)
   const`, `tilesAtLevel(uint8_t)`, `coverageDeficit()` (tiles where the
   *required* level is finer than the *achieved* one, i.e. `required > achieved`
   in level numbers). JSON round-trip
   with a **defined canonical form** (tiles sorted by `(level,row,col)`, fixed
   key order, integers only, policy scale as its parsed decimal string, no
   whitespace) and `sha256()` over it, for `--level-plan` reuse and the
   fingerprint's `levels_used`. The plan report (leaf counts, area and bytes per
   level, coverage deficit, area coarser than level 10, count-tile peak).
4. **Recon phase in `import_bag`** — a first pass that projects and
   georeferences exactly as today but, instead of accumulating CUBE, updates the
   count grid, the per-count-cell max influence radius, and the
   per-level-14-grid shallowest-64 reservoir, and spills each projected
   `GeoSounding` **in full** (every `Sounding` field including `intensity`,
   `beam_angle`, `slant_range` and `sonar_relative_position`, so the
   backscatter half of the equivalence test holds — the angular-response
   correction reads them) to a **per-level-10-grid spill file** under
   `--scratch-dir` (default: beside the output store, never
   `temp_directory_path()`, which is often tmpfs — r2 suggestion). A spilled
   record is ~80 B (`gz4d::PositionDegrees` + `Sounding`, `sounding.h`); free
   space is checked against the projected size (a 10 h M3 day at 2560
   soundings/s is ~7.4 GB) before phase 1 starts. Phase 2 replays the spill
   grid by grid, so the expensive projection/TF work runs once. The level-10
   partition (~870 m grids) is a scratch-file grouping independent of
   `coarsest_level`: a fine tile's replay reads one file; a coarse parent's
   replay reads its descendants' files. Residency during replay rests on the
   accumulator's eviction, not on the partition.
   **Superseded during implementation** (see Implementation sync): the level-10
   partition is gone — the spill is one chronological file replayed front to
   back, because CUBE's sliding-median pre-filter is order-dependent and a
   spatial partition breaks the byte-identity claim for tiles coarser than it.
5. **`--level-plan-out <f>` / `--level-plan <f>` / `--count-grid-out <d>`** —
   recon-only and reuse-a-plan modes, so the operator can inspect the estimate
   and the coverage deficit and approve before committing a multi-hour import.
6. **`MultiLevelAccumulator` in `store_import.h/cpp`** — owns one
   `{GeoMapSheet, ImportAccumulator}` per level present in the plan, each
   constructed with `cell_size_m` = that level's `nominalCellSizeMeters()`.
   `addBatch(soundings)` routes the batch to **every level whose emitted tiles
   the batch's influence-expanded bounds intersect** (`levelsIntersecting`) —
   parents included, since they are estimated in full. Each accumulator's
   scratch/reload/seed stores still tile identically to *its own* sheet, which
   is the invariant the `:1129-1130` comment protects.
   **Per-tile admission** (r3 must-fix 3): routing chooses the *levels*, but a
   batch is one ping (`import_bag_main.cpp:1293-1296`), so without a tile-level
   rule a swath clipping one emitted level-14 tile would build level-14 tiles
   over the whole neighbouring plain. Each accumulator's `GeoMapSheet` is
   therefore given the plan's **emitted set at its level** as an admission
   predicate applied **where grids are created**: `getOrCreateGridsIn`
   (`geo_map_sheet.cpp:147-168`, reached from `addSoundings` at `:102`) skips
   any index outside the set, so an off-plan grid is never created, never
   estimated, and never appears in `sheet_.grids()` for `finalize` to persist
   (`store_import.cpp:1157-1165`). The enumerator `gridIndicesForSoundings`
   (`geo_map_sheet.cpp:115-130`) is filtered by the same predicate so the
   reload/seed pass and the creation pass see one set — filtering only the
   enumerator would leave off-plan grids created and persisted while skipping
   their reload, worse than no predicate. Soundings near an emitted tile's
   edge still reach it (they are in its level-L expanded window), so seams
   inside the plan are exact. Because the emitted set at a level is that
   level's *touched* set (algorithm step 3, the window today's fixed path
   selects), single-level runs admit exactly today's grids. There is no
   *leaf* filter: every admitted tile that receives data is persisted, parents
   included (an admitted grid that stays empty is not written, as today —
   `store_import.cpp:505-509`).
   **One shared RAM budget**: `max_resident_tiles` keeps its operator-facing
   meaning as the store-wide total; `MultiLevelAccumulator` owns eviction and
   drops the globally coldest tiles across levels. Comparable coldness needs a
   **shared touch clock** (r2 must-fix 1): one monotonic counter injected into
   every sheet (`GeoMapSheet` gains an optional `std::shared_ptr<std::atomic<uint64_t>>`
   clock; null = today's per-sheet counter). New `ImportAccumulator` public API:
   `residentTiles()` (grid + last-touch), `persistAndDrop(grid)`; its own
   `evictColdTiles` trigger is disabled under multi-level ownership
   (`max_resident_tiles = 0`). `persistAndDrop` preserves today's two
   invariants: a throwing persist leaves the tile resident
   (`store_import.cpp:1069-1077`), and a dropped tile is recorded in `evicted_`
   so `addBatch`'s reload-before-add reloads it (`:1103-1106`).
   **Sidecars**: `MultiLevelAccumulator::finalize` writes `registry.json` and
   the backscatter metadata **once**, after all levels have finalised, rather
   than each accumulator writing its own (r2 suggestion). **Backscatter**: one
   `MbesBackscatterStore` per level into the single `--bs-store` dir (uma#383).
7. **`import_bag` wiring** — `--depth-adaptive` (off by default; fixed-level
   stays the default and the only `draft`/live behaviour) selects recon + spill
   + `MultiLevelAccumulator`. Policy tunables `--depth-adaptive-scale/-coarsest/
   -finest` (#369 defaults), `--count-level` (default = finest),
   `--min-obs-per-node` (5), `--blunder-allowance` (0.2),
   `--depth-adaptive-percentile` (2), `--scratch-dir`, and
   `--capture-spacing-scale` (0.71; also on `batch_regen_bag`, whose bit-exact
   regeneration needs the same gate); all validated once at startup —
   **including `finest <= 14`** (GGGS has levels 0–20; a tile finer than the
   level-14 survey-index footprint breaks ADR-0002's dirty-set guarantee) and
   `finest <= count-level <= 20`. A policy throw is fatal there. The live node
   gets no ROS parameter for the capture scale, matching `capture_distance_scale`
   today (`parameters.h:205` is its only site); both stay `Parameters` fields.
8. **`batch_regen` level-awareness (ADR-0002 amendment)** — `SheetFactory`
   becomes `std::function<std::unique_ptr<GeoMapSheet>(gggs::Level)>`; scatter
   routes to **every emitted tile containing the sounding** (parents included);
   the gather builds each bucket's sheet at that tile's own level. **Rename**
   `dirtyL10Tiles` → `dirtyTilesAtLevel` (it already takes `store_level`) and add
   `dirtyTiles(sqlite3*, new_bags, const LevelPlan &, sensor_filter)`: for each
   expanded level-14 footprint tile, the emitted ancestor at **every** level the
   plan holds over it (`tilesContaining`). Simpler than the r2 leaf rollup and
   still a conservative superset. The function **throws** (not `assert`) if a
   footprint tile is coarser than an emitted tile — the index footprint level is
   DB-sourced and may be mixed (`survey_index_query.cpp:192-193`), and
   `ancestorAtLevel` already throws on this case; the dry-run's catch falls back
   to full regen (r2 suggestion). ADR-0002's one-L14-tile margin survives and is
   re-argued: `finest_level <= 14` (validated) equals the index footprint level,
   so no tile is finer than the index, and the ~54 m margin still dominates the
   influence radius at every level.
9. **ADR-0003 amendment + minimal fingerprint** — `schema_version` 1 → 2; the
   scalar `cell_size_m` is replaced by a `tiling` object:
   `{"mode": "fixed"|"depth_adaptive", "cell_size_m": <float, fixed only>,
   "policy": {"capture_distance_scale", "capture_spacing_scale", "coarsest_level",
   "finest_level", "count_level", "min_obs_per_node", "blunder_allowance"},
   "levels_used": [...]}`. **`policy` is written in both modes** — the capture
   gate changes fixed-level output too, so a fixed store's fingerprint must
   carry `capture_distance_scale` and `capture_spacing_scale` or the staleness
   job ADR-0003's `cell_size_m` row does today is lost; the depth-adaptive-only
   keys are null in fixed mode. Staleness: any change of `mode` or `policy`
   forces a full regen. **No `level_plan_sha256` and no `--replace-tiling`** (r2
   must-fixes 3/4): under point 1, tiles at several levels on the same ground
   are the normal state, so a later import at other levels is additive like any
   other import and needs no refusal. What that leaves behind is documented,
   not guarded (README + ADR-0002 amendment): a re-import over already-covered
   ground under a different policy or count level leaves the earlier import's
   finer native tiles in place, and a fine-LOD reader prefers those over the
   newer, coarser, complete estimate — the same additive-merge contract the
   store has for same-level re-imports (`importTiles` merges, `save()` never
   deletes). `build_fingerprint.h/cpp` (new) implements
   **only** `schema_version` + `tiling` read/write; `import_bag` writes it after
   every successful import (fixed-level writes `mode: fixed`). The other
   ADR-0003 keys and `batch_regen --incremental` stay unimplemented and are
   marked so — ADR-0003's first partial implementation, not its completion.
10. **ADR-0002 amendment carries the decision.** The operator ruled out a new
    ADR, so the ADR-0002 amendment records: the quadtree-prefix decision and its
    rejected alternatives (table above), the required-versus-achieved rule, the
    parents-alive rule and its LOD consequence (ADR-0011 skips native parents),
    the spacing-aware capture and the removed floor, and the `finest <= 14`
    invariant. The README depth-adaptive section is the operator summary and
    links to it. This plan file is not the durable home.
11. **Mixed-level verification (hard requirement of this PR, not a follow-up)** —
    see Testing.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/parameters.h` / `src/parameters.cpp` | `capture_spacing_scale` (default 0.71; `--capture-spacing-scale` on both CLI tools); doc the two gates. |
| `cube_bathymetry/include/cube_bathymetry/geo_map_sheet.h` / `src/geo_map_sheet.cpp` | Optional admission predicate (the plan's emitted set at this sheet's level) applied in `getOrCreateGridsIn` and `gridIndicesForSoundings`. |
| `cube_bathymetry/src/node.cpp` | Capture = `max(scale·depth, capture_spacing_scale·distance_scale)`; 0.5 literal removed. |
| `cube_bathymetry/include/cube_bathymetry/count_grid.h` / `src/count_grid.cpp` | **New** — sparse count tiles, summed-area table, level of aggregation, percentile per coarse tile, persist/merge. |
| `cube_bathymetry/include/cube_bathymetry/level_plan.h` / `src/level_plan.cpp` | **New** — quadtree prefix, required-vs-achieved, queries, canonical JSON + sha256, plan report. |
| `cube_bathymetry/include/cube_bathymetry/build_fingerprint.h` / `src/build_fingerprint.cpp` | **New** — minimal ADR-0003 v2 read/write (`schema_version` + `tiling`). |
| (same files) | Optional shared touch clock. |
| `cube_bathymetry/include/cube_bathymetry/multi_level_accumulator.h` / `src/multi_level_accumulator.cpp` | **New** (sync) — `MultiLevelAccumulator` landed in its own pair of files rather than inside `store_import.*`: per-level sheets, shared touch clock, store-wide budget, global coldest-first eviction. |
| `cube_bathymetry/include/cube_bathymetry/recon.h` / `src/recon.cpp` | **New** (sync) — `ReconCollector`: count grid + `ShallowReservoir` decision depths + the chronological sounding spill and its replay; free-space check. |
| `cube_bathymetry/include/cube_bathymetry/map_sheet.h` | (sync) Admission predicate + touch-clock hooks on the sheet interface that `GeoMapSheet` implements. |
| `cube_bathymetry/include/cube_bathymetry/store_import.h` / `src/store_import.cpp` | `MultiLevelAccumulator` (routing, shared budget, global eviction, single sidecar write); `ImportAccumulator::residentTiles()` / `persistAndDrop()`. |
| `cube_bathymetry/src/import_bag_main.cpp` | Recon (count grid + per-cell max spread term + reservoir + one chronological spill under `--scratch-dir` with free-space check, `--count-resident-tiles`); `--depth-adaptive*`, `--count-level`, `--min-obs-per-node`, `--blunder-allowance`, `--level-plan[-out]`, `--count-grid-out`; validation incl. `finest <= 14`; multi-level path; fingerprint write; drop the `:1128-1130` single-resolution comment. |
| `cube_bathymetry/include/cube_bathymetry/batch_regen.h` / `src/batch_regen.cpp` | Level-parameterised `SheetFactory`; scatter to every emitted containing tile; per-tile gather. |
| `cube_bathymetry/src/batch_regen_main.cpp` | Accept a level plan; drop the fixed `cell_size_m` pin when a plan is in use; drop the `:825-827` comment; `dirtyTilesAtLevel` call at `:394`. |
| `cube_bathymetry/include/cube_bathymetry/survey_index_query.h` / `src/survey_index_query.cpp` | Rename `dirtyL10Tiles` → `dirtyTilesAtLevel`; add `dirtyTiles(..., const LevelPlan &, ...)` (throws on a coarser footprint). |
| `cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md` | Amendment: carries the design decision (step 10); level-aware rollup; margin re-argued. |
| `cube_bathymetry/docs/decisions/0003-staleness-fingerprint.md` | Amendment: schema v2 `tiling` object; partial-implementation note. |
| `cube_bathymetry/CMakeLists.txt` | New sources + tests. |
| `cube_bathymetry/test/test_count_grid.cpp` | **New**. |
| `cube_bathymetry/test/test_level_plan.cpp` | **New**. |
| `cube_bathymetry/test/test_build_fingerprint.cpp` | **New**. |
| `cube_bathymetry/test/test_mixed_level_import.cpp` | **New** — end-to-end mixed-level store + pyramid verification. |
| `cube_bathymetry/test/test_node.cpp` | Capture gate: spacing term, no 0.5 m floor. |
| `cube_bathymetry/test/test_tile_eviction_rss.cpp` | Mixed-level case: the store-wide budget holds across levels. |
| `cube_bathymetry/test/test_batch_regen.cpp` | Mixed-level scatter/gather + `dirtyTiles(plan)`. |
| `cube_bathymetry/test/test_survey_index_query.cpp` | Rename follow-through. |
| `cube_bathymetry/test/test_recon.cpp` | **New** (sync) — reservoir percentile, the chronological spill round-trip, scratch-dir refusal and cleanup. |
| `cube_bathymetry/test/test_geo_grid.cpp`, `test/test_parameters.cpp`, `test/test_publish_equivalence.cpp` | (sync) Follow-through only: the new `capture_spacing_scale` parameter in the fixtures these tests construct. |
| `README.md` | `import_bag` / `batch_regen_bag` flag tables; depth-adaptive section (operator summary, links ADR-0002 amendment); bounded-RAM comparison row; the new capture parameter and its live effect; fix the stale layer names at `README.md:99-100` (uma#248 is described as collapsing to `survey` + `reference`; the on-disk layers are `processed/`, `draft/`, `reference/`, `chart/`, legacy `survey/` auto-migrating — uma `marine_bathymetry_store/README.md:137-140`). |
| `unh_marine_autonomy` | **Separate PRs, not this diff** — uma#383 (prerequisite), uma#386 (policy floor). |

## Testing

| Test | Asserts |
|---|---|
| `test_node` (capture) | A sounding at 0.7 cell from a node is accepted at every depth (no holes); at 1 m depth on a 0.11 m cell the gate is `0.71·0.11 m`, not 0.5 m; at 40 m depth on a 1.81 m cell the depth term (2 m) wins. |
| `test_count_grid` | Counts accumulate per cell; summed-area sums equal brute force; LoA for a cell with `n_req` soundings inside is 0, for an empty region grows to the box that reaches them; persist/merge round-trip is additive. |
| `test_level_plan` | Every emitted tile's ancestors up to `coarsest_level` are emitted (full prefix); with `coarsest == finest` the emitted set equals that level's touched set; the touched set at a coarser level is never narrower than the union of its touched children (per-level floor); `levelNoFinerThan` clamps at `coarsest_level`; an LoA box crossing a count-tile edge reads the neighbour's counts; `levelNoFinerThan` rounds toward the coarser level (0.33 m → level 11, never 12); required-vs-achieved takes the coarser; a shoal refines only the touched children; a sparse shoal (counts below `n_req` at fine spacing) stays coarse and is reported as coverage deficit; the min-rollup makes a parent at least as shallow as any child; `finest > 14` and `count-level < finest` rejected; canonical JSON round-trip is lossless and `sha256()` is identical across insertion orders and across two processes. |
| **Single-level equivalence** (`test_mixed_level_import`) | With `coarsest_level == finest_level == 10` **and `capture_spacing_scale` pinned so the gate equals today's 0.5 m exactly** — set on `Parameters` as `0.5 / distance_scale` where `distance_scale` is the **requested** resolution both paths are built with (`GeoMapSheet` takes the requested, not the snapped, cell size — `geo_map_sheet.cpp:76` — so the test builds both paths from the same requested resolution and derives the pin from it; a hand-typed `0.5/0.91` against the 0.906 m nominal cell would give 0.4978 m and fail by construction) — the depth-adaptive path writes a store **byte-identical** to the fixed-level path over the same synthetic bags. Defined against the **persisted** set (the created set is the batch bounding rectangle and empty grids are never written, `store_import.cpp:505-509`): every `.tif` under `processed/` (and the backscatter `survey/` dir) identical file-for-file, the same file set on both sides; `registry.json` equal after parsing. If a GDAL-injected tag proves non-deterministic the fallback is band-data + geotransform equality, recorded in the test. The load-bearing regression guard. |
| **Parents under children** | A deep-plain-plus-shoal survey with `coarsest_level = 8` emits level 8 everywhere touched, level 9/10 over the plain, and finer tiles over the shoal; the parent tiles over the shoal hold a complete estimate (no holes), and `buildDepthOverviewPyramid` **skips** those parent slots as native (ADR-0011) and writes derived tiles only where no native tile exists; a level-by-level composite has no coverage hole. |
| **Halo/seam** | A sounding within one influence radius of a tile boundary at level L contributes to both level-L tiles; each tile's cells equal a whole-survey fixed-level build at that level. |
| **Bounded RAM** (`test_tile_eviction_rss`) | A mixed-level run with `max_resident_tiles = N` never holds more than N resident tiles summed across levels, evicts the globally coldest first (shared clock), and loses no data. |
| **Fingerprint** (`test_build_fingerprint`) | v2 round-trip; fixed-level writes `mode: fixed`; a v1 file reads as stale. |
| **batch_regen** | Mixed-level scatter/gather is bit-identical to the equivalent per-tile fixed-level build at each level; `dirtyTiles(plan)` returns the emitted ancestor at every level and stays a conservative superset; a footprint coarser than an emitted tile throws and the dry-run falls back to full regen. |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Capture decisions, not just implementations | The unit-of-decision, required-vs-achieved, parents-alive, and capture-gate decisions with their rejected alternatives are recorded above and carried into the ADR-0002 amendment (step 10), per the operator's instruction to settle them in the plan rather than a new ADR. |
| A change includes its consequences | In scope: ADR-0002/0003 amendments, `SheetFactory` signature, shared RAM budget + shared clock, `finest <= 14`, backscatter (uma#383), the policy floor (uma#386), the live-CUBE capture change (documented), README layer-name drift. |
| Test what breaks | Single-level equivalence makes the decoupling falsifiable; the parents-under-children test discharges "needs demonstrating rather than assuming"; the capture test pins the gate. |
| Human control and transparency | The plan report prints storage per level, the coverage deficit, and the resolution lost below level 10 before any import runs; `--level-plan-out` makes approval explicit. |
| Only what's needed | `--depth-adaptive` is opt-in; fixed-level stays the default; live/`draft` behaviour changes only by the capture gate, which is stated. No retroactive reprocess (gated on uma#366). |
| Improve incrementally | Live-node use of the count grid and spill is a named follow-up, not this PR. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| cube ADR-0001 (tile eviction / incremental publish) | Yes | Persist-then-drop semantics preserved per tile; eviction ownership moves to `MultiLevelAccumulator` with a shared clock; count tiles ride the same budget. |
| cube ADR-0002 (dirty-tile footprint math) | Yes | Amended: carries the design; rollup targets every emitted level; margin re-argued under `finest <= 14`. |
| cube ADR-0003 (staleness fingerprint) | Yes | Amended: schema v2 `tiling`; minimal read/write implemented; no plan hash. |
| cube ADR-0007 (backscatter addendum) | Yes | One store per level, cell-aligned; requires uma#383; test gated on it. |
| cube ADR-0008 (predicted-surface geometry) | Yes | A finer survey tile reading a coarser reference prior exercises the existing #115 level-walk fallback; covered by a test case. |
| uma ADR-0002 §D2 / ADR-0011 / ADR-0013 | Yes | Consumed, not changed: multi-level layer, native-wins with native parents skipped, bounded-LOD display; the PR demonstrates composition. |
| workspace ADR-0001 (adopt ADRs) | Yes | Satisfied by amending 0002/0003; a new ADR is deliberately not filed (operator decision). |
| workspace ADR-0018 (local-first CI) | Yes (routine) | `ci_local.sh` full-scope attestation before merge. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Store holds tiles at several levels on the same ground | ADR-0003 fingerprint schema; ADR-0002 rollup; overview pyramid behaviour (native parents skipped) | Yes (steps 8–10); pyramid consumed as-is and tested. |
| A later import at other levels | Nothing — additive, like any import; the pre-existing same-level merge contract (`importTiles` merges, `save()` never deletes; uma-side) is unchanged; a re-import over covered ground under a different policy leaves the earlier finer tiles in place, preferred by fine-LOD readers | Yes — stated in README + ADR-0002 amendment (step 9); no guard, no `--replace-tiling`. |
| Parents estimated in full under children | Import runtime: up to 7× the estimates over a shoal, 2–3× over a plain; plan report prints the multiplier | Yes — stated; no bound claimed for compute. |
| Capture gate becomes spacing-aware, floor removed | Live CUBE node behaviour at its fixed level; README parameter table; `.agents/README.md` parameter table (**this repo has none** — see below) | Yes (step 1, README); the guide is a follow-up issue. |
| `depthAdaptiveLevel` gains a horizontal-error floor | uma policy header + tests | uma#386, separate PR. |
| N per-level accumulators | `max_resident_tiles` semantics, ADR-0001 claim, README table, `test_tile_eviction_rss`, `GeoMapSheet` clock | Yes (step 6). |
| `finest_level` becomes a CLI tunable | ADR-0002's "no tile finer than the index" premise | Yes: validated at startup; invariant in the amendment. |
| `dirtyL10Tiles` → `dirtyTilesAtLevel` | `batch_regen_main.cpp:394`, `test_survey_index_query.cpp`, ADR-0002 text | Yes (step 8). |
| `BatchRegen::SheetFactory` signature | `batch_regen_main.cpp`, `test_batch_regen.cpp` | Yes. |
| New flags | `README.md` | Yes. |
| Deep water writes level 9 where today it writes level 10 | Operator awareness | Yes — plan report. Policy pinned by #369, not reopened. |
| Read-side fan-out and residency become reachable | uma#371, uma#376 | No — read-side follow-ups; the report uses uma#376's accounting. |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `README.md` — flag tables, bounded-RAM
  comparison table (~104-113), layer names (99-100), the capture parameter;
  `docs/decisions/0002-*.md` and `0003-*.md` amendments; the
  `import_bag_main.cpp:1128-1130` and `batch_regen_main.cpp:825-827` comments
  that state the single-resolution coupling as an invariant; the
  `parameters.h:200-205` capture comment.
- **Agent-instruction candidates** (proposals only): this repo has **no
  `.agents/README.md`** — the package-inventory / verified-parameter guide
  AGENTS.md expects, and the capture change is exactly the kind of parameter
  change it should record. Worth its own issue. ADR-0003's `tool_version` key
  reads `package.xml`'s `<version>` (`0.0.0`, never bumped) — inert; follow-up.
- **Knowledge candidate**: the two-gate capture analysis and Calder's CHRT/LoA
  lineage belong in a repo doc (the ADR-0002 amendment carries what this PR
  needs; a `docs/` note on prior art is a follow-up).

## Open Questions

- [ ] `--count-level` default = `finest_level` (14, ~630 MB/km² resident peak
      before eviction) keeps the whole ladder reachable; an operator surveying
      only deeper water can lower it (13: ~160 MB/km², caps achieved at 13).
      Defaulting to the full ladder with eviction unless the operator prefers
      the cheaper cap.
- [ ] Live-node follow-up (not this PR): the boat keeps the count grid and the
      per-tile spill in its live cache, re-derives the plan periodically, and
      adds child tiles when counts justify them, replaying the parent's spill.
      To be filed as its own cube issue once this PR's offline path exists.
- [ ] Whether `--depth-adaptive` should be reachable from
      `unh_echoboats_project11/scripts/build_bathy_store.sh` in this PR or a
      follow-up. Assuming follow-up: other repo, already known-stale guard.

## Estimated Scope

**Single PR** (operator decision, re-confirmed), ~12 atomic commits in the order
of the Approach steps: capture gate (1) → count grid (2) → level plan (3) →
fingerprint (9) → recon + CLI (4, 5, 7) → shared clock + multi-level accumulator
(6; backscatter test gated on uma#383) → batch_regen (8) → ADR amendments +
README (10) → mixed-level verification (11). Named split point if it balloons:
steps 1–7 + 9 as PR1, step 8 as a stacked PR2 — viable, not taken, because a
store `import_bag` can write but `batch_regen` cannot update is exactly the gap
the operator ruled in scope. External dependencies: uma#383 (must merge first),
uma#386 (independent).

## Revision history

- **r1** (`6e45b3c`, 2026-09-10) — initial plan: quadtree cut, shallowest-depth recon.
- **r2** (`52b25dd`, 2026-09-14) — after plan review r1 (`ee23303`): uma#383,
  `finest <= 14`, minimal fingerprint + `--replace-tiling`, percentile/min-count
  decision depth, shared RAM budget; suggestions folded in.
- **r3** (2026-09-14) — after plan review r2 (`153a634`) **and the operator-led
  design re-base**: parents alive under children (quadtree prefix, no leaf
  filter, no `--replace-tiling`, no plan hash); required-vs-achieved with a
  count grid (Calder LoA 2019) and per-L8 spill; spacing-aware capture,
  `k = 0.71`, 0.5 m floor removed, uma#386 filed; shared touch clock and new
  `ImportAccumulator` API named; `--scratch-dir` + free-space check; recon RAM
  stated; single sidecar owner; `dirtyTiles` throws; `test_survey_index_query`
  added; min-count guard dropped (counts subsume it).
- **r4** (2026-09-14) — after plan review r3 (`db95a50`): `--count-level`
  default = `finest_level` with `finest <= C <= 20` and the R-reading stated;
  `levelNoFinerThan` rounds toward the coarser level; `coverageDeficit` is
  `required > achieved`; **touched set** defined from the influence-expanded
  count cells and a **per-tile admission predicate** on `GeoMapSheet` (r3
  must-fix 3 — routing is per level, admission per emitted tile); decision-depth
  rollup = min over children, 64-shallowest reservoir with stated bytes/km²;
  descent root = `coarsest_level` throughout; equivalence pin computed from
  `gggs::Level(10).cellSize()` and `--capture-spacing-scale` named on both
  tools; spill = full `GeoSounding` at ~80 B (7.4 GB/day) per level-10 grid;
  compute multiplier row; `policy` written in both fingerprint modes;
  re-import-over-covered-ground documented; hole criterion 1.41×.
- **r5** (2026-09-14) — after plan review r4 (`e04fa0c`): admission predicate
  moved to where grids are **created** (`getOrCreateGridsIn`) with the
  enumerator filtered by the same set; touched set defined **per level** with
  the level's spacing floor (`max(recorded spread term, cell_L)`); rollup over
  *touched* children; `levelNoFinerThan` clamped to the policy range; LoA boxes
  stitched across count-tile edges, saturation = coarsest; equivalence stated
  against the persisted set and pinned from the requested resolution; Files
  row spill partition corrected to level 10.

## Implementation sync (2026-09-14, on the branch)

Where the committed implementation departs from the text above (inline sync
per `plan-task`'s during-implementation rules):

- **Emission rule (algorithm step 5)**: a grid is emitted only when its target
  is **at least its own level**; a child whose own target is coarser than its
  level is not emitted (its parent carries the ground). Step 5's "emit `g`
  regardless" produced level-13 tiles over the deep plain beside a shoal; the
  ADR-0002 amendment records the corrected rule.
- **Achieved level is evaluated once per count cell** via a per-tile histogram
  of achieved level (`CountGrid::achievedLevelHistogram` /
  `achievedLevelPercentile`), and the plan sums histograms per emitted tile;
  the per-tile percentile of *spacing* is kept for inspection only. Without
  this a survey was re-evaluated once per emitted tile at every level.
- **The recorded spread term is `Parameters::maxSpreadRadius`**, the
  level-independent cap (`CONF_99PC·√horizontal_error`) `influenceRadius`
  applies before its spacing floor, recorded as a **per-count-tile maximum**
  (not per cell); the touched set expands by `max(that, cell_L)`. Conservative:
  over-touched grids that receive nothing are never written.
- **No `sha256()`**: the plan hash had no consumer once `--replace-tiling` was
  dropped; canonical JSON determinism is tested by string equality across
  insertion orders. `LevelPlan` also records the touched sets in its JSON.
- **The spill is ONE chronological file**, not the level-10 partition of step 4
  (pre-push review must-fix 1, operator-settled): CUBE's sliding-median
  pre-filter is order-dependent, so a spatially partitioned replay would hand
  tiles coarser than the partition a different ping order than the fixed-level
  path saw, and the byte-identity claim would be false for the default levels 8
  and 9. Replaying one file front to back makes the equivalence hold by
  construction, and it also removes the unbounded per-grid `ofstream` set
  (must-fix 3). The record is the full `GeoSounding` at **64 B** (no padding),
  so a 10 h M3 day is ~5.9 GB; the free-space check projects from the bags'
  detections message count × 256 beams. `test_mixed_level_import`'s
  single-level equivalence case now runs the whole recon → plan → replay path,
  not `addBatch` alone.
- **`MultiLevelAccumulator` (step 6)** admits per level through
  `GeoMapSheet::setAdmission`, applied in `getOrCreateGridsIn` and
  `gridIndicesForSoundings`; the shared clock is `GeoMapSheet::setTouchClock`;
  the new `ImportAccumulator` API is `persistAndDrop` + `residentTiles`, and
  `evictColdTiles` now uses the same primitive. Sheets are built at
  `requestedCellSizeFor(level)` (the nominal cell nudged 1e-5 coarser so
  `fromCellSize` cannot snap a level finer); the equivalence test builds both
  paths at that requested resolution.
- **A CUBE node sits at its cell's south-west corner** (`GeoGrid::insert`
  measures distance to `CellIndex::position()`), so a sounding settles the
  nearest lattice node, which may be indexed by the cell to its north/east;
  the parents-under-children test accepts any of the four corner nodes.
- **`--replace-tiling` / plan hash / recon percentile min-count**: removed as
  the r3 revision says; the min-count guard is subsumed by the counts.
- **Files**: the mixed-level tests live in `test_mixed_level_import.cpp`
  (including the RAM-budget case; `test_tile_eviction_rss.cpp` is unchanged)
  and `test_recon.cpp` covers the recon collector; `import_bag`'s CLI
  validation is tested by running the binary. `main()` in `import_bag_main.cpp`
  crossed cpplint's function-size limit and four blocks moved into free
  functions verbatim.
- **Backscatter** is written per level into the single `--bs-store` dir; the
  mixed-level backscatter *read* test is gated on uma#383 and not in this PR's
  suite (the write path is exercised by the equivalence test's file
  comparison only for bathy).
- **Count grid is directory-backed with an LRU** (must-fix 2, operator-settled):
  `CountGrid::setSpillDir` caps the resident count tiles (`--count-resident-tiles`,
  default 256 ≈ 460 MB of level-14 tiles) and writes colder ones to the recon
  scratch dir as single-band UInt16 GeoTIFFs, reloaded on demand — during the
  recon and during plan computation, whose level-of-aggregation query needs only
  a 3×3 tile neighbourhood at a time. The plan report prints the resident peak,
  the budget and the spilled count. `tiles()` is gone from the public API;
  `grids()` + `tileAt()` replace it, and a returned `Tile *` is invalidated by
  the next access to a different tile.
- **Fingerprint records every input that decides the tiling** (must-fix 4):
  `iho_order`, `depth_adaptive_scale` (the ladder's `depth.capture_distance_scale`,
  which `--depth-adaptive-scale` actually sets), `decision_depth_percentile` and
  `achieved_percentile` join the schema-2 `tiling` object and `isStale`.
  `BuildFingerprint::write` now `fsync`s the temp file **and** the store
  directory after the rename (must-fix 6 — ADR-0003 says fsync and the code
  only `fflush`ed). A failed fingerprint write is no longer a warning that still
  prints "done!": `import_bag` exits **2** ("store complete, fingerprint not —
  a later incremental regen must do a full regen"), documented in `--help`.
- **Plan-aware `dirtyTiles` stays a conservative superset** (must-fix 5):
  footprint ground the plan emits nothing over is rolled up to the plan's
  coarsest level rather than silently dropped; an empty plan throws.
- **Spill cleanup moved after `finalize()`** so a crash between replay and
  persist no longer costs the projection pass, and `import_bag` warns about
  leftover `.recon_spill_<pid>_<start time>` directories (never deleting them: a
  concurrent import may own one).
- **The live node takes `capture_spacing_scale` as a ROS parameter**, declared
  `read_only`, so a deployment can pin the new gate (e.g. to `0.5 / cell_size`
  for the pre-#143 behaviour) from launch/YAML without a rebuild — and a runtime
  `param set` is rejected rather than accepted-and-ignored, since the value is
  read once in `on_configure`.
- **`decision_depth_percentile` is bounded** to `(0, 1]`: the per-grid depth
  histogram serves any rank at any density, so the only refused value is 0,
  which would make a single flier the decision depth. (The `(0, 0.05]` cap and
  its `kMaxDecisionDepthPercentile` constant existed only to keep a bounded
  64-deep reservoir from being asked for a rank it could not reach; the
  reservoir is gone and so is the cap.)
- **`batch_regen` still neither reads nor writes the fingerprint** — as plan
  step 9 and the ADR-0003 amendment's implementation-status note say, this PR
  is ADR-0003's first *partial* implementation: `import_bag` is the writer,
  `batch_regen --incremental`'s consumer is explicitly out of scope.
### Round-2 pre-push review fixes (2026-09-15)

The second pre-push review round (3 must-fix, 7 suggestions — all actioned, none
deferred):

- **A short spill replay is now fatal.** `ReconCollector::forEachSpilled` checks
  the stream after `flush()`/`close()`, throws on a partial trailing record, and
  returns the replayed count; `import_bag` compares that with
  `soundingsSpilled()` and fails **before** `finalize()`, so a disk-full at the
  last buffered flush can no longer drop the tail of a survey into a store that
  is then fingerprinted as complete.
- **`BuildFingerprint::write` reports the post-rename directory `open`/`fsync`
  failure** instead of returning success for a durability that did not happen —
  the same standard the temp-file write is held to.
- **Exit codes**: a failed `--tile-size-report` CSV no longer returns 1 ("the
  store may be incomplete"); it has its own code **3**, ranked below the missing
  fingerprint (2), documented in `--help`.
- **Recon free-space preflight** budgets the sounding spill *plus* an allowance
  of the same size for the count-tile spill that now shares the scratch dir. The
  count term scales with ground covered, which is not knowable before the pass,
  so it is stated as an allowance and not a bound.
- **`--count-resident-tiles` rejects a negative value** (cast to `size_t` it
  became `SIZE_MAX`, silently restoring the unbounded count grid); the check
  lives in a free function so `main()` stays inside cpplint's size limit.
- **Scratch dir keyed on pid *and* start time**, so a pid-reuse collision cannot
  present as an "already exists" refusal blamed on an unrelated dead process.
- **`ReconCollector::cleanup()` and `CountGrid::discardSpill()` report what they
  could not delete** (warnings — `cleanup()` runs from the destructor), and
  `discardSpill()` drops the `max_spread_term_` entries of the grids it forgets.
- **README** documents `--decision-depth-percentile`, `--achieved-percentile`,
  the `0 < p ≤ 5` bound the 64-deep reservoir imposes, and what the free-space
  check budgets.
- **Tests added**: `ReconCollector.ATruncatedSpillIsReportedRatherThanReplayedShort`
  (partial record throws; a whole lost record shows only in the returned count),
  `BuildFingerprint.ReportsAFailedDirectoryFsyncInsteadOfClaimingSuccess`
  (write-and-search-only directory; skipped as root), and a spread-term
  assertion in the count-grid spill test.

### Round-3 pre-push review fixes (2026-09-15)

The third pre-push review round (2 must-fix, 5 suggestions — all actioned at the
operator's direction, none deferred; the last fix pass before publish):

- **A failed spill replay is caught, not fatal-by-abort.** The `forEachSpilled`
  throws (failed flush, failed close, partial trailing record) had no handler on
  the depth-adaptive finish path and none in `main()`, so the disk-full tail the
  round-2 fix targets ended the run in `std::terminate` (SIGABRT) rather than the
  documented `error: ...` + exit 1.
- **The abort says what is on disk.** `addBatch` evicts tiles into the real `-o`
  store from the first batch, so a short or failed replay leaves partial-coverage
  tiles there; the old message ("Nothing is finalized … free space and re-run")
  invited a re-import that double-counts every twice-written tile. Both abort
  paths now go through `abortDirtyReplay()`, which names the store as dirty and
  **removes any pre-existing `build_fingerprint.json`** — it describes the store
  as it was before this run mutated it, and a later `batch_regen --incremental`
  would otherwise trust it. The double-count guarantee is closed, not just
  documented.
- **A filesystem with no directory fsync is not a failed write.** `EINVAL`,
  `ENOTSUP` and `ENOSYS` from the post-rename directory `fsync` (a number of
  network and FUSE mounts) now warn and return — the rename put a valid
  fingerprint in place, and throwing would exit 2 on every run and condemn such a
  store to a FULL regen forever. `EIO`/`ENOSPC`/`EBADF` still throw.
- **`--count-resident-tiles` states its real floor** (`CountGrid::kMinResidentTiles`
  = 16, the 3×3 level-of-aggregation neighbourhood plus headroom) at parse time
  and in `--help`, instead of validating ">= 0" and letting the `ReconCollector`
  constructor refuse 1–15 after the orphan warning and spill banner had printed.
- **The count-tile allowance is tunable**: `--count-spill-allowance <factor>`
  (default 1.0, 0 removes it). The doubling is an allowance in both directions —
  it can also refuse a dense survey over little ground whose spill would have
  fit, and `--scratch-dir` was the only advice on offer. Only the preflight check
  is affected.
- **README** says `capture_spacing_scale` is `read_only`: set from launch or
  YAML, a runtime `ros2 param set` is rejected (the operator-facing half of the
  round-2 must-fix).
- **Tests added**: `BuildFingerprint.TellsAnUnsupportedDirectoryFsyncFromAFailedOne`,
  `ImportBagCli.RefusesACountResidentBudgetBelowTheRealMinimum` and
  `ImportBagCli.CountSpillAllowanceIsTunableAndValidated` (both through the
  existing `runImportBag` harness), plus `--help` assertions for the new flag and
  the stated minimum. The two abort paths in `cube_depth_adaptive_finish` have no
  test: they are inside `main()`'s call tree past a real bag read, and this
  package has no bag fixture.
- **`main()` size**: the two new option paths pushed it past cpplint's 500-line
  function limit again, so the bag time-span report, the tiling-choice report and
  the finite-factor option check moved to free functions beside the other
  helpers.

- **Follow-ups filed / owed**: uma#383 (prerequisite for reading mixed-level
  backscatter), uma#386 (policy floor from horizontal error); to file after
  the PR: the live-node count-grid + spill follow-up, `.agents/README.md` for
  this repo, ADR-0003's inert `tool_version`, `build_bathy_store.sh` reach.

