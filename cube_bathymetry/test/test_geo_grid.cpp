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
#include "cube_bathymetry/geo_grid.h"
#include <cmath>
#include <vector>

namespace cube
{

class GeoGridTest : public ::testing::Test
{
protected:
  Parameters params{CellSizes(1.0f), "order1a"};
  gggs::Level level{gggs::Level::fromCellSize(1.0f)};

  gggs::GridIndex makeGridIndex(double lat, double lon)
  {
    return level.gridIndex(lat, lon);
  }

  GeoSounding makeGeoSounding(double lat, double lon, double depth,
    float vert_err = 0.5f, float horiz_err = 0.1f)
  {
    gz4d::GeoPointLatLongDegrees point(lat, lon, depth);
    GeoSounding s(point);
    s.sounding.vertical_error = vert_err;
    s.sounding.horizontal_error = horiz_err;
    return s;
  }
};

TEST_F(GeoGridTest, ConstructorStoresIndex)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  EXPECT_EQ(g.index(), grid_index);
}

TEST_F(GeoGridTest, InsertSingleSoundingReturnsTrue)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  auto s = makeGeoSounding(43.07, -70.76, -10.0);
  EXPECT_TRUE(g.insert(s));
}

TEST_F(GeoGridTest, InsertEmptyVectorReturnsFalse)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  std::vector<GeoSounding> empty;
  EXPECT_FALSE(g.insert(empty));
}

TEST_F(GeoGridTest, InsertNonEmptyVectorReturnsTrue)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  std::vector<GeoSounding> soundings;
  soundings.push_back(makeGeoSounding(43.07, -70.76, -10.0));
  soundings.push_back(makeGeoSounding(43.07, -70.76, -10.5));
  EXPECT_TRUE(g.insert(soundings));
}

TEST_F(GeoGridTest, ValuesAfterInsertionsContainsValidDepths)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // Insert enough soundings to trigger hypothesis creation
  for (int i = 0; i < 20; ++i) {
    g.insert(makeGeoSounding(43.07, -70.76, -10.0));
  }

  auto vals = g.values();
  bool found_valid = false;
  for (const auto& v : vals) {
    if (!std::isnan(v.depth)) {
      found_valid = true;
      break;
    }
  }
  EXPECT_TRUE(found_valid);
}

TEST_F(GeoGridTest, MultipleSoundingsConverge)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // Insert many consistent soundings at the same location
  for (int i = 0; i < 30; ++i) {
    g.insert(makeGeoSounding(43.07, -70.76, -10.0));
  }

  auto vals = g.values();
  bool found_close = false;
  for (const auto& v : vals) {
    if (!std::isnan(v.depth)) {
      // Depth should converge toward the inserted value
      EXPECT_NEAR(v.depth, -10.0f, 2.0f);
      found_close = true;
    }
  }
  EXPECT_TRUE(found_close);
}

}  // namespace cube
