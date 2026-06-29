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
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cube_bathymetry/common.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_mbes_backscatter_store/mbes_store.hpp"
#include "marine_mbes_backscatter_store/mbes_tile.hpp"
#include "marine_mbes_backscatter_store/tile_io.hpp"

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
  const marine_bathymetry_store::BathymetryTile & tile, GeoMapSheet & map_sheet,
  bool seed_settled)
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

    if (!seed_settled) {
      // Predicted-only prime (cube#89): seed the slope/blunder-rejection prior
      // but do NOT settle the cell. A `Chart` (contour) prior must turn the
      // blunder gate on WITHOUT filling the survey layer with coarse contour
      // depths (which would also contaminate the co-estimated backscatter with
      // non-measured cells). The survey accumulates real data on top; gap-filling
      // survey->chart is a query-time overlay, not a settled fill.
      continue;
    }

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
  GeoMapSheet & map_sheet,
  bool seed_settled)
{
  // Single fused grid per layer (unh_marine_autonomy#221): iterate the layer's
  // tiles directly. Empty map -> no-op.
  for (const auto & grid_tile : store.tiles(layer)) {
    primeFromTile(grid_tile.second, map_sheet, seed_settled);
  }
}

// ===========================================================================
// ImportAccumulator (cube_bathymetry#92): bounded-RAM offline import.
// ===========================================================================

ImportAccumulator::ImportAccumulator(GeoMapSheet & sheet, ImportAccumulatorConfig config)
: sheet_(sheet), cfg_(std::move(config))
{
}

std::size_t ImportAccumulator::residentTileCount() const
{
  return sheet_.residentTileCount();
}

void ImportAccumulator::persistBathyTile(const gggs::GridIndex & index)
{
  if (cfg_.store_dir.empty()) {
    return;
  }
  auto grid = sheet_.gridAt(index);
  if (!grid) {
    return;
  }
  // geoGridToTile flushes the median pre-filter (values()) and writes only the
  // finite cells; an all-no-data tile is not persisted (matches mapSheetToTiles).
  marine_bathymetry_store::BathymetryTile tile =
    geoGridToTile(*grid, cfg_.timestamp_ns, cfg_.source_index);
  if (!tile.dirty()) {
    return;
  }
  const std::string layer_dir = cfg_.store_dir + "/" +
    marine_bathymetry_store::layerDirName(cfg_.bathy_layer);
  std::filesystem::create_directories(layer_dir);
  // Atomic temp-then-rename via tile_io::saveTile -- a partial write never
  // corrupts the on-disk surface (same primitive the live node's eviction uses).
  marine_bathymetry_store::saveTile(
    tile, layer_dir + "/" + marine_bathymetry_store::tileFilename(index));
  ++bathy_persisted_;
}

void ImportAccumulator::persistBackscatterTile(const gggs::GridIndex & index)
{
  if (cfg_.bs_store_dir.empty()) {
    return;
  }
  auto grid = sheet_.gridAt(index);
  if (!grid) {
    return;
  }
  namespace mbs = marine_mbes_backscatter_store;
  // Only the finite co-estimated cells (NaN-intensity cells are skipped upstream).
  const std::map<gggs::CellIndex, mbs::MbesCell> cells =
    geoGridToBackscatterCells(*grid, cfg_.timestamp_ns, cfg_.bs_source_index);
  if (cells.empty()) {
    // No new finite intensity this pass -> nothing to merge. Crucially we do NOT
    // write an empty tile: that would clobber finite cells an earlier eviction of
    // this same tile already wrote to disk (the backscatter-loss failure mode).
    return;
  }
  const std::string layer_dir = cfg_.bs_store_dir + "/" +
    mbs::layerDirName(mbs::SourceLayer::Processed);
  const std::string path = layer_dir + "/" + mbs::tileFilename(index);
  const gggs::Level level = gggs::Level::fromCellSize(cfg_.cell_size_m);
  // Newest-finite-wins merge: start from the on-disk tile (if any) so finite
  // cells written by an earlier eviction of this tile survive, then overwrite the
  // cells this pass resurveyed. primeFromTile does NOT restore intensity on
  // reload, so without this merge a revisited tile would NaN-out its earlier
  // backscatter on the next save.
  mbs::MbesTile tile = std::filesystem::is_regular_file(path) ?
    mbs::loadTile(path, level) :
    mbs::MbesTile(index);
  for (const auto & entry : cells) {
    tile.set(entry.first.row(), entry.first.column(), entry.second);
  }
  std::filesystem::create_directories(layer_dir);
  mbs::saveTile(tile, path);
  ++bs_persisted_;
}

