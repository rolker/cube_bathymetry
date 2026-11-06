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
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cube_bathymetry/backscatter_table.h"

namespace cube
{

namespace
{

const double kNaN = std::numeric_limits<double>::quiet_NaN();

/// A float32 message value from a JSON number or one of the strings the
/// vector file uses for the non-finite cases.
float jsonFloat(const nlohmann::json & v)
{
  if (v.is_string()) {
    const std::string s = v.get<std::string>();
    if (s == "nan") {return std::numeric_limits<float>::quiet_NaN();}
    if (s == "inf") {return std::numeric_limits<float>::infinity();}
    if (s == "-inf") {return -std::numeric_limits<float>::infinity();}
    throw std::runtime_error("unexpected string in mode_keys.json: " + s);
  }
  return static_cast<float>(v.get<double>());
}

std::string tempPath(const std::string & tag)
{
  return (std::filesystem::temp_directory_path() /
         ("cube_bs_table_" + tag + "_" + std::to_string(::getpid()) + ".csv")).string();
}

std::string writeTemp(const std::string & tag, const std::string & content)
{
  const std::string path = tempPath(tag);
  std::ofstream out(path);
  out << content;
  return path;
}

const char * kHeader =
  "# format: angular-response-table v1\n"
  "# reference_mode: FMU_1000us_24000Hz_500kHz\n"
  "# range_ref_m: 10\n"
  "# mode_summary: FMU_1000us_24000Hz_500kHz pings=12 beams=345 declared_tvg=UNKNOWN\n"
  "mode,term,x,angle_lo_deg,angle_hi_deg,value_db,n,var_db2,db_relative_to_nadir,se_db,"
  "offset_db_vs_ref,offset_se_db,offset_anchored,declared_tvg_model,"
  "declared_tvg_absorption_db_per_km\n";

const char * kGoodRows =
  "FMU_1000us_24000Hz_500kHz,angle,1,,,-20,100,9,+0.000,0.3,0.000,0.000,true,UNKNOWN,nan\n"
  "FMU_1000us_24000Hz_500kHz,angle,11,,,-22,100,9,-2.000,0.3,0.000,0.000,true,UNKNOWN,nan\n"
  "FMU_1000us_24000Hz_500kHz,range,0,10,90,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n"
  "FMU_1000us_24000Hz_500kHz,range,1,10,90,-10,100,,,0,0.000,0.000,true,UNKNOWN,nan\n";

}  // namespace

TEST(KeyNumber, MatchesThePythonRule)
{
  EXPECT_EQ(keyNumber(0.001, 1e6), "1000");
  EXPECT_EQ(keyNumber(24000.0), "24000");
  EXPECT_EQ(keyNumber(2e-5, 1e6), "20");
  EXPECT_EQ(keyNumber(4.5e-7, 1e6), "0.45");
  EXPECT_EQ(keyNumber(500000.0, 1e-3), "500");
  EXPECT_EQ(keyNumber(1.0), "1");
  EXPECT_EQ(keyNumber(10.0), "10");
  EXPECT_EQ(keyNumber(0.0), "?");
  EXPECT_EQ(keyNumber(-5.0), "?");
  EXPECT_EQ(keyNumber(kNaN), "?");
  EXPECT_EQ(keyNumber(std::numeric_limits<double>::infinity()), "?");
  // Scaling can push a finite value out of range: tested on the scaled value too.
  EXPECT_EQ(keyNumber(1e308, 1e6), "?");
}

// The shared vectors: every case the Python spec is tested on, byte for byte.
TEST(ModeKey, MatchesTheSharedVectors)
{
  std::ifstream in(MODE_KEYS_FILE);
  ASSERT_TRUE(in) << MODE_KEYS_FILE;
  const auto doc = nlohmann::json::parse(in);
  const auto & cases = doc.at("cases");
  ASSERT_GE(cases.size(), 15u);
  for (const auto & c : cases) {
    std::vector<float> pulses, widths;
    std::vector<uint8_t> types;
    for (const auto & v : c.at("pulse_lengths_s")) {
      pulses.push_back(jsonFloat(v));
    }
    for (const auto & v : c.at("bandwidths_hz")) {
      widths.push_back(jsonFloat(v));
    }
    for (const auto & v : c.at("tx_signal_types")) {
      types.push_back(static_cast<uint8_t>(v.get<int>()));
    }
    const float frequency = jsonFloat(c.at("frequency_hz"));
    EXPECT_EQ(canonicalModeKey(pulses, widths, types, frequency), c.at("key").get<std::string>())
      << c.at("name").get<std::string>();
  }
}

TEST(DeclaredTvgState, EqualityTreatsNaNAbsorptionAsOneState)
{
  DeclaredTvgState a, b;
  EXPECT_EQ(a, b);  // UNKNOWN / NaN both
  b.model = DeclaredTvg::Log40R;
  EXPECT_NE(a, b);
  a.model = DeclaredTvg::Log40R;
  a.absorption_db_per_km = 80.0f;
  EXPECT_NE(a, b) << "declared absorption vs none";
  b.absorption_db_per_km = 80.0f;
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.toString(), "40LOGR/80 dB/km");
  EXPECT_EQ(DeclaredTvgState().toString(), "UNKNOWN");

