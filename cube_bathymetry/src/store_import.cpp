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
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

marine_bathymetry_store::BathymetryTile geoGridToTile(const GeoGrid & grid)
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
    // BathyCell is 2-band {depth, uncertainty} since uma#248 (the per-cell
    // timestamp/source bands were dropped; coarse provenance is store-level
    // StoreMetadata). values() returns float; widen to the store's double.
    tile.set(
      (*it).row(), (*it).column(),
      marine_bathymetry_store::BathyCell{
        static_cast<double>(v.depth),
        static_cast<double>(v.uncertainty)});
  }

  return tile;
}

std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile>
mapSheetToTiles(const GeoMapSheet & map_sheet)
{
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;

  for (const auto & grid : map_sheet.grids()) {
    if (!grid) {
      continue;
    }
    marine_bathymetry_store::BathymetryTile tile = geoGridToTile(*grid);
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
geoGridToBackscatterCells(const GeoGrid & grid)
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
    // Encode the Welford sufficient statistic into the 3-band MbesCell (uma#248
    // A.1); welfordFromCell is the exact inverse. r.intensity is the running mean;
    // r.intensity_var is the estimate variance (variance of the mean = M2/(n-1)/n),
    // NaN with < 2 samples.
    marine_mbes_backscatter_store::MbesCell cell;
    cell.mean = r.intensity;
    if (std::isnan(r.intensity_var)) {
      // n = 1 sentinel: a single beam has no dispersion. sample_sd == 0 with a
      // finite mean reconstructs to n = 1 (distinct from mean = NaN no-data).
      cell.standard_error = 0.0f;
      cell.sample_sd = 0.0f;
    } else {
      // n >= 2. sample_sd = sqrt(intensity_var * n) is the beams' sample stddev;
      // standard_error = scale * sqrt(intensity_var) is the confidence-scaled SE
      // of the mean (true SE = sqrt(intensity_var)).
      const float n = static_cast<float>(r.n_samples);
      cell.sample_sd = std::sqrt(r.intensity_var * n);
      cell.standard_error =
        kBackscatterConfidenceScale * std::sqrt(r.intensity_var);
    }
    cells.emplace(*it, cell);
  }

  return cells;
}

std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell>
mapSheetToBackscatterCells(const GeoMapSheet & map_sheet)
{
  std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell> cells;

  for (const auto & grid : map_sheet.grids()) {
    if (!grid) {
      continue;
    }
    std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell> grid_cells =
      geoGridToBackscatterCells(*grid);
    // Grids cover disjoint GGGS cells, so merge never collides.
    cells.merge(grid_cells);
  }

  return cells;
}

