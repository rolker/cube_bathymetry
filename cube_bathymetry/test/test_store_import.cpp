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
#include "marine_bathymetry_store/bathymetry_tile.hpp"

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

constexpr int64_t kStamp = 1234567890123456789LL;
constexpr uint16_t kSource = 7;
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

  auto tiles_a = mapSheetToEpochTiles(ms, kStamp, kSource);
  auto tiles_b = mapSheetToEpochTiles(ms, kStamp, kSource);

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
  auto tiles = mapSheetToEpochTiles(ms, kStamp, kSource);
  EXPECT_TRUE(tiles.empty());
}

}  // namespace cube
