# ADR-0008: Predicted-surface touchdown interpolation geometry

## Status

Accepted.

## Context

Issue #15 re-enabled CUBE's slope correction in `Node::insert`: when a node holds a
predicted depth and the sounding carries `predicted_depth_at_touchdown`, the queued
depth is offset by `predicted_depth_(node) − predicted_depth_at_touchdown`. The
external-prior load side already exists — `primeFromTile`/`loadIntoSheet`
(`store_import.cpp`, cube#89/#96) seed `predicted_depth_` from a Reference/Chart tile —
but nothing computed a predicted depth at each sounding's touchdown, so the offset was
always 0. Issue #59 ports the original's `cube_grid_interpolate` to close that gap. The
port must pick an interpolation geometry for each grid type.

## Decision

### Node lattice, not cell centers

`gggs::CellIndex::position()` returns the cell's **south-west corner**, and
`GeoGrid::insert` measures the node-to-sounding distance against exactly that position.
GGGS nodes therefore sit **on the integer (row, column) lattice** (rows from south,
columns from west, 960 per grid). The planar `Grid` likewise places node (x, y) at
`origin + index · sizes`.

Consequently the faithful port of `cube_grid_interpolate`'s `floor` lower-left pick is:

- **`Grid`** (planar): continuous pixel coordinates `rx = (x − origin.x)/sizes.x`,
  `ry = (y − origin.y)/sizes.y`; lower-left node `(floor(rx), floor(ry))`; bilinear
  weights `(rx − floor(rx), ry − floor(ry))` over the 4 surrounding nodes.
- **`GeoGrid`** (GGGS): continuous cell coordinates
  `r = (lat − southLatitude)/latitudinalSpan × 960`,
  `c = (normalizeLongitude(lon) − westLongitude)/longitudinalSpan × 960`; same floor
  lower-left stencil and weights, evaluated in (row, column) integer space.

Quadrant-based neighbor selection (±1 by which side of the cell *center* the touchdown
falls) would be correct only for center-positioned nodes; with SW-corner nodes it is
wrong, so it is deliberately not used.

Bilinear in GGGS (row, column) space is an equirectangular approximation within one
cell — the same approximation `GeoGrid::insert` already uses for node distances, where
its error was accepted as sub-millimetre at survey latitudes (cube#63). Physical-metre
bilinear would need per-row longitude scale factors for no measurable gain.

### Sentinels

The interpolation returns the no-correction sentinel `INVALID_DATA` when:

- the lower-left index is out of range (`Grid`: outside `[0, counts−2]`; `GeoGrid`:
  outside `[0, 958]` on either axis — the stencil would cross the tile edge), or
- any of the 4 corner nodes is absent, has no prediction (`INVALID_DATA`), or is NaN.

`Sounding::predicted_depth_at_touchdown` defaults to `INVALID_DATA`, and `Node::insert`
treats it as "no correction" — so every sentinel path degrades to the pre-#59
correct-but-uncorrected behaviour, never to a wrong correction. This mirrors the
original, where `cube_grid_interpolate` returns 0.0 on missing corners/out-of-tile and
the caller's `range != 0.0` guard skips the offset.

### Per-tile scope

Interpolation is evaluated per `GeoGrid` (tile). A sounding whose influence region
crosses a tile boundary gets slope-corrected inserts in the tile containing its
touchdown and uncorrected (sentinel) inserts in the neighbor tile's edge nodes. The
original had the same per-tile limitation. Accepted: edge nodes see a mix of corrected
and uncorrected soundings, biased toward uncorrected only within one cell of the tile
seam.

### The prior gates and corrects — it never fills

Slope correction consumes only `predicted_depth_` (and the blunder gate consumes its
variance). Seeding a base **hypothesis** from the prior — as the original
`cube_grid_initialise` did — is deliberately **not** ported: cube#89 decided a coarse
Chart/Reference prior must gate blunders and drive slope correction WITHOUT filling the
survey layer (which would also contaminate co-estimated backscatter with non-measured
cells). #59 upholds that: no `addHypothesis` from any prior path.

### Divergence from the original: no `var_pred`

`cube_grid_interpolate` optionally outputs an interpolated prediction variance
(`var_pred` via `cube_grid_est_interp_error`). The port threads only the interpolated
**depth** into `predicted_depth_at_touchdown`: `Node::insert`'s offset is a pure depth
delta and has no variance consumer. Recorded here (and consistent with
`docs/divergences_from_calder.md`'s verify-against-source rule) so a future consumer of
prediction variance at the touchdown knows it must port `cube_grid_est_interp_error`.

## Consequences

- Slope correction becomes active end-to-end on the offline import / batch-regen path,
  which already primes the Reference prior. The live node activates when cube#91 (live
  chart-prior seeding) lands — no further change needed here.
- `Grid` gains `setPredictedDepthAt(x, y, depth, variance)` — the minimal planar peer
  of `GeoGrid::setPredictedDepthAt` (lazy-create + `Node::setPredictedDepth`, no
  hypothesis).
- Both `insert` paths copy the `Sounding` once per sounding to stamp the touchdown
  value; O(1) with 4 node lookups, ahead of the per-cell loop (hot-path constraint of
  cube#63/#107 respected).
