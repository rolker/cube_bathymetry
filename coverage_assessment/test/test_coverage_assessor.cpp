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
#include <limits>
#include <string>
#include <vector>

#include "coverage_assessment/area_registry.h"
#include "coverage_assessment/coverage_assessor.h"
#include "coverage_assessment/source_accumulator.h"
#include "message_fixtures.h"

namespace coverage_assessment
{

namespace
{


const int64_t kBig = 1000000;
const double kInf = std::numeric_limits<double>::infinity();

// A source over lattice cells [0,10) x [0,10): fill(i, j, &elevation,
// &uncertainty) is called per cell and leaves NaN where it does not assign.
template<typename F>
SourceAccumulator makeSource(F fill)
{
  SourceAccumulator acc(1.0);
  acc.add(
    makeMessage(
      0.0, 0.0, 10, 10, 1.0, [&fill](double cx, double cy, float * e, float * u) {
        fill(static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy)), e, u);
      }), Affine2());
  return acc;
}

// Every cell of [0,10)^2 with the same elevation and uncertainty.
SourceAccumulator uniformSource(float elevation, float uncertainty)
{
  return makeSource(
    [ = ](int, int, float * e, float * u) {
      *e = elevation;
      *u = uncertainty;
    });
}

std::vector<Point2> square10()
{
  return {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}};
}

SurveyStandard standard(double a, double b)
{
  SurveyStandard s;
  s.name = "test";
  s.a = a;
  s.b = b;
  return s;
}

RegisteredArea makeArea(
  const std::vector<Point2> & polygon, const SurveyStandard & s, const std::string & id = "area")
{
  AreaRegistry registry(1.0, kBig);
  AreaDefinition d;
  d.id = id;
  d.polygon = polygon;
  d.standard = s;
  const RegistryResult r = registry.update(d);
  EXPECT_EQ(r.outcome, RegistryOutcome::kAdded) << r.reason;
  return *registry.find(id);
}

AreaAssessment assess(
  const RegisteredArea & area, const std::vector<const SourceAccumulator *> & sources,
  double h_surface = 0.0)
{
  return assessArea(area, sources, h_surface);
}

// The message's invariants plus agreement between the totals and the per-cell
// verdicts, which are computed separately.
void expectInvariants(const AreaAssessment & a)
{
  EXPECT_LE(a.area_meeting_standard, a.area_covered);
  EXPECT_LE(a.area_covered, a.area_total);
  double unique_sum = 0.0;
  for (const SourceShare & s : a.sources) {
    EXPECT_LE(s.area_meeting_standard, s.area_covered);
    EXPECT_LE(s.area_unique, s.area_covered);
    EXPECT_LE(s.area_covered, a.area_covered);
    unique_sum += s.area_unique;
  }
  EXPECT_LE(unique_sum, a.area_covered);

  std::size_t outside = 0, uncovered = 0, below = 0, meets = 0;
  for (uint8_t v : a.verdict) {
    outside += (v == kOutside);
    uncovered += (v == kNotCovered);
    below += (v == kBelowStandard);
    meets += (v == kMeetsStandard);
  }
  const double cs2 = a.cell_size * a.cell_size;
  EXPECT_EQ(a.area_total, static_cast<double>(uncovered + below + meets) * cs2);
  EXPECT_EQ(a.area_covered, static_cast<double>(below + meets) * cs2);
  EXPECT_EQ(a.area_meeting_standard, static_cast<double>(meets) * cs2);
  EXPECT_EQ(outside + uncovered + below + meets, a.verdict.size());
}

}  // namespace

// ---------------------------------------------------------------- assessor

TEST(CoverageAssessor, KnownAreaAndOneSourceCoveringTheLeftHalf)
{
  // Columns 0..4 surveyed at 12 m depth with 0.3 m uncertainty; a = 0.5 m.
  const auto src = makeSource(
    [](int i, int, float * e, float * u) {
      if (i < 5) {
        *e = -12.0f;
        *u = 0.3f;
      }
    });
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
  const AreaAssessment a = assess(area, {&src});
  EXPECT_EQ(a.area_id, "area");
  EXPECT_EQ(a.cell_size, 1.0);
  EXPECT_EQ(a.area_total, 100.0);
  EXPECT_EQ(a.area_covered, 50.0);
  EXPECT_EQ(a.area_meeting_standard, 50.0);
  ASSERT_EQ(a.sources.size(), 1u);
  EXPECT_EQ(a.sources[0].area_covered, 50.0);
  EXPECT_EQ(a.sources[0].area_meeting_standard, 50.0);
  EXPECT_EQ(a.sources[0].area_unique, 50.0);
  EXPECT_EQ(a.cells_above_surface, 0u);
  expectInvariants(a);
  // Verdicts per cell: (2, 2) meets, (7, 2) not covered; mask is the full square.
  EXPECT_EQ(a.verdict[2 * 10 + 2], kMeetsStandard);
  EXPECT_EQ(a.verdict[2 * 10 + 7], kNotCovered);
}

