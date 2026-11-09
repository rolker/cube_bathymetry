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
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "cube_bathymetry/angular_response_curve.h"
#include "cube_bathymetry/backscatter_table.h"

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

// Tier-2 (#87): a curve with the self-describing TL header parses tl_removed +
// absorption_db_per_m; the data columns are unchanged.
TEST(AngularResponseCurve, ParsesTier2Header)
{
  const std::string csv =
    "# Empirical angular response (tier-2)\n"
    "# tl_removed: true\n"
    "# absorption_db_per_m: 0.04897\n"
    "# water_temp_c: 24\n"
    "# tl_model: 40*log10(R) + 2*alpha*R   (R = twtt*c/2, metres)\n"
    "abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n"
    "1.0,-40.0,100,+0.000\n"
    "3.0,-41.0,100,-1.000\n";
  const std::string path = writeTemp(csv);

  auto loaded = loadAngularResponseCurveWithHeader(path);
  std::remove(path.c_str());

  EXPECT_TRUE(loaded.tl_removed);
  EXPECT_FLOAT_EQ(loaded.absorption_db_per_m, 0.04897f);
  ASSERT_EQ(loaded.points.size(), 2u);
  EXPECT_FLOAT_EQ(loaded.points[0].first, 1.0f);
  EXPECT_FLOAT_EQ(loaded.points[1].second, -1.0f);
}

// Absence of the TL header -> tier-1 defaults (tl_removed=false, absorption=0),
// so a tier-1 curve keeps loading unchanged.
TEST(AngularResponseCurve, AbsentHeaderDefaultsToTier1)
{
  const std::string csv =
    "# M3 empirical angular response\n"
    "abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n"
    "1.0,-40.0,100,+0.000\n";
  const std::string path = writeTemp(csv);

  auto loaded = loadAngularResponseCurveWithHeader(path);
  std::remove(path.c_str());

  EXPECT_FALSE(loaded.tl_removed);
  EXPECT_FLOAT_EQ(loaded.absorption_db_per_m, 0.0f);
  ASSERT_EQ(loaded.points.size(), 1u);
}

// An explicit "# tl_removed: false" header parses as tier-1 (the derive tool
// writes this for tier-1 runs).
TEST(AngularResponseCurve, ExplicitTlRemovedFalse)
{
  const std::string csv =
    "# tl_removed: false\n"
    "abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n"
    "1.0,-40.0,100,+0.000\n";
  const std::string path = writeTemp(csv);

  auto loaded = loadAngularResponseCurveWithHeader(path);
  std::remove(path.c_str());

  EXPECT_FALSE(loaded.tl_removed);
  EXPECT_FLOAT_EQ(loaded.absorption_db_per_m, 0.0f);
}

// The backward-compatible loadAngularResponseCurve() still returns just the
// points (drops the TL provenance) -- existing call sites are unaffected.
TEST(AngularResponseCurve, BackCompatLoaderReturnsPoints)
{
  const std::string csv =
    "# tl_removed: true\n"
    "# absorption_db_per_m: 0.05\n"
    "abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n"
    "1.0,-40.0,100,+0.000\n"
    "3.0,-41.0,100,-1.000\n";
  const std::string path = writeTemp(csv);

  auto curve = loadAngularResponseCurve(path);
  std::remove(path.c_str());

  ASSERT_EQ(curve.size(), 2u);
  EXPECT_FLOAT_EQ(curve[1].second, -1.0f);
}

// Round trip from derive_angular_response.py (cube_bathymetry#170): the golden
// curve test/data/derived_curve_tier2_seawater.csv is written by the script's
// write_csv (and checked to still match it by test_derive_angular_response.py).
// It carries the full provenance header -- absorption_source, salinity_psu, ph,
// depth_m, frequency_khz, box, source_bags -- which the loader must ignore while
// still reading tl_removed, the seawater absorption_db_per_m and the data rows.
TEST(AngularResponseCurve, LoadsDeriveToolSeawaterCurve)
{
  auto loaded = loadAngularResponseCurveWithHeader(DERIVED_CURVE_GOLDEN_FILE);

  EXPECT_TRUE(loaded.tl_removed);
  EXPECT_FLOAT_EQ(loaded.absorption_db_per_m, 0.125363f);
  ASSERT_EQ(loaded.points.size(), 4u);
  EXPECT_FLOAT_EQ(loaded.points[0].first, 1.0f);
  EXPECT_FLOAT_EQ(loaded.points[0].second, 0.0f);
  EXPECT_FLOAT_EQ(loaded.points[1].first, 3.0f);
  EXPECT_FLOAT_EQ(loaded.points[1].second, -2.046f);
  EXPECT_FLOAT_EQ(loaded.points[2].first, 5.0f);
  EXPECT_FLOAT_EQ(loaded.points[2].second, -0.294f);
  EXPECT_FLOAT_EQ(loaded.points[3].first, 15.0f);
  EXPECT_FLOAT_EQ(loaded.points[3].second, 9.472f);
}

