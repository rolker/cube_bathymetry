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
#include "cube_bathymetry/grid.h"
#include <cmath>

namespace cube
{

class GridTest : public ::testing::Test
{
protected:
  Parameters params{CellSizes(1.0f), "order1a"};
};

TEST_F(GridTest, ConstructorSetsProperties)
{
  CellCounts counts(10, 10);
  CellSizes sizes(1.0f);
  MapPosition origin(100.0, 200.0);

  Grid g(counts, sizes, origin, params);

  EXPECT_EQ(g.cellCounts().x, 10);
  EXPECT_EQ(g.cellCounts().y, 10);
  EXPECT_DOUBLE_EQ(g.cellSizes().x, 1.0);
  EXPECT_DOUBLE_EQ(g.cellSizes().y, 1.0);
  EXPECT_DOUBLE_EQ(g.origin().x, 100.0);
  EXPECT_DOUBLE_EQ(g.origin().y, 200.0);
}

TEST_F(GridTest, BoundsCorrect)
{
  CellCounts counts(10, 20);
  CellSizes sizes(0.5f);
  MapPosition origin(0.0, 0.0);

  Grid g(counts, sizes, origin, params);
  auto b = g.bounds();

  EXPECT_DOUBLE_EQ(b.minimum.x, 0.0);
  EXPECT_DOUBLE_EQ(b.minimum.y, 0.0);
  EXPECT_DOUBLE_EQ(b.maximum.x, 5.0);
  EXPECT_DOUBLE_EQ(b.maximum.y, 10.0);
}

TEST_F(GridTest, ValuesInitiallyNaN)
{
  CellCounts counts(5, 5);
  CellSizes sizes(1.0f);
  MapPosition origin(0.0, 0.0);

  Grid g(counts, sizes, origin, params);
  auto vals = g.values();

  EXPECT_EQ(vals.size(), 25u);
  for (const auto& v : vals) {
    EXPECT_TRUE(std::isnan(v.depth));
  }
}

TEST_F(GridTest, InsertSingleSounding)
{
  CellCounts counts(10, 10);
  CellSizes sizes(1.0f);
  MapPosition origin(0.0, 0.0);

  Grid g(counts, sizes, origin, params);

  MapSounding s(5.0, 5.0, -10.0f);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;

  EXPECT_TRUE(g.insert(s));
}

TEST_F(GridTest, InsertMultipleSoundingsProducesDepth)
{
  CellCounts counts(10, 10);
  CellSizes sizes(1.0f);
  MapPosition origin(0.0, 0.0);

  Grid g(counts, sizes, origin, params);

  // Insert many consistent soundings at the center
  for (int i = 0; i < 20; ++i) {
    MapSounding s(5.0, 5.0, -10.0f);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    g.insert(s);
  }

  auto vals = g.values();

  // At least some nodes should have non-NaN values
  bool found_valid = false;
  for (const auto& v : vals) {
    if (!std::isnan(v.depth)) {
      found_valid = true;
      break;
    }
  }
  EXPECT_TRUE(found_valid);
}

TEST_F(GridTest, InsertOutsideGridReturnsFalse)
{
  CellCounts counts(10, 10);
  CellSizes sizes(1.0f);
  MapPosition origin(0.0, 0.0);

  Grid g(counts, sizes, origin, params);

  // Sounding far outside the grid
  MapSounding s(100.0, 100.0, -10.0f);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;

  EXPECT_FALSE(g.insert(s));
}

TEST_F(GridTest, InsertVectorOfSoundings)
{
  CellCounts counts(10, 10);
  CellSizes sizes(1.0f);
  MapPosition origin(0.0, 0.0);

  Grid g(counts, sizes, origin, params);

  std::vector<MapSounding> soundings;
  for (int i = 0; i < 5; ++i) {
    MapSounding s(5.0, 5.0, -10.0f);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    soundings.push_back(s);
  }

  EXPECT_TRUE(g.insert(soundings));
}

}  // namespace cube