TEST(CoverageAssessor, CoverageOutsideThePolygonIsNotCounted)
{
  // The source covers all 100 cells; the polygon is the right triangle
  // (0,0) (20,0) (0,20) clipped to... a 10x10 grid holds only its lower part:
  // triangle (0,0) (10,0) (0,10) has centres with i + j + 1 < 10: 45 cells.
  const auto src = uniformSource(-10.0f, 0.2f);
  const RegisteredArea area = makeArea({{0.0, 0.0}, {10.0, 0.0}, {0.0, 10.0}}, standard(0.5, 0.0));
  const AreaAssessment a = assess(area, {&src});
  EXPECT_EQ(a.area_total, 45.0);
  EXPECT_EQ(a.area_covered, 45.0);
  EXPECT_EQ(a.area_meeting_standard, 45.0);
  EXPECT_EQ(a.sources[0].area_covered, 45.0);
  EXPECT_EQ(a.sources[0].area_unique, 45.0);
  expectInvariants(a);
}

TEST(CoverageAssessor, TwoOverlappingSourcesShareTheOverlap)
{
  // A covers columns 0..5 (60 cells), B columns 4..9 (60 cells): the overlap is
  // columns 4 and 5 (20 cells), the union the whole square.
  const auto a_src = makeSource(
    [](int i, int, float * e, float * u) {
      if (i <= 5) {
        *e = -10.0f;
        *u = 0.2f;
      }
    });
  const auto b_src = makeSource(
    [](int i, int, float * e, float * u) {
      if (i >= 4) {
        *e = -10.0f;
        *u = 0.2f;
      }
    });
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
  const AreaAssessment r = assess(area, {&a_src, &b_src});
  EXPECT_EQ(r.area_total, 100.0);
  EXPECT_EQ(r.area_covered, 100.0);
  EXPECT_EQ(r.area_meeting_standard, 100.0);
  ASSERT_EQ(r.sources.size(), 2u);
  EXPECT_EQ(r.sources[0].area_covered, 60.0);
  EXPECT_EQ(r.sources[1].area_covered, 60.0);
  EXPECT_EQ(r.sources[0].area_unique, 40.0);
  EXPECT_EQ(r.sources[1].area_unique, 40.0);
  // Sums across sources exceed the totals; area_unique cannot.
  EXPECT_GT(r.sources[0].area_covered + r.sources[1].area_covered, r.area_covered);
  EXPECT_LE(r.sources[0].area_unique + r.sources[1].area_unique, r.area_covered);
  expectInvariants(r);

  // Source order is the order given.
  const AreaAssessment swapped = assess(area, {&b_src, &a_src});
  EXPECT_EQ(swapped.sources[0].area_unique, 40.0);
  EXPECT_EQ(swapped.sources[0].area_covered, 60.0);
  EXPECT_EQ(swapped.area_covered, 100.0);
}

TEST(CoverageAssessor, MissingUncertaintyCoversButDoesNotMeet)
{
  const auto src = uniformSource(-10.0f, kNan);
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
  const AreaAssessment a = assess(area, {&src});
  EXPECT_EQ(a.area_covered, 100.0);
  EXPECT_EQ(a.area_meeting_standard, 0.0);
  EXPECT_EQ(a.sources[0].area_covered, 100.0);
  EXPECT_EQ(a.sources[0].area_meeting_standard, 0.0);
  EXPECT_EQ(a.sources[0].area_unique, 100.0);
  expectInvariants(a);
}

TEST(CoverageAssessor, NegativeInfiniteAndNanUncertaintyNeverMeet)
{
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
  for (float u : {-0.1f, std::numeric_limits<float>::infinity(), kNan}) {
    const auto src = uniformSource(-10.0f, u);
    const AreaAssessment a = assess(area, {&src});
    EXPECT_EQ(a.area_covered, 100.0) << u;
    EXPECT_EQ(a.area_meeting_standard, 0.0) << u;
    EXPECT_EQ(a.sources[0].area_meeting_standard, 0.0) << u;
  }
}