// A per-mode angular-response-table v1 (cube_bathymetry#172) handed to this
// single-curve loader -- an old binary, or a new one before PR 2 adds the table
// loader -- must yield an EMPTY curve (which every caller already reports
// loudly), never rows parsed out of it: the mode key in column 0 starts with a
// letter, so std::stof rejects every row, and the table carries no tl_removed
// or absorption_db_per_m header. The fixture is written by
// test_derive_angular_response.py (mode_table_lines).
// PR #175 review: import_bag fingerprints the curve it applied, so the loader
// hands back the bytes it parsed, and parsing those bytes again gives the same
// curve -- one read, one hash, one parse.
TEST(AngularResponseCurve, LoaderReturnsTheBytesItParsed)
{
  std::string bytes;
  const auto loaded = loadAngularResponseCurveWithHeader(DERIVED_CURVE_GOLDEN_FILE, &bytes);
  ASSERT_FALSE(loaded.points.empty());
  std::ifstream in(DERIVED_CURVE_GOLDEN_FILE, std::ios::binary);
  std::ostringstream expected;
  expected << in.rdbuf();
  EXPECT_EQ(bytes, expected.str()) << "the bytes are the file's, exactly";

  const auto reparsed = parseAngularResponseCurve(bytes);
  EXPECT_EQ(reparsed.points, loaded.points);
  EXPECT_EQ(reparsed.tl_removed, loaded.tl_removed);
  EXPECT_FLOAT_EQ(reparsed.absorption_db_per_m, loaded.absorption_db_per_m);

  // The one-argument loader is the two-argument one without the bytes.
  EXPECT_EQ(loadAngularResponseCurveWithHeader(DERIVED_CURVE_GOLDEN_FILE).points, loaded.points);

  // A missing file leaves the bytes empty rather than stale.
  bytes = "stale";
  EXPECT_TRUE(loadAngularResponseCurveWithHeader("/nonexistent/curve.csv", &bytes).points.empty());
  EXPECT_TRUE(bytes.empty());

  // The size cap holds while reading (PR #175 review): a file one byte over a
  // small cap loads as nothing, one exactly at the cap loads; a directory is
  // not a curve.
  const std::string content = expected.str();
  EXPECT_TRUE(
    loadAngularResponseCurveWithHeader(DERIVED_CURVE_GOLDEN_FILE, &bytes, content.size() - 1)
    .points.empty());
  EXPECT_TRUE(bytes.empty());
  EXPECT_EQ(
    loadAngularResponseCurveWithHeader(DERIVED_CURVE_GOLDEN_FILE, &bytes, content.size()).points,
    loaded.points);
  EXPECT_EQ(bytes, content);
  EXPECT_TRUE(loadAngularResponseCurveWithHeader("/tmp", &bytes).points.empty());
  EXPECT_TRUE(bytes.empty());
}

TEST(AngularResponseCurve, ModeTableLoadsAsAnEmptyCurve)
{
  auto loaded = loadAngularResponseCurveWithHeader(MODE_TABLE_FIXTURE_FILE);

  EXPECT_TRUE(loaded.points.empty());
  EXPECT_FALSE(loaded.tl_removed);
  EXPECT_FLOAT_EQ(loaded.absorption_db_per_m, 0.0f);
  EXPECT_TRUE(loadAngularResponseCurve(MODE_TABLE_FIXTURE_FILE).empty());
}

