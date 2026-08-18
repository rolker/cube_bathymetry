# Plan: Port predicted-surface producer (cube_grid_initialise + touchdown interpolation)

## Issue

https://github.com/rolker/cube_bathymetry/issues/59

## Context

Issue #15 landed `Node::setPredictedDepth`, `Sounding::predicted_depth_at_touchdown`, and
the corrected offset in `Node::insert`. However, with no producer wiring the fields, the
offset is always 0 in production: `predicted_depth_` stays `INVALID_DATA` on every node and
`predicted_depth_at_touchdown` stays at its sentinel on every sounding.

This issue ports the two-piece producer system that #15 explicitly deferred:

1. **External-prior load** (`cube_grid_initialise` analog): for each node, call
   `setPredictedDepth(depth, var)` from an external prior array, then seed a base null
   hypothesis via `Node::addHypothesis(depth, var)`.
2. **Per-sounding touchdown interpolation** (`cube_grid_interpolate` analog): bilinear blend
   of the 4 surrounding nodes' `predicted_depth_` at the sounding's touchdown `(x,y)`;
   write the result into `sounding_copy.predicted_depth_at_touchdown` before calling
   `node->insert`. Return the no-correction sentinel (`INVALID_DATA`) if any corner is
   no-data.

`GeoGrid::setPredictedDepthAt` (the per-cell slope-prior setter) and
`GeoMapSheet::setPredictedDepthAt` already exist. Missing pieces are: the null-hypothesis
seed companion, the interpolation method on both `Grid` and `GeoGrid`, and the node-level
prior-load wiring.

## Approach

### Step 1 — ADR: predicted-surface interpolation geometry

Write `cube_bathymetry/docs/decisions/0008-predicted-surface-interpolation-geometry.md`.
Capture: for `Grid`, standard Cartesian bilinear in pixel coordinates (identical to
`cube_grid_interpolate`); for `GeoGrid`, bilinear in GGGS row/column integer coordinates
(equivalent to the equirectangular approximation used in `GeoGrid::insert` — error < 1 mm
at any survey latitude ≥ 10°, consistent with the sub-millimetre gate already accepted
there). Rationale: the GGGS cell geometry is not a regular physical grid (column width
varies by latitude), so physical-metre bilinear would require per-cell scale factors; the
row/column integer space is a regular unit grid, matches what `GeoGrid` already uses for
distance, and is accurate enough. Both grids skip interpolation if any corner node has
`INVALID_DATA` (faithful port of `cube_grid.c:2379-2384`).

### Step 2 — Grid: external-prior load + null-hypothesis seed

Add to `Grid`:
```cpp
// grid.h
bool initializePredictedSurface(
  const std::vector<float> & depths, float variance,
  bool variance_is_percent = false,
  const std::vector<bool> * mask = nullptr);
```
Iterates `(y, x)` over `counts_.y × counts_.x`, computes per-cell variance (absolute or
`(variance/100 * depth)^2`), calls `node->setPredictedDepth(depth, var)` and
`node->addHypothesis(depth, var)` for unmasked nodes with valid `depth` (not NaN, not
`INVALID_DATA`). Mirrors `cube_grid_initialise` (`cube_grid.c:1665`) exactly. Returns
`false` if the `depths` size doesn't match `counts_.x * counts_.y`.

### Step 3 — GeoGrid: null-hypothesis seed companion

`GeoGrid::setPredictedDepthAt` already seeds slope-correction only. Add:
```cpp
// geo_grid.h
void initializePredictedDepthAt(
  const gggs::CellIndex & cell, float depth, float variance);
```
Lazy-creates the node (like `setPredictedDepthAt`), then calls `setPredictedDepth` AND
`addHypothesis`. This is the GGGS analog of the per-node body of `cube_grid_initialise`.

### Step 4 — Grid: touchdown interpolation + wire into insert

Add to `Grid`:
```cpp
// grid.h
float interpolatePredictedDepth(double x, double y) const;
```
Finds `col = floor((x - origin_.x) / sizes_.x)`, `row = floor(...)`, reads `predicted_depth_`
from the 4 corners (lazy-created nodes absent → `INVALID_DATA`), returns `INVALID_DATA` if
any corner is INVALID or NaN, otherwise bilinear blend matching `cube_grid_interpolate`
formula (`cube_grid.c:2395-2403`).