TEST(CoverageAssessor, DepthDependentThresholdSameUncertaintyFailsShallowMeetsDeep)
{
  // a = 0.5, b = 0.1: allowed(2) = sqrt(0.25 + 0.04) = 0.5385,
  // allowed(10) = sqrt(0.25 + 1.0) = 1.118. Uncertainty 0.6 fails at 2 m, meets at 10 m.
  const auto src = makeSource(
    [](int i, int, float * e, float * u) {
      *e = i < 5 ? -2.0f : -10.0f;
      *u = 0.6f;
    });
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.1));
  const AreaAssessment a = assess(area, {&src});
  EXPECT_EQ(a.area_covered, 100.0);
  EXPECT_EQ(a.area_meeting_standard, 50.0);
  EXPECT_EQ(a.verdict[3 * 10 + 1], kBelowStandard);
  EXPECT_EQ(a.verdict[3 * 10 + 8], kMeetsStandard);
  expectInvariants(a);
}

TEST(CoverageAssessor, EqualityMeets)
{
  // a = 3, b = 0.4, depth 10: allowed = sqrt(9 + 16) = 5 exactly.
  const RegisteredArea area = makeArea(square10(), standard(3.0, 0.4));
  const auto at = uniformSource(-10.0f, 5.0f);
  EXPECT_EQ(assess(area, {&at}).area_meeting_standard, 100.0);
  const auto above = uniformSource(-10.0f, 5.01f);
  EXPECT_EQ(assess(area, {&above}).area_meeting_standard, 0.0);
  const auto below = uniformSource(-10.0f, 4.99f);
  EXPECT_EQ(assess(area, {&below}).area_meeting_standard, 100.0);
}

TEST(CoverageAssessor, DepthComesFromTheSmallestUncertaintySource)
{
  // a = 0.5, b = 0.05: allowed(10) = 0.7071, allowed(2) = 0.5099.
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.05));

  // Winner (u 0.52) is the deep one: the cell meets, although the shallow source's
  // own depth would not accept 0.52.
  {
    const auto deep = uniformSource(-10.0f, 0.52f);
    const auto shallow = uniformSource(-2.0f, 0.6f);
    const AreaAssessment a = assess(area, {&deep, &shallow});
    EXPECT_EQ(a.area_meeting_standard, 100.0);
    EXPECT_EQ(a.sources[0].area_meeting_standard, 100.0);
    EXPECT_EQ(a.sources[1].area_meeting_standard, 0.0);  // 0.6 > allowed(2)
    expectInvariants(a);
  }
  // Winner (u 0.52) is the shallow one: the union does NOT meet, although the
  // other source alone does (0.6 <= allowed(10)).
  {
    const auto shallow = uniformSource(-2.0f, 0.52f);
    const auto deep = uniformSource(-10.0f, 0.6f);
    const AreaAssessment a = assess(area, {&shallow, &deep});
    EXPECT_EQ(a.area_covered, 100.0);
    EXPECT_EQ(a.area_meeting_standard, 0.0);
    EXPECT_EQ(a.sources[0].area_meeting_standard, 0.0);  // 0.52 > allowed(2) = 0.5099
    EXPECT_EQ(a.sources[1].area_meeting_standard, 100.0);
    // A source can meet where the assessment does not (the message allows it).
    EXPECT_GT(a.sources[1].area_meeting_standard, a.area_meeting_standard);
    expectInvariants(a);
  }
}

TEST(CoverageAssessor, PerSourceIsJudgedOnItsOwnGridWhileTheUnionMeets)
{
  // A is too uncertain alone (0.9 > allowed), B is fine; the union meets through B.
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
  const auto poor = uniformSource(-10.0f, 0.9f);
  const auto good = uniformSource(-10.0f, 0.3f);
  const AreaAssessment a = assess(area, {&poor, &good});
  EXPECT_EQ(a.area_meeting_standard, 100.0);
  EXPECT_EQ(a.sources[0].area_covered, 100.0);
  EXPECT_EQ(a.sources[0].area_meeting_standard, 0.0);
  EXPECT_EQ(a.sources[1].area_meeting_standard, 100.0);
  EXPECT_EQ(a.sources[0].area_unique, 0.0);
  EXPECT_EQ(a.sources[1].area_unique, 0.0);
  expectInvariants(a);
}