// Provenance lines are matched by their exact key only: a value that happens
// to contain another key's text (a bag named like a header key, extra colons)
// does not set tl_removed or absorption_db_per_m, and an override-style header
// with no salinity/pH/temperature lines still loads its alpha (cube#170).
TEST(AngularResponseCurve, IgnoresProvenanceLines)
{
  const std::string csv =
    "# Empirical angular response (derive_angular_response.py, cube#81)\n"
    "# tl_removed: true\n"
    "# absorption_db_per_m: 0.13\n"
    "# absorption_source: override\n"
    "# tl_model: 40*log10(R) + 2*alpha*R   (R = twtt*c/2, metres)\n"
    "# box: 42.95 -70.65 43 -70.58\n"
    "# source_bags: absorption_db_per_m: 9.9,tl_removed: false,run_2026-08-26T12:00\n"
    "abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n"
    "1.0,-40.0,100,+0.000\n"
    "3.0,-41.0,100,-1.000\n";
  const std::string path = writeTemp(csv);

  auto loaded = loadAngularResponseCurveWithHeader(path);
  std::remove(path.c_str());

  EXPECT_TRUE(loaded.tl_removed);
  EXPECT_FLOAT_EQ(loaded.absorption_db_per_m, 0.13f);
  ASSERT_EQ(loaded.points.size(), 2u);
  EXPECT_FLOAT_EQ(loaded.points[1].second, -1.0f);
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

// cube_bathymetry#172: the per-beam correction index selects the path. 0 is
// the legacy curve (unchanged values); the reserved values short-circuit; a
// table entry applies `raw - rel - r_g - offset` and ignores the legacy curve.
TEST(AngularResponseCurve, CorrectionIndexSelectsThePath)
{
  Parameters params{CellSizes(1.0f), "order1a"};
  params.backscatter_angle_correction = BackscatterAngleCorrection::Empirical;
  params.angular_response_curve = {{1.0f, 0.0f}, {41.0f, -8.0f}};  // legacy: -4 dB at 21 deg
  std::string error;
  params.backscatter_table = loadBackscatterTable(MODE_TABLE_FIXTURE_FILE, error);
  ASSERT_TRUE(error.empty()) << error;
  const float angle_11 = static_cast<float>(11.0 * M_PI / 180.0);
  const float angle_21 = static_cast<float>(21.0 * M_PI / 180.0);
  const float r0 = static_cast<float>(params.backscatter_table.range_ref_m);

  // Unresolved -> the legacy curve, as before #172 (the default argument).
  EXPECT_NEAR(correctBeamIntensity(-20.0f, angle_21, r0, params), -16.0, 1e-5);
  EXPECT_NEAR(correctBeamIntensity(-20.0f, angle_21, r0, params, kCorrectionUnresolved),
    -16.0, 1e-5);
  // Reserved values.
  EXPECT_FLOAT_EQ(correctBeamIntensity(-20.0f, angle_21, r0, params, kCorrectionIdentity), -20.0f);
  EXPECT_TRUE(std::isnan(correctBeamIntensity(-20.0f, angle_21, r0, params, kCorrectionExclude)));
  // Entry 2 (FMU 1 ms) at 11 deg, a quarter decade beyond R0: +0.8 +2.25.
  EXPECT_NEAR(
    correctBeamIntensity(-20.0f, angle_11, r0 * std::pow(10.0f, 0.25f), params, 2),
    -20.0 + 0.8 + 2.25, 1e-4);
  // A table-corrected beam never sees the legacy curve or the TL term.
  params.backscatter_tl_removed = true;
  params.backscatter_absorption_db_per_m = 0.1f;
  EXPECT_NEAR(correctBeamIntensity(-20.0f, angle_11, r0, params, 2), -19.2, 1e-4);
  // Uncorrectable under each fallback: entry 3 is unanchored.
  EXPECT_FLOAT_EQ(correctBeamIntensity(-20.0f, angle_11, r0, params, 3), -20.0f);
  params.backscatter_mode_fallback = BackscatterModeFallback::Exclude;
  EXPECT_TRUE(std::isnan(correctBeamIntensity(-20.0f, angle_11, r0, params, 3)));
  // Mode None does not switch a resolved table beam off: the table is its own
  // enable (the caller gates None by never resolving modes).
  params.backscatter_mode_fallback = BackscatterModeFallback::Identity;
  params.backscatter_angle_correction = BackscatterAngleCorrection::None;
  EXPECT_NEAR(correctBeamIntensity(-20.0f, angle_11, r0, params, 2), -19.2, 1e-4);
  EXPECT_FLOAT_EQ(correctBeamIntensity(-20.0f, angle_21, r0, params, 0), -20.0f);
}

}  // namespace cube
