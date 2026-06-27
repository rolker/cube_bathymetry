# Plan: Lossless tile eviction + incremental publish to bound long-duration growth

## Issue

https://github.com/rolker/cube_bathymetry/issues/70

## Context

Two unbounded-growth paths survived the #21 GeoMapSheet migration and caused a
real deployment incident (udp_bridge saturation, 2026-06-10). #69 (single-fused-grid
store API) is confirmed merged (PR#71, 2026-06-26) — this plan targets the post-#69
layout. Both problems are fixed in one combined PR with staged commits per operator
decision.

**Problem 1**: `GeoMapSheet::grids_` (a `std::map<gggs::GridIndex, shared_ptr<GeoGrid>>`)
only grows. Each tile is up to 920k cells; RAM scales with surveyed area forever.

**Problem 2**: `publishGrid()` calls `geoMapSheetToGridMap()` over ALL grids every
~5 s, emitting a single monolithic `GridMap`. Message size and per-cycle cost grow
with surveyed area.

**Design record**: `docs/decisions/0001-tile-eviction-and-incremental-publish.md`
(the project's first ADR; numbering is per-repo, so it is `0001`, NOT workspace
ADR-0008). Two key operator decisions, settled during implementation, shape the
design:
- Eviction must be **lossless**: persist the tile before dropping it, and never
  evict-and-lose. Without a configured `draft_dir`, do not evict (WARN instead).
- Lossless reload is achieved by **reseeding settled CUBE state** (not merge-on-save):
  a revisited/primed tile re-emits its persisted depth/uncertainty through
  `values()` so the next save is complete.

## Approach

### A. Lossless reload of settled CUBE state (commit: reload)

1. **`Node::seedSettledDepth(depth, uncertainty, params)`** — creates one hypothesis
   with `current_estimate=depth`, `number_of_samples=1`, and
   `input_sample_variance = current_variance = predicted_variance =
   (uncertainty / stddev_to_confidence_interval_scale)²`. Round-trips through
   `extractDepthAndUncertainty` and acts as the prior for continued accumulation
   (West-Harrison DLM). `number_of_samples=1` so new in-situ data dominates.
2. **`GeoGrid::setSettledDepthAt(cell, depth, uncertainty)`** and
   **`GeoMapSheet::setSettledDepthAt(cell, depth, uncertainty)`** — surface the seed
   up the stack (mirror `setPredictedDepthAt`). Do NOT mark dirty (reproduce
   persisted data; the next survey ping dirties it).
3. **`primeFromTile` / `loadIntoSheet`** — in addition to the existing
   `setPredictedDepthAt` (slope prior, kept), call `setSettledDepthAt` so the
   startup prime round-trips through `values()`. Closes the latent cross-session
   partial-revisit data loss.

### B. LRU eviction, persist-then-drop, persistence-required (commit: eviction)

4. **LRU tracking in GeoMapSheet** — `last_touch_` (`std::map<GridIndex,uint64_t>`)
   bumped on `getOrCreateGridsIn` insert and `getOrCreateGrid`. Add
   `coldTiles(size_t max_resident)` (coldest indices beyond budget, LRU order),
   `dropTile(index)` (erase from `grids_`, `last_touch_`, both dirty sets),
   `residentTileCount()`, `lastTouchOf(index)`. New ROS param `max_resident_tiles`
   (default 64).
5. **Node-orchestrated eviction** on the maintenance tick: if `draft_dir` set and
   `residentTileCount() > max_resident_tiles`, for each cold tile: save it if dirty
   (complete state), then `dropTile()` and record in `evicted_indices_`. If
   `draft_dir` empty: do NOT evict; throttled WARN that RAM is unbounded without
   persistence.
6. **Revisit reload** — in `pingCallback`, before `addSoundings`, map each sounding
   to its grid index; for any index in `evicted_indices_`, load that single tile
   from disk (`loadWindow` over the tile bounds → scratch store) and reseed via the
   settled-depth path, then erase from `evicted_indices_`.

### C. Bounded publish: windowed CA grid + incremental tiles (commit: publish)

7. Split `publishGrid()` into:
   - **`publishCaGrid()`** — vessel position from TF (`earth` ← `base_link`); select
     resident grids within `ca_window_radius_m` (new param, default 200.0); project
     only those onto the existing `grid` topic. Unknown cells = NaN (lethal under
     `unsurveyed_is_lethal`). TF miss → fall back to full resident set (bounded by
     eviction) rather than dropping the CA grid.
   - **`publishDirtyTiles()`** — for each publish-dirty tile, project that single
     tile and publish on a new `~/tiles` `LifecyclePublisher<GridMap>` (BEST_EFFORT).
8. **GeoMapSheet publish-dirty set** — second set tracked alongside the save-dirty
   set (`publishDirtyGrids()` / `clearPublishDirtyGrids()`) so publish and save
   cadences don't interfere and the set clears regardless of persistence.
9. **Projection subset** — refactor `grid_projection` to expose
   `geoGridsToGridMap(grids, frame, cell_size, affine)`; `geoMapSheetToGridMap`
   keeps its signature and delegates. Per-tile and windowed publishes call the subset
   form.
10. **Coherence WARN** — at `on_configure`, WARN if `max_resident_tiles` < the CA
    window's tile span. `clear_grid` service kept (CUBE-state reset semantics).

### D. Tests (commit: tests)

a. `test_node.cpp` / `test_geo_grid.cpp`: `SeedSettledDepthRoundTrips` — seed a value,
   assert `values()` / `extractDepthAndUncertainty` re-emit the same depth + uncertainty.
b. `test_geo_map_sheet.cpp`: `EvictionBoundsTileCount` — synthetic track over
   N > budget tiles; `coldTiles` + `dropTile` keep `residentTileCount() ≤ budget`.
c. `test_geo_map_sheet.cpp`: `EvictionLeavesLruTilesResident` — the most-recently-touched
   `max_resident_tiles` tiles survive eviction.
d. `test_persistence.cpp` (or new): `RevisitAfterEvictPreservesData` — survey a tile,
   save, drop, reseed from the saved tile, sparsely resurvey, re-save; assert the
   un-resurveyed cells are intact on disk (the lossless guarantee).
e. `test_publish_equivalence.cpp`: extend to cover the per-tile (`geoGridsToGridMap`)
   path; keep `ProjectionPlacesCellCentersExactly` unchanged.
f. `test_tile_eviction_rss.cpp` (new): long synthetic track; assert `grids_.size()`
   stays bounded while on-disk tile count keeps growing.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/docs/decisions/0001-…md` | New ADR (created) |
| `include/cube_bathymetry/node.h` / `src/node.cpp` | `seedSettledDepth()` |
| `include/cube_bathymetry/geo_grid.h` / `src/geo_grid.cpp` | `setSettledDepthAt()` |
| `include/cube_bathymetry/geo_map_sheet.h` / `src/geo_map_sheet.cpp` | `setSettledDepthAt`, last-touch, `coldTiles`, `dropTile`, `residentTileCount`, `lastTouchOf`, publish-dirty set |
| `include/cube_bathymetry/store_import.h` / `src/store_import.cpp` | `primeFromTile` also seeds settled depth |
| `include/cube_bathymetry/grid_projection.h` / `src/grid_projection.cpp` | `geoGridsToGridMap` subset projection |
| `src/cube_bathymetry_node.cpp` | windowed `publishCaGrid`, `publishDirtyTiles`, eviction + revisit reload, new params, `~/tiles` publisher, coherence WARN |
| `test/test_node.cpp`, `test/test_geo_map_sheet.cpp`, `test/test_persistence.cpp`, `test/test_publish_equivalence.cpp`, `test/test_tile_eviction_rss.cpp` | new + extended tests |
| `CMakeLists.txt` | add `test_tile_eviction_rss` target |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Fixes confirmed operational failures; eviction/window sizes configurable |
| Test what breaks | Round-trip, bounded count, LRU order, **lossless revisit**, per-tile projection, long-track growth |
| A change includes its consequences | Nav2 consumer behavior documented; `clear_grid` kept; latent cross-session loss closed |
| Capture decisions | ADR-0001 records reload mechanism, eviction policy, window strategy, per-tile schema |
| Safety First | Window-boundary unknown cells = NaN (lethal under `unsurveyed_is_lethal`); TF-gap fallback keeps CA grid alive; **no survey-data loss** |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| Workspace ADR-0001 (Adopt ADRs) | Yes | Project ADR-0001 written |
| Workspace ADR-0002 (Worktree isolation) | Yes (satisfied) | In feature/issue-70 worktree |
| Workspace ADR-0008 (ROS 2 conventions) | Yes | `~/tiles` topic, BEST_EFFORT QoS, GridMap type per ROS 2 conventions |

## Consequences

| If we change… | Also update… | Included? |
|---|---|---|
| Retire whole-area `grid` publish | Nav2 costmap consumer | Yes — window boundary NaN/lethal preserved + TF-gap fallback |
| Add `~/tiles` publisher | RViz full-survey display | Scoped out (noted, follow-up) |
| Add eviction | Reload path (lossless) + startup prime | Yes — settled-depth reseed both places |
| Add params (`max_resident_tiles`, `ca_window_radius_m`) | Launch/param docs | Yes — on_configure logs + coherence WARN |

## Open Questions

- None — design choices captured in ADR-0001; operator decisions (lossless
  persist-then-drop; settled-state reload; both evict + startup scope; `n=1`
  prior; persistence required for eviction) are binding.

## Estimated Scope

Single PR, staged commits: ADR → reload → eviction → publish → tests.
