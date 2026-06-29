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
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cube_bathymetry/common.h"
#include "cube_bathymetry/hypothesis.h"
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

namespace
{
// Per-tile raw-intensity-sample spill (cube#92). A tiny binary file, scratch-only
// and single-machine, so native layout/endianness is fine. Format:
//   uint32 magic | uint32 ncells | ncells * { uint16 row, uint16 col,
//                                              uint32 nsamples,
//                                              nsamples * BeamIntensitySample }
// BeamIntensitySample is three tightly-packed floats (asserted below), so the
// sample run is written/read as a contiguous block.
constexpr uint32_t kSpillMagic = 0x43425331u;  // "CBS1"
static_assert(
  sizeof(BeamIntensitySample) == 3 * sizeof(float),
  "BeamIntensitySample must be tightly packed for the block spill read/write");

std::string spillFileName(const gggs::GridIndex & index)
{
  // Reuse the bathy tile naming (level_row_col.tif) for a unique per-tile stem.
  return marine_bathymetry_store::tileFilename(index) + ".spill";
}
}  // namespace

ImportAccumulator::ImportAccumulator(GeoMapSheet & sheet, ImportAccumulatorConfig config)
: sheet_(sheet), cfg_(std::move(config))
{
}

ImportAccumulator::~ImportAccumulator()
{
  // RAII safety net: finalize() normally cleans up, but an exception on the import
  // path must not leak the scratch spill.
  cleanupScratch();
}

void ImportAccumulator::cleanupScratch()
{
  if (!scratch_dir_.empty()) {
    std::error_code ec;
    std::filesystem::remove_all(scratch_dir_, ec);  // best-effort; never throws here
    scratch_dir_.clear();
  }
}

std::size_t ImportAccumulator::residentTileCount() const
{
  return sheet_.residentTileCount();
}

void ImportAccumulator::spillIntensitySamples(const gggs::GridIndex & index)
{
  if (cfg_.bs_store_dir.empty()) {
    return;  // no backscatter product -> no need to retain raw samples
  }
  auto grid = sheet_.gridAt(index);
  if (!grid) {
    return;
  }
  const std::map<gggs::CellIndex, std::vector<BeamIntensitySample>> samples =
    grid->nodeIntensitySamples();

  // Lazily create a unique scratch dir on first spill.
  if (scratch_dir_.empty()) {
    static std::atomic<uint64_t> counter{0};
    const auto ns =
      std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path base = std::filesystem::temp_directory_path() /
      ("cube_import_spill_" + std::to_string(ns) + "_" +
      std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(base);
    scratch_dir_ = base.string();
  }

  const std::string path = scratch_dir_ + "/" + spillFileName(index);
  if (samples.empty()) {
    // Nothing to retain; drop any stale spill so a later reload can't restore
    // outdated samples for this tile.
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("import_bag: cannot open spill file " + path);
  }
  const uint32_t magic = kSpillMagic;
  const uint32_t ncells = static_cast<uint32_t>(samples.size());
  out.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
  out.write(reinterpret_cast<const char *>(&ncells), sizeof(ncells));
  for (const auto & entry : samples) {
    const uint16_t row = entry.first.row();
    const uint16_t col = entry.first.column();
    const uint32_t n = static_cast<uint32_t>(entry.second.size());
    out.write(reinterpret_cast<const char *>(&row), sizeof(row));
    out.write(reinterpret_cast<const char *>(&col), sizeof(col));
    out.write(reinterpret_cast<const char *>(&n), sizeof(n));
    out.write(
      reinterpret_cast<const char *>(entry.second.data()),
      static_cast<std::streamsize>(entry.second.size() * sizeof(BeamIntensitySample)));
  }
  if (!out) {
    throw std::runtime_error("import_bag: failed writing spill file " + path);
  }
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
    // No finite intensity this pass -> nothing to write. Do NOT write an empty
    // tile over a populated one. With the sample spill/restore this is only hit
    // for a genuinely intensity-less tile (it never loses an earlier write).
    return;
  }
  const std::string layer_dir = cfg_.bs_store_dir + "/" +
    mbs::layerDirName(mbs::SourceLayer::Processed);
  const std::string path = layer_dir + "/" + mbs::tileFilename(index);
  // Plain overwrite (no on-disk merge): the reload-before-add path restores the
  // tile's raw intensity samples before a revisit accretes onto them, so at every
  // eviction the in-RAM tile already holds the COMPLETE sample population for each
  // cell -- its corrected summary is the complete value, and overwriting the
  // on-disk tile reproduces it losslessly. (The old newest-finite-wins disk merge
  // was only needed when reload could not restore intensity; #92 removes that gap.)
  mbs::MbesTile tile(index);
  for (const auto & entry : cells) {
    tile.set(entry.first.row(), entry.first.column(), entry.second);
  }
  std::filesystem::create_directories(layer_dir);
  mbs::saveTile(tile, path);
  ++bs_persisted_;
}

