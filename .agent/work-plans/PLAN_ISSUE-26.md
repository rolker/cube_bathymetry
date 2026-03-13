# Plan: Build failure — cellColumnCount() API change in marine_autonomy

## Issue

https://github.com/rolker/cube_bathymetry/issues/26

## Context

The upstream `marine_autonomy` package changed `gggs::GridBounds::cellColumnCount()`
to require a `uint32_t row` parameter, reflecting that GGGS uses polar scaling
(1×/3×/9× longitude widening at 72°/80° latitude thresholds). The previous no-arg
signature assumed uniform column counts across all rows.

`bag_to_geotiff.cpp` calls `bounds.cellColumnCount()` at line 432 to determine the
total GeoTIFF raster width. This is the only broken call site — the other 6 calls
use `grid->index().cellColumnCount()` on `GridIndex`, which is a `static constexpr`
(always 960) and is unaffected.

However, fixing line 432 requires more than adding a row parameter. A GeoTIFF has
uniform pixel dimensions, but `GridBounds` may span rows with different column
counts. Per maintainer direction: use the widest row for raster width, and
interpolate narrower (polar-scaled) rows to fill the full width.

## Approach

1. **Compute max column count across all rows** — Iterate from
   `bounds.minimum().row()` to `bounds.maximum().row()`, calling
   `bounds.cellColumnCount(row)` for each grid row, and track the maximum.
   Use this as the GeoTIFF raster width (`columns`).

2. **Adjust the per-grid write loop for variable column widths** — Currently
   lines 466–483 compute `column_offset` and write each grid's 960×960 block
   assuming uniform column spacing. When a grid is in a polar-scaled row
   (fewer grid columns = fewer cell columns in the bounds), its data must be
   stretched to fill the wider raster. The stretch factor is the ratio of
   the max column count to the current row's column count (always 1, 3, or 9).

   For rows that need stretching:
   - Use nearest-neighbor resampling: each source cell maps to
     `stretch_factor` output pixels.
   - Expand values into a temporary row buffer before writing to GDAL.
   - Apply the same stretching to both depth and uncertainty bands.

3. **Adjust column_offset for polar rows** — The `column_offset` calculation
   on line 468 uses `grid->index().cellColumnCount()` (always 960). This is
   correct for the source data indexing, but the *output* column offset must
   be scaled by the stretch factor so polar grids are placed correctly in
   the wider raster.

4. **Build and verify** — Confirm `bag_to_geotiff` compiles and links
   successfully. Since there are no automated tests for this tool, verify
   the build is the primary check.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/bag_to_geotiff.cpp` | Fix `cellColumnCount()` call; add max-column computation; add row stretching logic for polar-scaled rows |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | The only downstream consumer of this code is the GeoTIFF file itself. No documentation references specific output dimensions. Build verification is sufficient. |
| Only what's needed | Minimal change: fix the compilation error and handle variable-width rows. No refactoring beyond what's needed. |
| Test what breaks | No existing tests for `bag_to_geotiff`. Adding tests is tracked separately (#3, #4). Build verification confirms the fix compiles. |
| Improve incrementally | Single focused fix in one file. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0002 — Worktree isolation | Yes | Working in `feature/issue-26` worktree |
| 0008 — ROS 2 conventions | No | No package structure changes |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| GeoTIFF output geometry | Any documentation describing output format | Yes — no such docs exist currently |
| `bag_to_geotiff.cpp` compilation | `make build` should succeed end-to-end | Yes — build verification in step 4 |

## Open Questions

- **Interpolation method**: Nearest-neighbor is simplest and preserves exact
  depth/uncertainty values. Bilinear would smooth across cell boundaries.
  Plan uses nearest-neighbor; maintainer can request bilinear if preferred.

## Estimated Scope

Single PR, single file change.