In `Grid::insert`, before the node loop, compute:
```cpp
Sounding snd_copy = sounding.sounding;
snd_copy.predicted_depth_at_touchdown =
  interpolatePredictedDepth(sounding.x, sounding.y);
```
Pass `snd_copy` to `node->insert` instead of `sounding.sounding`.

### Step 5 — GeoGrid: touchdown interpolation + wire into insert

Add to `GeoGrid`:
```cpp
// geo_grid.h
float interpolatePredictedDepth(double lat, double lon) const;
```
Finds the GGGS cell containing `(lat, lon)` via `gggs::CellIndex::fromPosition`, identifies
the 4-cell neighbourhood in `(row, col)` integer space (current cell + `row+1`, `col+1`,
`row+1,col+1`), reads each node's `predictedDepth()` (absent node → `INVALID_DATA`). If any
corner is `INVALID_DATA`, returns `INVALID_DATA`. Otherwise, computes `delta_row` and
`delta_col` as the fractional position within the cell and applies the bilinear formula.

In `GeoGrid::insert`, before the node loop:
```cpp
Sounding snd_copy = sounding.sounding;
snd_copy.predicted_depth_at_touchdown =
  interpolatePredictedDepth(geo_sounding.latitude, geo_sounding.longitude);
```
Pass `snd_copy` to `node->insert`.

### Step 6 — Prior-load mechanism in the node

**Proposed mechanism (operator decision required — see Open Questions):**
`prior_bathymetry_dir` ROS parameter pointing to a `marine_bathymetry_store` tile directory.
At `onConfigure`, if non-empty, iterate tile cells in that directory and call
`geo_map_sheet_->initializePredictedDepthAt(cell, depth, variance)` per cell, using the
tile's stored depth and uncertainty (same code pattern as the draft-tile startup prime at
`cube_bathymetry_node.cpp:252-315`, which calls `setSettledDepthAt`). This reuses all
existing tile-read infrastructure with no new format or library dependencies.

Alternative considered: `prior_bathymetry_file` pointing to a GeoTIFF. Rejected (for now)
as it would add a GDAL dependency; flag for follow-on if ENC/chart import is needed.
`Grid::initializePredictedSurface` is available for the planar grid path if needed.

`GeoMapSheet` needs a thin `initializePredictedDepthAt` forwarder (same pattern as the
existing `setPredictedDepthAt` forwarder at `geo_map_sheet.cpp:205-209`).

### Step 7 — Tests

**`test_grid.cpp`:**
- `InitializePredictedSurface_SetsDepthAndHypothesis`: seed a small grid with known depths,
  assert `node->predictedDepth()` matches and `extractDepthAndUncertainty` yields the seeded
  depth before any soundings.
- `InterpolatePredictedDepth_BilinearInterior`: 2×2 grid, known corner depths, assert
  interpolated value at interior `(0.5, 0.5)` equals expected bilinear result (within 1e-5).
- `InterpolatePredictedDepth_NoDataSentinel`: one corner node absent/INVALID → returns
  `INVALID_DATA`.
- `InsertUsesTouchdownPrediction`: seed a 3×3 grid prior, insert a sounding with known
  touchdown position, verify `predicted_depth_at_touchdown` was set on the copy by
  extracting the slope-offset from the converged estimate.

**`test_geo_grid.cpp`:**
- `InitializePredictedDepthAt_SeedsNodeAndHypothesis`: call `initializePredictedDepthAt`,
  assert `predictedDepthAt` returns seeded value and `values()` yields the seeded depth.
- `InterpolatePredictedDepth_GGGS_BilinearInterior`: seed 4 adjacent GGGS cells with known
  depths; assert interpolated value at their shared interior point.