TEST(CoverageAssessor, EqualUncertaintiesTheShallowerDepthSuppliesTheVerdict)
{
  // a = 0.5, b = 0.05; both sources 0.6: allowed(10) = 0.7071 accepts it,
  // allowed(2) = 0.5099 does not. The tie goes to the shallower depth, which fails,
  // in either source order.
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.05));
  const auto deep = uniformSource(-10.0f, 0.6f);
  const auto shallow = uniformSource(-2.0f, 0.6f);
  const AreaAssessment ds = assess(area, {&deep, &shallow});
  const AreaAssessment sd = assess(area, {&shallow, &deep});
  EXPECT_EQ(ds.area_covered, 100.0);
  EXPECT_EQ(ds.area_meeting_standard, 0.0);
  EXPECT_EQ(sd.area_meeting_standard, 0.0);
  EXPECT_EQ(ds.sources[0].area_meeting_standard, 100.0);  // deep alone meets
  EXPECT_EQ(ds.sources[1].area_meeting_standard, 0.0);
  expectInvariants(ds);
  expectInvariants(sd);
}

TEST(CoverageAssessor, DepthWithoutUncertaintyCoversButDoesNotCompete)
{
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.05));
  // S1 has a shallow depth and no uncertainty; S2 has a deep depth and 0.6. S1
  // covers but does not compete, so S2 supplies the verdict at ITS depth (10 m,
  // allowed 0.7071) and the cell meets. If S1 competed at 2 m it would fail.
  const auto no_unc = uniformSource(-2.0f, kNan);
  const auto with_unc = uniformSource(-10.0f, 0.6f);
  const AreaAssessment a = assess(area, {&no_unc, &with_unc});
  EXPECT_EQ(a.area_covered, 100.0);
  EXPECT_EQ(a.area_meeting_standard, 100.0);
  EXPECT_EQ(a.sources[0].area_covered, 100.0);
  EXPECT_EQ(a.sources[0].area_meeting_standard, 0.0);
  EXPECT_EQ(a.sources[1].area_meeting_standard, 100.0);
  EXPECT_EQ(a.sources[0].area_unique, 0.0);  // both cover
  expectInvariants(a);

  // Where only the no-uncertainty source has data the cell is covered, unique to
  // it, and does not meet.
  const auto half = makeSource(
    [](int i, int, float * e, float * u) {
      if (i < 5) {
        *e = -10.0f;
        *u = 0.2f;
      }
    });
  const AreaAssessment b = assess(area, {&no_unc, &half});
  EXPECT_EQ(b.area_covered, 100.0);
  EXPECT_EQ(b.area_meeting_standard, 50.0);
  EXPECT_EQ(b.sources[0].area_unique, 50.0);
  EXPECT_EQ(b.sources[1].area_unique, 0.0);
  expectInvariants(b);
}

TEST(CoverageAssessor, StandardNotSetNothingMeets)
{
  const auto src = uniformSource(-10.0f, 0.0f);  // uncertainty exactly 0
  const double nan = std::numeric_limits<double>::quiet_NaN();
  // The registry refuses the malformed ones, so build the areas directly: the
  // assessor must still fail closed on any of them.
  struct Case {double a; double b;};
  for (const Case c : {Case{0.0, 0.0}, Case{nan, 1.0}, Case{1.0, nan}, Case{-1.0, 1.0},
      Case{1.0, -0.1}, Case{kInf, 0.0}, Case{0.5, kInf}})
  {
    RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
    area.definition.standard = standard(c.a, c.b);
    const AreaAssessment a = assess(area, {&src});
    EXPECT_EQ(a.area_covered, 100.0) << c.a << "," << c.b;
    EXPECT_EQ(a.area_meeting_standard, 0.0) << c.a << "," << c.b;
    EXPECT_EQ(a.sources[0].area_meeting_standard, 0.0) << c.a << "," << c.b;
    expectInvariants(a);
  }
  // And a real standard with the same data does meet (uncertainty 0 <= 0.5).
  const RegisteredArea ok = makeArea(square10(), standard(0.5, 0.0));
  EXPECT_EQ(assess(ok, {&src}).area_meeting_standard, 100.0);
  // b alone sets a standard.
  const RegisteredArea b_only = makeArea(square10(), standard(0.0, 0.1));
  EXPECT_TRUE(b_only.definition.standard.isSet());
  EXPECT_EQ(assess(b_only, {&src}).area_meeting_standard, 100.0);  // 0 <= 1.0
}

