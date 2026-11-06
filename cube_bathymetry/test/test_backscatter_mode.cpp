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
#include <string>
#include <vector>

#include "cube_bathymetry/backscatter_mode.h"

namespace cube
{

namespace
{

SonarInfoState state(
  int64_t stamp_ns, float pulse_s, float bandwidth_hz, uint8_t type,
  DeclaredTvg tvg = DeclaredTvg::Unknown,
  float alpha = std::numeric_limits<float>::quiet_NaN())
{
  SonarInfoState s;
  s.stamp_ns = stamp_ns;
  s.pulse_lengths_s = {pulse_s};
  s.bandwidths_hz = {bandwidth_hz};
  s.tx_signal_types = {type};
  s.declared.model = tvg;
  s.declared.absorption_db_per_km = alpha;
  return s;
}

const float kF = 500000.0f;

struct Fixture
{
  BackscatterTable table;
  Fixture()
  {
    std::string error;
    table = loadBackscatterTable(MODE_TABLE_FIXTURE_FILE, error);
    if (!error.empty()) {
      throw std::runtime_error(error);
    }
  }
};

}  // namespace

TEST(BackscatterModeResolver, TimeOrderedAssociationAndReasons)
{
  Fixture f;
  BackscatterModeResolver r(&f.table);
  std::vector<std::string> warnings;
  r.setMismatchWarning([&](const std::string & w) {warnings.push_back(w);});

  EXPECT_TRUE(r.addSonarInfo(state(100, 0.001f, 24000.0f, 2)));           // FMU 1 ms
  EXPECT_TRUE(r.addSonarInfo(state(200, 5e-5f, 20000.0f, 1)));            // CW 50 us, UNKNOWN
  EXPECT_TRUE(r.addSonarInfo(state(300, 0.005f, 24000.0f, 2)));           // FMU 5 ms (unanchored)
  EXPECT_TRUE(r.addSonarInfo(state(400, 2e-5f, 50000.0f, 1)));            // CW 20 us (no entry)

  ModeResolution why;
  EXPECT_EQ(r.resolve(50, kF, why), kCorrectionUnresolved);
  EXPECT_EQ(why, ModeResolution::NoSonarInfo);
  EXPECT_EQ(r.resolve(100, kF, why), 2);   // at the stamp: that SonarInfo applies
  EXPECT_EQ(why, ModeResolution::Resolved);
  EXPECT_EQ(r.resolve(150, kF, why), 2);
  EXPECT_EQ(r.resolve(200, kF, why), 1);
  EXPECT_EQ(r.resolve(250, kF, why), 1);
  EXPECT_EQ(r.resolve(350, kF, why), kCorrectionUnresolved);
  EXPECT_EQ(why, ModeResolution::Unanchored);
  EXPECT_EQ(r.resolve(450, kF, why), kCorrectionUnresolved);
  EXPECT_EQ(why, ModeResolution::NoEntry);
  EXPECT_EQ(r.stateAt(99), nullptr);
  ASSERT_NE(r.stateAt(450), nullptr);
  EXPECT_EQ(r.stateAt(450)->stamp_ns, 400);

  const auto & c = r.counts();
  EXPECT_EQ(c.pings, 7u);
  EXPECT_EQ(c.resolvedTotal(), 4u);
  EXPECT_EQ(c.unresolved(), 3u);
  EXPECT_EQ(c.resolved_by_mode.at("FMU_1000us_24000Hz_500kHz"), 2u);
  EXPECT_EQ(c.resolved_by_mode.at("CW_50us_20000Hz_500kHz"), 2u);
  EXPECT_EQ(c.no_sonar_info, 1u);
  EXPECT_EQ(c.unanchored_by_mode.at("FMU_5000us_24000Hz_500kHz"), 1u);
  EXPECT_EQ(c.no_entry_by_mode.at("CW_20us_50000Hz_500kHz"), 1u);
  EXPECT_EQ(c.sonar_info_seen, 4u);
  EXPECT_EQ(c.stale, 0u);

  // The CW 50 us entry recorded 40LOGR/80 dB/km; the stream declared UNKNOWN:
  // counted per ping, warned once.
  ASSERT_EQ(warnings.size(), 1u);
  EXPECT_NE(warnings[0].find("CW_50us_20000Hz_500kHz"), std::string::npos);
  EXPECT_NE(warnings[0].find("40LOGR/80 dB/km"), std::string::npos);
  EXPECT_EQ(c.declared_mismatch.at({"CW_50us_20000Hz_500kHz", "UNKNOWN"}), 2u);

  const std::string report = r.report();
  EXPECT_NE(report.find("4 of 7 pings resolved"), std::string::npos) << report;
  EXPECT_NE(report.find("FMU_1000us_24000Hz_500kHz: 2 pings"), std::string::npos) << report;
  EXPECT_NE(report.find("before the first SonarInfo"), std::string::npos) << report;
  EXPECT_NE(report.find("no table entry"), std::string::npos) << report;
  EXPECT_NE(report.find("unanchored"), std::string::npos) << report;
  EXPECT_NE(report.find("differs from the table"), std::string::npos) << report;
}

TEST(BackscatterModeResolver, ExcludeFallbackAndPingFloor)
{
  Fixture f;
  BackscatterModeResolver r(&f.table, BackscatterModeFallback::Exclude, 200);
  r.addSonarInfo(state(100, 0.005f, 24000.0f, 2));   // FMU 5 ms: 169 pings, unanchored
  r.addSonarInfo(state(200, 5e-5f, 20000.0f, 1, DeclaredTvg::Log40R, 80.0f));  // matches
  ModeResolution why;
  EXPECT_EQ(r.resolve(50, kF, why), kCorrectionExclude);
  EXPECT_EQ(why, ModeResolution::NoSonarInfo);
  EXPECT_EQ(r.resolve(150, kF, why), kCorrectionExclude);
  EXPECT_EQ(why, ModeResolution::Unanchored) << "unanchored is checked before the floor";
  EXPECT_EQ(r.resolve(250, kF, why), 1);
  EXPECT_TRUE(r.counts().declared_mismatch.empty()) << "a matching declaration is silent";
  EXPECT_NE(r.report().find("fallback exclude, ping floor 200"), std::string::npos);

  // A floor above the 1 ms mode's 93890 pings treats it as absent.
  BackscatterModeResolver strict(&f.table, BackscatterModeFallback::Exclude, 100000);
  strict.addSonarInfo(state(100, 0.001f, 24000.0f, 2));
  EXPECT_EQ(strict.resolve(150, kF, why), kCorrectionExclude);
  EXPECT_EQ(why, ModeResolution::UnderPingFloor);
  EXPECT_EQ(strict.counts().under_floor_by_mode.at("FMU_1000us_24000Hz_500kHz"), 1u);
}

TEST(BackscatterModeResolver, HeartbeatsDedupAndLateMessagesCountStale)
{
  Fixture f;
  BackscatterModeResolver r(&f.table);
  const auto fm = state(100, 0.001f, 24000.0f, 2);
  EXPECT_TRUE(r.addSonarInfo(fm));
  EXPECT_EQ(r.resolve(500, kF), 2);
  EXPECT_EQ(r.resolve(510, kF), 2);
  // The same message again (a heartbeat) changes nothing and is not late.
  EXPECT_TRUE(r.addSonarInfo(fm));
  EXPECT_EQ(r.counts().stale, 0u);
  EXPECT_EQ(r.counts().sonar_info_seen, 2u);
  // A mode change stamped 505 arriving now: the ping at 510 was corrected
  // under the previous mode and is counted stale; 500 was not.
  EXPECT_FALSE(r.addSonarInfo(state(505, 5e-5f, 20000.0f, 1)));
  EXPECT_EQ(r.counts().stale, 1u);
  EXPECT_EQ(r.counts().late_sonar_info, 1u);
  EXPECT_EQ(r.resolve(520, kF), 1) << "pings after the arrival use it";
  EXPECT_EQ(r.resolve(502, kF), 2) << "association stays by stamp";
  EXPECT_NE(r.report().find("1 ping(s) resolved before a later-arriving SonarInfo"),
    std::string::npos);
  // A different message at an existing stamp replaces it -- and since the
  // ping at 520 was already corrected under the replaced one, that is late too.
  EXPECT_FALSE(r.addSonarInfo(state(505, 0.001f, 24000.0f, 2)));
  EXPECT_EQ(r.counts().stale, 2u);
  EXPECT_EQ(r.resolve(530, kF), 2);
}

// cube_bathymetry#172 review: a bridge that republishes SonarInfo with a new
// stamp on every ping under a steady mode adds no history entry and is not
// late; a late OLDER message only counts the pings it would really have
// corrected differently (the ones before the next stored stamp).
TEST(BackscatterModeResolver, SteadyRepublishAndOutOfOrderMessagesAreNotStale)
{
  Fixture f;
  BackscatterModeResolver r(&f.table);
  const int64_t s = 1000000000LL;
  EXPECT_TRUE(r.addSonarInfo(state(1 * s, 0.001f, 24000.0f, 2)));   // FMU 1 ms
  for (int i = 0; i < 50; ++i) {
    const int64_t ping = 2 * s + i * 100000000LL;
    EXPECT_EQ(r.resolve(ping, kF), 2);
    // Delivered just after the ping it describes, stamped with it: same mode.
    EXPECT_TRUE(r.addSonarInfo(state(ping, 0.001f, 24000.0f, 2))) << i;
  }
  EXPECT_EQ(r.historySize(), 1u) << "a steady mode stores one state";
  EXPECT_EQ(r.counts().stale, 0u);

  // A real change at 10 s, pings after it, then a late message stamped 9 s
  // with a third mode: the pings at 10 s and later use the 10 s state, so the
  // late one describes none of them.
  EXPECT_TRUE(r.addSonarInfo(state(10 * s, 5e-5f, 20000.0f, 1)));   // CW 50 us
  EXPECT_EQ(r.resolve(10 * s + 1, kF), 1);
  EXPECT_EQ(r.resolve(10 * s + 2, kF), 1);
  EXPECT_TRUE(r.addSonarInfo(state(9 * s, 0.005f, 24000.0f, 2)));   // FMU 5 ms, late
  EXPECT_EQ(r.counts().stale, 0u) << "no resolved ping lies between 9 s and 10 s";
  EXPECT_EQ(r.resolve(9 * s + 5, kF), kCorrectionUnresolved) << "the 5 ms entry is unanchored";
  // A re-delivery at 10 s that changes only the declared TVG changes no
  // correction: stored, but nothing is stale.
  EXPECT_TRUE(r.addSonarInfo(state(10 * s, 5e-5f, 20000.0f, 1, DeclaredTvg::Log40R, 80.0f)));
  EXPECT_EQ(r.counts().stale, 0u);
}

// The bound (cube_bathymetry#172 review must-fix): a sonar alternating modes on
// every ping republishes SonarInfo every ping; the history must not grow with
// the run, yet a ping that arrives late but inside the window still resolves
// against the state that was in force at its stamp.
TEST(BackscatterModeResolver, HistoryIsBoundedByTheWindowAndTheCap)
{
  Fixture f;
  BackscatterModeResolver r(&f.table);
  const int64_t step = 100000000LL;  // 10 Hz
  const int n = 20000;               // 2000 s, far beyond the 60 s window
  for (int i = 0; i < n; ++i) {
    const int64_t t = i * step;
    if (i % 2 == 0) {
      r.addSonarInfo(state(t, 0.001f, 24000.0f, 2));   // FMU 1 ms -> idx 2
    } else {
      r.addSonarInfo(state(t, 5e-5f, 20000.0f, 1));    // CW 50 us -> idx 1
    }
    ASSERT_EQ(r.resolve(t + step / 2, kF), i % 2 == 0 ? 2 : 1) << i;
  }
  const std::size_t window_entries =
    static_cast<std::size_t>(BackscatterModeResolver::kHistoryWindowNs / step);
  EXPECT_LE(r.historySize(), window_entries + 2) << "bounded by the window, not the run";
  EXPECT_GE(r.historySize(), window_entries - 2) << "the window itself is kept";
  EXPECT_EQ(r.counts().stale, 0u);

  // Late pings inside the window, 59 s and 30 s behind the newest one, still
  // resolve against the state in force at their stamps.
  const int64_t newest = (n - 1) * step + step / 2;
  for (const int64_t behind : {59LL * 1000000000LL, 30LL * 1000000000LL}) {
    const int64_t late = newest - behind;
    EXPECT_EQ(r.resolve(late, kF), (late / step) % 2 == 0 ? 2 : 1) << behind;
  }

  // A SonarInfo stream with no pings at all is held by the hard cap.
  BackscatterModeResolver idle(&f.table);
  const int64_t many = 3 * static_cast<int64_t>(BackscatterModeResolver::kMaxHistory);
  for (int64_t i = 0; i < many; ++i) {
    idle.addSonarInfo(
      i % 2 == 0 ? state(i, 0.001f, 24000.0f, 2) : state(i, 5e-5f, 20000.0f, 1));
  }
  EXPECT_EQ(idle.historySize(), BackscatterModeResolver::kMaxHistory);
  ASSERT_NE(idle.stateAt(many), nullptr) << "the newest states are the ones kept";
  EXPECT_EQ(idle.stateAt(0), nullptr) << "the oldest ones went first";
}

TEST(BackscatterModeResolver, MixedDeclarationAlwaysMismatches)
{
  const std::string path = (std::filesystem::temp_directory_path() /
    ("cube_bs_mode_mixed_" + std::to_string(::getpid()) + ".csv")).string();
  {
    std::ofstream out(path);
    out << "# format: angular-response-table v1\n# reference_mode: X\n# range_ref_m: 10\n"
      "mode,term,x,angle_lo_deg,angle_hi_deg,value_db,n,var_db2,db_relative_to_nadir,se_db,"
      "offset_db_vs_ref,offset_se_db,offset_anchored,declared_tvg_model,"
      "declared_tvg_absorption_db_per_km\n"
      "FMU_1000us_24000Hz_500kHz,angle,1,,,-20,100,9,0,0.3,0,0,true,MIXED,nan\n";
  }
  std::string error;
  const auto table = loadBackscatterTable(path, error);
  ASSERT_TRUE(error.empty()) << error;
  BackscatterModeResolver r(&table);
  int warned = 0;
  r.setMismatchWarning([&](const std::string &) {++warned;});
  r.addSonarInfo(state(100, 0.001f, 24000.0f, 2));
  ModeResolution why;
  EXPECT_EQ(r.resolve(150, kF, why), 1) << "still applied";
  EXPECT_EQ(r.resolve(160, kF, why), 1);
  EXPECT_EQ(warned, 1);
  EXPECT_EQ(r.counts().declared_mismatch.at({"FMU_1000us_24000Hz_500kHz", "UNKNOWN"}), 2u);
  std::filesystem::remove(path);
}

TEST(BackscatterModeResolver, NoTableResolvesNothing)
{
  BackscatterModeResolver r(nullptr);
  r.addSonarInfo(state(100, 0.001f, 24000.0f, 2));
  ModeResolution why;
  EXPECT_EQ(r.resolve(150, kF, why), kCorrectionUnresolved);
  EXPECT_EQ(why, ModeResolution::NoEntry);
  EXPECT_NE(r.report().find("no table"), std::string::npos);
}

}  // namespace cube