void ImportAccumulator::restoreSpilledSamples(const gggs::GridIndex & index)
{
  // Best-effort: a missing/corrupt/truncated spill only degrades backscatter for
  // this tile to the post-reload samples; the depth reload already succeeded.
  if (scratch_dir_.empty()) {
    return;
  }
  const std::string path = scratch_dir_ + "/" + spillFileName(index);
  if (!std::filesystem::is_regular_file(path)) {
    return;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return;
  }
  uint32_t magic = 0;
  uint32_t ncells = 0;
  in.read(reinterpret_cast<char *>(&magic), sizeof(magic));
  in.read(reinterpret_cast<char *>(&ncells), sizeof(ncells));
  if (!in || magic != kSpillMagic) {
    return;
  }
  for (uint32_t c = 0; c < ncells; ++c) {
    uint16_t row = 0;
    uint16_t col = 0;
    uint32_t n = 0;
    in.read(reinterpret_cast<char *>(&row), sizeof(row));
    in.read(reinterpret_cast<char *>(&col), sizeof(col));
    in.read(reinterpret_cast<char *>(&n), sizeof(n));
    if (!in) {
      return;  // truncated header -> stop (what was restored already stands)
    }
    std::vector<BeamIntensitySample> vec(n);
    if (n > 0) {
      in.read(
        reinterpret_cast<char *>(vec.data()),
        static_cast<std::streamsize>(static_cast<std::size_t>(n) *
        sizeof(BeamIntensitySample)));
      if (!in) {
        return;  // truncated sample run -> stop
      }
    }
    sheet_.setSettledIntensitySamplesAt(gggs::CellIndex(index, row, col), std::move(vec));
  }
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
      // seed_settled=true: restore each cell's settled depth/uncertainty as a CUBE
      // hypothesis so accumulation continues from the saved state (ADR-0001).
      primeFromTile(it->second, sheet_);
    }
  } catch (const std::exception & e) {
    // The on-disk surface is the real data: on a load error the caller drops the
    // partial re-created grid rather than let a later save clobber the intact file.
    std::cerr << "import_bag: could not reload evicted tile on revisit: "
              << e.what() << " (dropping the partial re-created tile to protect "
      "the on-disk surface; will retry on the next revisit)" << std::endl;
    return false;
  }
  // Restore the raw intensity samples onto the just-reseeded hypotheses so the
  // revisit's beams blend with the pre-eviction population (lossless backscatter).
  // Runs AFTER the depth reseed and BEFORE the batch's soundings are added.
  restoreSpilledSamples(index);
  return true;
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
  // (bathy + backscatter summary) and its raw intensity samples spilled to scratch
  // BEFORE it is dropped, so eviction is lossless and the tile reloads (with its
  // full sample population) on revisit. A tile whose persist/spill THROWS is left
  // resident (never dropped): RAM stays transiently over budget rather than losing
  // unsaved data; the next batch retries.
  for (const auto & index : sheet_.coldTiles(cfg_.max_resident_tiles)) {
    try {
      persistBathyTile(index);
      persistBackscatterTile(index);
      spillIntensitySamples(index);
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
  // Reload-BEFORE-add (cube#92 lossless blend): reload any evicted tile this batch
  // is about to touch FIRST, so the batch's beams accrete onto the reloaded
  // hypotheses (settled depth + restored raw intensity samples) instead of forming
  // a fresh, partial tile. gridIndicesForSoundings computes the SAME expanded
  // window addSoundings will touch (including a near-seam neighbour tile), so no
  // evicted tile is missed. A tile whose reload FAILS is recorded and dropped
  // after the add, protecting the intact on-disk surface (it stays evicted and
  // retries on the next revisit).
  std::vector<gggs::GridIndex> reload_failed;
  if (!cfg_.store_dir.empty() && !evicted_.empty()) {
    for (const auto & idx : sheet_.gridIndicesForSoundings(soundings)) {
      if (evicted_.count(idx)) {
        if (reloadEvictedTile(idx)) {
          evicted_.erase(idx);
        } else {
          reload_failed.push_back(idx);
        }
      }
    }
  }

  sheet_.addSoundings(soundings, time);

  for (const auto & idx : reload_failed) {
    sheet_.dropTile(idx);  // protect the intact on-disk surface
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
  // The spilled raw samples were only needed to reload an evicted tile mid-run;
  // the import is complete, so delete the scratch dir.
  cleanupScratch();
}

}  // namespace cube
