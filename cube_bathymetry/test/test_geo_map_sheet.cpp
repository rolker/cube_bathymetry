// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#include "cube_bathymetry/geo_map_sheet.h"
#include "marine_autonomy/gggs.h"

namespace cube
{

class GeoMapSheetTest : public ::testing::Test
{
protected:
  float cell_size{1.0f};
};

TEST_F(GeoMapSheetTest, AddSoundingsUpdatesTimestamp)
{
  GeoMapSheet ms(cell_size);

  auto before = std::chrono::steady_clock::now();

  std::vector<GeoSounding> soundings;
  gz4d::GeoPointLatLongDegrees point(43.0, -70.0, -10.0);
  GeoSounding s(point);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;
  soundings.push_back(s);

  ms.addSoundings(soundings);

  EXPECT_GE(ms.lastUpdateTime(), before);
}

TEST_F(GeoMapSheetTest, AddEmptySoundingsNoTimestampUpdate)
{
  GeoMapSheet ms(cell_size);

  auto initial_time = ms.lastUpdateTime();

  std::vector<GeoSounding> soundings;
  ms.addSoundings(soundings);

  EXPECT_EQ(ms.lastUpdateTime(), initial_time);
}

TEST_F(GeoMapSheetTest, AddSoundingsCreatesGrids)
{
  GeoMapSheet ms(cell_size);

  std::vector<GeoSounding> soundings;
  gz4d::GeoPointLatLongDegrees point(43.0, -70.0, -10.0);
  GeoSounding s(point);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;
  soundings.push_back(s);

  ms.addSoundings(soundings);
  EXPECT_FALSE(ms.grids().empty());
}

TEST_F(GeoMapSheetTest, TimestampNotUpdatedWhenNoInsert)
{
  GeoMapSheet ms(cell_size);

  // Add soundings to establish grids and a baseline timestamp
  std::vector<GeoSounding> soundings1;
  gz4d::GeoPointLatLongDegrees point1(43.0, -70.0, -10.0);
  GeoSounding s1(point1);
  s1.sounding.vertical_error = 0.5f;
  s1.sounding.horizontal_error = 0.1f;
  soundings1.push_back(s1);

  auto time1 = std::chrono::steady_clock::now();
  ms.addSoundings(soundings1, time1);

  auto baseline_time = ms.lastUpdateTime();
  EXPECT_EQ(baseline_time, time1);

  // Verify the first insertion produced non-NaN grid data
  bool has_real_data = false;
  for (const auto & g  :  ms.grids()) {
    auto vals = g->values();
    has_real_data = std::any_of(vals.begin(), vals.end(),
        [](const auto & v){return !std::isnan(v.depth);});
    if(has_real_data) {
      break;
    }
  }
  EXPECT_TRUE(has_real_data) << "First insertion should produce non-NaN grid data";

  // Add an empty soundings vector at a later time — no data inserted,
  // so the timestamp must not advance
  std::vector<GeoSounding> empty_soundings;
  auto time2 = time1 + std::chrono::seconds(10);
  ms.addSoundings(empty_soundings, time2);

  EXPECT_EQ(ms.lastUpdateTime(), baseline_time);
}

TEST_F(GeoMapSheetTest, DirtyTrackingSetOnInsertClearedOnDemand)
{
  GeoMapSheet ms(cell_size);

  EXPECT_TRUE(ms.dirtyGrids().empty()) << "fresh sheet has no dirty grids";

  std::vector<GeoSounding> soundings;
  gz4d::GeoPointLatLongDegrees point(43.0, -70.0, -10.0);
  GeoSounding s(point);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;
  soundings.push_back(s);

  ms.addSoundings(soundings);
  EXPECT_FALSE(ms.dirtyGrids().empty()) << "insert should mark grids dirty";

  // Every dirty index must correspond to an existing grid.
  for (const auto & idx : ms.dirtyGrids()) {
    EXPECT_NE(ms.gridAt(idx), nullptr);
  }

  ms.clearDirtyGrids();
  EXPECT_TRUE(ms.dirtyGrids().empty()) << "clearDirtyGrids should empty the set";
}

TEST_F(GeoMapSheetTest, EmptyInsertDoesNotMarkDirty)
{
  GeoMapSheet ms(cell_size);
  std::vector<GeoSounding> empty;
  ms.addSoundings(empty);
  EXPECT_TRUE(ms.dirtyGrids().empty());
}

TEST_F(GeoMapSheetTest, GridAtReturnsNullForAbsentIndex)
{
  GeoMapSheet ms(cell_size);
  gggs::Level level = gggs::Level::fromCellSize(cell_size);
  auto absent = level.gridIndex(10.0, 10.0);
  EXPECT_EQ(ms.gridAt(absent), nullptr);
}

TEST_F(GeoMapSheetTest, SetPredictedDepthAtSeedsCellWithoutDirtying)
{
  GeoMapSheet ms(cell_size);
  gggs::Level level = gggs::Level::fromCellSize(cell_size);

  gggs::CellIndex cell = level.cellIndex(gggs::geoPoint(43.07, -70.76));
  const float depth = -8.0f;
  ms.setPredictedDepthAt(cell, depth, 0.25f);

  // The grid is created but priming must not mark it dirty (it reproduces
  // already-persisted data; re-saving would be redundant churn).
  EXPECT_TRUE(ms.dirtyGrids().empty());

  auto grid = ms.gridAt(cell.grid());
  ASSERT_NE(grid, nullptr);
  EXPECT_FLOAT_EQ(grid->predictedDepthAt(cell), depth);
}

namespace
{
// Add one sounding at (lat, lon) so a single addSoundings touches exactly one
// grid -- used to build a synthetic multi-tile track with a known touch order.
void addOneAt(GeoMapSheet & ms, double lat, double lon)
{
  std::vector<GeoSounding> soundings;
  gz4d::GeoPointLatLongDegrees point(lat, lon, -10.0);
  GeoSounding s(point);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;
  soundings.push_back(s);
  ms.addSoundings(soundings);
}
}  // namespace

// Lossless reload at the sheet level (#70, ADR-0001): setSettledDepthAt must make
// the seeded value re-emit through values() (so the cell survives the next save),
// without marking the grid dirty (it reproduces persisted data).
TEST_F(GeoMapSheetTest, SetSettledDepthAtRoundTripsThroughValues)
{
  GeoMapSheet ms(cell_size);
  gggs::Level level = gggs::Level::fromCellSize(cell_size);
  gggs::CellIndex cell = level.cellIndex(gggs::geoPoint(43.07, -70.76));

  ms.setSettledDepthAt(cell, -8.0f, 0.42f);
  EXPECT_TRUE(ms.dirtyGrids().empty()) << "reload must not dirty the grid";

  auto grid = ms.gridAt(cell.grid());
  ASSERT_NE(grid, nullptr);
  std::size_t finite = 0;
  for (const auto & v : grid->values()) {
    if (!std::isnan(v.depth)) {
      ++finite;
      EXPECT_NEAR(v.depth, -8.0f, 1e-4) << "seeded depth re-emits through values()";
      EXPECT_NEAR(v.uncertainty, 0.42f, 1e-4) << "seeded uncertainty re-emits";
    }
  }
  EXPECT_EQ(finite, 1u) << "exactly the one seeded cell is finite";
}

// Index of the currently most-recently-touched resident grid.
gggs::GridIndex hottestIndex(const GeoMapSheet & ms)
{
  gggs::GridIndex hottest;
  uint64_t best = 0;
  for (const auto & g : ms.grids()) {
    const uint64_t t = ms.lastTouchOf(g->index());
    if (t >= best) {
      best = t;
      hottest = g->index();
    }
  }
  return hottest;
}

// LRU eviction bounds the resident tile count: a track over many tiles, trimmed
// to budget via coldTiles()+dropTile(), leaves exactly `budget` resident. (A
// single sounding's bounds can straddle GGGS edges, so the exact tile count per
// step is incidental -- the invariant is the post-eviction bound.)
TEST_F(GeoMapSheetTest, EvictionBoundsTileCount)
{
  GeoMapSheet ms(cell_size);
  const std::size_t budget = 3;
  for (int i = 0; i < 8; ++i) {
    addOneAt(ms, 43.0 + 0.05 * i, -70.0);  // ~0.05 deg >> one ~960m grid span
  }
  ASSERT_GT(ms.residentTileCount(), budget) << "need more tiles than the budget";

  for (const auto & idx : ms.coldTiles(budget)) {
    ms.dropTile(idx);
  }
  EXPECT_EQ(ms.residentTileCount(), budget);
}

// Eviction is LRU: coldTiles() returns indices in ascending last-touch order, and
// the single most-recently-touched grid survives eviction to budget 1.
TEST_F(GeoMapSheetTest, EvictionLeavesLruTilesResident)
{
  GeoMapSheet ms(cell_size);
  for (int i = 0; i < 6; ++i) {
    addOneAt(ms, 43.0 + 0.05 * i, -70.0);
  }
  ASSERT_GT(ms.residentTileCount(), 1u);

  // coldTiles must be ordered coldest-first (ascending last-touch).
  const auto cold_all = ms.coldTiles(1);
  for (std::size_t i = 1; i < cold_all.size(); ++i) {
    EXPECT_LE(ms.lastTouchOf(cold_all[i - 1]), ms.lastTouchOf(cold_all[i]))
      << "coldTiles must be sorted ascending by last-touch";
  }

  // Evicting to budget 1 leaves exactly the hottest grid resident.
  const gggs::GridIndex hottest = hottestIndex(ms);
  for (const auto & idx : cold_all) {
    ms.dropTile(idx);
  }
  EXPECT_EQ(ms.residentTileCount(), 1u);
  EXPECT_NE(ms.gridAt(hottest), nullptr)
    << "the most-recently-touched grid must survive LRU eviction";
}

// dropTile clears the grid from every tracking structure (grids, last-touch, and
// BOTH dirty sets) so a dropped index can't linger as a phantom save/publish.
TEST_F(GeoMapSheetTest, DropTileClearsAllTracking)
{
  GeoMapSheet ms(cell_size);
  addOneAt(ms, 43.0, -70.0);
  // Pick a grid that actually received data (a single sounding's expanded bounds
  // can create neighbour grids that were never inserted into, hence not dirty).
  ASSERT_FALSE(ms.dirtyGrids().empty());
  const gggs::GridIndex idx = *ms.dirtyGrids().begin();

  ASSERT_NE(ms.gridAt(idx), nullptr);
  ASSERT_TRUE(ms.dirtyGrids().count(idx));
  ASSERT_TRUE(ms.publishDirtyGrids().count(idx));
  ASSERT_GT(ms.lastTouchOf(idx), 0u);

  ms.dropTile(idx);

  EXPECT_EQ(ms.gridAt(idx), nullptr);
  EXPECT_FALSE(ms.dirtyGrids().count(idx));
  EXPECT_FALSE(ms.publishDirtyGrids().count(idx));
  EXPECT_EQ(ms.lastTouchOf(idx), 0u);
}

// Save-failure safety (#70 review must-fix): the node's eviction drops only cold
// tiles that are NOT still dirty -- a tile whose save failed stays in the dirty
// set and must be KEPT, never dropped, or its unsaved soundings are lost. This
// pins the primitive composition the node relies on (coldTiles + dirtyGrids +
// dropTile): if a save fails (every tile still dirty), nothing is evicted.
TEST_F(GeoMapSheetTest, EvictionKeepsStillDirtyColdTiles)
{
  GeoMapSheet ms(cell_size);
  for (int i = 0; i < 8; ++i) {
    addOneAt(ms, 43.0 + 0.05 * i, -70.0);
  }
  const std::size_t budget = 3;
  ASSERT_GT(ms.residentTileCount(), budget);

  // Simulate a failed save: the dirty set is unchanged (nothing persisted). Every
  // dirty (unsaved-with-data) tile must survive eviction; only clean tiles may go.
  const std::set<gggs::GridIndex> still_dirty = ms.dirtyGrids();
  ASSERT_FALSE(still_dirty.empty());
  for (const auto & idx : ms.coldTiles(budget)) {
    if (still_dirty.count(idx)) {
      continue;  // unsaved -- must not drop (the must-fix behavior)
    }
    ms.dropTile(idx);
  }
  for (const auto & idx : still_dirty) {
    EXPECT_NE(ms.gridAt(idx), nullptr)
      << "an unsaved (still-dirty) tile was evicted -- that would lose data";
  }
}

// coldTiles is empty when the resident count is within budget (no spurious
// eviction), whether the budget equals or exceeds the resident count.
TEST_F(GeoMapSheetTest, ColdTilesEmptyWithinBudget)
{
  GeoMapSheet ms(cell_size);
  for (int i = 0; i < 3; ++i) {
    addOneAt(ms, 43.0 + 0.05 * i, -70.0);
  }
  const std::size_t resident = ms.residentTileCount();
  EXPECT_TRUE(ms.coldTiles(resident).empty()) << "budget == resident: nothing cold";
  EXPECT_TRUE(ms.coldTiles(resident + 5).empty()) << "budget > resident: nothing cold";
}

}  // namespace cube
