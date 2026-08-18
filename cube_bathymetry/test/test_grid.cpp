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

  // Negative horizontal uncertainty: finite, but sqrt(horizontal_error) is NaN,
  // which would reintroduce the NaN propagation the guard prevents.
  MapSounding neg_hu(5.0, 5.0, -10.0f);
  neg_hu.sounding.vertical_error = 0.5f;
  neg_hu.sounding.horizontal_error = -0.1f;
  EXPECT_FALSE(g.insert(neg_hu));

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

// ---- Predicted-surface producer (#59, ADR-0008) ----
//
// Node lattice for the 2x2 grids below (origin (0,0), 1 m cells):
//   node (x=0,y=0) = -10   node (x=1,y=0) = -12
//   node (x=0,y=1) = -11   node (x=1,y=1) = -13

class GridPredictedSurfaceTest : public GridTest
{
protected:
  Grid makeSeededGrid()
  {
    Grid g(CellCounts(2, 2), CellSizes(1.0f), MapPosition(0.0, 0.0), params);
    g.setPredictedDepthAt(0, 0, -10.0f, 0.25f);
    g.setPredictedDepthAt(1, 0, -12.0f, 0.25f);
    g.setPredictedDepthAt(0, 1, -11.0f, 0.25f);
    g.setPredictedDepthAt(1, 1, -13.0f, 0.25f);
    return g;
  }
};

TEST_F(GridPredictedSurfaceTest, SetPredictedDepthAtRoundTrip)
{
  auto g = makeSeededGrid();
  // At exactly the lower-left node the bilinear weights collapse to z[0].
  EXPECT_FLOAT_EQ(g.interpolatePredictedDepth(0.0, 0.0), -10.0f);
  // Out-of-range seeding is ignored, not UB.
  g.setPredictedDepthAt(5, 5, -20.0f, 0.25f);
}

TEST_F(GridPredictedSurfaceTest, InterpolateBilinearInteriorOffCenter)
{
  auto g = makeSeededGrid();
  // Off-center on BOTH axes (dx=0.25, dy=0.75) to catch axis mix-ups:
  // -10*0.75*0.25 + -12*0.25*0.25 + -11*0.75*0.75 + -13*0.25*0.75 = -11.25
  EXPECT_NEAR(g.interpolatePredictedDepth(0.25, 0.75), -11.25f, 1e-5);
}

TEST_F(GridPredictedSurfaceTest, InterpolateNoDataCornerSentinel)
{
  Grid g(CellCounts(2, 2), CellSizes(1.0f), MapPosition(0.0, 0.0), params);
  // Fully unseeded grid: no correction anywhere.
  EXPECT_EQ(g.interpolatePredictedDepth(0.25, 0.75), INVALID_DATA);
  // Three of four corners seeded: still the sentinel.
  g.setPredictedDepthAt(0, 0, -10.0f, 0.25f);
  g.setPredictedDepthAt(1, 0, -12.0f, 0.25f);
  g.setPredictedDepthAt(0, 1, -11.0f, 0.25f);
  EXPECT_EQ(g.interpolatePredictedDepth(0.25, 0.75), INVALID_DATA);
}

TEST_F(GridPredictedSurfaceTest, InterpolateOutOfRangeSentinel)
{
  auto g = makeSeededGrid();
  // Stencil would need node column 2 / row 2 (grid has 0..1) or column -1.
  EXPECT_EQ(g.interpolatePredictedDepth(1.5, 0.5), INVALID_DATA);
  EXPECT_EQ(g.interpolatePredictedDepth(0.5, 1.5), INVALID_DATA);
  EXPECT_EQ(g.interpolatePredictedDepth(-0.5, 0.5), INVALID_DATA);
  EXPECT_EQ(g.interpolatePredictedDepth(0.5, -0.5), INVALID_DATA);
}

TEST_F(GridPredictedSurfaceTest, InsertAppliesSlopeOffset)
{
  auto g = makeSeededGrid();

  // Touchdown (0.25, 0.75): interpolated predicted depth = -11.25 (test above).
  // Only node (x=0,y=1) at (0,1) is within the ~0.55 m capture radius
  // (distance 0.354 m); its offset = predicted@node - predicted@touchdown
  // = -11 - (-11.25) = +0.25, so the queued depth is -11 + 0.25 = -10.75.
  for (int i = 0; i < 5; ++i) {
    MapSounding s(0.25, 0.75, -11.0f);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    ASSERT_TRUE(g.insert(s));
  }

  auto vals = g.values();
  ASSERT_EQ(vals.size(), 4u);
  EXPECT_NEAR(vals[1 * 2 + 0].depth, -10.75, 1e-3);
  // Gates-not-fills (ADR-0008): primed-but-unsurveyed nodes still read NaN.
  EXPECT_TRUE(std::isnan(vals[0].depth));
  EXPECT_TRUE(std::isnan(vals[1].depth));
  EXPECT_TRUE(std::isnan(vals[3].depth));
}

TEST_F(GridPredictedSurfaceTest, InsertWithoutPriorUnchanged)
{
  Grid g(CellCounts(2, 2), CellSizes(1.0f), MapPosition(0.0, 0.0), params);

  // No prior anywhere: the offset-0 path must reproduce the raw depth.
  for (int i = 0; i < 5; ++i) {
    MapSounding s(0.25, 0.75, -11.0f);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    ASSERT_TRUE(g.insert(s));
  }

  auto vals = g.values();
  EXPECT_NEAR(vals[1 * 2 + 0].depth, -11.0, 1e-3);
}

}  // namespace cube