TEST(CoverageAssessor, WaterSurfaceShiftChangesTheVerdict)
{
  // Elevation -2, uncertainty 0.6, a = 0.5, b = 0.1. Surface at 0: depth 2,
  // allowed 0.5385, fails. Surface at +8: depth 10, allowed 1.118, meets.
  const auto src = uniformSource(-2.0f, 0.6f);
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.1));
  EXPECT_EQ(assess(area, {&src}, 0.0).area_meeting_standard, 0.0);
  EXPECT_EQ(assess(area, {&src}, 8.0).area_meeting_standard, 100.0);
  // The surface moves the covered area too: at -3 the seabed (-2) is above it.
  const AreaAssessment above = assess(area, {&src}, -3.0);
  EXPECT_EQ(above.area_covered, 0.0);
  EXPECT_EQ(above.area_meeting_standard, 0.0);
  expectInvariants(above);
}

TEST(CoverageAssessor, NegativeDepthIsNotCoveredAndCellsBelowMinusOneAreCounted)
{
  // Surface 0; elevation = -depth, so elevation +3 is depth -3 (not covered,
  // counted), +0.5 is depth -0.5 (not covered, not counted: within the margin),
  // +1 is exactly -1 (not counted: the margin is strict), 0 is depth 0
  // (covered), -5 is depth 5 (covered).
  const auto src = makeSource(
    [](int i, int, float * e, float * u) {
      static const float elev[10] = {3.0f, 3.0f, 3.0f, 0.5f, 1.0f, 0.0f, -5.0f, -5.0f, kNan, kNan};
      *e = elev[i];
      *u = 0.1f;
    });
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
  const AreaAssessment a = assess(area, {&src});
  EXPECT_EQ(a.area_total, 100.0);
  EXPECT_EQ(a.area_covered, 30.0);  // columns 5, 6, 7
  EXPECT_EQ(a.area_meeting_standard, 30.0);
  EXPECT_EQ(a.sources[0].area_covered, 30.0);
  EXPECT_EQ(a.cells_above_surface, 30u);  // columns 0, 1, 2 only
  EXPECT_EQ(a.verdict[4 * 10 + 0], kNotCovered);
  EXPECT_EQ(a.verdict[4 * 10 + 3], kNotCovered);
  EXPECT_EQ(a.verdict[4 * 10 + 5], kMeetsStandard);
  expectInvariants(a);
}

TEST(CoverageAssessor, NegativeDepthOutsideThePolygonIsNotCounted)
{
  const auto src = uniformSource(5.0f, 0.1f);  // depth -5 everywhere
  const RegisteredArea area =
    makeArea({{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}}, standard(0.5, 0.0));
  const AreaAssessment a = assess(area, {&src});
  EXPECT_EQ(a.cells_above_surface, 16u);
  EXPECT_EQ(a.area_covered, 0.0);
}

TEST(CoverageAssessor, NoSourcesAndEmptySourcesScoreZero)
{
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
  const AreaAssessment none = assess(area, {});
  EXPECT_EQ(none.area_total, 100.0);
  EXPECT_EQ(none.area_covered, 0.0);
  EXPECT_EQ(none.area_meeting_standard, 0.0);
  EXPECT_TRUE(none.sources.empty());
  expectInvariants(none);

  // A source that has sent nothing scores zero but keeps its slot.
  const SourceAccumulator empty(1.0);
  const auto full = uniformSource(-10.0f, 0.1f);
  const AreaAssessment two = assess(area, {&empty, &full});
  ASSERT_EQ(two.sources.size(), 2u);
  EXPECT_EQ(two.sources[0].area_covered, 0.0);
  EXPECT_EQ(two.sources[0].area_unique, 0.0);
  EXPECT_EQ(two.sources[1].area_unique, 100.0);
  expectInvariants(two);
}

TEST(CoverageAssessor, PolygonSmallerThanACellHasZeroTotal)
{
  const auto src = uniformSource(-10.0f, 0.1f);
  const RegisteredArea area = makeArea({{0.1, 0.1}, {0.4, 0.1}, {0.1, 0.4}}, standard(0.5, 0.0));
  const AreaAssessment a = assess(area, {&src});
  EXPECT_EQ(a.area_total, 0.0);
  EXPECT_EQ(a.area_covered, 0.0);
  expectInvariants(a);
}