bool ImportAccumulator::reloadEvictedTile(const gggs::GridIndex & index)
{
  if (cfg_.store_dir.empty()) {
    return true;
  }
  try {
    marine_bathymetry_store::BathymetryStore scratch =
      marine_bathymetry_store::BathymetryStore::fromCellSize(cfg_.cell_size_m);
    const auto sw = index.southWestPosition();
    const auto ne = index.northEastPosition();
    marine_bathymetry_store::loadWindow(scratch, cfg_.store_dir, sw, ne, nullptr);
    const auto & tiles = scratch.tiles(cfg_.bathy_layer);
    auto it = tiles.find(index);
    if (it != tiles.end()) {
      // seed_settled=true: restore the settled depth/uncertainty as a CUBE
      // hypothesis so accumulation continues from the saved state (ADR-0001).
      primeFromTile(it->second, sheet_);
    }
    return true;
  } catch (const std::exception & e) {
    // The on-disk surface is the real data: on a load error drop the partial
    // re-created grid rather than let a later save clobber the intact file.
    std::cerr << "import_bag: could not reload evicted tile on revisit: "
              << e.what() << " (dropping the partial re-created tile to protect "
      "the on-disk surface; will retry on the next revisit)" << std::endl;
    return false;
  }
}

void ImportAccumulator::evictColdTiles()
{
  if (cfg_.max_resident_tiles == 0) {
    return;  // unbounded: never evict (preserves the pre-#92 behavior)
  }
  if (cfg_.store_dir.empty()) {
    return;  // nowhere to persist -> never evict (dropping would lose data)
  }
  if (sheet_.residentTileCount() <= cfg_.max_resident_tiles) {
    return;
  }
  // Persist-then-drop the coldest tiles beyond budget. Each is written to disk
  // (bathy + backscatter) BEFORE it is dropped, so eviction is lossless and the
  // tile reloads on revisit (ADR-0001). A tile whose persist THROWS is left
  // resident (never dropped): RAM stays transiently over budget rather than
  // losing unsaved data; the next batch retries.
  for (const auto & index : sheet_.coldTiles(cfg_.max_resident_tiles)) {
    try {
      persistBathyTile(index);
      persistBackscatterTile(index);
    } catch (const std::exception & e) {
      std::cerr << "import_bag: failed to persist cold tile for eviction: "
                << e.what() << " (keeping it resident to avoid data loss)"
                << std::endl;
      continue;  // do NOT drop -- lossless guarantee
    }
    sheet_.dropTile(index);
    evicted_.insert(index);
  }
}

void ImportAccumulator::addBatch(
  const std::vector<GeoSounding> & soundings,
  std::chrono::steady_clock::time_point time)
{
  sheet_.addSoundings(soundings, time);

  // Reload-on-revisit (mirror cube#70 pingCallback). Any evicted tile this batch
  // just re-created/dirtied must be reseeded from disk BEFORE its next persist,
  // or that persist overwrites the tile's full on-disk surface with only the
  // freshly-accumulated cells. Key off the DIRTY set -- the grids insert()
  // actually touched -- NOT the sounding centres: addSoundings expands the bounds
  // by a cell and spills into neighbour tiles near a GGGS seam, so a centre-only
  // check would miss an evicted neighbour and clobber it. primeFromTile does not
  // mark dirty, so a reloaded grid stays dirty for the save (reloaded settled
  // cells + new cells); a resurveyed cell keeps the new value, others the
  // reloaded one.
  if (!cfg_.store_dir.empty() && !evicted_.empty()) {
    std::vector<gggs::GridIndex> revisited;
    for (const auto & idx : sheet_.dirtyGrids()) {
      if (evicted_.count(idx)) {
        revisited.push_back(idx);
      }
    }
    for (const auto & idx : revisited) {
      if (reloadEvictedTile(idx)) {
        evicted_.erase(idx);
      } else {
        sheet_.dropTile(idx);  // protect the intact on-disk surface
      }
    }
  }

  evictColdTiles();
}

void ImportAccumulator::finalize(
  const marine_bathymetry_store::SourceRegistry & bathy_registry,
  const marine_mbes_backscatter_store::SourceRegistry * bs_registry)
{
  // Persist every still-resident tile (evicted tiles are already durable). Snapshot
  // the indices first so the persist loop iterates a stable, deterministic order.
  std::vector<gggs::GridIndex> resident;
  for (const auto & grid : sheet_.grids()) {
    if (grid) {
      resident.push_back(grid->index());
    }
  }
  for (const auto & index : resident) {
    persistBathyTile(index);
    persistBackscatterTile(index);
  }
  // Registries are store-wide sidecars: written once at the end so every cell's
  // source_index resolves (the per-tile eviction writes carry no registry).
  if (!cfg_.store_dir.empty()) {
    std::filesystem::create_directories(cfg_.store_dir);
    bathy_registry.saveRegistry(cfg_.store_dir);
  }
  if (!cfg_.bs_store_dir.empty() && bs_registry != nullptr) {
    std::filesystem::create_directories(cfg_.bs_store_dir);
    bs_registry->saveRegistry(cfg_.bs_store_dir);
  }
}

}  // namespace cube
