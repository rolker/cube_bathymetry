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
#include <limits>
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
  // ping at 520 was already corrected under the replaced one, that is late
  // too, while the ping at 510 now agrees with the state in force again: its
  // count is taken back (review round 2: a ping counts at most once).
  EXPECT_FALSE(r.addSonarInfo(state(505, 0.001f, 24000.0f, 2)));
  EXPECT_EQ(r.counts().stale, 1u);
  EXPECT_EQ(r.counts().late_sonar_info, 2u);
  EXPECT_EQ(r.resolve(530, kF), 2);
}

// cube_bathymetry#172 review: a bridge that republishes SonarInfo with a new
// stamp on every ping under a steady mode is not late; a late OLDER message
// only counts the pings it would really have corrected differently (the ones
// before the next stored stamp).
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
  EXPECT_EQ(r.historySize(), 51u) << "distinct stamps are kept (the window bounds them)";
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

// cube_bathymetry#172 review round 2: the same state under a new stamp is
// stored, not folded into the entry in force, or a late message stamped
// between the two would take over the pings after the second.
TEST(BackscatterModeResolver, OutOfOrderSonarInfoKeepsTheLaterRepeat)
{
  Fixture f;
  BackscatterModeResolver r(&f.table);
  r.addSonarInfo(state(100, 0.001f, 24000.0f, 2));   // A: FMU 1 ms -> idx 2
  r.addSonarInfo(state(300, 0.001f, 24000.0f, 2));   // A again, new stamp
  r.addSonarInfo(state(200, 5e-5f, 20000.0f, 1));    // B, late: CW 50 us -> idx 1
  EXPECT_EQ(r.resolve(150, kF), 2);
  EXPECT_EQ(r.resolve(250, kF), 1);
  EXPECT_EQ(r.resolve(350, kF), 2) << "A@300 is in force at 350, not the late B@200";
  // A same-stamp re-delivery is still one entry.
  r.addSonarInfo(state(300, 0.001f, 24000.0f, 2));
  EXPECT_EQ(r.historySize(), 3u);
}