TEST(CoverageAssessor, CellSizeScalesAreas)
{
  SourceAccumulator acc(0.5);
  acc.add(
    makeMessage(0.0, 0.0, 20, 20, 0.5, [](double, double, float * e, float * u) {
      *e = -10.0f;
      *u = 0.1f;
    }), Affine2());
  AreaRegistry registry(0.5, kBig);
  AreaDefinition d;
  d.id = "half";
  d.polygon = square10();
  d.standard = standard(0.5, 0.0);
  ASSERT_EQ(registry.update(d).outcome, RegistryOutcome::kAdded);
  const AreaAssessment a = assess(*registry.find("half"), {&acc});
  EXPECT_EQ(a.cell_size, 0.5);
  EXPECT_EQ(a.area_total, 100.0);  // 400 cells of 0.25 m^2
  EXPECT_EQ(a.area_covered, 100.0);
  EXPECT_EQ(a.area_meeting_standard, 100.0);
}

TEST(CoverageAssessor, CallerBugsThrowRatherThanScoreZero)
{
  const RegisteredArea area = makeArea(square10(), standard(0.5, 0.0));
  const auto src = uniformSource(-10.0f, 0.1f);
  EXPECT_THROW(assess(area, {&src}, std::numeric_limits<double>::quiet_NaN()),
    std::invalid_argument);
  EXPECT_THROW(assess(area, {&src}, kInf), std::invalid_argument);
  EXPECT_THROW(assess(area, {nullptr}), std::invalid_argument);
  const SourceAccumulator other(0.5);
  EXPECT_THROW(assess(area, {&other}), std::invalid_argument);
}

TEST(CoverageAssessor, AssessAllFollowsRegistryOrder)
{
  AreaRegistry registry(1.0, kBig);
  AreaDefinition left;
  left.id = "left";
  left.polygon = {{0.0, 0.0}, {5.0, 0.0}, {5.0, 10.0}, {0.0, 10.0}};
  left.standard = standard(0.5, 0.0);
  AreaDefinition right = left;
  right.id = "right";
  right.polygon = {{5.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {5.0, 10.0}};
  registry.update(right);
  registry.update(left);
  const auto src = makeSource(
    [](int i, int, float * e, float * u) {
      if (i < 5) {
        *e = -10.0f;
        *u = 0.1f;
      }
    });
  const auto all = assessAll(registry, {&src}, 0.0);
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].area_id, "right");
  EXPECT_EQ(all[0].area_covered, 0.0);
  EXPECT_EQ(all[1].area_id, "left");
  EXPECT_EQ(all[1].area_covered, 50.0);
}

// ---------------------------------------------------------------- registry