  DeclaredTvg parsed;
  EXPECT_TRUE(parseDeclaredTvg("MIXED", parsed));
  EXPECT_EQ(parsed, DeclaredTvg::Mixed);
  EXPECT_FALSE(parseDeclaredTvg("40logr", parsed));
  EXPECT_EQ(declaredTvgFromMessageValue(3), DeclaredTvg::Log40R);
  EXPECT_EQ(declaredTvgFromMessageValue(9), DeclaredTvg::Unknown);

  BackscatterModeFallback fb;
  EXPECT_TRUE(parseBackscatterModeFallback("Exclude", fb));
  EXPECT_EQ(fb, BackscatterModeFallback::Exclude);
  EXPECT_TRUE(parseBackscatterModeFallback("identity", fb));
  EXPECT_EQ(fb, BackscatterModeFallback::Identity);
  EXPECT_FALSE(parseBackscatterModeFallback("nearest", fb));
}

TEST(BackscatterTable, DetectsTablesByTheFormatLineOnly)
{
  EXPECT_TRUE(isAngularResponseTable(MODE_TABLE_FIXTURE_FILE));
  EXPECT_FALSE(isAngularResponseTable(DERIVED_CURVE_GOLDEN_FILE));
  EXPECT_FALSE(isAngularResponseTable("/no/such/file_172.csv"));
  EXPECT_FALSE(isAngularResponseTable(""));
}

// cube_bathymetry#172 review: a near-miss first line must not fall through to
// the single-curve loader (an empty curve and only a WARN). A BOM or leading
// blank lines are tolerated; another version is a table, and refused.
TEST(BackscatterTable, NearMissFormatLinesAreTablesAndOnlyV1Loads)
{
  const std::string header(kHeader);
  const std::string body = header.substr(header.find('\n') + 1) + kGoodRows;
  struct Case
  {
    const char * tag;
    std::string first;
    bool loads;
  };
  const std::vector<Case> cases = {
    {"bom", "\xEF\xBB\xBF# format: angular-response-table v1\n", true},
    {"blank_lines", "\n  \n# format: angular-response-table v1\r\n", true},
    {"trailing_blank", "# format: angular-response-table v1  \n", true},
    {"v2", "# format: angular-response-table v2\n", false},
    {"no_version", "# format: angular-response-table\n", false},
  };
  for (const auto & c : cases) {
    const auto path = writeTemp(c.tag, c.first + body);
    EXPECT_TRUE(isAngularResponseTable(path)) << c.tag;
    std::string error;
    const auto table = loadBackscatterTable(path, error);
    EXPECT_EQ(table.entries.size(), c.loads ? 1u : 0u) << c.tag << ": " << error;
    if (!c.loads) {
      const bool named = error.find("unsupported table format") != std::string::npos;
      EXPECT_TRUE(named) << c.tag << ": " << error;
    }
    std::filesystem::remove(path);
  }
  const auto empty = writeTemp("empty", "\n\n");
  EXPECT_FALSE(isAngularResponseTable(empty));
  std::string error;
  EXPECT_TRUE(loadBackscatterTable(empty, error).empty());
  EXPECT_NE(error.find("empty file"), std::string::npos) << error;
  std::filesystem::remove(empty);
}

