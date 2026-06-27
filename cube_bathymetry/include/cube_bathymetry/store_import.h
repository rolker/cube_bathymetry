// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


#ifndef CUBE_BATHYMETRY__STORE_IMPORT_H_
#define CUBE_BATHYMETRY__STORE_IMPORT_H_

#include <cstdint>
#include <map>

#include "cube_bathymetry/geo_grid.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_mbes_backscatter_store/mbes_cell.hpp"

namespace cube
{

/// @brief Design note (issue #21) — live CUBE draft-tile persistence.
///
/// The live `cube_bathymetry_node` accumulates into a geographic
/// @ref GeoMapSheet (migrated from the Cartesian `MapSheet`, #21) so that
/// `geoGridToTile` / `mapSheetToTiles` persist live data directly into the
/// `marine_bathymetry_store` `draft/` layer tiles via `tile_io` (atomic
/// temp-then-rename) with NO lossy Cartesian→geographic resample. The single
/// fused `draft` grid (no per-day epochs, unh_marine_autonomy#221) accumulates
/// newest-value-wins per cell. The costmap `bathymetry_layer` (#164) and the sim
/// live loop (#77) then read exactly what CUBE writes.
///
/// Restart recovery primes `Node::setPredictedDepth` from the loaded draft depth
/// (warm-start for slope correction) -- it does NOT reconstruct CUBE hypothesis,
/// queue, or pre-filter state (full Node deserialization is out of scope). CUBE
/// continues accumulating from scratch on top of the seeded prediction surface.

/// @brief Convert one CUBE @ref GeoGrid into a marine_bathymetry_store tile.
///
/// `GeoGrid::values()` is a flat, positional `vector<DepthAndUncertainty>` in
/// `gggs::CellAreaIterator` order over `grid.index()` (row 0 = south, column 0 =
/// west, row-major; NaN-filled where no estimate exists). `BathymetryTile::set`
/// uses the **same** GGGS cell order (`TiledRasterTile::offset(row,col)` is
/// row-major, row 0 = south), so we walk a second `CellAreaIterator` over the
/// same index in lockstep with `values()` and read `(*it).row()/.column()` to
/// recover each value's destination cell. Only **finite-depth** cells are
/// written; NaN cells are skipped (the tile defaults to NaN no-data).
///
/// `values()` returns `float`; the store cell is `double`, so each field is
/// widened. `values()` mutates node state (it flushes the median pre-filter), so
/// it is called exactly **once** per grid here.
///
/// @param grid          The CUBE grid to convert.
/// @param timestamp_ns  Acquisition/import time written into every finite cell
///                      (nanoseconds since the Unix epoch). A single value per
///                      import keeps the result deterministic.
/// @param source_index  Registry source index written into every finite cell.
/// @return A `BathymetryTile` for `grid.index()` holding the finite cells.
  marine_bathymetry_store::BathymetryTile geoGridToTile(
    const GeoGrid & grid, int64_t timestamp_ns, uint16_t source_index);

/// @brief Convert every grid of a @ref GeoMapSheet into a per-grid tile map.
///
/// Iterates `map_sheet.grids()` and converts each through @ref geoGridToTile.
/// A grid that produces **no** finite cells is omitted (an empty tile would
/// just persist as an all-no-data file). The result is keyed by
/// `gggs::GridIndex` and is suitable to pass straight to
/// `marine_bathymetry_store::BathymetryStore::importTiles`.
///
/// Deterministic for a fixed map sheet: `grids()` returns grids in
/// `gggs::GridIndex` map order, and each grid converts deterministically.
///
/// @param map_sheet     The CUBE map sheet to convert.
/// @param timestamp_ns  Acquisition/import time for every finite cell.
/// @param source_index  Registry source index for every finite cell.
  std::map < gggs::GridIndex, marine_bathymetry_store::BathymetryTile >
  mapSheetToTiles(
    const GeoMapSheet & map_sheet, int64_t timestamp_ns, uint16_t source_index);

/// @brief Convert one CUBE @ref GeoGrid into its finite-backscatter cells (#80).
///
/// The offline backscatter counterpart to @ref geoGridToTile. Walks
/// `grid.nodeRecords()` (the enriched per-node output, ADR-0007 D5) in lockstep
/// with a `gggs::CellAreaIterator` over `grid.index()` — the same positional
/// scheme @ref geoGridToTile uses for the bathy tile — and emits one
/// `marine_mbes_backscatter_store::MbesCell` per cell whose co-estimated
/// `intensity` is finite. Cells with NaN intensity (no intensity-bearing beams)
/// are skipped, mirroring the NaN-depth skip in the bathy path.
///
/// The returned map is keyed by `gggs::CellIndex` so the caller can write each
/// cell with the store's public `set()` (no bulk import API is added to the
/// separate `marine_mbes_backscatter_store` package; #80 stays within
/// `cube_bathymetry`). `intensity_var` (NaN with < 2 samples) maps to the cell's
/// `intensity_variance` quality band (ADR-0007 D6). `timestamp_ns`/`source_index`
/// are stamped into every emitted cell, exactly as the bathy path does — so the
/// Processed product carries provenance, not timestamp=0/source_index=0.
///
/// `nodeRecords()` flushes the median pre-filter, so it is called once per grid.
///
/// @param grid          The CUBE grid to convert.
/// @param timestamp_ns  Acquisition/import time written into every emitted cell.
/// @param source_index  Registry source index written into every emitted cell.
/// @return A `gggs::CellIndex -> MbesCell` map of the finite-intensity cells.
  std::map < gggs::CellIndex, marine_mbes_backscatter_store::MbesCell >
  geoGridToBackscatterCells(
    const GeoGrid & grid, int64_t timestamp_ns, uint16_t source_index);

/// @brief Convert every grid of a @ref GeoMapSheet into one backscatter-cell map.
///
/// Iterates `map_sheet.grids()` and merges each grid's
/// @ref geoGridToBackscatterCells result. Grids cover disjoint GGGS cells, so the
/// merge never collides. The caller writes the cells into a
/// `marine_mbes_backscatter_store::MbesBackscatterStore` via `set()` (Processed
/// layer, #80). Deterministic for a fixed map sheet.
///
/// @param map_sheet     The CUBE map sheet to convert.
/// @param timestamp_ns  Acquisition/import time for every emitted cell.
/// @param source_index  Registry source index for every emitted cell.
  std::map < gggs::CellIndex, marine_mbes_backscatter_store::MbesCell >
  mapSheetToBackscatterCells(
    const GeoMapSheet & map_sheet, int64_t timestamp_ns, uint16_t source_index);

/// @brief Seed predicted depths in @p map_sheet from every finite cell of @p tile.
///
/// For each finite-depth cell of @p tile, finds or creates the matching
/// GeoGrid/Node in @p map_sheet and calls `Node::setPredictedDepth` via
/// `GeoMapSheet::setPredictedDepthAt`. The seeded variance is the stored 1-sigma
/// uncertainty squared, floored at a small positive epsilon (Node::setPredictedDepth
/// requires a finite positive variance; a single-sample CUBE cell can persist a
/// zero or non-finite uncertainty, so every finite-depth cell is still seeded).
/// Does NOT insert a CUBE hypothesis and does NOT mark the sheet dirty.
  void primeFromTile(
    const marine_bathymetry_store::BathymetryTile & tile, GeoMapSheet & map_sheet);

/// @brief Load every tile of @p layer from @p store into @p map_sheet,
///        priming predicted depths cell-by-cell.
///
/// Iterates the layer's single fused tile set (unh_marine_autonomy#221, no
/// per-day epochs) and calls @ref primeFromTile on each. Only finite-depth cells
/// are seeded. Warm-starts slope correction from persisted draft tiles on
/// restart; does not reconstruct CUBE hypothesis state (#21). A no-op if @p layer
/// holds no tiles.
  void loadIntoSheet(
    const marine_bathymetry_store::BathymetryStore & store,
    marine_bathymetry_store::SourceLayer layer,
    GeoMapSheet & map_sheet);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__STORE_IMPORT_H_
