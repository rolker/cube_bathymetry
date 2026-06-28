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


#include "cube_bathymetry/store_import.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "cube_bathymetry/common.h"
#include "marine_autonomy/gggs.h"

namespace cube
{

namespace
{
// Lower bound on the predicted-depth variance (meter^2) seeded by primeFromTile.
// A single-sample CUBE cell can persist an exactly-zero or non-finite 1-sigma
// uncertainty; Node::setPredictedDepth rejects a non-positive variance, so we
// floor it. 1e-4 m^2 == 1 cm 1-sigma: small enough to act as a confident prior,
// large enough to keep the blunder-limit sqrt() well-defined.
constexpr double kPrimeVarianceFloor = 1e-4;
}  // namespace

marine_bathymetry_store::BathymetryTile geoGridToTile(
  const GeoGrid & grid, int64_t timestamp_ns, uint16_t source_index)
{
  marine_bathymetry_store::BathymetryTile tile(grid.index());

  // values() mutates node state (flushes the median pre-filter) -- call once and
  // cache. It is positional in CellAreaIterator order over grid.index().
  const std::vector<DepthAndUncertainty> values = grid.values();

  // Walk the SAME iterator the same way values() does, so values[k] belongs to
  // the cell visited on the k-th iteration. CellAreaIterator(grid) covers the
  // full 960x960 grid row-major (row 0 = south, column 0 = west) -- exactly the
  // layout BathymetryTile::set(row, col, ...) expects.
  gggs::CellAreaIterator it(grid.index());
  std::size_t k = 0;
  for (; it.valid() && k < values.size(); it.next(), ++k) {
    const DepthAndUncertainty & v = values[k];
    if (std::isnan(v.depth)) {
      continue;  // no estimate here; leave the tile's NaN no-data sentinel
    }
    tile.set(
      (*it).row(), (*it).column(),
      marine_bathymetry_store::BathyCell{
        static_cast<double>(v.depth),
        static_cast<double>(v.uncertainty),
        timestamp_ns,
        source_index});
  }

  return tile;
}

std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile>
mapSheetToTiles(
  const GeoMapSheet & map_sheet, int64_t timestamp_ns, uint16_t source_index)
{
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;

  for (const auto & grid : map_sheet.grids()) {
    if (!grid) {
      continue;
    }
    marine_bathymetry_store::BathymetryTile tile =
      geoGridToTile(*grid, timestamp_ns, source_index);
    // Drop a grid that yielded no finite cells -- an all-no-data tile would just
    // persist as an empty file.
    if (!tile.dirty()) {
      continue;
    }
    tiles.emplace(grid->index(), std::move(tile));
  }

  return tiles;
}

std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell>
geoGridToBackscatterCells(
  const GeoGrid & grid, int64_t timestamp_ns, uint16_t source_index)
{
  std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell> cells;

  // nodeRecords() mutates node state (flushes the median pre-filter) -- call once
  // and cache. It is positional in CellAreaIterator order over grid.index(), the
  // same scheme geoGridToTile uses for the bathy tile.
  const std::vector<NodeRecord> records = grid.nodeRecords();

  // Walk the SAME iterator the same way nodeRecords() does, so records[k] belongs
  // to the cell visited on the k-th iteration, and *it IS that cell's CellIndex.
  gggs::CellAreaIterator it(grid.index());
  std::size_t k = 0;
  for (; it.valid() && k < records.size(); it.next(), ++k) {
    const NodeRecord & r = records[k];
    if (std::isnan(r.intensity)) {
      continue;  // no co-estimated backscatter here -- skip (mirrors NaN-depth skip)
    }
    // intensity_var is the estimate variance (NaN with < 2 samples); it rides
    // into the quality band (ADR-0007 D6). timestamp/source stamp provenance.
    cells.emplace(
      *it,
      marine_mbes_backscatter_store::MbesCell{
        r.intensity, r.intensity_var, timestamp_ns, source_index});
  }

  return cells;
}

std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell>
mapSheetToBackscatterCells(
  const GeoMapSheet & map_sheet, int64_t timestamp_ns, uint16_t source_index)
{
  std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell> cells;

  for (const auto & grid : map_sheet.grids()) {
    if (!grid) {
      continue;
    }
    std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell> grid_cells =
      geoGridToBackscatterCells(*grid, timestamp_ns, source_index);
    // Grids cover disjoint GGGS cells, so merge never collides.
    cells.merge(grid_cells);
  }

  return cells;
}

void primeFromTile(
  const marine_bathymetry_store::BathymetryTile & tile, GeoMapSheet & map_sheet)
{
  const gggs::GridIndex & grid = tile.index();
  const std::vector<double> & depth = tile.depthBand();
  const std::vector<double> & uncertainty = tile.uncertaintyBand();

  // Walk the tile in GGGS cell order (row 0 = south, row-major) -- the same order
  // BathymetryTile's bands use -- and prime every finite-depth cell.
  gggs::CellAreaIterator it(grid);
  std::size_t k = 0;
  for (; it.valid() && k < depth.size(); it.next(), ++k) {
    const double d = depth[k];
    if (std::isnan(d)) {
      continue;  // no-data cell -- nothing to prime
    }
    // Variance from the stored 1-sigma uncertainty (sigma^2). Node::setPredictedDepth
    // requires a *finite positive* variance for a real (finite) predicted depth --
    // its blunder limit takes sqrt(depth - variance), so a zero or sentinel variance
    // would mis-scale (or silently neutralize) blunder rejection. A single-sample
    // CUBE cell can have an exactly-zero or non-finite uncertainty, so floor the
    // variance at a small positive epsilon rather than skip the cell: the depth
    // prior is still worth seeding for slope correction even when the persisted
    // confidence is unusable.
    const double u = uncertainty[k];
    double variance = (std::isfinite(u) && u > 0.0) ? (u * u) : 0.0;
    variance = std::max(variance, kPrimeVarianceFloor);
    map_sheet.setPredictedDepthAt(
      *it, static_cast<float>(d), static_cast<float>(variance));

    // Lossless reload (ADR-0001): also reseed the SETTLED depth as a CUBE
    // hypothesis so the primed cell round-trips through values() and survives the
    // next whole-tile save. Without this, a tile primed at startup, partially
    // resurveyed, then re-saved would lose its un-resurveyed cells (the latent
    // cross-session loss #70 closes). `u` is the stored 1.96-sigma confidence
    // interval, exactly what seedSettledDepth() expects.
    map_sheet.setSettledDepthAt(*it, static_cast<float>(d), static_cast<float>(u));
  }
}

void loadIntoSheet(
  const marine_bathymetry_store::BathymetryStore & store,
  marine_bathymetry_store::SourceLayer layer,
  GeoMapSheet & map_sheet)
{
  // Single fused grid per layer (unh_marine_autonomy#221): iterate the layer's
  // tiles directly. Empty map -> no-op.
  for (const auto & grid_tile : store.tiles(layer)) {
    primeFromTile(grid_tile.second, map_sheet);
  }
}

}  // namespace cube