TEST(BackscatterTable, LoadsTheFixture)
{
  std::string error;
  const auto table = loadBackscatterTable(MODE_TABLE_FIXTURE_FILE, error);
  ASSERT_TRUE(error.empty()) << error;
  ASSERT_EQ(table.entries.size(), 3u);
  EXPECT_EQ(table.reference_mode, "FMU_1000us_24000Hz_500kHz");
  EXPECT_DOUBLE_EQ(table.range_ref_m, 18.9);
  EXPECT_DOUBLE_EQ(table.range_node_spacing_decades, 0.25);
  EXPECT_DOUBLE_EQ(table.angle_bin_width_deg, 2.0);
  EXPECT_DOUBLE_EQ(table.specular_exclusion_deg, 10.0);
  EXPECT_EQ(table.source_bags, "bag_a");

  // Entries in order of first appearance; correction_idx is 1-based.
  EXPECT_EQ(table.find("CW_50us_20000Hz_500kHz"), std::optional<uint16_t>(1));
  EXPECT_EQ(table.find("FMU_1000us_24000Hz_500kHz"), std::optional<uint16_t>(2));
  EXPECT_EQ(table.find("FMU_5000us_24000Hz_500kHz"), std::optional<uint16_t>(3));
  EXPECT_FALSE(table.find("CW_20us_50000Hz_500kHz").has_value());
  EXPECT_EQ(table.entry(kCorrectionUnresolved), nullptr);
  EXPECT_EQ(table.entry(kCorrectionIdentity), nullptr);
  EXPECT_EQ(table.entry(kCorrectionExclude), nullptr);
  EXPECT_EQ(table.entry(4), nullptr);

  const ModeEntry & cw = *table.entry(1);
  EXPECT_EQ(cw.key, "CW_50us_20000Hz_500kHz");
  ASSERT_EQ(cw.angle.size(), 2u);
  EXPECT_TRUE(cw.angle[0].weighable());
  EXPECT_DOUBLE_EQ(cw.angle[0].var_db2, 12.0);
  EXPECT_FALSE(cw.angle[1].weighable()) << "n = 1 wrote var_db2 empty";
  EXPECT_EQ(cw.angle[1].n, 1u);
  EXPECT_TRUE(std::isnan(cw.angle[1].se_db));
  EXPECT_EQ(cw.unweighable_bins, 1u);
  // Both groups were written zero (no range rows): every DECLARED group is
  // still present, with no nodes (a zero term).
  ASSERT_EQ(cw.groups.size(), 2u);
  EXPECT_TRUE(cw.groups[0].nodes.empty());
  EXPECT_TRUE(cw.groups[1].nodes.empty());
  EXPECT_DOUBLE_EQ(cw.groups[1].angle_lo_deg, 30.0);
  EXPECT_TRUE(cw.offset_anchored);
  EXPECT_DOUBLE_EQ(cw.offset_db_vs_ref, -17.25);
  EXPECT_DOUBLE_EQ(cw.offset_se_db, 0.31);
  EXPECT_EQ(cw.declared.model, DeclaredTvg::Log40R);
  EXPECT_FLOAT_EQ(cw.declared.absorption_db_per_km, 80.0f);
  EXPECT_EQ(cw.pings, 217255u);
  EXPECT_EQ(cw.beams, 55617280u);

  const ModeEntry & fm = *table.entry(2);
  ASSERT_EQ(fm.angle.size(), 3u);
  EXPECT_DOUBLE_EQ(fm.angle[2].db_relative_to_nadir, -7.2);
  ASSERT_EQ(fm.groups.size(), 2u);
  EXPECT_DOUBLE_EQ(fm.groups[0].angle_lo_deg, 10.0);
  EXPECT_DOUBLE_EQ(fm.groups[0].angle_hi_deg, 30.0);
  ASSERT_EQ(fm.groups[0].nodes.size(), 3u);
  EXPECT_DOUBLE_EQ(fm.groups[0].nodes[0].x, -0.25);
  EXPECT_DOUBLE_EQ(fm.groups[0].nodes[2].value_db, -2.25);
  EXPECT_DOUBLE_EQ(fm.groups[1].angle_lo_deg, 30.0);
  ASSERT_EQ(fm.groups[1].nodes.size(), 2u);
  EXPECT_EQ(fm.declared.model, DeclaredTvg::Unknown);
  EXPECT_TRUE(std::isnan(fm.declared.absorption_db_per_km));
  EXPECT_DOUBLE_EQ(fm.offset_db_vs_ref, 0.0);
  EXPECT_EQ(fm.pings, 93890u);

  const ModeEntry & rare = *table.entry(3);
  EXPECT_FALSE(rare.offset_anchored);
  EXPECT_TRUE(std::isnan(rare.offset_db_vs_ref));
  EXPECT_EQ(rare.pings, 169u);
  ASSERT_EQ(rare.groups.size(), 2u);
  EXPECT_EQ(rare.groups[0].nodes.size(), 2u);
  EXPECT_TRUE(rare.groups[1].nodes.empty()) << "30-90 was written zero";
}

