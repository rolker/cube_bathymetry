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

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "cube_bathymetry/common.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_mbes_backscatter_store/mbes_cell.hpp"

namespace cube
{

namespace
{
// A handful of synthetic soundings near a single point so they all land in one
// GGGS grid (kept tiny so the test is fast and deterministic).
std::vector<GeoSounding> makeSoundings()
{
  std::vector<GeoSounding> soundings;
  const double base_lat = 43.07;
  const double base_lon = -70.76;
  for (int i = 0; i < 5; ++i) {
    // Small lat/lon offsets (~ a few metres) so they spread across a few cells.
    gz4d::GeoPointLatLongDegrees point(
      base_lat + i * 1e-5, base_lon + i * 1e-5, -10.0 - i);
    GeoSounding s(point);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    soundings.push_back(s);
  }
  return soundings;
}

// Same synthetic soundings as makeSoundings(), but each carries a constant
// backscatter intensity so the CUBE node co-estimates a finite per-cell
// intensity (the offline backscatter surface, #80). beam_angle is carried too
// (it rides the {intensity, angle} pair), though by default
// (BackscatterAngleCorrection::None) the surfaced value is uncorrected so the
// angle does not change it here.
std::vector<GeoSounding> makeSoundingsWithIntensity(float intensity)
{
  std::vector<GeoSounding> soundings = makeSoundings();
  for (auto & s : soundings) {
    s.sounding.intensity = intensity;
    s.sounding.beam_angle = 0.1f;
  }
  return soundings;
}

// The SAME lat/lon footprint as makeSoundings() (so the touchdown cells coincide
// cell-for-cell) but every sounding sits at a single, much deeper depth. Used to
// stage a false-deep "blunder" against a shallow predicted surface (#89).
std::vector<GeoSounding> makeDeepSoundings(float depth)
{
  std::vector<GeoSounding> soundings;
  const double base_lat = 43.07;
  const double base_lon = -70.76;
  for (int i = 0; i < 5; ++i) {
    gz4d::GeoPointLatLongDegrees point(
      base_lat + i * 1e-5, base_lon + i * 1e-5, depth);
    GeoSounding s(point);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    soundings.push_back(s);
  }
  return soundings;
}

constexpr float kIntensity = 42.5f;
}  // namespace

// The produced tile cells must match the grid's finite values cell-for-cell, in
// depth and uncertainty, and only finite-depth cells must be written.
TEST(StoreImport, TileCellsMatchGridValues)
{
  GeoMapSheet ms(1.0f);
  ms.addSoundings(makeSoundings());

  auto grids = ms.grids();
  ASSERT_FALSE(grids.empty());

  std::size_t finite_total = 0;
  for (const auto & grid : grids) {
    ASSERT_TRUE(static_cast<bool>(grid));

    // Cache values() once (it mutates node state) and re-derive the cell mapping
    // the same way geoGridToTile does, then compare against the produced tile.
    const std::vector<DepthAndUncertainty> values = grid->values();
    const marine_bathymetry_store::BathymetryTile tile =
      geoGridToTile(*grid);

    gggs::CellAreaIterator it(grid->index());
    std::size_t k = 0;
    std::size_t finite_in_grid = 0;
    for (; it.valid() && k < values.size(); it.next(), ++k) {
      const marine_bathymetry_store::BathyCell cell =
        tile.get((*it).row(), (*it).column());
      if (std::isnan(values[k].depth)) {
        // Skipped cell -- the tile keeps its NaN no-data sentinel.
        EXPECT_FALSE(cell.hasData());
      } else {
        ++finite_in_grid;
        ASSERT_TRUE(cell.hasData());
        EXPECT_DOUBLE_EQ(cell.depth, static_cast<double>(values[k].depth));
        EXPECT_DOUBLE_EQ(cell.uncertainty, static_cast<double>(values[k].uncertainty));
      }
    }
    finite_total += finite_in_grid;
  }
  EXPECT_GT(finite_total, 0u) << "synthetic soundings should populate some cells";
}

// Converting the same map sheet twice must yield an identical tile set: same
// grids, and byte-identical depth/uncertainty bands (BathyCell is 2-band since
// uma#248 — the timestamp/source bands were dropped).
TEST(StoreImport, ConversionIsDeterministic)
{
  GeoMapSheet ms(1.0f);
  ms.addSoundings(makeSoundings());

  auto tiles_a = mapSheetToTiles(ms);
  auto tiles_b = mapSheetToTiles(ms);

  ASSERT_FALSE(tiles_a.empty());
  ASSERT_EQ(tiles_a.size(), tiles_b.size());

  auto it_a = tiles_a.begin();
  auto it_b = tiles_b.begin();
  for (; it_a != tiles_a.end(); ++it_a, ++it_b) {
    EXPECT_EQ(it_a->first, it_b->first);  // same GridIndex key

    const auto & ta = it_a->second;
    const auto & tb = it_b->second;

    // depthBand() returns NaN for no-data cells, so compare with a NaN-aware
    // predicate (NaN != NaN under ==).
    const auto & da = ta.depthBand();
    const auto & db = tb.depthBand();
    const auto & ua = ta.uncertaintyBand();
    const auto & ub = tb.uncertaintyBand();
    ASSERT_EQ(da.size(), db.size());
    for (std::size_t i = 0; i < da.size(); ++i) {
      if (std::isnan(da[i])) {
        EXPECT_TRUE(std::isnan(db[i]));
      } else {
        EXPECT_DOUBLE_EQ(da[i], db[i]);
        EXPECT_DOUBLE_EQ(ua[i], ub[i]);
      }
    }
  }
}

// A map sheet with no soundings produces no tiles (no empty all-no-data files).
TEST(StoreImport, EmptyMapSheetYieldsNoTiles)
{
  GeoMapSheet ms(1.0f);
  auto tiles = mapSheetToTiles(ms);
  EXPECT_TRUE(tiles.empty());
}

// Priming a fresh sheet from a tile must seed the predicted depth at every
// finite cell, matching the tile's stored depth.
TEST(StoreImport, PrimeFromTileSeedsFiniteCells)
{
  // Build a source sheet, convert one grid to a tile.
  GeoMapSheet source(1.0f);
  source.addSoundings(makeSoundings());
  auto grids = source.grids();
  ASSERT_FALSE(grids.empty());

  const std::vector<DepthAndUncertainty> values = grids.front()->values();
  const marine_bathymetry_store::BathymetryTile tile =
    geoGridToTile(*grids.front());

  // Prime a fresh sheet from that tile.
  GeoMapSheet primed(1.0f);
  primeFromTile(tile, primed);

  auto primed_grid = primed.gridAt(tile.index());
  ASSERT_NE(primed_grid, nullptr);

  // Every finite tile cell must have a matching primed predicted depth.
  gggs::CellAreaIterator it(tile.index());
  std::size_t k = 0;
  std::size_t checked = 0;
  for (; it.valid() && k < values.size(); it.next(), ++k) {
    if (std::isnan(values[k].depth)) {
      continue;
    }
    EXPECT_FLOAT_EQ(primed_grid->predictedDepthAt(*it), values[k].depth);
    ++checked;
  }
  EXPECT_GT(checked, 0u) << "tile should carry some finite cells to prime";

  // Priming reproduces persisted data -- it must not mark the sheet dirty.
  EXPECT_TRUE(primed.dirtyGrids().empty());
}

// loadIntoSheet primes a fresh sheet from a store round-trip:
// build a sheet -> tiles -> store draft layer -> load into a fresh sheet ->
// predicted depths match.
TEST(StoreImport, LoadIntoSheetRoundTrip)
{
  GeoMapSheet source(1.0f);
  source.addSoundings(makeSoundings());

  auto tiles = mapSheetToTiles(source);
  ASSERT_FALSE(tiles.empty());

  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(1.0f);

  // Copy the tile map (importTiles consumes it) but keep a reference set.
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles_copy = tiles;
  store.importTiles(
    marine_bathymetry_store::SourceLayer::Survey, std::move(tiles));

  GeoMapSheet loaded(1.0f);
  loadIntoSheet(
    store, marine_bathymetry_store::SourceLayer::Survey, loaded);

  std::size_t checked = 0;
  for (const auto & grid_tile : tiles_copy) {
    const auto & tile = grid_tile.second;
    auto loaded_grid = loaded.gridAt(tile.index());
    ASSERT_NE(loaded_grid, nullptr);

    const std::vector<double> & depth = tile.depthBand();
    gggs::CellAreaIterator it(tile.index());
    std::size_t k = 0;
    for (; it.valid() && k < depth.size(); it.next(), ++k) {
      if (std::isnan(depth[k])) {
        continue;
      }
      EXPECT_FLOAT_EQ(
        loaded_grid->predictedDepthAt(*it), static_cast<float>(depth[k]));
      ++checked;
    }
  }
  EXPECT_GT(checked, 0u);
}

// Loading from a layer with no tiles is a no-op (no crash, no priming).
TEST(StoreImport, LoadIntoSheetEmptyLayerIsNoOp)
{
  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(1.0f);
  GeoMapSheet loaded(1.0f);
  loadIntoSheet(
    store, marine_bathymetry_store::SourceLayer::Survey, loaded);
  EXPECT_TRUE(loaded.grids().empty());
}

// Intensity-bearing soundings must produce a non-empty backscatter cell map whose
// cells match the grid's finite-intensity nodeRecords() entries cell-for-cell,
// encode the 3-band MbesCell sufficient statistic (mean + n=1 sentinel here), and
// (with a constant input intensity) surface that same constant mean (the surfaced
// backscatter is uncorrected by default, BackscatterAngleCorrection::None, #80).
TEST(StoreImport, BackscatterCellsMatchGridRecords)
{
  GeoMapSheet ms(1.0f);
  ms.addSoundings(makeSoundingsWithIntensity(kIntensity));

  auto grids = ms.grids();
  ASSERT_FALSE(grids.empty());

  const auto cells = mapSheetToBackscatterCells(ms);
  EXPECT_FALSE(cells.empty())
    << "intensity-bearing soundings should co-estimate some backscatter cells";

  std::size_t finite_total = 0;
  for (const auto & grid : grids) {
    ASSERT_TRUE(static_cast<bool>(grid));

    // nodeRecords() flushes the median pre-filter; cache it and re-derive the
    // cell mapping the same way geoGridToBackscatterCells does.
    const std::vector<NodeRecord> records = grid->nodeRecords();

    gggs::CellAreaIterator it(grid->index());
    std::size_t k = 0;
    for (; it.valid() && k < records.size(); it.next(), ++k) {
      const auto found = cells.find(*it);
      if (std::isnan(records[k].intensity)) {
        // No co-estimated backscatter -- the cell must be absent from the map.
        EXPECT_EQ(found, cells.end());
      } else {
        ++finite_total;
        ASSERT_NE(found, cells.end());
        // mean is the running-mean band, unchanged from the NodeRecord intensity.
        EXPECT_FLOAT_EQ(found->second.mean, records[k].intensity);
        // Constant input intensity, uncorrected surface by default (None) ->
        // emitted mean is that constant (mean of equal per-beam intensities).
        EXPECT_FLOAT_EQ(found->second.mean, kIntensity);
        // Each cell here has a single intensity-bearing beam (the soundings spread
        // one-per-cell), so intensity_var is NaN and the n=1 sentinel is written:
        // sample_sd == 0, standard_error == 0. welfordFromCell reconstructs n = 1.
        if (std::isnan(records[k].intensity_var)) {
          EXPECT_FLOAT_EQ(found->second.sample_sd, 0.0f);
          EXPECT_FLOAT_EQ(found->second.standard_error, 0.0f);
          const IntensityWelford w = welfordFromCell(found->second);
          EXPECT_EQ(w.n, 1u);
          EXPECT_DOUBLE_EQ(w.mean, static_cast<double>(records[k].intensity));
        } else {
          // n >= 2: sample_sd = sqrt(var*n), standard_error = scale*sqrt(var).
          const float n = static_cast<float>(records[k].n_samples);
          EXPECT_FLOAT_EQ(
            found->second.sample_sd, std::sqrt(records[k].intensity_var * n));
          EXPECT_FLOAT_EQ(
            found->second.standard_error,
            kBackscatterConfidenceScale * std::sqrt(records[k].intensity_var));
        }
      }
    }
  }
  EXPECT_GT(finite_total, 0u)
    << "intensity-bearing soundings should populate some backscatter cells";

  // Independent cross-check: the bathy conversion reaches each cell through a
  // DIFFERENT production path (values() -> geoGridToTile, not nodeRecords()).
  // Every winning hypothesis here carries intensity-bearing beams (all soundings
  // have intensity), so the set of backscatter cells must equal the set of
  // finite-depth bathy cells. A GGGS-walk / iterator-alignment bug in only one of
  // the two conversion functions would make the sets diverge and fail here.
  std::size_t bathy_finite = 0;
  for (const auto & grid : grids) {
    const marine_bathymetry_store::BathymetryTile tile =
      geoGridToTile(*grid);
    gggs::CellAreaIterator it(grid->index());
    for (; it.valid(); it.next()) {
      const marine_bathymetry_store::BathyCell bcell =
        tile.get((*it).row(), (*it).column());
      const bool in_backscatter = cells.count(*it) > 0;
      if (bcell.hasData()) {
        ++bathy_finite;
        EXPECT_TRUE(in_backscatter)
          << "a finite-depth bathy cell must also carry surfaced backscatter";
      } else {
        EXPECT_FALSE(in_backscatter)
          << "a no-data bathy cell must not appear in the backscatter map";
      }
    }
  }
  EXPECT_EQ(bathy_finite, cells.size())
    << "backscatter cells must match the bathy finite-depth cells 1:1";
}

// Soundings with NO intensity (NaN) must produce an EMPTY backscatter cell map
// even though they DO produce finite depth cells -- a NaN-propagation guard so a
// future regression in the intensity threading surfaces as a test failure rather
// than a silently empty Processed product (#80).
TEST(StoreImport, BackscatterNaNPropagation)
{
  GeoMapSheet ms(1.0f);
  ms.addSoundings(makeSoundings());  // no intensity set -> Sounding::intensity is NaN

  // Sanity: the same soundings DO yield finite bathy tiles, so an empty
  // backscatter map is about missing intensity, not missing data.
  auto bathy = mapSheetToTiles(ms);
  ASSERT_FALSE(bathy.empty());

  GeoMapSheet ms_bs(1.0f);
  ms_bs.addSoundings(makeSoundings());
  const auto cells = mapSheetToBackscatterCells(ms_bs);
  EXPECT_TRUE(cells.empty())
    << "soundings without intensity must surface no backscatter cells";
}

// Predicted-only prime (seed_settled=false, #89): seeds the predicted surface
// (turns the blunder gate on) but must NOT settle the cell -- no CUBE hypothesis,
// no values() output, sheet stays clean. Contrasted in the same test against the
// default seed_settled=true, which DOES settle the cell.
TEST(StoreImport, PredictedOnlyPrimeSeedsNoSettledHypothesis)
{
  // Build a tile with finite depths from the synthetic soundings.
  GeoMapSheet source(1.0f);
  source.addSoundings(makeSoundings());
  auto grids = source.grids();
  ASSERT_FALSE(grids.empty());

  const std::vector<DepthAndUncertainty> values = grids.front()->values();
  const marine_bathymetry_store::BathymetryTile tile =
    geoGridToTile(*grids.front());

  // Predicted-only prime: seeds the predicted surface but creates no hypothesis.
  GeoMapSheet predicted_only(1.0f);
  primeFromTile(tile, predicted_only, /*seed_settled=*/false);

  // Contrast: the default prime (seed_settled=true) DOES settle each cell.
  GeoMapSheet settled(1.0f);
  primeFromTile(tile, settled, /*seed_settled=*/true);

  auto po_grid = predicted_only.gridAt(tile.index());
  auto st_grid = settled.gridAt(tile.index());
  ASSERT_NE(po_grid, nullptr);
  ASSERT_NE(st_grid, nullptr);

  // values() is a const method (it flushes the median pre-filter), so it is safe
  // to call through the const grid handle.
  const std::vector<DepthAndUncertainty> po_values = po_grid->values();
  const std::vector<DepthAndUncertainty> st_values = st_grid->values();

  gggs::CellAreaIterator it(tile.index());
  std::size_t k = 0;
  std::size_t checked = 0;
  for (; it.valid() && k < values.size(); it.next(), ++k) {
    if (std::isnan(values[k].depth)) {
      continue;
    }
    ++checked;
    // The predicted surface IS seeded for the predicted-only prime.
    EXPECT_FLOAT_EQ(po_grid->predictedDepthAt(*it), values[k].depth);
    // Predicted-only: NO settled hypothesis -> values() carries no estimate.
    EXPECT_TRUE(std::isnan(po_values[k].depth))
      << "predicted-only prime must not settle a hypothesis";
    // Default prime: the SAME cell carries a settled estimate (the distinguishing
    // contrast -- predicted-only vs predicted+settled).
    EXPECT_FALSE(std::isnan(st_values[k].depth))
      << "default prime (seed_settled=true) must settle the cell";
  }
  EXPECT_GT(checked, 0u) << "tile should carry some finite cells to prime";

  // Priming reproduces persisted data -- it must never mark the sheet dirty.
  EXPECT_TRUE(predicted_only.dirtyGrids().empty());
}

// The load-bearing behavior (#89): a seeded (shallow) predicted surface turns on
// CUBE's blunder gate so a false-deep sounding is REJECTED, whereas with no prior
// the same deep sounding is accepted. Demonstrates the bathy store is protected
// from false-deep detections by the Chart-prior prime.
TEST(StoreImport, SeededPredictedSurfaceRejectsDeepBlunder)
{
  // A shallow predicted surface (~ -10 m) from synthetic soundings -> the Chart-
  // like prior tile we seed from.
  GeoMapSheet shallow_src(1.0f);
  shallow_src.addSoundings(makeSoundings());
  auto shallow_tiles = mapSheetToTiles(shallow_src);
  ASSERT_FALSE(shallow_tiles.empty());

  // A clearly-too-deep blunder at the SAME locations (identical touchdown cells),
  // far below any shallow-surface blunder limit. With target ~ -10 m and the
  // defaults (blunder_minimum 10, blunder_percent 0.25, blunder_scalar 3), the
  // limit is ~ -20 m, so -150 m is unambiguously a blunder.
  constexpr float kDeep = -150.0f;
  const std::vector<GeoSounding> deep = makeDeepSoundings(kDeep);

  // Baseline: WITHOUT a prior, predicted_depth_ stays INVALID_DATA, so Node::insert
  // skips the blunder gate and the deep sounding is accepted -> finite deep cells.
  GeoMapSheet no_prior(1.0f);
  no_prior.addSoundings(deep);
  std::size_t accepted_deep = 0;
  for (const auto & grid : no_prior.grids()) {
    for (const auto & v : grid->values()) {
      if (!std::isnan(v.depth) && v.depth < -100.0f) {
        ++accepted_deep;
      }
    }
  }
  EXPECT_GT(accepted_deep, 0u)
    << "without a predicted surface the deep blunder must be accepted";

  // WITH the shallow predicted surface primed (predicted-only): the blunder gate is
  // active at every primed cell, so the deep sounding that touches it is rejected.
  GeoMapSheet primed(1.0f);
  for (const auto & gt : shallow_tiles) {
    primeFromTile(gt.second, primed, /*seed_settled=*/false);
  }
  primed.addSoundings(deep);

  // Every cell that carries a (shallow) predicted depth must end with NO settled
  // estimate: the deep sounding it received was blunder-rejected, and the
  // predicted-only prime left no hypothesis of its own.
  std::size_t gated_cells = 0;
  for (const auto & gt : shallow_tiles) {
    const auto & tile = gt.second;
    auto grid = primed.gridAt(tile.index());
    ASSERT_NE(grid, nullptr);
    const std::vector<DepthAndUncertainty> vals = grid->values();
    const std::vector<double> & depth = tile.depthBand();
    gggs::CellAreaIterator it(tile.index());
    std::size_t k = 0;
    for (; it.valid() && k < depth.size() && k < vals.size(); it.next(), ++k) {
      if (std::isnan(depth[k])) {
        continue;  // not a primed cell
      }
      ++gated_cells;
      EXPECT_TRUE(std::isnan(vals[k].depth))
        << "a deep blunder must be rejected where a shallow predicted surface gates";
    }
  }
  EXPECT_GT(gated_cells, 0u) << "the shallow tile should prime some cells to gate";
}

// ---- primeFromPriorLayers (#91/#119): the shared prior-layer prime ----

// Reference-only store: the helper primes the predicted surface and the blunder
// gate rejects a false-deep sounding at every primed cell (the live-node #91
// path, same load-bearing behavior as SeededPredictedSurfaceRejectsDeepBlunder).
TEST(StoreImport, PrimeFromPriorLayersReferenceGatesDeepBlunder)
{
  GeoMapSheet shallow_src(1.0f);
  shallow_src.addSoundings(makeSoundings());
  auto shallow_tiles = mapSheetToTiles(shallow_src);
  ASSERT_FALSE(shallow_tiles.empty());

  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(
    1.0f, /*reference_writable=*/true);
  store.importTiles(
    marine_bathymetry_store::SourceLayer::Reference, shallow_tiles);

  GeoMapSheet primed(1.0f);
  const PriorLayerPrimeResult r = primeFromPriorLayers(store, primed);
  EXPECT_GT(r.reference_tiles, 0u);
  EXPECT_EQ(r.chart_tiles, 0u);
  EXPECT_EQ(r.level_mismatched, 0u);
  EXPECT_TRUE(primed.dirtyGrids().empty()) << "priming must not mark dirty";

  primed.addSoundings(makeDeepSoundings(-150.0f));
  std::size_t gated_cells = 0;
  for (const auto & gt : shallow_tiles) {
    auto grid = primed.gridAt(gt.first);
    ASSERT_NE(grid, nullptr);
    const std::vector<DepthAndUncertainty> vals = grid->values();
    const std::vector<double> & depth = gt.second.depthBand();
    gggs::CellAreaIterator it(gt.first);
    std::size_t k = 0;
    for (; it.valid() && k < depth.size() && k < vals.size(); it.next(), ++k) {
      if (std::isnan(depth[k])) {
        continue;
      }
      ++gated_cells;
      EXPECT_TRUE(std::isnan(vals[k].depth))
        << "deep blunder must be rejected at a Reference-primed cell";
    }
  }
  EXPECT_GT(gated_cells, 0u);
}

// Chart-only store: the helper reads the Chart layer too — the #119 semantics
// (charted waters gate) through the shared helper.
TEST(StoreImport, PrimeFromPriorLayersChartGatesDeepBlunder)
{
  GeoMapSheet shallow_src(1.0f);
  shallow_src.addSoundings(makeSoundings());
  auto shallow_tiles = mapSheetToTiles(shallow_src);
  ASSERT_FALSE(shallow_tiles.empty());

  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(
    1.0f, /*reference_writable=*/false, /*chart_staging_writable=*/true);
  store.importTiles(marine_bathymetry_store::SourceLayer::Chart, shallow_tiles);

  GeoMapSheet primed(1.0f);
  const PriorLayerPrimeResult r = primeFromPriorLayers(store, primed);
  EXPECT_EQ(r.reference_tiles, 0u);
  EXPECT_GT(r.chart_tiles, 0u);
  EXPECT_EQ(r.level_mismatched, 0u);

  primed.addSoundings(makeDeepSoundings(-150.0f));
  std::size_t gated_cells = 0;
  for (const auto & gt : shallow_tiles) {
    auto grid = primed.gridAt(gt.first);
    ASSERT_NE(grid, nullptr);
    const std::vector<DepthAndUncertainty> vals = grid->values();
    const std::vector<double> & depth = gt.second.depthBand();
    gggs::CellAreaIterator it(gt.first);
    std::size_t k = 0;
    for (; it.valid() && k < depth.size() && k < vals.size(); it.next(), ++k) {
      if (std::isnan(depth[k])) {
        continue;
      }
      ++gated_cells;
      EXPECT_TRUE(std::isnan(vals[k].depth))
        << "deep blunder must be rejected at a Chart-primed cell";
    }
  }
  EXPECT_GT(gated_cells, 0u);
}

// Where BOTH layers cover a cell, Reference (higher priority) must win: the
// helper primes Chart first, then Reference overwrites.
TEST(StoreImport, PrimeFromPriorLayersReferencePrecedesChart)
{
  // Reference surface ~ -10 m (makeSoundings), Chart surface -30 m at the SAME
  // footprint (makeDeepSoundings shares the lat/lon pattern).
  GeoMapSheet ref_src(1.0f);
  ref_src.addSoundings(makeSoundings());
  auto ref_tiles = mapSheetToTiles(ref_src);
  ASSERT_FALSE(ref_tiles.empty());

  GeoMapSheet chart_src(1.0f);
  chart_src.addSoundings(makeDeepSoundings(-30.0f));
  auto chart_tiles = mapSheetToTiles(chart_src);
  ASSERT_FALSE(chart_tiles.empty());

  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(
    1.0f, /*reference_writable=*/true, /*chart_staging_writable=*/true);
  store.importTiles(marine_bathymetry_store::SourceLayer::Chart, chart_tiles);
  store.importTiles(marine_bathymetry_store::SourceLayer::Reference, ref_tiles);

  GeoMapSheet primed(1.0f);
  const PriorLayerPrimeResult r = primeFromPriorLayers(store, primed);
  EXPECT_GT(r.reference_tiles, 0u);
  EXPECT_GT(r.chart_tiles, 0u);

  // Every cell the Reference tile covers must carry the REFERENCE depth, not
  // the Chart depth it was primed with first.
  std::size_t checked = 0;
  for (const auto & gt : ref_tiles) {
    auto grid = primed.gridAt(gt.first);
    ASSERT_NE(grid, nullptr);
    const std::vector<double> & depth = gt.second.depthBand();
    gggs::CellAreaIterator it(gt.first);
    std::size_t k = 0;
    for (; it.valid() && k < depth.size(); it.next(), ++k) {
      if (std::isnan(depth[k])) {
        continue;
      }
      ++checked;
      EXPECT_FLOAT_EQ(
        grid->predictedDepthAt(*it), static_cast<float>(depth[k]))
        << "Reference must overwrite the Chart prime where both cover a cell";
    }
  }
  EXPECT_GT(checked, 0u);
}

// A multi-level prior store (the #115 ENC case): tiles at a coarser level than
// the sheet must be counted and SKIPPED, never primed cell-for-cell (the
// plan-review must-fix — primeFromTile has no cross-level guard).
TEST(StoreImport, PrimeFromPriorLayersSkipsLevelMismatchedTiles)
{
  // Build the coarse tile DIRECTLY (a handful of synthetic soundings never
  // captures a node on ~7 m cells): a dense shallow tile at a coarser GGGS
  // level than the 1 m sheet below.
  const gggs::GridIndex coarse_grid =
    gggs::Level::fromCellSize(8.0f).gridIndex(43.07, -70.76);
  marine_bathymetry_store::BathymetryTile ctile(coarse_grid);
  for (gggs::CellAreaIterator cit(coarse_grid); cit.valid(); cit.next()) {
    ctile.set(
      (*cit).row(), (*cit).column(),
      marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
  }
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> coarse_tiles;
  coarse_tiles.emplace(coarse_grid, std::move(ctile));

  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(
    8.0f, /*reference_writable=*/true);
  store.importTiles(
    marine_bathymetry_store::SourceLayer::Reference, std::move(coarse_tiles));

  GeoMapSheet fine(1.0f);
  const PriorLayerPrimeResult r = primeFromPriorLayers(store, fine);
  EXPECT_EQ(r.total(), 0u) << "no exact-level tile exists to prime";
  EXPECT_GT(r.level_mismatched, 0u);
  EXPECT_TRUE(fine.grids().empty())
    << "a skipped coarse tile must not lazy-create wrong-geometry nodes";
}

// Backscatter Welford round-trip incl. n>=2 real dispersion (#96): several beams
// with DIFFERENT intensities on one cell produce a finite intensity_var; the
// 3-band encode + welfordFromCell reconstruct the sample count, mean, and estimate
// variance -- the seed-from-store path an off-boat re-run relies on.
TEST(StoreImport, BackscatterWelfordRoundTripMultiSample)
{
  GeoMapSheet ms(1.0f);
  // Several soundings at the SAME position (one cell) with distinct intensities so
  // the node co-estimates n>=2 with real dispersion (intensity_var finite).
  std::vector<GeoSounding> stacked;
  const float intensities[] = {30.0f, 32.0f, 28.0f, 31.0f, 29.0f};
  for (float bs : intensities) {
    gz4d::GeoPointLatLongDegrees point(43.07, -70.76, -12.0);
    GeoSounding s(point);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    s.sounding.intensity = bs;
    s.sounding.beam_angle = 0.0f;
    stacked.push_back(s);
  }
  ms.addSoundings(stacked);

  const auto cells = mapSheetToBackscatterCells(ms);
  ASSERT_FALSE(cells.empty());

  std::size_t checked_multi = 0;
  for (const auto & grid : ms.grids()) {
    const std::vector<NodeRecord> records = grid->nodeRecords();
    gggs::CellAreaIterator it(grid->index());
    std::size_t k = 0;
    for (; it.valid() && k < records.size(); it.next(), ++k) {
      if (std::isnan(records[k].intensity)) {continue;}
      const auto found = cells.find(*it);
      ASSERT_NE(found, cells.end());
      if (!std::isnan(records[k].intensity_var)) {
        ++checked_multi;
        const IntensityWelford w = welfordFromCell(found->second);
        EXPECT_EQ(w.n, records[k].n_samples);
        EXPECT_NEAR(w.mean, static_cast<double>(records[k].intensity), 1e-4);
        // Estimate variance ((m2/(n-1))/n) round-trips to intensity_var.
        const double recon_var = (w.m2 / (w.n - 1)) / w.n;
        EXPECT_NEAR(
          recon_var, static_cast<double>(records[k].intensity_var), 1e-4);
      }
    }
  }
  EXPECT_GT(checked_multi, 0u)
    << "stacked soundings should co-estimate at least one n>=2 cell";
}

// welfordFromCell unit round-trip: the n=1 sentinel, the no-data cell, and the
// n>=2 path each invert the 3-band encode exactly (#96).
TEST(StoreImport, WelfordFromCellInvertsEncode)
{
  namespace mbs = marine_mbes_backscatter_store;
  // n = 1 sentinel: sample_sd == 0 with a finite mean -> n = 1, M2 = 0.
  {
    mbs::MbesCell cell;
    cell.mean = 25.0f;
    cell.standard_error = 0.0f;
    cell.sample_sd = 0.0f;
    const IntensityWelford w = welfordFromCell(cell);
    EXPECT_EQ(w.n, 1u);
    EXPECT_DOUBLE_EQ(w.mean, 25.0);
    EXPECT_DOUBLE_EQ(w.m2, 0.0);
  }
  // no-data cell (mean NaN) -> empty Welford {n = 0}.
  {
    const mbs::MbesCell cell;  // defaults: mean NaN
    const IntensityWelford w = welfordFromCell(cell);
    EXPECT_EQ(w.n, 0u);
  }
  // n >= 2: build the 3-band from a known (n, var), invert, confirm n recovered.
  {
    const uint32_t n = 7;
    const float var = 0.5f;  // variance of the mean
    mbs::MbesCell cell;
    cell.mean = 40.0f;
    cell.sample_sd = std::sqrt(var * n);
    cell.standard_error = kBackscatterConfidenceScale * std::sqrt(var);
    const IntensityWelford w = welfordFromCell(cell);
    EXPECT_EQ(w.n, n);
    EXPECT_DOUBLE_EQ(w.mean, 40.0);
    const double recon_var = (w.m2 / (w.n - 1)) / w.n;
    EXPECT_NEAR(recon_var, static_cast<double>(var), 1e-5);
  }
}

}  // namespace cube
