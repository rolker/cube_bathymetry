// Copyright 2026 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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

#include "cube_bathymetry/gain_declaration.h"

namespace cube
{

namespace
{

constexpr int64_t kSecond = 1000000000LL;

GainDeclaration twoSegments()
{
  GainDeclaration d;
  d.path = "test.csv";
  d.reference_state = "pre_step";
  d.segments = {
    {"low", 100 * kSecond, 200 * kSecond, 23.0, "measured", "n"},
    {"never", 1000 * kSecond, 2000 * kSecond, -4.0, "operator_reported", "n"}};
  return d;
}

GeoSounding sounding(float intensity)
{
  GeoSounding s(gz4d::GeoPointLatLongDegrees(43.0, -70.6, 10.0));
  s.sounding.intensity = intensity;
  return s;
}

}  // namespace

TEST(GainStateResolver, CountsPerSegmentAndUndeclared)
{
  const GainDeclaration d = twoSegments();
  GainStateResolver resolver(&d);
  EXPECT_DOUBLE_EQ(resolver.apply(99 * kSecond), 0.0);
  EXPECT_DOUBLE_EQ(resolver.apply(100 * kSecond), 23.0);
  EXPECT_DOUBLE_EQ(resolver.apply(199 * kSecond), 23.0);
  EXPECT_DOUBLE_EQ(resolver.apply(200 * kSecond), 0.0);
  EXPECT_DOUBLE_EQ(resolver.apply(1500 * kSecond), -4.0);
  EXPECT_EQ(resolver.counts().pings, 5u);
  EXPECT_EQ(resolver.counts().undeclared, 2u);
  EXPECT_EQ(resolver.counts().by_segment.at("low"), 2u);
  EXPECT_EQ(resolver.counts().by_segment.at("never"), 1u);
  EXPECT_TRUE(resolver.emptySegments().empty());
}

TEST(GainStateResolver, ShiftAddsOffsetToFiniteIntensityOnly)
{
  std::vector<GeoSounding> soundings = {
    sounding(40.0f), sounding(std::numeric_limits<float>::quiet_NaN()),
    sounding(-30.0f)};
  shiftSoundings(soundings, 23.0);
  EXPECT_FLOAT_EQ(soundings[0].sounding.intensity, 63.0f);   // the pinned sign
  EXPECT_TRUE(std::isnan(soundings[1].sounding.intensity));
  EXPECT_FLOAT_EQ(soundings[2].sounding.intensity, -7.0f);
  shiftSoundings(soundings, 0.0);
  EXPECT_FLOAT_EQ(soundings[0].sounding.intensity, 63.0f);
}

// The shift knows nothing of the mode table: a sounding whose correction
// index is unresolved, identity or excluded is shifted all the same.
TEST(GainStateResolver, UnresolvedModePingStillShifted)
{
  std::vector<GeoSounding> soundings = {sounding(10.0f), sounding(10.0f), sounding(10.0f)};
  soundings[0].sounding.correction_idx = kCorrectionUnresolved;
  soundings[1].sounding.correction_idx = kCorrectionIdentity;
  soundings[2].sounding.correction_idx = kCorrectionExclude;
  shiftSoundings(soundings, 5.0);
  for (const auto & s : soundings) {
    EXPECT_FLOAT_EQ(s.sounding.intensity, 15.0f);
  }
  EXPECT_EQ(soundings[2].sounding.correction_idx, kCorrectionExclude);   // stays excluded
}

TEST(GainStateResolver, ReportNamesEmptySegment)
{
  const GainDeclaration d = twoSegments();
  GainStateResolver resolver(&d);
  resolver.apply(150 * kSecond);
  resolver.apply(5 * kSecond);
  const std::string report = resolver.report();
  EXPECT_NE(report.find("reference state 'pre_step'"), std::string::npos) << report;
  EXPECT_NE(
    report.find(
      "low [1970-01-01T00:01:40Z, 1970-01-01T00:03:20Z) +23.000000 dB (measured): 1 pings"),
    std::string::npos) << report;
  EXPECT_NE(report.find("(operator_reported): 0 pings"), std::string::npos) << report;
  EXPECT_NE(report.find("undeclared (at reference): 1 pings"), std::string::npos) << report;
  EXPECT_NE(
    report.find("segment 'never' is declared but no ping fell in it"), std::string::npos)
    << report;
  EXPECT_EQ(resolver.emptySegments(), std::vector<std::string>{"never"});
}

TEST(GainRecordProblem, MatrixOfDeclaredAndRecorded)
{
  const GainDeclaration d = twoSegments();
  // neither side has one
  EXPECT_EQ(gainRecordProblem("t", "", "", nullptr, ""), "");
  // both, equal
  EXPECT_EQ(gainRecordProblem("t", "abc", "pre_step", &d, "abc"), "");
  // recorded but none given
  EXPECT_NE(gainRecordProblem("t", "abc", "pre_step", nullptr, ""), "");
  // given but none recorded
  EXPECT_NE(gainRecordProblem("t", "", "", &d, "abc"), "");
  // different digest, different reference
  EXPECT_NE(gainRecordProblem("t", "abd", "pre_step", &d, "abc"), "");
  EXPECT_NE(gainRecordProblem("t", "abc", "other", &d, "abc"), "");
}

}  // namespace cube