TEST(BackscatterTable, LookupFollowsTheTableContract)
{
  std::string error;
  const auto table = loadBackscatterTable(MODE_TABLE_FIXTURE_FILE, error);
  ASSERT_TRUE(error.empty()) << error;
  const double r0 = table.range_ref_m;
  const auto id = BackscatterModeFallback::Identity;
  const auto ex = BackscatterModeFallback::Exclude;

  // FMU 1 ms at 11 deg, one quarter decade beyond R0: rel -0.8, r(10-30) -2.25.
  EXPECT_NEAR(correctWithTable(-20.0, 11.0, r0 * std::pow(10.0, 0.25), table, 2, id),
    -20.0 + 0.8 + 2.25, 1e-9);
  // At R0 the range term is 0 by construction.
  EXPECT_NEAR(correctWithTable(-20.0, 11.0, r0, table, 2, id), -19.2, 1e-9);
  // The specular band uses the FIRST group; the angle curve interpolates
  // between the 1 and 11 deg centres.
  EXPECT_NEAR(correctWithTable(-20.0, 5.0, r0 * std::pow(10.0, -0.25), table, 2, id),
    -20.0 + 0.32 - 1.5, 1e-9);
  // 60 deg: second group; angle beyond the last centre clamps to -7.2.
  EXPECT_NEAR(correctWithTable(-20.0, 60.0, r0 * std::pow(10.0, 0.25), table, 2, id),
    -20.0 + 7.2 + 1.0, 1e-9);
  // Beyond the end nodes the range term clamps.
  EXPECT_NEAR(correctWithTable(-20.0, 60.0, r0 * 100.0, table, 2, id), -20.0 + 7.2 + 1.0, 1e-9);
  // 20 deg sits 0.3 of the way from the 11 deg centre to the 41 deg one.
  EXPECT_NEAR(correctWithTable(-20.0, 20.0, r0 * 100.0, table, 2, id),
    -20.0 + (0.8 + 0.3 * 6.4) + 2.25, 1e-9);
  // Between two nodes: linear.
  EXPECT_NEAR(correctWithTable(-20.0, 11.0, r0 * std::pow(10.0, 0.125), table, 2, id),
    -20.0 + 0.8 + 1.125, 1e-9);
  // Below the first centre the curve clamps to the nadir bin (0).
  EXPECT_NEAR(correctWithTable(-20.0, 0.2, r0, table, 2, id), -20.0, 1e-9);

  // CW 50 us: the offset moves it onto the reference mode's level; no range rows.
  EXPECT_NEAR(correctWithTable(-30.0, 1.0, 3.0, table, 1, id), -30.0 + 17.25, 1e-9);
  // Its 41 deg bin has no variance: uncorrectable -> identity or NaN.
  EXPECT_DOUBLE_EQ(correctWithTable(-30.0, 41.0, 3.0, table, 1, id), -30.0);
  EXPECT_TRUE(std::isnan(correctWithTable(-30.0, 41.0, 3.0, table, 1, ex)));
  // Nearest-centre bin: 20 deg is closer to the 1 deg bin than to 41 (weighable).
  EXPECT_FALSE(std::isnan(correctWithTable(-30.0, 20.0, 3.0, table, 1, ex)));
  EXPECT_TRUE(std::isnan(correctWithTable(-30.0, 22.0, 3.0, table, 1, ex)));

  // Unanchored mode, unknown entry, reserved index, NaN angle, bad range.
  EXPECT_DOUBLE_EQ(correctWithTable(-30.0, 1.0, 3.0, table, 3, id), -30.0);
  EXPECT_TRUE(std::isnan(correctWithTable(-30.0, 1.0, 3.0, table, 3, ex)));
  EXPECT_DOUBLE_EQ(correctWithTable(-30.0, 1.0, 3.0, table, 7, id), -30.0);
  EXPECT_DOUBLE_EQ(correctWithTable(-30.0, 1.0, 3.0, table, kCorrectionUnresolved, id), -30.0);
  EXPECT_DOUBLE_EQ(correctWithTable(-30.0, kNaN, 3.0, table, 2, id), -30.0);
  EXPECT_TRUE(std::isnan(correctWithTable(-30.0, 11.0, 0.0, table, 2, ex)));
  EXPECT_TRUE(std::isnan(correctWithTable(-30.0, 11.0, kNaN, table, 2, ex)));

  // At or beyond 90 deg the writer has no bin and no group (it drops such
  // beams): uncorrectable, never a clamped angle term with a zero range term.
  EXPECT_DOUBLE_EQ(correctWithTable(-30.0, 90.0, r0, table, 2, id), -30.0);
  EXPECT_TRUE(std::isnan(correctWithTable(-30.0, 95.0, r0, table, 2, ex)));
  EXPECT_FALSE(std::isnan(correctWithTable(-30.0, 89.9, r0, table, 2, ex)));

  // The status each of those cases reports (what the per-beam counts use).
  EXPECT_EQ(tableBeamStatus(11.0, r0, table, 2), TableBeamStatus::Corrected);
  EXPECT_EQ(tableBeamStatus(11.0, r0, table, 7), TableBeamStatus::NoEntry);
  EXPECT_EQ(tableBeamStatus(1.0, 3.0, table, 3), TableBeamStatus::Unanchored);
  EXPECT_EQ(tableBeamStatus(kNaN, r0, table, 2), TableBeamStatus::NoFiniteAngle);
  EXPECT_EQ(tableBeamStatus(90.0, r0, table, 2), TableBeamStatus::AngleAtOrBeyond90);
  EXPECT_EQ(tableBeamStatus(11.0, 0.0, table, 2), TableBeamStatus::NoFiniteRange);
  EXPECT_EQ(tableBeamStatus(41.0, 3.0, table, 1), TableBeamStatus::UnweighableBin);
  EXPECT_TRUE(std::isnan(beamAbsAngleDeg(std::numeric_limits<float>::quiet_NaN())));
  EXPECT_NEAR(beamAbsAngleDeg(-static_cast<float>(M_PI) / 4.0f), 45.0, 1e-5);
}