- `InterpolatePredictedDepth_GGGS_NoDataSentinel`: one corner absent → `INVALID_DATA`.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/docs/decisions/0008-predicted-surface-interpolation-geometry.md` | New ADR (Grid Cartesian vs GeoGrid GGGS row/col bilinear decision) |
| `cube_bathymetry/include/cube_bathymetry/grid.h` | Declare `initializePredictedSurface`, `interpolatePredictedDepth` |
| `cube_bathymetry/src/grid.cpp` | Implement both; call `interpolatePredictedDepth` in `insert` |
| `cube_bathymetry/include/cube_bathymetry/geo_grid.h` | Declare `initializePredictedDepthAt`, `interpolatePredictedDepth` |
| `cube_bathymetry/src/geo_grid.cpp` | Implement both; call `interpolatePredictedDepth` in `insert` |
| `cube_bathymetry/include/cube_bathymetry/geo_map_sheet.h` | Declare `initializePredictedDepthAt` forwarder |
| `cube_bathymetry/src/geo_map_sheet.cpp` | Implement forwarder |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | Add `prior_bathymetry_dir` param + startup prime loop calling `initializePredictedDepthAt` per tile cell |
| `cube_bathymetry/test/test_grid.cpp` | 4 new tests (bilinear accuracy, sentinel, insert wiring, init) |
| `cube_bathymetry/test/test_geo_grid.cpp` | 3 new tests (GGGS bilinear, sentinel, init) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Tests land in same PR; README updated to document `prior_bathymetry_dir` parameter |
| Test what breaks | Bilinear math, no-data sentinel, and the insert wiring are all tested explicitly |
| Only what's needed | No GeoTIFF/GDAL dependency; reuses existing tile-store machinery |
| Capture decisions, not just implementations | Grid-vs-GeoGrid geometry captured in ADR-0008 before code lands |
| Improve incrementally | Prior-load and interpolation land together (neither delivers value alone); no other scope |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0001 (project) — tile eviction | Indirectly | `initializePredictedDepthAt` lazy-creates nodes; no dirty-marking (reproduces prior data, not survey data). Consistent with `setSettledDepthAt` pattern. |
| 0008 (workspace) — ROS 2 conventions | Yes | New public C++ API follows ROS 2 naming/style; new node parameter follows `declare_parameter` pattern |
| ADR-0008 (project, new) | Yes — this PR writes it | Commits the Grid-vs-GeoGrid geometry decision before implementation |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `Grid::insert` wraps sounding copy | Existing `Grid::insert` callers unchanged (public API unchanged) | Yes — internal |
| `GeoGrid::insert` wraps sounding copy | Same | Yes — internal |
| `GeoMapSheet` adds `initializePredictedDepthAt` | `cube_bathymetry_node.cpp` startup prime | Yes |
| New `prior_bathymetry_dir` parameter | README / node parameters section | Yes |

## Documentation & Instruction Impact

- **Stale docs**: `cube_bathymetry/README.md` node-parameters section — add `prior_bathymetry_dir` (type, default, effect).
- **Agent-instruction candidates**: None — the pattern (port an original C function, wrap sounding copy in insert, lazy-create nodes) is standard for this repo and already documented in `docs/divergences_from_calder.md`.

## Open Questions

- [ ] **Prior-load mechanism**: plan proposes `prior_bathymetry_dir` (existing tile-store format at startup). Operator to confirm or redirect to GeoTIFF/ENC import. This drives the node implementation (Step 6).
- [ ] **GeoGrid GGGS cell-corner convention**: `(row, col)` of current cell + `(row+1, col+1)` assumes cell centers are row/col integers. Confirm GGGS convention for fractional position within a cell (which corner is the origin?). Will be pinned during implementation via `gggs::CellIndex::fromPosition` + `CellAreaIterator` inspection.
- [ ] **Variance convention for prior load**: original `cube_grid_initialise` supports both fixed-metres and percentage-of-depth variance. Confirm which mode the node should expose as a parameter (or expose both with a boolean param).

## Estimated Scope

Single PR. ~10 new public methods across 5 headers/sources, one new ADR, ~200 lines of
tests. Risk: low — the node-level insert paths are the hot path but the new bilinear
interpolation is a cheap pre-loop operation (O(1) per sounding, 4 hash-map probes).
