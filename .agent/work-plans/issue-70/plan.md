# Plan: Tile eviction + incremental publish to bound long-duration growth

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

## Approach

1. **Write ADR-0008** — Commit `docs/decisions/0008-tile-eviction-and-incremental-publish.md`
   capturing: eviction policy (LRU-by-last-touch, configurable `max_resident_tiles`
   budget, lazy warm-start reload on revisit), per-tile output schema (`grid_map_msgs/
   GridMap`, topic `~/tiles`, BEST_EFFORT QoS, one message per dirty tile per publish
   cycle), and CA-grid windowing strategy (vessel-centered, `ca_window_radius_m`,
   unknown cells = NaN, lethal under `unsurveyed_is_lethal`). ADR is the interface
   contract for the deferred boat→CAMP live coverage view (uma#86, #250).

2. **LRU eviction in GeoMapSheet** — Add `last_touch_` tracking (`std::map<gggs::
   GridIndex, uint64_t>` sequence counter) updated on every insert and
   getOrCreateGrid call. Add `evictColdTiles(size_t max_resident)` that erases tiles
   beyond budget in LRU order; returns evicted indices. New ROS parameter
   `max_resident_tiles` (default 64). The node calls `evictColdTiles()` inside
   `saveDirtyTiles()` after a successful save (tiles are already on disk). On revisit,
   a new empty `GeoGrid` is created as usual and optionally warm-start primed from disk
   (the existing `loadIntoSheet` / `setPredictedDepthAt` path).

3. **Replace monolithic publish with incremental per-tile + windowed CA grid** —
   Split `publishGrid()` into two methods:
   - `publishDirtyTiles()`: for each dirty tile, call `geoMapSheetToGridMap` on that
     single tile (pass a 1-tile sheet view or adapt the helper to accept a grid span),
     publish on new `LifecyclePublisher<grid_map_msgs::msg::GridMap>` at topic
     `~/tiles`. Called at the same ~5 s cadence, clears dirty set after save.
   - `publishCaGrid()`: look up vessel position from TF (`base_link` → `map`),
     extract a `ca_window_radius_m`-radius subgrid of the resident tiles centered on
     the vessel, publish on the existing `grid` topic. Cells with no data = NaN
     (unknown/lethal under `unsurveyed_is_lethal`). Removes `clear_grid` as the
     default mitigation path (the CA grid is already bounded; keep the service for
     explicit operator resets).

4. **Update consumers** — The existing `grid` topic keeps its name, type, and frame
   (`map`); only the extent changes (bounded window vs. whole area). Nav2 costmap
   plugin needs no code change; `unsurveyed_is_lethal` defaults cover window boundary
   cells. RViz full-survey display: add a note that the full-survey reconstruction
   from `~/tiles` is out of scope for this PR (follow-up). The `clear_grid` service
   is kept (its semantics are unchanged — it resets CUBE state, not just publish).

5. **Tests** — Four additions:
   a. `test_geo_map_sheet.cpp`: `EvictionBoundsTileCount` — drive a synthetic track
      over N > `max_resident_tiles` tiles; assert resident count stays ≤ budget.
   b. `test_geo_map_sheet.cpp`: `EvictionLeavesLruTilesResident` — verify that the
      `max_resident_tiles` most-recently-touched tiles survive eviction.
   c. `test_publish_equivalence.cpp`: extend or replace `GeoProjectionMatchesLegacyMapSheet`
      to cover the per-tile projection path (feed one tile, project it, assert depth
      equivalence against the full-sheet projection). Keep
      `ProjectionPlacesCellCentersExactly` unchanged.
   d. `test_tile_eviction_rss.cpp` (new): long synthetic track (~200 tiles); assert
      `grids_.size()` stays bounded; assert on-disk tile count keeps growing beyond the
      resident budget. No RSS measurement (not portable in unit tests); tile-count
      bounding is the equivalent assertion.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/docs/decisions/0008-…md` | New ADR (created) |
| `include/cube_bathymetry/geo_map_sheet.h` | Add `last_touch_`, `evictColdTiles()`, `lastTouchOf()` |
| `src/geo_map_sheet.cpp` | Implement LRU tracking and eviction |
| `src/cube_bathymetry_node.cpp` | Replace `publishGrid()`, add `publishDirtyTiles()`/`publishCaGrid()`, new params, new publisher |
| `test/test_geo_map_sheet.cpp` | Add eviction tests |
| `test/test_publish_equivalence.cpp` | Extend for per-tile path |
| `test/test_tile_eviction_rss.cpp` | New long-track bounded-count test |
| `CMakeLists.txt` | Add `test_tile_eviction_rss` target |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Fixes confirmed operational failures; window size and tile budget are configurable, not over-engineered |
| Test what breaks | Four targeted tests covering the failure modes (unbounded count, wrong LRU order, per-tile projection regression, long-track growth) |
| A change includes its consequences | Nav2 consumer behavior documented; `clear_grid` service kept; test_publish_equivalence updated |
| Capture decisions | ADR-0008 records eviction policy, CA window strategy, and per-tile schema as shared interface contract |
| Safety First | Unknown cells at window boundary = NaN, lethal under `unsurveyed_is_lethal` — safety guarantee preserved |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (Adopt ADRs) | Yes | ADR-0008 written for this PR |
| ADR-0002 (Worktree isolation) | Yes (satisfied) | In feature/issue-70 worktree |
| ADR-0008 (ROS 2 conventions) | Yes | `~/tiles` topic, BEST_EFFORT QoS, GridMap type — all per ROS 2 conventions |

## Consequences

| If we change… | Also update… | Included? |
|---|---|---|
| Retire whole-area `grid` publish | Nav2 costmap consumer | Yes — window boundary NaN/lethal behavior preserved |
| Add `~/tiles` publisher | RViz full-survey display | Scoped out (noted, follow-up) |
| Add `max_resident_tiles` param | Launch files, parameter docs | Yes — documented in node on_configure log |
| `test_publish_equivalence.cpp` | CMakeLists (no new target, same test) | Yes |

## Open Questions

- None — all design choices captured in ADR-0008; operator decisions from Issue
  Review checkpoint are binding.

## Estimated Scope

Single PR, four staged commits: ADR → eviction → incremental publish → tests.