TEST(BackscatterTable, AMinimalTableLoads)
{
  const auto path = writeTemp("good", std::string(kHeader) + kGoodRows);
  std::string error;
  const auto table = loadBackscatterTable(path, error);
  ASSERT_TRUE(error.empty()) << error;
  ASSERT_EQ(table.entries.size(), 1u);
  EXPECT_EQ(table.entries[0].pings, 12u);
  ASSERT_EQ(table.entries[0].groups.size(), 1u);
  EXPECT_NEAR(table.entries[0].groups[0].at(0.5), -5.0, 1e-12);
  EXPECT_NEAR(correctWithTable(-20.0, 11.0, 100.0, table, 1, BackscatterModeFallback::Exclude),
    -20.0 + 2.0 + 10.0, 1e-9);
  std::filesystem::remove(path);
}

// A table is read whole or not at all: every malformation names its line and
// yields an EMPTY table, never a partial one.
TEST(BackscatterTable, RejectsMalformedTablesWhole)
{
  struct Case
  {
    const char * tag;
    std::string content;
    const char * expect;
  };
  const std::string header(kHeader);
  const std::string good(kGoodRows);
  const std::vector<Case> cases = {
    {"no_format", "# not a table\n" + header.substr(header.find('\n') + 1) + good,
      "not an angular-response table"},
    {"field_count", header + "FMU_1000us_24000Hz_500kHz,angle,1,,,-20\n", "expected 15 fields"},
    {"scalars_disagree", header + good +
      "FMU_1000us_24000Hz_500kHz,angle,21,,,-25,100,9,-5.000,0.3,1.000,0.000,true,UNKNOWN,nan\n",
      "disagree"},
    {"no_rel", header +
      "FMU_1000us_24000Hz_500kHz,angle,1,,,-20,100,9,,0.3,0.000,0.000,true,UNKNOWN,nan\n",
      "without db_relative_to_nadir"},
    {"bad_term", header +
      "FMU_1000us_24000Hz_500kHz,slope,1,,,-20,100,9,0,0.3,0.000,0.000,true,UNKNOWN,nan\n",
      "unknown term"},
    {"range_no_bounds",
      header + good.substr(0, good.find("\nFMU_1000us_24000Hz_500kHz,range") + 1) +
      "FMU_1000us_24000Hz_500kHz,range,0,,,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n",
      "angle_lo_deg < angle_hi_deg"},
    {"range_only", header +
      "FMU_1000us_24000Hz_500kHz,range,0,10,90,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n",
      "no angle rows"},
    {"anchored_no_offset", header +
      "FMU_1000us_24000Hz_500kHz,angle,1,,,-20,100,9,0,0.3,,,true,UNKNOWN,nan\n",
      "no finite offset_db_vs_ref"},
    {"bad_tvg", header +
      "FMU_1000us_24000Hz_500kHz,angle,1,,,-20,100,9,0,0.3,0,0,true,SOMETHING,nan\n",
      "malformed declared_tvg_model"},
    {"numeric_key", header + "1000,angle,1,,,-20,100,9,0,0.3,0,0,true,UNKNOWN,nan\n",
      "must start with a letter"},
    {"dup_bin", header + good +
      "FMU_1000us_24000Hz_500kHz,angle,1,,,-20,100,9,0,0.3,0.000,0.000,true,UNKNOWN,nan\n",
      "duplicate angle bin"},
    {"dup_node", header + good +
      "FMU_1000us_24000Hz_500kHz,range,0,10,90,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n",
      "duplicate range node"},
    {"no_rows", header, "no table rows"},
    {"no_reference", "# format: angular-response-table v1\n# range_ref_m: 10\n" +
      header.substr(header.find("mode,term")) + good, "reference_mode"},
    {"no_r0", "# format: angular-response-table v1\n# reference_mode: X\n" +
      header.substr(header.find("mode,term")) + good, "range_ref_m"},
    {"bad_columns", "# format: angular-response-table v1\n# reference_mode: X\n"
      "# range_ref_m: 10\nmode,term,x\n" + good, "unexpected column line"},
    {"bad_x",
      header + "FMU_1000us_24000Hz_500kHz,angle,abc,,,-20,100,9,0,0.3,0,0,true,UNKNOWN,nan\n",
      "malformed x"},
    {"negative_bin",
      header + "FMU_1000us_24000Hz_500kHz,angle,-1,,,-20,100,9,0,0.3,0,0,true,UNKNOWN,nan\n",
      "outside [0, 90) deg"},
    {"bin_at_90",
      header + "FMU_1000us_24000Hz_500kHz,angle,90,,,-20,100,9,0,0.3,0,0,true,UNKNOWN,nan\n",
      "outside [0, 90) deg"},
    {"group_below_0",
      header + good.substr(0, good.find("\nFMU_1000us_24000Hz_500kHz,range") + 1) +
      "FMU_1000us_24000Hz_500kHz,range,0,-5,30,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n",
      "outside [0, 90] deg"},
    {"group_above_90",
      header + good.substr(0, good.find("\nFMU_1000us_24000Hz_500kHz,range") + 1) +
      "FMU_1000us_24000Hz_500kHz,range,0,10,95,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n",
      "outside [0, 90] deg"},
    {"dup_summary",
      header.substr(0, header.find("mode,term")) +
      "# mode_summary: FMU_1000us_24000Hz_500kHz pings=99 beams=1\n" +
      header.substr(header.find("mode,term")) + good,
      "two '# mode_summary:' lines"},
    {"summary_no_pings",
      "# format: angular-response-table v1\n# reference_mode: X\n# range_ref_m: 10\n"
      "# mode_summary: FMU_1000us_24000Hz_500kHz beams=345\n" +
      header.substr(header.find("mode,term")) + good,
      "has no pings="},
  };
  for (const auto & c : cases) {
    const auto path = writeTemp(c.tag, c.content);
    std::string error;
    const auto table = loadBackscatterTable(path, error);
    EXPECT_TRUE(table.empty()) << c.tag;
    EXPECT_NE(error.find(c.expect), std::string::npos) << c.tag << ": " << error;
    EXPECT_NE(error.find(path), std::string::npos) << c.tag << " names the file";
    std::filesystem::remove(path);
  }
  std::string error;
  EXPECT_TRUE(loadBackscatterTable("/no/such/table_172.csv", error).empty());
  EXPECT_NE(error.find("cannot open"), std::string::npos);
}

