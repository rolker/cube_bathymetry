# Plan: Port predicted-surface producer (touchdown interpolation; prior load reuses the Reference prime)

## Issue

https://github.com/rolker/cube_bathymetry/issues/59

> Revised after Plan Review (progress.md, 2026-08-18) and the operator checkpoint:
> reuse the existing Reference-layer prime instead of a new prior-load path, and
> the prior gates/slope-corrects only — it never seeds hypotheses (upholds the
> cube#89 "coarse prior gates, does not fill" decision).

## Context

Issue #15 landed `Node::setPredictedDepth`, `Sounding::predicted_depth_at_touchdown`, and
the corrected offset in `Node::insert`. Since then, cube#89/#96 also landed the
**external-prior load**: `loadIntoSheet(store, SourceLayer::Reference, sheet,
/*seed_settled=*/false)` → `primeFromTile` (`store_import.cpp`) walks a Reference/Chart
tile and seeds `predicted_depth_` on every finite cell via
`GeoMapSheet::setPredictedDepthAt`, and the batch importer already primes from a coarse
Reference prior. So Piece 1 of the original `cube_grid_initialise` port **already
exists** on the GeoGrid path.

What is missing is Piece 2 — the `cube_grid_interpolate` analog: nothing computes a
predicted depth **at each sounding's touchdown**, so `predicted_depth_at_touchdown`
stays at its `INVALID_DATA` sentinel and the #15 offset is always 0. This plan adds the
touchdown interpolation on both grids and wires it into `insert`.

Scope of activation: with this PR, slope correction becomes active on the **offline
import / batch-regen path** (which already primes the Reference prior). The **live
node** does not yet prime a prior at startup — that is cube#91 (open, separate); when
it lands, the same interpolation activates live with no further change here.

## Geometry (resolves the review's stencil finding)

`gggs::CellIndex::position()` is the cell's **south-west corner**, and
`GeoGrid::insert` distance-gates nodes against exactly that position — i.e. GGGS nodes
sit **on the integer (row, col) lattice**, not at cell centers. Therefore the faithful
port of `cube_grid_interpolate`'s `floor` lower-left pick is:

- continuous coords `r = (lat − southLatitude)/latitudinalSpan × 960`,
  `c = (normLon − westLongitude)/longitudinalSpan × 960`
- lower-left node `(row0, col0) = (floor(r), floor(c))`, corners
  `(row0, col0) … (row0+1, col0+1)`, weights `(r − row0, c − col0)`

Quadrant-based neighbor selection (±1 by side-of-center) would be correct only for
center-positioned nodes; with SW-corner nodes it would be wrong. The planar `Grid` is
the same in map coordinates: nodes at `origin + index·sizes`, `floor` lower-left.

Out-of-range stencils (touchdown within the edge cell row/column of a tile, or outside
it) return `INVALID_DATA` → no correction — the same per-tile limitation as the
original (`cube_grid_interpolate` errors out for points outside the tile). A sounding
whose influence crosses a tile boundary gets corrected inserts in its own tile and
uncorrected inserts in the neighbor; documented in the ADR.

**Recorded divergence** (review suggestion): the port threads only the interpolated
**depth** into `predicted_depth_at_touchdown` — the original's optional `var_pred`
output has no consumer in `Node::insert`'s depth-delta offset, so it is not ported.

## Approach

### Step 1 — ADR: predicted-surface interpolation geometry

Write `cube_bathymetry/docs/decisions/0008-predicted-surface-interpolation-geometry.md`
capturing: SW-corner node lattice ⇒ floor lower-left stencil on both grids; bilinear in
(row, col) integer space for `GeoGrid` (equirectangular within a cell, consistent with
`GeoGrid::insert`'s accepted sub-mm approximation) and in pixel space for `Grid`; the
`INVALID_DATA` sentinel on any missing corner / out-of-range; the per-tile edge
limitation; the no-`var_pred` divergence; and that the prior **gates and
slope-corrects only** — never fills (cube#89 upheld; no hypothesis seeding anywhere in
this PR).

### Step 2 — Grid: predicted-depth seeding + touchdown interpolation + wiring

```cpp
// grid.h
void setPredictedDepthAt(uint32_t x, uint32_t y, float depth, float variance);
float interpolatePredictedDepth(double x, double y) const;
```

`setPredictedDepthAt` is the minimal planar analog of `GeoGrid::setPredictedDepthAt`
(lazy-create node, forward to `Node::setPredictedDepth`) — no hypothesis seed, no bulk
loader. `interpolatePredictedDepth`: `rx = (x − origin.x)/sizes.x`, `col0 = floor(rx)`
(similarly `row0`); require `0 ≤ col0 ≤ counts.x−2` and `0 ≤ row0 ≤ counts.y−2`; read
the 4 corner nodes' `predictedDepth()` (absent node ⇒ `INVALID_DATA`); any corner
`INVALID_DATA`/NaN ⇒ return `INVALID_DATA`; else standard bilinear.

In `Grid::insert`, after the door gate and before the node loop:

```cpp
Sounding snd = sounding.sounding;
snd.predicted_depth_at_touchdown = interpolatePredictedDepth(sounding.x, sounding.y);
```

and pass `snd` to `node->insert`.

### Step 3 — GeoGrid: touchdown interpolation + wiring

```cpp
// geo_grid.h
float interpolatePredictedDepth(double latitude, double longitude) const;
```

Continuous (r, c) from `index_`'s south/west + spans (× 960, longitude via
`normalizeLongitude`), floor lower-left, require `row0/col0 ∈ [0, 958]`, corners via
`predictedDepthAt`-equivalent lookups (4 hash probes), sentinel rules as above,
bilinear in (r − row0, c − col0).

In `GeoGrid::insert`, after the door gate and before the cell loop: copy the sounding,
set `predicted_depth_at_touchdown = interpolatePredictedDepth(lat, lon)`, insert the
copy. O(1) per sounding — negligible against the per-cell loop (cube#63/#107 hot-path
constraint respected).

### Step 4 — Tests

**`test_grid.cpp`:**
- `SetPredictedDepthAt_RoundTrip`: seed, verify via interpolation at an exact node.
- `InterpolatePredictedDepth_BilinearInterior`: 2×2 seeded corners, assert exact
  bilinear value at an **off-center** interior point (deltas ≠ 0.5 on both axes, to
  catch axis mix-ups).
- `InterpolatePredictedDepth_NoDataSentinel`: one corner unseeded ⇒ `INVALID_DATA`.
- `InterpolatePredictedDepth_OutOfRange`: touchdown outside the node lattice ⇒
  `INVALID_DATA`.
- `InsertAppliesSlopeOffset`: seed a sloped 2×2 predicted surface, insert a sounding at
  an off-node touchdown, verify the converged estimate carries the predicted-surface
  offset (end-to-end wiring through `Node::insert`).
- `InsertWithoutPrior_Unchanged`: no prior seeded ⇒ estimate equals the plain sounding
  depth (offset 0 path intact).

**`test_geo_grid.cpp`:**
- `InterpolatePredictedDepth_GGGS_BilinearInterior`: seed 4 adjacent cells via
  `setPredictedDepthAt`, assert the bilinear value at an off-center point between
  their SW corners.
- `InterpolatePredictedDepth_GGGS_NoDataSentinel`: one corner absent ⇒ `INVALID_DATA`.
- `InterpolatePredictedDepth_GGGS_EdgeSentinel`: point whose stencil crosses the tile
  edge (row0 = 959) ⇒ `INVALID_DATA`.
- `InsertAppliesSlopeOffset_GGGS`: seeded sloped prior + insert ⇒ offset visible in the
  converged estimate; and unseeded grid ⇒ unchanged estimate.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/docs/decisions/0008-predicted-surface-interpolation-geometry.md` | New ADR (geometry, sentinels, gates-not-fills, divergences) |
| `cube_bathymetry/include/cube_bathymetry/grid.h` | Declare `setPredictedDepthAt`, `interpolatePredictedDepth` |
| `cube_bathymetry/src/grid.cpp` | Implement both; wire touchdown into `insert` |
| `cube_bathymetry/include/cube_bathymetry/geo_grid.h` | Declare `interpolatePredictedDepth` |
| `cube_bathymetry/src/geo_grid.cpp` | Implement; wire touchdown into `insert` |
| `cube_bathymetry/test/test_grid.cpp` | 6 new tests |
| `cube_bathymetry/test/test_geo_grid.cpp` | 4 new tests |

No `GeoMapSheet`, node-parameter, or `cube_bathymetry_node.cpp` changes: the prior load
already exists (`loadIntoSheet`/`primeFromTile`), and live-node priming is cube#91.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Tests land in the same PR; ADR records the decisions; no public-API removal |
| Test what breaks | Bilinear math, sentinels, out-of-range, and end-to-end insert wiring all tested |
| Only what's needed | No new prior-load path (reuses Reference prime); no hypothesis seeding; no GDAL |
| Capture decisions, not just implementations | ADR-0008 (project) written before code |
| Improve incrementally | Activates offline path now; live path activates via cube#91 with no change here |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `Grid`/`GeoGrid::insert` pass a per-sounding copy | Public API unchanged; callers unaffected | Yes — internal |
| Slope correction active on offline import | `docs/divergences_from_calder.md` no-`var_pred` note (in ADR) | Yes |
| New `Grid::setPredictedDepthAt` | Peer-documented against GeoGrid's | Yes |

## Open Questions

None — the three prior open questions were resolved: prior source = existing
Reference-layer prime (plan review must-fix, operator confirmed); GGGS corner
convention = SW-corner lattice (pinned by reading `cell_index.h`); the variance-mode
parameter question is moot (no new prior-load path).

## Estimated Scope

Single PR. 3 new public methods across 2 headers, one new ADR, ~10 tests. Risk: low —
interpolation is O(1)/sounding (4 lookups) ahead of the per-cell loop.