// cube_bathymetry#172 review round 2: one ping counts as stale at most once,
// however many late messages land before it, and a late message that restores
// the correction it got takes the count back.
TEST(BackscatterModeResolver, AStalePingCountsOnceAndCanBeTakenBack)
{
  Fixture f;
  BackscatterModeResolver r(&f.table);
  r.addSonarInfo(state(0, 0.001f, 24000.0f, 2));          // A: FMU 1 ms -> idx 2
  ASSERT_EQ(r.resolve(20, kF), 2);
  EXPECT_FALSE(r.addSonarInfo(state(10, 5e-5f, 20000.0f, 1)));   // late B: CW 50 us -> 1
  EXPECT_EQ(r.counts().stale, 1u);
  EXPECT_FALSE(r.addSonarInfo(state(15, 2e-5f, 50000.0f, 1)));   // late C: no entry -> 0
  EXPECT_EQ(r.counts().stale, 1u) << "the same ping, counted once";
  EXPECT_EQ(r.counts().late_sonar_info, 2u);
  EXPECT_TRUE(r.addSonarInfo(state(18, 0.001f, 24000.0f, 2)));   // late A again: what it got
  EXPECT_EQ(r.counts().stale, 0u) << "the state now in force agrees with its correction";
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

// cube_bathymetry#172 review round 2: import_bag reads SonarInfo as it comes
// and resolves pings only once the TF frontier passes them. A TF stall through
// more state changes than the cap must not drop the states the waiting pings
// need (they would be reported as "before the first SonarInfo"): the oldest
// waiting ping holds the history.
TEST(BackscatterModeResolver, TheOldestWaitingPingHoldsTheHistoryPastTheCap)
{
  Fixture f;
  BackscatterModeResolver r(&f.table);
  ASSERT_EQ(r.resolve(1, kF), kCorrectionUnresolved);   // a ping drained: the window is live
  const int64_t many = 2 * static_cast<int64_t>(BackscatterModeResolver::kMaxHistory);
  r.holdFrom(int64_t{10});   // the oldest ping still waiting for the TF frontier
  for (int64_t i = 0; i < many; ++i) {
    const int64_t t = 5 + i;
    r.addSonarInfo(t % 2 == 0 ? state(t, 0.001f, 24000.0f, 2) : state(t, 5e-5f, 20000.0f, 1));
  }
  EXPECT_GT(r.historySize(), BackscatterModeResolver::kMaxHistory) << "held past the cap";
  EXPECT_LE(r.historySize(), static_cast<std::size_t>(many) - 4) << "but not before the hold";
  // Every waiting ping still finds the state in force at its stamp.
  for (const int64_t ping : {10LL, 11LL, 4000LL, 8000LL}) {
    EXPECT_EQ(r.resolve(ping, kF), ping % 2 == 0 ? 2 : 1) << ping;
  }
  // Released (nothing waiting), the next message brings it back to the cap.
  r.holdFrom(std::nullopt);
  r.addSonarInfo(state(5 + many, 0.001f, 24000.0f, 2));
  EXPECT_EQ(r.historySize(), BackscatterModeResolver::kMaxHistory);
}

// cube_bathymetry#172 review round 2: time going backwards (a sim restart,
// `ros2 bag play --loop`; ~/reset keeps the resolver). The window anchor was a
// running maximum, so every SonarInfo of the new timeline pruned the history
// to its newest entry before the OLD anchor, and a ping whose successor
// SonarInfo had arrived first got no state at all.
TEST(BackscatterModeResolver, TimeGoingBackwardsReAnchorsTheWindow)
{
  Fixture f;
  const int64_t s = 1000000000LL;
  BackscatterModeResolver r(&f.table);
  r.addSonarInfo(state(990 * s, 5e-5f, 20000.0f, 1));       // old timeline: CW 50 us
  ASSERT_EQ(r.resolve(1000 * s, kF), 1);
  // The new timeline: two states arrive before its first ping (offline the
  // pings wait for the TF frontier; live a mode switch precedes the ping).
  r.addSonarInfo(state(10 * s, 5e-5f, 20000.0f, 1));        // CW 50 us -> idx 1
  r.addSonarInfo(state(20 * s, 0.001f, 24000.0f, 2));       // FMU 1 ms -> idx 2
  ModeResolution why = ModeResolution::Resolved;
  EXPECT_EQ(r.resolve(15 * s, kF, why), 1) << "the 10 s state, not 'no SonarInfo'";
  EXPECT_EQ(why, ModeResolution::Resolved);
  EXPECT_EQ(r.counts().window_reanchored, 1u);
  EXPECT_EQ(r.resolve(25 * s, kF), 2);
  // The old timeline's 990 s entry is gone: when the new timeline reaches
  // 995 s in a steady mode, the 20 s state is still the one in force.
  EXPECT_EQ(r.resolve(995 * s, kF), 2);
  EXPECT_EQ(r.counts().no_sonar_info, 0u);
  EXPECT_EQ(r.counts().stale, 0u) << "the old timeline's pings are not looked back on";
  EXPECT_NE(r.report().find("re-anchored"), std::string::npos) << r.report();

  // A lone ping more than the window late, with no SonarInfo behind the
  // window, only moves the anchor: the current timeline keeps its history.
  BackscatterModeResolver lone(&f.table);
  lone.addSonarInfo(state(100 * s, 0.001f, 24000.0f, 2));
  for (int64_t t = 100; t <= 200; ++t) {
    ASSERT_EQ(lone.resolve(t * s, kF), 2) << t;
  }
  lone.resolve(50 * s, kF, why);
  EXPECT_EQ(why, ModeResolution::NoSonarInfo);
  EXPECT_EQ(lone.counts().window_reanchored, 1u);
  EXPECT_EQ(lone.resolve(201 * s, kF), 2) << "the current state survives a stray late ping";

  // A late SonarInfo behind the window, followed by an in-window ping, was a
  // late message, not a new timeline: a later stray ping drops nothing.
  BackscatterModeResolver late(&f.table);
  late.addSonarInfo(state(100 * s, 0.001f, 24000.0f, 2));
  ASSERT_EQ(late.resolve(200 * s, kF), 2);
  late.addSonarInfo(state(50 * s, 5e-5f, 20000.0f, 1));     // 150 s behind: stored as is
  EXPECT_EQ(late.resolve(201 * s, kF), 2);
  EXPECT_EQ(late.resolve(60 * s, kF), 1) << "re-anchored, the 50 s state kept";
  EXPECT_EQ(late.resolve(202 * s, kF), 2) << "and the 100 s state too";
}

// The node's unresolved-ping warning throttle: a clock that went backwards
// (negative elapsed time) is "warn now", not a mute until it catches up.
TEST(BackscatterModeResolver, WarnIntervalTreatsABackwardClockAsDue)
{
  const int64_t minute = 60LL * 1000000000LL;
  EXPECT_TRUE(warnIntervalDue(std::nullopt, 5, minute)) << "never fired";
  EXPECT_FALSE(warnIntervalDue(1000 * minute, 1000 * minute + 1, minute));
  EXPECT_FALSE(warnIntervalDue(1000 * minute, 1001 * minute - 1, minute));
  EXPECT_TRUE(warnIntervalDue(1000 * minute, 1001 * minute, minute));
  EXPECT_TRUE(warnIntervalDue(1000 * minute, 3 * minute, minute)) << "clock went backwards";
  EXPECT_TRUE(warnIntervalDue(1000 * minute, 1000 * minute - 1, minute));
}

// cube_bathymetry#172 review: a table run in which nothing resolved must be
// detectable, with the reasons, so import_bag can fail it and the node warn.
TEST(BackscatterModeResolver, ResolvedNothingAndTheUnresolvedSummary)
{
  Fixture f;
  BackscatterModeResolver r(&f.table, BackscatterModeFallback::Exclude);
  EXPECT_FALSE(r.resolvedNothing()) << "no pings yet is not 'nothing resolved'";
  r.resolve(50, kF);                                    // no SonarInfo yet
  EXPECT_TRUE(r.resolvedNothing());
  r.addSonarInfo(state(100, 0.001f, 24000.0f, 2));
  r.resolve(150, std::numeric_limits<float>::quiet_NaN());   // no frequency: ?kHz
  r.addSonarInfo(state(200, 0.005f, 24000.0f, 2));
  r.resolve(250, kF);                                   // unanchored
  EXPECT_TRUE(r.resolvedNothing());
  const std::string summary = r.unresolvedSummary();
  EXPECT_NE(summary.find("3 of 3 ping(s) not corrected"), std::string::npos) << summary;
  EXPECT_NE(summary.find("fallback exclude"), std::string::npos) << summary;
  EXPECT_NE(summary.find("1 before any SonarInfo (2 SonarInfo message(s) seen)"),
    std::string::npos) << summary;
  EXPECT_NE(summary.find("1 with no table entry (1 of them without a usable frequency"),
    std::string::npos) << summary;
  EXPECT_NE(summary.find("1 unanchored, 0 under the ping floor"), std::string::npos) << summary;
  EXPECT_EQ(r.resolve(160, kF), 2);
  EXPECT_FALSE(r.resolvedNothing()) << "one resolved ping is a corrected run";
}

// cube_bathymetry#172 review: a resolved ping can still have beams the table
// cannot correct; they take the fallback and must be counted per reason, so a
// run cannot report every ping resolved while dropping its backscatter.
TEST(BackscatterModeResolver, BeamsTheTableCannotCorrectAreCountedPerReason)
{
  Fixture f;
  BackscatterModeResolver r(&f.table, BackscatterModeFallback::Exclude);
  r.addSonarInfo(state(100, 5e-5f, 20000.0f, 1));   // CW 50 us: bins 1 (ok) and 41 (no var)
  const uint16_t idx = r.resolve(150, kF);
  ASSERT_EQ(idx, 1);
  const float deg = static_cast<float>(M_PI / 180.0);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  r.countBeam(idx, -30.0f, 1.0f * deg, 3.0f);     // correctable
  r.countBeam(idx, -30.0f, -2.0f * deg, 3.0f);    // correctable (|angle|)
  r.countBeam(idx, -30.0f, 41.0f * deg, 3.0f);    // unweighable bin
  r.countBeam(idx, -30.0f, nan, 3.0f);            // no angle
  r.countBeam(idx, -30.0f, 1.0f * deg, nan);      // no range
  r.countBeam(idx, -30.0f, 90.0f * deg, 3.0f);    // at 90 deg
  r.countBeam(idx, nan, 1.0f * deg, 3.0f);        // no intensity: not a sample
  r.countBeam(kCorrectionExclude, -30.0f, 1.0f * deg, 3.0f);  // the ping's reason counts it
  const auto & c = r.counts();
  EXPECT_EQ(c.beams_correctable, 2u);
  const std::string mode = "CW_50us_20000Hz_500kHz";
  EXPECT_EQ(c.beam_fallback.at({mode, "unweighable angle bin"}), 1u);
  EXPECT_EQ(c.beam_fallback.at({mode, "no finite beam angle"}), 1u);
  EXPECT_EQ(c.beam_fallback.at({mode, "no finite positive slant range"}), 1u);
  EXPECT_EQ(c.beam_fallback.at({mode, "beam angle at or beyond 90 deg"}), 1u);
  EXPECT_EQ(c.beam_fallback.size(), 4u);
  const std::string report = r.report();
  EXPECT_NE(report.find("beams of resolved pings: 2 correctable, 4 not (exclude)"),
    std::string::npos) << report;
  EXPECT_NE(report.find(mode + ": 1 beam(s), unweighable angle bin: exclude"),
    std::string::npos) << report;
}

// cube_bathymetry#172 review: the declared-TVG warning must not claim the
// table is applied when the entry was rejected (under the floor, unanchored).
TEST(BackscatterModeResolver, MismatchWarningSaysWhenTheEntryIsNotApplied)
{
  Fixture f;
  BackscatterModeResolver r(&f.table, BackscatterModeFallback::Exclude, 1000000);
  std::vector<std::string> warnings;
  r.setMismatchWarning([&](const std::string & w) {warnings.push_back(w);});
  r.addSonarInfo(state(100, 5e-5f, 20000.0f, 1));   // CW 50 us declared UNKNOWN, table 40LOGR
  ModeResolution why;
  EXPECT_EQ(r.resolve(150, kF, why), kCorrectionExclude);
  EXPECT_EQ(why, ModeResolution::UnderPingFloor);
  ASSERT_EQ(warnings.size(), 1u);
  EXPECT_EQ(warnings[0].find("applied as is"), std::string::npos) << warnings[0];
  EXPECT_NE(warnings[0].find("the entry is not applied (mode entry under the ping floor), "
    "the fallback exclude is"), std::string::npos) << warnings[0];

  BackscatterModeResolver applied(&f.table);
  applied.setMismatchWarning([&](const std::string & w) {warnings.push_back(w);});
  applied.addSonarInfo(state(100, 5e-5f, 20000.0f, 1));
  EXPECT_EQ(applied.resolve(150, kF), 1);
  ASSERT_EQ(warnings.size(), 2u);
  EXPECT_NE(warnings[1].find("the table is applied as is"), std::string::npos) << warnings[1];
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
