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
#include <cmath>
#include <vector>
#include "cube_bathymetry/map_sheet.h"

namespace cube
{

class MapSheetTest : public ::testing::Test
{
protected:
  CellCounts counts{10, 10};
  CellSizes sizes{1.0f};
};

TEST_F(MapSheetTest, ConstructorSetsProperties)
{
  MapSheet ms(counts, sizes, "order1a");

  EXPECT_DOUBLE_EQ(ms.cellSizes().x, 1.0);
  EXPECT_DOUBLE_EQ(ms.cellSizes().y, 1.0);
  EXPECT_EQ(ms.cellCountsPerGrid().x, 10);
  EXPECT_EQ(ms.cellCountsPerGrid().y, 10);
}

TEST_F(MapSheetTest, InitiallyNoGrids)
{
  MapSheet ms(counts, sizes);
  EXPECT_TRUE(ms.grids().empty());
}

TEST_F(MapSheetTest, AddSoundingsCreatesGrids)
{
  MapSheet ms(counts, sizes);

  std::vector<MapSounding> soundings;
  for (int i = 0; i < 5; ++i) {
    MapSounding s(5.0, 5.0, -10.0f);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    soundings.push_back(s);
  }

  ms.addSoundings(soundings);
  EXPECT_FALSE(ms.grids().empty());
}

TEST_F(MapSheetTest, AddEmptySoundingsNoOp)
{
  MapSheet ms(counts, sizes);
  std::vector<MapSounding> soundings;

  ms.addSoundings(soundings);
  EXPECT_TRUE(ms.grids().empty());
}

TEST_F(MapSheetTest, GetOrCreateGridsCreatesOnDemand)
{
  MapSheet ms(counts, sizes);

  MapBounds bounds(MapPosition(0.0, 0.0), MapPosition(5.0, 5.0));
  auto grids = ms.getOrCreateGridsIn(bounds);

  EXPECT_FALSE(grids.empty());
}

TEST_F(MapSheetTest, GridIndexComputation)
{
  MapSheet ms(counts, sizes);

  // With 10x10 cells at 1m, grid size is 10x10m
  auto idx = ms.gridIndex(MapPosition(5.0, 5.0));
  EXPECT_EQ(idx.x, 0);
  EXPECT_EQ(idx.y, 0);

  auto idx2 = ms.gridIndex(MapPosition(15.0, 25.0));
  EXPECT_EQ(idx2.x, 1);
  EXPECT_EQ(idx2.y, 2);
}

TEST_F(MapSheetTest, NegativeCoordinateGridIndex)
{
  MapSheet ms(counts, sizes);

  auto idx = ms.gridIndex(MapPosition(-5.0, -5.0));
  EXPECT_EQ(idx.x, -1);
  EXPECT_EQ(idx.y, -1);
}

TEST_F(MapSheetTest, GridBoundsExpandWithData)
{
  MapSheet ms(counts, sizes);

  std::vector<MapSounding> soundings;
  MapSounding s(5.0, 5.0, -10.0f);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;
  soundings.push_back(s);

  ms.addSoundings(soundings);

  auto bounds = ms.gridBounds();
  EXPECT_TRUE(valid(bounds));
}

TEST_F(MapSheetTest, SpreadSoundingsCreateMultipleGrids)
{
  MapSheet ms(counts, sizes);

  std::vector<MapSounding> soundings;
  // Soundings in different grid tiles
  for (int i = 0; i < 3; ++i) {
    MapSounding s(i * 15.0, i * 15.0, -10.0f);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    soundings.push_back(s);
  }

  ms.addSoundings(soundings);

  // Should have created multiple grids
  EXPECT_GT(ms.grids().size(), 1u);
}

}  // namespace cube