TEST(AreaRegistry, AddReplaceByIdAndStableOrder)
{
  AreaRegistry registry(1.0, kBig);
  AreaDefinition a;
  a.id = "a";
  a.stamp_ns = 100;
  a.polygon = square10();
  a.standard = standard(0.5, 0.0);
  AreaDefinition b = a;
  b.id = "b";
  EXPECT_EQ(registry.update(a).outcome, RegistryOutcome::kAdded);
  EXPECT_EQ(registry.update(b).outcome, RegistryOutcome::kAdded);
  ASSERT_EQ(registry.areas().size(), 2u);
  EXPECT_EQ(registry.areas()[0].definition.id, "a");
  EXPECT_EQ(registry.areas()[0].mask.count, 100u);

  // Same id, newer stamp: replaced in place, order unchanged, mask redone.
  AreaDefinition a2 = a;
  a2.stamp_ns = 200;
  a2.polygon = {{0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {0.0, 4.0}};
  a2.standard = standard(1.5, 0.0);
  EXPECT_EQ(registry.update(a2).outcome, RegistryOutcome::kReplaced);
  ASSERT_EQ(registry.areas().size(), 2u);
  EXPECT_EQ(registry.areas()[0].definition.id, "a");
  EXPECT_EQ(registry.areas()[0].mask.count, 16u);
  EXPECT_EQ(registry.areas()[0].definition.standard.a, 1.5);
  EXPECT_EQ(registry.areas()[1].definition.id, "b");

  // Equal stamp: the last received wins.
  AreaDefinition a3 = a2;
  a3.standard = standard(2.5, 0.0);
  EXPECT_EQ(registry.update(a3).outcome, RegistryOutcome::kReplaced);
  EXPECT_EQ(registry.find("a")->definition.standard.a, 2.5);
}

TEST(AreaRegistry, StaleStampIsIgnored)
{
  AreaRegistry registry(1.0, kBig);
  AreaDefinition a;
  a.id = "a";
  a.stamp_ns = 200;
  a.polygon = square10();
  a.standard = standard(0.5, 0.0);
  registry.update(a);
  AreaDefinition old = a;
  old.stamp_ns = 100;
  old.standard = standard(9.0, 0.0);
  const RegistryResult r = registry.update(old);
  EXPECT_EQ(r.outcome, RegistryOutcome::kIgnoredStale);
  EXPECT_FALSE(r.reason.empty());
  EXPECT_EQ(registry.find("a")->definition.standard.a, 0.5);
  EXPECT_EQ(registry.find("a")->definition.stamp_ns, 200);
}

TEST(AreaRegistry, InvalidAreaKeepsTheOldOneAndIsNotAddedIfNew)
{
  AreaRegistry registry(1.0, 1000);
  AreaDefinition good;
  good.id = "a";
  good.stamp_ns = 100;
  good.polygon = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}};
  good.standard = standard(0.5, 0.0);
  ASSERT_EQ(registry.update(good).outcome, RegistryOutcome::kAdded);

  const double nan = std::numeric_limits<double>::quiet_NaN();
  std::vector<AreaDefinition> bad(6, good);
  bad[0].polygon = {{0.0, 0.0}, {5.0, 5.0}};                                // 2 vertices
  bad[1].polygon = {{0.0, 0.0}, {nan, 0.0}, {0.0, 5.0}};                    // non-finite
  bad[2].polygon = {{0.0, 0.0}, {5000.0, 0.0}, {5000.0, 5000.0}};           // oversize
  bad[3].standard = standard(-0.5, 0.0);                                    // negative a
  bad[4].standard = standard(0.5, nan);                                     // NaN b
  bad[5].standard = standard(kInf, 0.0);                                    // infinite a
  for (AreaDefinition & d : bad) {
    d.stamp_ns = 500;  // newer, so only the validation can be what refuses it
    const RegistryResult r = registry.update(d);
    EXPECT_EQ(r.outcome, RegistryOutcome::kRejected);
    EXPECT_FALSE(r.reason.empty());
    EXPECT_NE(r.reason.find("'a'"), std::string::npos) << r.reason;
  }
  ASSERT_EQ(registry.areas().size(), 1u);
  EXPECT_EQ(registry.find("a")->definition.stamp_ns, 100);
  EXPECT_EQ(registry.find("a")->definition.standard.a, 0.5);
  EXPECT_EQ(registry.find("a")->mask.count, 100u);

  // A new id that is invalid is not added.
  AreaDefinition fresh = bad[0];
  fresh.id = "fresh";
  EXPECT_EQ(registry.update(fresh).outcome, RegistryOutcome::kRejected);
  EXPECT_EQ(registry.find("fresh"), nullptr);
  EXPECT_EQ(registry.areas().size(), 1u);
}

TEST(AreaRegistry, UnsetStandardIsAcceptedAndReported)
{
  AreaRegistry registry(1.0, kBig);
  AreaDefinition d;
  d.id = "unset";
  d.polygon = square10();  // standard left at a = b = 0
  EXPECT_EQ(registry.update(d).outcome, RegistryOutcome::kAdded);
  EXPECT_FALSE(registry.find("unset")->definition.standard.isSet());
}

TEST(SurveyStandard, AllowedAndIsSet)
{
  EXPECT_FALSE(SurveyStandard().isSet());
  EXPECT_TRUE(standard(0.5, 0.0).isSet());
  EXPECT_TRUE(standard(0.0, 0.01).isSet());
  EXPECT_FALSE(standard(0.0, 0.0).isSet());
  EXPECT_FALSE(standard(kInf, 0.0).isSet());
  EXPECT_FALSE(standard(-1.0, 1.0).isSet());
  EXPECT_EQ(standard(3.0, 0.4).allowed(10.0), 5.0);
  EXPECT_EQ(standard(0.5, 0.0).allowed(100.0), 0.5);
}

}  // namespace coverage_assessment
