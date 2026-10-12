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
#include <string>
#include <vector>

#include "coverage_assessment/area_registry.h"
#include "coverage_assessment/assessment_raster.h"
#include "coverage_assessment/coverage_assessor.h"
#include "coverage_assessment/source_accumulator.h"
#include "message_fixtures.h"

namespace coverage_assessment
{

namespace
{

const int64_t kBig = 1000000;

// A source over lattice cells [0,20) x [0,20), depth 10 (surface 0), the given
// uncertainty everywhere.
SourceAccumulator uniformSource(float uncertainty)
{
  SourceAccumulator acc(1.0);
  acc.add(constantMessage(0.0, 0.0, 20, 20, -10.0f, uncertainty), Affine2());
  return acc;
}

AreaDefinition rectArea(
  const std::string & id, double x0, double y0, double x1, double y1, double a)
{
  AreaDefinition d;
  d.id = id;
  d.polygon = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
  d.standard.a = a;
  return d;
}

// The grid value at the centre of lattice cell (i, j).
float valueAt(const grid_map::GridMap & g, int64_t i, int64_t j)
{
  return g.atPosition(
    kMeetsStandardLayer,
    grid_map::Position(static_cast<double>(i) + 0.5, static_cast<double>(j) + 0.5));
}

}  // namespace

TEST(AssessmentRaster, ValuesGeometryAndFrameOfOneArea)
{
  // Source covers columns 0..5 of a 10 x 10 area at u = 0.3 (a = 0.5 meets);
  // columns 6..9 are not covered.
  SourceAccumulator src(1.0);
  src.add(constantMessage(0.0, 0.0, 6, 20, -10.0f, 0.3f), Affine2());
  AreaRegistry registry(1.0, kBig);
  ASSERT_EQ(registry.update(rectArea("a", 2.0, 3.0, 12.0, 13.0, 0.5)).outcome,
    RegistryOutcome::kAdded);
  const auto assessed = assessAll(registry, {&src}, 0.0);

  const CoverageGridResult r = buildCoverageGrid(assessed, 1.0, "boat1/map", kBig);
  ASSERT_TRUE(r.ok) << r.error;
  const grid_map::GridMap & g = r.grid;
  EXPECT_EQ(g.getFrameId(), "boat1/map");
  EXPECT_EQ(g.getResolution(), 1.0);
  EXPECT_EQ(g.getLength().x(), 10.0);
  EXPECT_EQ(g.getLength().y(), 10.0);
  EXPECT_EQ(g.getPosition().x(), 7.0);
  EXPECT_EQ(g.getPosition().y(), 8.0);
  EXPECT_EQ(g.getSize()(0), 10);
  EXPECT_EQ(g.getSize()(1), 10);
  ASSERT_TRUE(g.exists("meets_standard"));
  EXPECT_EQ(g.getLayers().size(), 1u);

  // Lattice columns 2..5 are covered and meet (1); 6..11 are in the area but not
  // covered (NaN). Rows 3..12 all alike.
  for (int64_t j = 3; j < 13; ++j) {
    for (int64_t i = 2; i < 12; ++i) {
      if (i <= 5) {
        EXPECT_EQ(valueAt(g, i, j), 1.0f) << i << "," << j;
      } else {
        EXPECT_TRUE(std::isnan(valueAt(g, i, j))) << i << "," << j;
      }
    }
  }
  // Exactly 40 cells are 1 and none is 0.
  int ones = 0, zeros = 0, nans = 0;
  const grid_map::Matrix & m = g.get("meets_standard");
  for (int r2 = 0; r2 < m.rows(); ++r2) {
    for (int c = 0; c < m.cols(); ++c) {
      ones += (m(r2, c) == 1.0f);
      zeros += (m(r2, c) == 0.0f);
      nans += std::isnan(m(r2, c));
    }
  }
  EXPECT_EQ(ones, 40);
  EXPECT_EQ(zeros, 0);
  EXPECT_EQ(nans, 60);
}

TEST(AssessmentRaster, BelowStandardIsZeroAndNotCoveredIsNan)
{
  const auto src = uniformSource(0.9f);  // fails a = 0.5 everywhere it covers
  AreaRegistry registry(1.0, kBig);
  registry.update(rectArea("a", 0.0, 0.0, 10.0, 10.0, 0.5));
  const CoverageGridResult r = buildCoverageGrid(assessAll(registry, {&src}, 0.0), 1.0, "map",
      kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(valueAt(r.grid, 4, 4), 0.0f);
  EXPECT_EQ(valueAt(r.grid, 0, 0), 0.0f);
  EXPECT_EQ(valueAt(r.grid, 9, 9), 0.0f);
}

// Overlapping areas with different standards: the overlap is 1 only where it
// meets BOTH. Uncertainty 0.3 meets a = 0.5 and fails a = 0.2.
TEST(AssessmentRaster, OverlapIsOneOnlyIfEveryAreaIsMet)
{
  const auto src = uniformSource(0.3f);
  AreaRegistry registry(1.0, kBig);
  registry.update(rectArea("easy", 0.0, 0.0, 6.0, 10.0, 0.5));    // columns 0..5
  registry.update(rectArea("strict", 4.0, 0.0, 10.0, 10.0, 0.2));  // columns 4..9
  const auto assessed = assessAll(registry, {&src}, 0.0);
  ASSERT_EQ(assessed.size(), 2u);
  EXPECT_EQ(assessed[0].area_meeting_standard, 60.0);
  EXPECT_EQ(assessed[1].area_meeting_standard, 0.0);

  const CoverageGridResult r = buildCoverageGrid(assessed, 1.0, "map", kBig);
  ASSERT_TRUE(r.ok) << r.error;
  for (int64_t j = 0; j < 10; ++j) {
    for (int64_t i = 0; i < 4; ++i) {
      EXPECT_EQ(valueAt(r.grid, i, j), 1.0f) << "easy only " << i << "," << j;
    }
    for (int64_t i = 4; i < 6; ++i) {
      EXPECT_EQ(valueAt(r.grid, i, j), 0.0f) << "overlap " << i << "," << j;
    }
    for (int64_t i = 6; i < 10; ++i) {
      EXPECT_EQ(valueAt(r.grid, i, j), 0.0f) << "strict only " << i << "," << j;
    }
  }
}

// Bounding box over several areas, including negative lattice indices, and
// cells of the box that belong to no area stay NaN.
TEST(AssessmentRaster, BoundingBoxCoversAllAreasAndGapsAreNan)
{
  SourceAccumulator src(1.0);
  src.add(constantMessage(-10.0, -10.0, 40, 40, -10.0f, 0.3f), Affine2());
  AreaRegistry registry(1.0, kBig);
  registry.update(rectArea("sw", -8.0, -6.0, -4.0, -2.0, 0.5));  // 4 x 4 at (-8..-5, -6..-3)
  registry.update(rectArea("ne", 5.0, 7.0, 9.0, 12.0, 0.5));     // 4 x 5 at (5..8, 7..11)
  const CoverageGridResult r =
    buildCoverageGrid(assessAll(registry, {&src}, 0.0), 1.0, "map", kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.grid.getLength().x(), 17.0);  // -8 .. 9
  EXPECT_EQ(r.grid.getLength().y(), 18.0);  // -6 .. 12
  EXPECT_EQ(r.grid.getPosition().x(), 0.5);
  EXPECT_EQ(r.grid.getPosition().y(), 3.0);
  EXPECT_EQ(valueAt(r.grid, -8, -6), 1.0f);
  EXPECT_EQ(valueAt(r.grid, -5, -3), 1.0f);
  EXPECT_EQ(valueAt(r.grid, 5, 7), 1.0f);
  EXPECT_EQ(valueAt(r.grid, 8, 11), 1.0f);
  EXPECT_TRUE(std::isnan(valueAt(r.grid, 0, 0)));    // in the box, in no area
  EXPECT_TRUE(std::isnan(valueAt(r.grid, -4, -6)));  // just outside "sw"
  int ones = 0;
  const grid_map::Matrix & m = r.grid.get("meets_standard");
  for (int a = 0; a < m.rows(); ++a) {
    for (int b = 0; b < m.cols(); ++b) {
      ones += (m(a, b) == 1.0f);
    }
  }
  EXPECT_EQ(ones, 16 + 20);
}

TEST(AssessmentRaster, NonDefaultCellSizeKeepsTheLattice)
{
  SourceAccumulator src(0.5);
  src.add(
    makeMessage(0.0, 0.0, 40, 40, 0.5, [](double, double, float * e, float * u) {
      *e = -10.0f;
      *u = 0.3f;
    }), Affine2());
  AreaRegistry registry(0.5, kBig);
  registry.update(rectArea("a", 1.0, 1.0, 4.0, 3.0, 0.5));
  const CoverageGridResult r =
    buildCoverageGrid(assessAll(registry, {&src}, 0.0), 0.5, "map", kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.grid.getResolution(), 0.5);
  EXPECT_EQ(r.grid.getLength().x(), 3.0);
  EXPECT_EQ(r.grid.getLength().y(), 2.0);
  EXPECT_EQ(r.grid.getSize()(0), 6);
  EXPECT_EQ(r.grid.getSize()(1), 4);
  EXPECT_EQ(r.grid.atPosition("meets_standard", grid_map::Position(1.25, 1.25)), 1.0f);
  EXPECT_EQ(r.grid.atPosition("meets_standard", grid_map::Position(3.75, 2.75)), 1.0f);
}

TEST(AssessmentRaster, FailsWithAReasonWhenThereIsNothingToDrawOrItIsTooBig)
{
  const auto src = uniformSource(0.3f);
  const CoverageGridResult none = buildCoverageGrid({}, 1.0, "map", kBig);
  EXPECT_FALSE(none.ok);
  EXPECT_FALSE(none.error.empty());

  // An area smaller than a cell holds no cell centre.
  AreaRegistry tiny(1.0, kBig);
  AreaDefinition d = rectArea("tiny", 0.1, 0.1, 0.4, 0.4, 0.5);
  d.polygon = {{0.1, 0.1}, {0.4, 0.1}, {0.1, 0.4}};
  tiny.update(d);
  EXPECT_FALSE(buildCoverageGrid(assessAll(tiny, {&src}, 0.0), 1.0, "map", kBig).ok);

  // Two small areas far apart: each within the per-area cap, together over it.
  AreaRegistry far(1.0, 1000);
  far.update(rectArea("a", 0.0, 0.0, 10.0, 10.0, 0.5));
  far.update(rectArea("b", 900.0, 900.0, 910.0, 910.0, 0.5));
  ASSERT_EQ(far.areas().size(), 2u);
  const CoverageGridResult over = buildCoverageGrid(assessAll(far, {&src}, 0.0), 1.0, "map", 1000);
  EXPECT_FALSE(over.ok);
  EXPECT_NE(over.error.find("over the limit"), std::string::npos) << over.error;

  EXPECT_FALSE(buildCoverageGrid(assessAll(tiny, {&src}, 0.0), 0.0, "map", kBig).ok);
}

}  // namespace coverage_assessment