// cube_bathymetry#172 review: the writer omits the rows of a group it did not
// fit. When that is the FIRST group, the specular band and the first group's
// own beams must get its zero term -- not the next group's range term, which is
// what a lookup over the written groups alone gives them.
TEST(BackscatterTable, AnUnfittedFirstGroupIsAZeroTermNotTheNextGroup)
{
  const std::string rows =
    "FMU_1000us_24000Hz_500kHz,angle,1,,,-20,100,9,+0.000,0.3,0.000,0.000,true,UNKNOWN,nan\n"
    "FMU_1000us_24000Hz_500kHz,range,0,30,90,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n"
    "FMU_1000us_24000Hz_500kHz,range,1,30,90,-10,100,,,0,0.000,0.000,true,UNKNOWN,nan\n";
  const std::string header(kHeader);
  const std::string declared = header.substr(0, header.find("mode,term")) +
    "# range_angle_groups_deg: 10-30,30-90\n" + header.substr(header.find("mode,term"));
  const auto path = writeTemp("unfitted_first", declared + rows);
  std::string error;
  const auto table = loadBackscatterTable(path, error);
  ASSERT_TRUE(error.empty()) << error;
  const ModeEntry & e = table.entries.at(0);
  ASSERT_EQ(e.groups.size(), 2u) << "both declared groups exist";
  EXPECT_DOUBLE_EQ(e.groups[0].angle_lo_deg, 10.0);
  EXPECT_DOUBLE_EQ(e.groups[0].angle_hi_deg, 30.0);
  EXPECT_TRUE(e.groups[0].nodes.empty());
  ASSERT_EQ(e.groups[1].nodes.size(), 2u);
  const auto id = BackscatterModeFallback::Identity;
  const double far = 100.0;  // log10(R / R0) = 1: the 30-90 group reads -10 there
  EXPECT_EQ(e.groupFor(5.0), &e.groups[0]) << "the specular band takes the first DECLARED group";
  EXPECT_EQ(e.groupFor(20.0), &e.groups[0]);
  EXPECT_EQ(e.groupFor(60.0), &e.groups[1]);
  EXPECT_NEAR(correctWithTable(-20.0, 5.0, far, table, 1, id), -20.0, 1e-9)
    << "specular band: zero range term";
  EXPECT_NEAR(correctWithTable(-20.0, 20.0, far, table, 1, id), -20.0, 1e-9)
    << "inside the unfitted first group: zero range term";
  EXPECT_NEAR(correctWithTable(-20.0, 60.0, far, table, 1, id), -20.0 + 10.0, 1e-9);
  std::filesystem::remove(path);

  // Without the header line (a hand-made table) the rows are all there is:
  // the first WRITTEN group serves the specular band, as documented.
  const auto bare = writeTemp("unfitted_first_bare", header + rows);
  const auto legacy = loadBackscatterTable(bare, error);
  ASSERT_TRUE(error.empty()) << error;
  ASSERT_EQ(legacy.entries.at(0).groups.size(), 1u);
  EXPECT_NEAR(correctWithTable(-20.0, 5.0, far, legacy, 1, id), -20.0 + 10.0, 1e-9);
  std::filesystem::remove(bare);
}

