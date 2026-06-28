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

constexpr int64_t kStamp = 1234567890123456789LL;
constexpr uint16_t kSource = 7;
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
      geoGridToTile(*grid, kStamp, kSource);

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
        EXPECT_EQ(cell.timestamp, kStamp);
        EXPECT_EQ(cell.source_index, kSource);
      }
    }
    finite_total += finite_in_grid;
  }
  EXPECT_GT(finite_total, 0u) << "synthetic soundings should populate some cells";
}

// Converting the same map sheet twice must yield an identical tile set: same
// grids, and byte-identical depth/uncertainty/timestamp/source bands.
TEST(StoreImport, ConversionIsDeterministic)
{
  GeoMapSheet ms(1.0f);
  ms.addSoundings(makeSoundings());

  auto tiles_a = mapSheetToTiles(ms, kStamp, kSource);
  auto tiles_b = mapSheetToTiles(ms, kStamp, kSource);

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
    EXPECT_EQ(ta.timestampBand(), tb.timestampBand());
    EXPECT_EQ(ta.sourceBand(), tb.sourceBand());
  }
}

// A map sheet with no soundings produces no tiles (no empty all-no-data files).
TEST(StoreImport, EmptyMapSheetYieldsNoTiles)
{
  GeoMapSheet ms(1.0f);
  auto tiles = mapSheetToTiles(ms, kStamp, kSource);
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
    geoGridToTile(*grids.front(), kStamp, kSource);

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

  auto tiles = mapSheetToTiles(source, kStamp, kSource);
  ASSERT_FALSE(tiles.empty());

  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(1.0f);

  // Copy the tile map (importTiles consumes it) but keep a reference set.
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles_copy = tiles;
  store.importTiles(
    marine_bathymetry_store::SourceLayer::Draft, std::move(tiles));

  GeoMapSheet loaded(1.0f);
  loadIntoSheet(
    store, marine_bathymetry_store::SourceLayer::Draft, loaded);

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
    store, marine_bathymetry_store::SourceLayer::Draft, loaded);
  EXPECT_TRUE(loaded.grids().empty());
}

// Intensity-bearing soundings must produce a non-empty backscatter cell map whose
// cells match the grid's finite-intensity nodeRecords() entries cell-for-cell,
// carry the import timestamp/source, and (with a constant input intensity) surface
// that same constant value (the surfaced backscatter is uncorrected by default,
// BackscatterAngleCorrection::None, #80).
TEST(StoreImport, BackscatterCellsMatchGridRecords)
{
  GeoMapSheet ms(1.0f);
  ms.addSoundings(makeSoundingsWithIntensity(kIntensity));

  auto grids = ms.grids();
  ASSERT_FALSE(grids.empty());

  const auto cells = mapSheetToBackscatterCells(ms, kStamp, kSource);
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
        EXPECT_FLOAT_EQ(found->second.intensity, records[k].intensity);
        // Constant input intensity, uncorrected surface by default (None) ->
        // emitted value is that constant (mean of equal per-beam intensities).
        EXPECT_FLOAT_EQ(found->second.intensity, kIntensity);
        EXPECT_EQ(found->second.timestamp, kStamp);
        EXPECT_EQ(found->second.source_index, kSource);
        // intensity_variance mirrors intensity_var (NaN with < 2 samples); a
        // finite value must round-trip exactly.
        if (std::isnan(records[k].intensity_var)) {
          EXPECT_TRUE(std::isnan(found->second.intensity_variance));
        } else {
          EXPECT_FLOAT_EQ(found->second.intensity_variance, records[k].intensity_var);
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
      geoGridToTile(*grid, kStamp, kSource);
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
  auto bathy = mapSheetToTiles(ms, kStamp, kSource);
  ASSERT_FALSE(bathy.empty());

  GeoMapSheet ms_bs(1.0f);
  ms_bs.addSoundings(makeSoundings());
  const auto cells = mapSheetToBackscatterCells(ms_bs, kStamp, kSource);
  EXPECT_TRUE(cells.empty())
    << "soundings without intensity must surface no backscatter cells";
}

}  // namespace cube
