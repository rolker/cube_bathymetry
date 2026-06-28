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
#include <cstdio>
#include <fstream>
#include <string>
#include "cube_bathymetry/angular_response_curve.h"

namespace cube
{

namespace
{
// Write `content` to a unique temp file; the path is returned for loading and
// removed by the caller.
std::string writeTemp(const std::string & content)
{
  std::string path = std::string(std::tmpnam(nullptr)) + ".csv";
  std::ofstream out(path);
  out << content;
  out.close();
  return path;
}
}  // namespace

// A well-formed curve CSV (header + comment + data rows, like the M3 seed) parses
// into ascending {abs_angle_deg, db_relative_to_nadir} pairs, reading columns 0
// and 3 and skipping the header and comment lines.
TEST(AngularResponseCurve, ParsesSeedFormat)
{
  const std::string csv =
    "# M3 empirical angular response\n"
    "abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n"
    "1.0,-40.155,17984,+0.000\n"
    "3.0,-40.328,19986,-0.173\n"
    "5.0,-40.971,15999,-0.816\n";
  const std::string path = writeTemp(csv);

  auto curve = loadAngularResponseCurve(path);
  std::remove(path.c_str());

  ASSERT_EQ(curve.size(), 3u);
  EXPECT_FLOAT_EQ(curve[0].first, 1.0f);
  EXPECT_FLOAT_EQ(curve[0].second, 0.0f);
  EXPECT_FLOAT_EQ(curve[1].first, 3.0f);
  EXPECT_FLOAT_EQ(curve[1].second, -0.173f);
  EXPECT_FLOAT_EQ(curve[2].first, 5.0f);
  EXPECT_FLOAT_EQ(curve[2].second, -0.816f);
}

// Out-of-order rows are sorted ascending by angle; malformed rows are skipped.
TEST(AngularResponseCurve, SortsAndSkipsMalformed)
{
  const std::string csv =
    "abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n"
    "5.0,-40.0,10,-0.8\n"
    "this,is,a,bad,row\n"      // 4th field "bad" -> stof throws -> skipped
    "1.0,-40.0,10,0.0\n";
  const std::string path = writeTemp(csv);

  auto curve = loadAngularResponseCurve(path);
  std::remove(path.c_str());

  ASSERT_EQ(curve.size(), 2u);
  EXPECT_FLOAT_EQ(curve[0].first, 1.0f);   // sorted ascending
  EXPECT_FLOAT_EQ(curve[1].first, 5.0f);
}

// A missing/empty path yields an empty curve (the correction degrades to no-op).
TEST(AngularResponseCurve, MissingFileIsEmpty)
{
  EXPECT_TRUE(loadAngularResponseCurve("").empty());
  EXPECT_TRUE(loadAngularResponseCurve("/no/such/curve_file_12345.csv").empty());
}

// Mode-string parsing is case-insensitive; unknown values are rejected.
TEST(AngularResponseCurve, ParsesMode)
{
  BackscatterAngleCorrection mode = BackscatterAngleCorrection::Empirical;
  ASSERT_TRUE(parseBackscatterAngleCorrection("none", mode));
  EXPECT_EQ(mode, BackscatterAngleCorrection::None);

  ASSERT_TRUE(parseBackscatterAngleCorrection("Empirical", mode));
  EXPECT_EQ(mode, BackscatterAngleCorrection::Empirical);

  ASSERT_TRUE(parseBackscatterAngleCorrection("EMPIRICAL", mode));
  EXPECT_EQ(mode, BackscatterAngleCorrection::Empirical);

  // Unknown value: rejected, out param left unchanged.
  mode = BackscatterAngleCorrection::Empirical;
  EXPECT_FALSE(parseBackscatterAngleCorrection("bogus", mode));
  EXPECT_EQ(mode, BackscatterAngleCorrection::Empirical);
}

}  // namespace cube
