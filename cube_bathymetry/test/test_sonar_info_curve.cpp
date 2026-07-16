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

// Tests for the SonarInfo -> AngularResponseCurve validation (#102) and the
// 'auto' correction mode (curve presence enables the empirical correction).

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <string>
#include "cube_bathymetry/angular_response_curve.h"
#include "cube_bathymetry/sonar_info_curve.h"
#include "marine_interfaces/msg/sonar_info.hpp"

using marine_interfaces::msg::SonarInfo;

namespace
{
SonarInfo tier2Info()
{
  SonarInfo info;
  info.angular_response_angle_deg = {30.5f, 0.5f};   // unsorted on purpose
  info.angular_response_db_rel_nadir = {-12.0f, 0.0f};
  info.angular_response_tl = SonarInfo::ANGULAR_RESPONSE_TL_REMOVED;
  info.angular_response_absorption_db_per_m = 0.00025f;
  return info;
}
}  // namespace

TEST(SonarInfoCurve, AcceptsTier2AndSorts)
{
  cube::AngularResponseCurve curve;
  std::string reason;
  ASSERT_TRUE(cube::curveFromSonarInfo(tier2Info(), curve, reason));
  ASSERT_EQ(curve.points.size(), 2u);
  EXPECT_FLOAT_EQ(curve.points[0].first, 0.5f);     // sorted ascending
  EXPECT_FLOAT_EQ(curve.points[1].first, 30.5f);
  EXPECT_FLOAT_EQ(curve.points[1].second, -12.0f);
  EXPECT_TRUE(curve.tl_removed);
  EXPECT_FLOAT_EQ(curve.absorption_db_per_m, 0.00025f);
  EXPECT_TRUE(reason.empty());
}

TEST(SonarInfoCurve, AcceptsTier1WithZeroAlpha)
{
  auto info = tier2Info();
  info.angular_response_tl = SonarInfo::ANGULAR_RESPONSE_TL_IN;
  info.angular_response_absorption_db_per_m =
    std::numeric_limits<float>::quiet_NaN();  // producer convention for TL_IN
  cube::AngularResponseCurve curve;
  std::string reason;
  ASSERT_TRUE(cube::curveFromSonarInfo(info, curve, reason));
  EXPECT_FALSE(curve.tl_removed);
  EXPECT_FLOAT_EQ(curve.absorption_db_per_m, 0.0f);  // consumer default
}

TEST(SonarInfoCurve, RejectsEmptyUnknownProvenanceMismatchAndNanAlpha)
{
  cube::AngularResponseCurve curve;
  std::string reason;

  SonarInfo empty;  // no curve declared: reject, but that's not an error
  EXPECT_FALSE(cube::curveFromSonarInfo(empty, curve, reason));

  auto unknown = tier2Info();
  unknown.angular_response_tl = SonarInfo::ANGULAR_RESPONSE_TL_UNKNOWN;
  EXPECT_FALSE(cube::curveFromSonarInfo(unknown, curve, reason));
  EXPECT_NE(reason.find("UNKNOWN TL provenance"), std::string::npos);

  auto mismatch = tier2Info();
  mismatch.angular_response_db_rel_nadir.pop_back();
  EXPECT_FALSE(cube::curveFromSonarInfo(mismatch, curve, reason));
  EXPECT_NE(reason.find("disagree"), std::string::npos);

  auto nan_alpha = tier2Info();  // TL_REMOVED with unknown alpha: unusable
  nan_alpha.angular_response_absorption_db_per_m =
    std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(cube::curveFromSonarInfo(nan_alpha, curve, reason));
  EXPECT_NE(reason.find("not finite"), std::string::npos);

  auto nan_point = tier2Info();  // wire input: non-finite points rejected
  nan_point.angular_response_db_rel_nadir[0] =
    std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(cube::curveFromSonarInfo(nan_point, curve, reason));
  EXPECT_NE(reason.find("non-finite point"), std::string::npos);
}

TEST(SonarInfoCurve, ParseAutoModeAndAutoAppliesLikeEmpirical)
{
  cube::BackscatterAngleCorrection mode = cube::BackscatterAngleCorrection::None;
  EXPECT_TRUE(cube::parseBackscatterAngleCorrection("auto", mode));
  EXPECT_EQ(mode, cube::BackscatterAngleCorrection::Auto);
  EXPECT_TRUE(cube::parseBackscatterAngleCorrection("AUTO", mode));
  EXPECT_FALSE(cube::parseBackscatterAngleCorrection("automatic", mode));

  // Auto with a curve corrects exactly like Empirical; without one it is
  // the identity.
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  params.angular_response_curve = {{0.0f, 0.0f}, {60.0f, -12.0f}};
  params.backscatter_angle_correction = cube::BackscatterAngleCorrection::Auto;
  const double corrected = cube::correctBeamIntensity(
    -20.0f, static_cast<float>(30.0 * M_PI / 180.0), NAN, params);
  params.backscatter_angle_correction =
    cube::BackscatterAngleCorrection::Empirical;
  EXPECT_DOUBLE_EQ(corrected, cube::correctBeamIntensity(
      -20.0f, static_cast<float>(30.0 * M_PI / 180.0), NAN, params));
  EXPECT_NE(corrected, -20.0);
  params.angular_response_curve.clear();
  params.backscatter_angle_correction = cube::BackscatterAngleCorrection::Auto;
  EXPECT_DOUBLE_EQ(-20.0, cube::correctBeamIntensity(
      -20.0f, static_cast<float>(30.0 * M_PI / 180.0), NAN, params));
}