IntensityWelford welfordFromCell(
  const marine_mbes_backscatter_store::MbesCell & cell)
{
  IntensityWelford w;
  if (!cell.hasData()) {
    return w;  // no-data cell (mean NaN) -> empty Welford {n=0}
  }
  w.mean = static_cast<double>(cell.mean);
  if (cell.sample_sd == 0.0f) {
    // n = 1 sentinel (a single-beam node, or -- the accepted limitation -- an n>=2
    // cell whose samples were all exactly identical, M2 == 0). Reconstruct n = 1.
    w.n = 1;
    w.m2 = 0.0;
    return w;
  }
  // Divide the confidence scale back out of standard_error to recover the true
  // standard error of the mean, then invert n = (sample_sd / SE)^2 and
  // M2 = sample_sd^2 * (n - 1). round() absorbs float round-trip error in n.
  const double sample_sd = static_cast<double>(cell.sample_sd);
  const double se =
    static_cast<double>(cell.standard_error) / kBackscatterConfidenceScale;
  if (!(se > 0.0)) {
    // Defensive: a well-formed encode pairs a non-zero sample_sd with a non-zero
    // standard_error (both are zero iff the variance is zero, handled above). A
    // corrupt or hand-built tile with sample_sd != 0 but a zero / negative /
    // non-finite standard_error would drive ratio = sample_sd / se to +/-inf or
    // NaN, and std::lround(inf/NaN) is undefined behaviour with an out-of-range
    // uint32_t cast. A zero SE carries no dispersion information, so fall back to
    // the n = 1 sentinel rather than invoke UB.
    w.n = 1;
    w.m2 = 0.0;
    return w;
  }
  const double ratio = sample_sd / se;
  w.n = static_cast<uint32_t>(std::lround(ratio * ratio));
  if (w.n < 2) {
    w.n = 2;  // sample_sd != 0 implies >= 2 samples; guard against round-to-1
  }
  w.m2 = sample_sd * sample_sd * static_cast<double>(w.n - 1);
  return w;
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

PriorLayerPrimeResult primeFromPriorLayers(
  const marine_bathymetry_store::BathymetryStore & store,
  GeoMapSheet & map_sheet)
{
  PriorLayerPrimeResult result;
  const uint8_t survey_level = map_sheet.gridLevel().level();

  const auto prime_layer =
    [&](marine_bathymetry_store::SourceLayer layer, std::size_t & primed) {
      for (const auto & grid_tile : store.tiles(layer)) {
        // Exact-level scope: primeFromTile walks the tile's OWN cell iterator
        // with no cross-level guard, so a coarse tile from a multi-level store
        // (#115) would seed wrong-geometry cells. Count and skip; cross-level
        // priming is the per-tile importer's resample path, not this bulk one.
        if (grid_tile.first.level() != survey_level) {
          ++result.level_mismatched;
          continue;
        }
        primeFromTile(grid_tile.second, map_sheet, /*seed_settled=*/false);
        ++primed;
      }
    };

  // Chart first (lowest-priority prior), Reference on top so it overwrites
  // where the layers overlap -- the store's SourceLayer priority ordering.
  prime_layer(marine_bathymetry_store::SourceLayer::Chart, result.chart_tiles);
  prime_layer(
    marine_bathymetry_store::SourceLayer::Reference, result.reference_tiles);
  return result;
}

// ===========================================================================
// ImportAccumulator (cube_bathymetry#92): bounded-RAM offline import.
// ===========================================================================

namespace
{
// Per-tile intensity-Welford spill (cube#92/#93). A tiny binary file, scratch-only
// and single-machine, so native layout/endianness is fine. Format:
//   uint32 magic | uint32 ncells |
//   ncells * { uint16 row, uint16 col, uint32 n, double mean, double m2 }
// Each cell's record is a fixed 24 bytes -- O(1)/cell (cube#93 replaced the
// variable-length raw-sample spill with the 3-number corrected-intensity Welford).
constexpr uint32_t kSpillMagic = 0x43425332u;  // "CBS2" (cube#93 triplet format)

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

void ImportAccumulator::persistResidentTile(const gggs::GridIndex & index)
{
  // Batch-regen gather (#96): write only this tile (bathy + backscatter). The
  // gather sheet may also hold neighbour grids a near-seam sounding spilled into;
  // those are each written by their OWN tile's gather, so we never persist them here.
  persistBathyTile(index);
  persistBackscatterTile(index);
}

void ImportAccumulator::spillIntensitySamples(const gggs::GridIndex & index)
{
  if (cfg_.bs_store_dir.empty()) {
    return;  // no backscatter product -> no need to retain the intensity Welford
  }
  auto grid = sheet_.gridAt(index);
  if (!grid) {
    return;
  }
  const std::map<gggs::CellIndex, IntensityWelford> welford =
    grid->nodeIntensityWelford();

  // Lazily create a unique scratch dir on first spill. mkdtemp atomically creates
  // a directory with a name guaranteed unique against any other process (it retries
  // internally on collision), so two concurrent import_bag processes can never
  // share a spill dir and cross-corrupt each other's tiles. A steady_clock-ns +
  // per-process atomic counter name could collide cross-process: the counter resets
  // per process, leaving the ns the only distinguisher, and two processes can read
  // the same coarse ns tick.
  if (scratch_dir_.empty()) {
    std::string tmpl =
      (std::filesystem::temp_directory_path() / "cube_import_spill_XXXXXX").string();
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (::mkdtemp(buf.data()) == nullptr) {
      throw std::runtime_error(
        std::string("import_bag: cannot create spill scratch dir from template ") +
        tmpl + ": " + std::strerror(errno));
    }
    scratch_dir_ = std::string(buf.data());
  }

  const std::string path = scratch_dir_ + "/" + spillFileName(index);
  if (welford.empty()) {
    // Nothing to retain; drop any stale spill so a later reload can't restore
    // outdated state for this tile.
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("import_bag: cannot open spill file " + path);
  }
  const uint32_t magic = kSpillMagic;
  const uint32_t ncells = static_cast<uint32_t>(welford.size());
  out.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
  out.write(reinterpret_cast<const char *>(&ncells), sizeof(ncells));
  for (const auto & entry : welford) {
    const uint16_t row = entry.first.row();
    const uint16_t col = entry.first.column();
    const uint32_t n = entry.second.n;
    const double mean = entry.second.mean;
    const double m2 = entry.second.m2;
    out.write(reinterpret_cast<const char *>(&row), sizeof(row));
    out.write(reinterpret_cast<const char *>(&col), sizeof(col));
    out.write(reinterpret_cast<const char *>(&n), sizeof(n));
    out.write(reinterpret_cast<const char *>(&mean), sizeof(mean));
    out.write(reinterpret_cast<const char *>(&m2), sizeof(m2));
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
  marine_bathymetry_store::BathymetryTile tile = geoGridToTile(*grid);
  if (!tile.dirty()) {
    return;
  }
  // The off-boat CUBE re-run is the authoritative product: always the `survey`
  // layer (uma#248 collapsed the draft/processed split into one).
  const std::string layer_dir = cfg_.store_dir + "/" +
    marine_bathymetry_store::layerDirName(
    marine_bathymetry_store::SourceLayer::Survey);
  std::filesystem::create_directories(layer_dir);
  // tile_io::saveTile writes the GTiff directly to the final path and checks the
  // flush/close result (an I/O error or full disk throws), but it is NOT crash-atomic:
  // it does not temp-then-rename, so a crash/kill mid-write can leave a partial tile
  // at the final path. Making that write atomic is a tracked marine_tiled_raster_store
  // follow-up (#96 review); a batch-regen re-run reproduces the tile exactly.
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
    geoGridToBackscatterCells(*grid);
  if (cells.empty()) {
    // No finite intensity this pass -> nothing to write. Do NOT write an empty
    // tile over a populated one. With the sample spill/restore this is only hit
    // for a genuinely intensity-less tile (it never loses an earlier write).
    return;
  }
  const std::string layer_dir = cfg_.bs_store_dir + "/" +
    mbs::layerDirName(mbs::SourceLayer::Survey);
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
    IntensityWelford w;
    in.read(reinterpret_cast<char *>(&row), sizeof(row));
    in.read(reinterpret_cast<char *>(&col), sizeof(col));
    in.read(reinterpret_cast<char *>(&w.n), sizeof(w.n));
    in.read(reinterpret_cast<char *>(&w.mean), sizeof(w.mean));
    in.read(reinterpret_cast<char *>(&w.m2), sizeof(w.m2));
    if (!in) {
      return;  // truncated record -> stop (what was restored already stands)
    }
    sheet_.setSettledIntensityWelfordAt(gggs::CellIndex(index, row, col), w);
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
    const auto & tiles =
      scratch.tiles(marine_bathymetry_store::SourceLayer::Survey);
    auto it = tiles.find(index);
    if (it != tiles.end()) {
      // seed_settled=true: restore each cell's settled depth/uncertainty as a CUBE
      // hypothesis so accumulation continues from the saved state (ADR-0001).
      primeFromTile(it->second, sheet_);
    }
  } catch (const std::exception & e) {
    // The on-disk surface is the real data: on a load error the caller drops the
    // partial re-created grid rather than let a later save clobber the intact file.
    // The tile stays evicted, so a later batch that revisits it retries the reload;
    // but this batch's soundings on the tile are dropped now and not re-processed
    // (the caller WARNs with the count).
    std::cerr << "import_bag: could not reload evicted tile on revisit: "
              << e.what() << " (dropping this batch's soundings on the tile to "
      "protect the on-disk surface; a later batch revisiting the tile retries the "
      "reload, but these dropped soundings are not re-processed)" << std::endl;
    return false;
  }
  // Restore the raw intensity samples onto the just-reseeded hypotheses so the
  // revisit's beams blend with the pre-eviction population (lossless backscatter).
  // Runs AFTER the depth reseed and BEFORE the batch's soundings are added.
  restoreSpilledSamples(index);
  return true;
}

namespace
{
// Cross-level reference prime (#115): seed the predicted surface of the FINE survey
// grid @p survey_index from a COARSER reference tile @p coarse_tile by
// nearest-neighbour resample. `loadWindow` returns reference tiles at any GGGS
// level, but a coarser tile is keyed by its own (different) GridIndex, so the
// same-level `tiles.find(index)` in rung 2 misses it and the blunder gate would be
// silently inactive (the bug #115 fixes). GGGS is nested, so the fine survey grid
// lies wholly inside exactly one coarse tile; we walk the FINE survey cells (not the
// coarse ones) so every survey node that a sounding can land on is gated even where
// one coarse cell spans many survey cells.
//
// Predicted-only, exactly like primeFromTile(seed_settled=false): the coarse prior
// turns the gate on but is never settled as measured data and seeds no backscatter.
void primeFromTileResample(
  const marine_bathymetry_store::BathymetryTile & coarse_tile,
  const gggs::GridIndex & survey_index, GeoMapSheet & map_sheet)
{
  const gggs::GridIndex & coarse_grid = coarse_tile.index();
  const gggs::Level ref_level(coarse_grid.level());

  // gggs::CellIndex::position() returns the cell's SOUTH-WEST corner, not its
  // center; add half a survey cell in each axis so we resolve the coarse cell that
  // contains the fine cell's CENTER (a nearest-neighbour resample keyed on the cell
  // midpoint, not its corner).
  const double half_cell_lat =
    survey_index.latitudinalSpan() / gggs::GridIndex::cellRowCount() * 0.5;
  const double half_cell_lon =
    survey_index.longitudinalSpan() / gggs::GridIndex::cellColumnCount() * 0.5;

  gggs::CellAreaIterator it(survey_index);
  for (; it.valid(); it.next()) {
    const geographic_msgs::msg::GeoPoint sw = (*it).position();
    const geographic_msgs::msg::GeoPoint center =
      gggs::geoPoint(sw.latitude + half_cell_lat, sw.longitude + half_cell_lon);
    // Level::cellIndex composes gridIndex + CellIndex in one call.
    const gggs::CellIndex coarse_cell = ref_level.cellIndex(center);
    // GGGS nesting guarantees an interior center resolves into coarse_grid; guard
    // the boundary case where a center rounds to a neighbour grid we did not load.
    if (coarse_cell.grid() != coarse_grid) {
      continue;
    }
    const marine_bathymetry_store::BathyCell cell =
      coarse_tile.get(coarse_cell.row(), coarse_cell.column());
    if (std::isnan(cell.depth)) {
      continue;  // no coarse prior here -- leave the fine survey cell ungated
    }
    // Same variance derivation as primeFromTile: sigma^2 floored at a small positive
    // epsilon (Node::setPredictedDepth needs a finite positive variance).
    double variance = (std::isfinite(cell.uncertainty) && cell.uncertainty > 0.0) ?
      (cell.uncertainty * cell.uncertainty) : 0.0;
    variance = std::max(variance, kPrimeVarianceFloor);
    map_sheet.setPredictedDepthAt(
      *it, static_cast<float>(cell.depth), static_cast<float>(variance));
  }
}
}  // namespace

bool ImportAccumulator::seedNewTile(const gggs::GridIndex & index)
{
  // Two-rung seed precedence (#96), run once per tile on first touch. survey wins
  // over reference: a survey tile is measured CUBE data (settle it, seed its
  // backscatter); a reference tile is a coarse read-only prior (gate only).

  // Rung 1 -- survey: a pre-existing survey bathy tile (an incremental import into
  // an existing store, or -- via reloadEvictedTile -- an already-written tile).
  // loadWindow silently returns 0 when store_dir has no survey/ layer yet (a fresh
  // import), so this falls through to the reference rung with no error/warning.
  // skip_survey_seed disables this rung for the batch-regen gather: replaying a
  // tile's complete sounding population onto a warm-start from that same tile in the
  // OUTPUT store would double-count it (a silent blend, not an exact rebuild).
  if (!cfg_.store_dir.empty() && !cfg_.skip_survey_seed) {
    try {
      marine_bathymetry_store::BathymetryStore scratch =
        marine_bathymetry_store::BathymetryStore::fromCellSize(cfg_.cell_size_m);
      const auto sw = index.southWestPosition();
      const auto ne = index.northEastPosition();
      marine_bathymetry_store::loadWindow(scratch, cfg_.store_dir, sw, ne, nullptr);
      const auto & tiles =
        scratch.tiles(marine_bathymetry_store::SourceLayer::Survey);
      auto it = tiles.find(index);
      if (it != tiles.end()) {
        // Settled warm-start: the survey layer round-trips as a CUBE hypothesis and
        // refines under new soundings (ADR-0001).
        primeFromTile(it->second, sheet_, /*seed_settled=*/true);
        // Reconstruct each cell's corrected-intensity Welford from the survey
        // backscatter tile so the re-run's beams blend with the stored population
        // (lossless backscatter seed). welfordFromCell inverts the 3-band write.
        if (!cfg_.bs_store_dir.empty()) {
          namespace mbs = marine_mbes_backscatter_store;
          const std::string bs_path = cfg_.bs_store_dir + "/" +
            mbs::layerDirName(mbs::SourceLayer::Survey) + "/" +
            mbs::tileFilename(index);
          if (std::filesystem::is_regular_file(bs_path)) {
            const gggs::Level level = gggs::Level::fromCellSize(cfg_.cell_size_m);
            const mbs::MbesTile bs_tile = mbs::loadTile(bs_path, level);
            gggs::CellAreaIterator cit(index);
            for (; cit.valid(); cit.next()) {
              const mbs::MbesCell c = bs_tile.get((*cit).row(), (*cit).column());
              if (c.hasData()) {
                sheet_.setSettledIntensityWelfordAt(*cit, welfordFromCell(c));
              }
            }
          }
        }
        seeded_.insert(index);
        return true;
      }
    } catch (const std::exception & e) {
      // A survey-seed read error means the on-disk survey tile EXISTS but could not
      // be loaded (a fresh import returns 0 tiles WITHOUT throwing, so it never
      // reaches here). Accumulating from scratch and then persisting would overwrite
      // that intact-but-unreadable tile with partial data -- the same data-loss the
      // reload path guards against. Mirror reloadEvictedTile: signal failure so the
      // caller drops this tile (and its soundings) to protect the on-disk surface,
      // and leave it UNseeded so a later batch retries the seed.
      std::cerr << "import_bag: could not survey-seed tile on first touch: "
                << e.what() << " (dropping this batch's soundings on the tile to "
        "protect the on-disk surface; a later batch retries the seed, but these "
        "dropped soundings are not re-processed)" << std::endl;
      return false;
    }
  }

  // Rung 2 -- reference: a coarse read-only prior. Predicted-only prime
  // (seed_settled=false) turns the blunder gate on but does NOT settle the cell as
  // measured data (no values() output, sheet stays clean) and seeds NO backscatter.
  if (!cfg_.reference_store_dir.empty()) {
    try {
      marine_bathymetry_store::BathymetryStore ref =
        marine_bathymetry_store::BathymetryStore::fromCellSize(cfg_.cell_size_m);
      const auto sw = index.southWestPosition();
      const auto ne = index.northEastPosition();
      marine_bathymetry_store::loadWindow(
        ref, cfg_.reference_store_dir, sw, ne, nullptr);
      // Chart exact-level prime FIRST (#119): since the reference->chart layer
      // split, official chart products live in the Chart layer, which this gate
      // never consulted -- charted-waters imports ran ungated. Chart is the
      // lowest-priority prior, so it primes before Reference below, which
      // overwrites where both layers cover a cell. Chart cross-level resampling
      // is deferred (the #115 fallback below stays Reference-only).
      const auto & chart_tiles =
        ref.tiles(marine_bathymetry_store::SourceLayer::Chart);
      auto chart_it = chart_tiles.find(index);
      if (chart_it != chart_tiles.end()) {
        primeFromTile(chart_it->second, sheet_, /*seed_settled=*/false);
      }
      const auto & tiles =
        ref.tiles(marine_bathymetry_store::SourceLayer::Reference);
      // Phase A -- exact same-level match: a reference tile at the survey GGGS level
      // coincides cell-for-cell with the survey tile, so prime it directly.
      auto it = tiles.find(index);
      if (it != tiles.end()) {
        primeFromTile(it->second, sheet_, /*seed_settled=*/false);
      } else {
        // Phase B -- cross-level fallback (#115): loadWindow ALSO returns reference
        // tiles at coarser levels, but each is keyed by its own (coarser) GridIndex,
        // so the find() above missed them and, before this fix, a multi-level
        // reference store (e.g. ENC exports at L5/L7/L8 under an L10 survey) left the
        // blunder gate silently inactive. GGGS is nested, so exactly one coarse tile
        // per level contains this survey tile; pick the FINEST such tile (highest
        // level number below the survey level -- closest to the survey resolution,
        // still shoal-biased/conservative for a false-deep gate) and resample its
        // shallow prior onto the fine survey cells.
        //
        // Containment must be VERIFIED, not assumed from level alone: loadWindow's
        // overlap test is inclusive (tile_io.cpp tileOverlapsBox -- a coarse tile
        // whose edge merely touches the survey window counts as overlapping), so a
        // survey tile flush against a coarse-tile boundary ALSO pulls in the
        // edge-adjacent coarse neighbor. That neighbor shares no cell with the
        // survey tile, and picking it would make primeFromTileResample's
        // grid-mismatch guard skip every fine cell -- reinstating the #115 silent
        // gate-off for boundary tiles. GGGS nesting gives the containing coarse
        // tile at any level as the one holding the survey tile's CENTER, so match
        // on that and reject any neighbor the inclusive window also returned.
        const geographic_msgs::msg::GeoPoint survey_sw = index.southWestPosition();
        const geographic_msgs::msg::GeoPoint survey_ne = index.northEastPosition();
        const geographic_msgs::msg::GeoPoint survey_center = gggs::geoPoint(
          0.5 * (survey_sw.latitude + survey_ne.latitude),
          0.5 * (survey_sw.longitude + survey_ne.longitude));
        const marine_bathymetry_store::BathymetryTile * fallback = nullptr;
        for (const auto & entry : tiles) {
          const gggs::GridIndex & cand = entry.first;
          if (cand.level() >= index.level()) {
            continue;  // not coarser than the survey tile
          }
          if (gggs::Level(cand.level()).gridIndex(survey_center) != cand) {
            continue;  // edge-adjacent neighbor, not the tile that contains us
          }
          if (fallback == nullptr ||
            cand.level() > fallback->index().level())
          {
            fallback = &entry.second;
          }
        }
        if (fallback != nullptr) {
          primeFromTileResample(*fallback, index, sheet_);
          // Auditability (#115): name the fallback level used so the import log shows
          // a cross-level prior was active for this tile.
          std::cerr << "import_bag: reference blunder gate for survey tile " << index
                    << " seeded via cross-level fallback (reference level "
                    << static_cast<int>(fallback->index().level())
                    << " -> survey level " << static_cast<int>(index.level()) << ")"
                    << std::endl;
        }
      }
    } catch (const std::exception & e) {
      std::cerr << "import_bag: could not reference-seed tile on first touch: "
                << e.what() << " (no prior gate for this tile)" << std::endl;
    }
  }

  // else blank -- nothing to seed; still mark it seeded so we do not retry.
  seeded_.insert(index);
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
  // (bathy + backscatter summary) and its corrected-intensity Welford spilled to
  // scratch BEFORE it is dropped, so eviction is lossless and the tile reloads
  // (with its intensity sufficient statistic) on revisit. A tile whose persist/spill
  // THROWS is left
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
  // hypotheses (settled depth + restored corrected-intensity Welford) instead of
  // forming a fresh, partial tile. gridIndicesForSoundings computes the SAME
  // expanded window addSoundings will touch (including a near-seam neighbour tile),
  // so no evicted tile is missed. A tile whose reload FAILS is recorded and, AFTER
  // the add, has this batch's soundings on it dropped to protect the intact on-disk
  // surface. The tile stays evicted, so a LATER batch that revisits it retries the
  // reload -- but the soundings dropped here are NOT replayed (offline is a single
  // pass over the bag), so this is a real, bounded loss, not the "lossless"
  // eviction blend; it is counted and WARNed below rather than hidden.
  // For each tile this batch is about to touch: reload it if it was evicted, else
  // seed it if this is its first touch (seed precedence #96 -- survey warm-start,
  // reference gate, or blank). Both run BEFORE the soundings are added so the new
  // beams accrete onto the seeded/reloaded hypotheses (lossless blend). Iterating
  // the window every batch is cheap; seedNewTile is a marked-once no-op thereafter.
  std::vector<gggs::GridIndex> reload_failed;
  for (const auto & idx : sheet_.gridIndicesForSoundings(soundings)) {
    if (evicted_.count(idx)) {
      if (reloadEvictedTile(idx)) {
        evicted_.erase(idx);
      } else {
        reload_failed.push_back(idx);
      }
    } else if (!seeded_.count(idx)) {
      if (!seedNewTile(idx)) {
        // Survey-seed read error on an existing tile: protect it exactly like a
        // failed reload -- drop this batch's soundings on it after the add so the
        // intact-but-unreadable on-disk surface is never overwritten from scratch.
        reload_failed.push_back(idx);
      }
    }
  }

  sheet_.addSoundings(soundings, time);

  if (!reload_failed.empty()) {
    // Count and report the loss before dropping: the tiles that failed to reload
    // are about to be discarded together with the soundings this batch put on them,
    // and offline those soundings are never replayed. Count soundings whose centre
    // falls in a failed tile (a sounding can also spread into neighbour tiles, so
    // this is a conservative floor, not an exact cell tally -- see
    // GeoMapSheet::gridIndexForSounding).
    const std::set<gggs::GridIndex> failed_set(
      reload_failed.begin(), reload_failed.end());
    std::size_t dropped_soundings = 0;
    for (const auto & s : soundings) {
      if (failed_set.count(sheet_.gridIndexForSounding(s))) {
        ++dropped_soundings;
      }
    }
    std::cerr << "import_bag: WARNING permanently dropping ~" << dropped_soundings
              << " sounding(s) centred in " << reload_failed.size()
              << " un-reloadable/un-seedable tile(s) this batch (reload or "
      "survey-seed failed; the on-disk surface is preserved, but these soundings "
      "are NOT re-processed offline -- not the lossless eviction blend)" << std::endl;
    for (const auto & idx : reload_failed) {
      sheet_.dropTile(idx);  // protect the intact on-disk surface
    }
  }

  evictColdTiles();
}

void ImportAccumulator::finalize(
  const marine_bathymetry_store::StoreMetadata * bathy_metadata,
  const marine_mbes_backscatter_store::StoreMetadata * bs_metadata)
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
  // Store-level provenance sidecars (uma#248 replaced the per-cell SourceRegistry
  // interning table with one coarse StoreMetadata `registry.json` at the store
  // root). Written once at the end; skipped when absent or empty.
  if (!cfg_.store_dir.empty() && bathy_metadata != nullptr &&
    !bathy_metadata->empty())
  {
    std::filesystem::create_directories(cfg_.store_dir);
    bathy_metadata->save(cfg_.store_dir);
  }
  if (!cfg_.bs_store_dir.empty() && bs_metadata != nullptr &&
    !bs_metadata->empty())
  {
    std::filesystem::create_directories(cfg_.bs_store_dir);
    bs_metadata->save(cfg_.bs_store_dir);
  }
  // The spilled raw samples were only needed to reload an evicted tile mid-run;
  // the import is complete, so delete the scratch dir.
  cleanupScratch();
}

}  // namespace cube
