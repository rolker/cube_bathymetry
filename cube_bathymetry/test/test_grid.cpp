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
#include <limits>
#include <vector>
#include "cube_bathymetry/grid.h"

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
  for (const auto & v  : vals) {
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
  for (const auto & v  : vals) {
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

TEST_F(GridTest, InsertNonFiniteSoundingReturnsFalse)
{
  Grid g(CellCounts(10, 10), CellSizes(1.0f), MapPosition(0.0, 0.0), params);

  const float nan = std::numeric_limits<float>::quiet_NaN();

  // NaN depth (e.g. from a NaN attitude in the error model)
  MapSounding bad_depth(5.0, 5.0, nan);
  bad_depth.sounding.vertical_error = 0.5f;
  bad_depth.sounding.horizontal_error = 0.1f;
  EXPECT_FALSE(g.insert(bad_depth));

  // NaN vertical uncertainty (the value that becomes the CUBE variance)
  MapSounding bad_vu(5.0, 5.0, -10.0f);
  bad_vu.sounding.vertical_error = nan;
  bad_vu.sounding.horizontal_error = 0.1f;
  EXPECT_FALSE(g.insert(bad_vu));

  // NaN horizontal uncertainty
  MapSounding bad_hu(5.0, 5.0, -10.0f);
  bad_hu.sounding.vertical_error = 0.5f;
  bad_hu.sounding.horizontal_error = nan;
  EXPECT_FALSE(g.insert(bad_hu));

  // Non-positive variance (a divide-by-zero / negative-variance hazard)
  MapSounding zero_vu(5.0, 5.0, -10.0f);
  zero_vu.sounding.vertical_error = 0.0f;
  zero_vu.sounding.horizontal_error = 0.1f;
  EXPECT_FALSE(g.insert(zero_vu));

  // Nothing got inserted: the whole grid is still empty.
  for (const auto & v  : g.values()) {
    EXPECT_TRUE(std::isnan(v.depth));
  }
}

// Regression for #36 / the sim sonar-path break: a single NaN sounding
// (the symptom of missing attitude/odom TF) must not be able to wipe out a
// cell already estimated from good soundings.
TEST_F(GridTest, NonFiniteSoundingDoesNotPoisonGoodData)
{
  Grid g(CellCounts(10, 10), CellSizes(1.0f), MapPosition(0.0, 0.0), params);

  for (int i = 0; i < 20; ++i) {
    MapSounding s(5.0, 5.0, -10.0f);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    g.insert(s);
  }

  // Capture that a populated depth exists before the bad insert.
  bool had_valid = false;
  for (const auto & v  : g.values()) {
    if (!std::isnan(v.depth)) {had_valid = true; break;}
  }
  ASSERT_TRUE(had_valid);

  // Inject a NaN-uncertainty sounding at the same location.
  MapSounding poison(5.0, 5.0, -10.0f);
  poison.sounding.vertical_error = std::numeric_limits<float>::quiet_NaN();
  poison.sounding.horizontal_error = 0.1f;
  EXPECT_FALSE(g.insert(poison));

  // The good estimate survives, finite.
  bool still_valid = false;
  for (const auto & v  : g.values()) {
    if (!std::isnan(v.depth)) {
      EXPECT_TRUE(std::isfinite(v.depth));
      EXPECT_TRUE(std::isfinite(v.uncertainty));
      still_valid = true;
    }
  }
  EXPECT_TRUE(still_valid);
}

}  // namespace cube