TEST(BackscatterTable, DeclaredGroupsMustCoverEveryRangeRow)
{
  const std::string header(kHeader);
  const std::string head = header.substr(0, header.find("mode,term"));
  const std::string columns = header.substr(header.find("mode,term"));
  struct Case
  {
    const char * tag;
    const char * groups;
    const char * expect;
  };
  const std::vector<Case> cases = {
    {"undeclared", "10-30,30-50", "does not declare"},
    {"garbled", "10to30", "is not <lo>-<hi>"},
    {"reversed", "30-10", "0 <= lo < hi <= 90"},
    {"beyond", "10-95", "0 <= lo < hi <= 90"},
    {"overlap", "10-50,30-90", "overlaps"},
  };
  for (const auto & c : cases) {
    const auto path = writeTemp(
      c.tag, head + "# range_angle_groups_deg: " + c.groups + "\n" + columns + kGoodRows);
    std::string error;
    EXPECT_TRUE(loadBackscatterTable(path, error).empty()) << c.tag;
    EXPECT_NE(error.find(c.expect), std::string::npos) << c.tag << ": " << error;
    std::filesystem::remove(path);
  }
  // kGoodRows' one group is 10-90: declaring exactly it loads.
  const auto path = writeTemp(
    "declared_ok", head + "# range_angle_groups_deg: 10-90\n" + columns + kGoodRows);
  std::string error;
  EXPECT_EQ(loadBackscatterTable(path, error).entries.size(), 1u) << error;
  std::filesystem::remove(path);
}

TEST(BackscatterTable, OverlappingGroupsAreRejected)
{
  const auto path = writeTemp("overlap", std::string(kHeader) +
      "FMU_1000us_24000Hz_500kHz,angle,1,,,-20,100,9,0,0.3,0.000,0.000,true,UNKNOWN,nan\n"
      "FMU_1000us_24000Hz_500kHz,range,0,10,50,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n"
      "FMU_1000us_24000Hz_500kHz,range,0,30,90,0,100,,,0,0.000,0.000,true,UNKNOWN,nan\n");
  std::string error;
  EXPECT_TRUE(loadBackscatterTable(path, error).empty());
  EXPECT_NE(error.find("overlapping"), std::string::npos) << error;
  std::filesystem::remove(path);
}

}  // namespace cube
