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
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "cube_bathymetry/recon.h"

namespace cube
{

namespace
{
std::string scratch(const std::string & tag)
{
  const auto dir = std::filesystem::temp_directory_path() /
    ("cube_recon_" + tag + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(dir);
  return dir.string();
}

/// A sounding at `depth` (the STORED value, negative-down) whose water depth
/// under the transducer is `water` -- by default the same magnitude, i.e. a
/// geoid-free datum. The recon's decision depth reads the second, never the
/// first (see `ReconCollector::add`), so the two are separable here.
GeoSounding sounding(
  double lat, double lon, float depth, float horiz = 0.01f,
  float water = std::numeric_limits<float>::quiet_NaN())
{
  GeoSounding s(gz4d::GeoPointLatLongDegrees(lat, lon, depth));
  s.sounding.vertical_error = 0.05f;
  s.sounding.horizontal_error = horiz;
  s.sounding.intensity = -20.0f;
  s.sounding.beam_angle = 0.3f;
  s.sounding.slant_range = 12.5f;
  s.sounding.sonar_relative_position.x = 1.0;
  s.sounding.sonar_relative_position.y = -2.0;
  // Positive down, as the projector builds it (range * cos(tx) * cos(rx)).
  s.sounding.sonar_relative_position.z =
    std::isfinite(water) ? std::abs(water) : std::abs(depth);
  return s;
}
}  // namespace

TEST(DepthHistogram, ReadsThePercentileToWithinOneBin)
{
  DepthHistogram h;
  EXPECT_TRUE(std::isnan(h.decisionDepth(0.02)));
  // 100 soundings from -1 (shallowest) to -100 (deepest), added deep first.
  for (int i = 100; i >= 1; --i) {
    h.add(static_cast<float>(-i));
  }
  EXPECT_EQ(h.count, 100u);
  EXPECT_DOUBLE_EQ(h.bin_width, DepthHistogram::kInitialBinWidth);
  // The answer is the shallow edge of the bin the rank falls in: always in
  // (truth, truth + bin_width] -- never deeper than the true percentile (the
  // unsafe direction), never more than one bin shallower.
  auto within_a_bin_of = [&h](double p, float truth) {
      const float d = h.decisionDepth(p);
      EXPECT_GT(d, truth) << "p=" << p << ": errs deep, the unsafe direction";
      EXPECT_LE(d, truth + static_cast<float>(h.bin_width)) << "p=" << p;
    };
  within_a_bin_of(0.02, -2.0f);   // rank 2 -> the second shallowest
  within_a_bin_of(0.5, -50.0f);   // the median: served, not refused
  within_a_bin_of(0.9, -90.0f);   // far past any bounded "shallowest" window
  // p = 0 -> rank clamps to 1 -> the shallowest itself (no flier guard).
  within_a_bin_of(0.0, -1.0f);

  // One flier at -0.1 among 101: ceil(0.02 * 101) = 3, the third shallowest
  // (-0.1, -1, -2 -> -2); the flier never becomes the decision depth.
  h.add(-0.1f);
  within_a_bin_of(0.02, -2.0f);

  // Non-finite depths are ignored, not counted.
  h.add(std::nanf(""));
  EXPECT_EQ(h.count, 101u);
}

TEST(DepthHistogram, HonoursThePercentileAtSurveyDensity)
{
  // The failure this replaced: a level-14 grid of a real M3 line holds ~200 k
  // soundings, and water-column fliers run to many hundreds -- far beyond the
  // 64 the old reservoir kept, so the 2nd percentile came back as the 64th
  // shallowest raw sounding, metres above anything CUBE accepted.
  DepthHistogram h;
  const int kBathy = 100000;
  const int kFliers = 2000;  // >> the 64 the old reservoir could hold
  std::mt19937 rng(12345);
  std::uniform_real_distribution<float> bathy(-41.0f, -36.0f);
  std::uniform_real_distribution<float> flier(-20.0f, -1.0f);  // water column
  for (int i = 0; i < kBathy; ++i) {
    h.add(bathy(rng));
  }
  for (int i = 0; i < kFliers; ++i) {
    h.add(flier(rng));
  }
  EXPECT_EQ(h.count, static_cast<uint64_t>(kBathy + kFliers));
  // The fliers are 2000/102000 = 1.96 % of the grid, just under the 2nd
  // percentile, so the decision depth must land in the bathymetry.
  const float d = h.decisionDepth(0.02);
  EXPECT_GE(d, -41.5f);
  EXPECT_LE(d, -35.5f) << "the decision depth is above the bathymetry: "
                       << "the flier guard did not hold at survey density";
  // And the whole set's shallowest is nowhere near it.
  EXPECT_GT(h.decisionDepth(0.0), -21.0f);
}

TEST(DepthHistogram, BoundsItsBinsByCoarseningTheWidth)
{
  // A pathological spread (a surface flier over abyssal depth) must not grow
  // the per-grid memory without limit: the width doubles and the bins merge.
  DepthHistogram h;
  for (int i = 0; i < 4000; ++i) {
    h.add(static_cast<float>(-i));  // 0 .. -4000 m, 0.25 m bins would be 16000
  }
  EXPECT_LE(h.bins.size(), DepthHistogram::kMaxBins);
  EXPECT_GT(h.bin_width, DepthHistogram::kInitialBinWidth);
  // Still answers the percentile, now to within the coarsened width.
  const float d = h.decisionDepth(0.02);
  EXPECT_GT(d, -80.0f);  // rank 80 of 4000 -> -79
  EXPECT_LE(d, -79.0f + static_cast<float>(h.bin_width));
  // Depths outside any plausible bathymetry are clamped, not binned away.
  h.add(-1e9f);
  EXPECT_EQ(h.count, 4001u);
}

TEST(DepthHistogram, FlierGuardAtSmallCounts)
{
  DepthHistogram h;
  h.add(-10.0f);
  h.add(-0.2f);  // flier
  h.add(-10.5f);
  // ceil(0.02 * 3) = 1 -> the shallowest (the flier): with three soundings the
  // percentile cannot exclude anything -- the count grid is what keeps such a
  // grid from being refined (it cannot achieve a fine level).
  EXPECT_GE(h.decisionDepth(0.02), -0.25f);
  EXPECT_LE(h.decisionDepth(0.02), 0.0f);
}

TEST(ReconCollector, CountsHistogramsAndPlans)
{
  LevelPlanPolicy policy;
  ReconCollector recon(policy, "");  // no spill
  Parameters params{CellSizes(1.0f), "order1a"};

  // A dense patch: 8 soundings in each of a few level-14 cells at 40 m.
  std::vector<GeoSounding> batch;
  for (int i = 0; i < 8; ++i) {
    for (int k = 0; k < 5; ++k) {
      batch.push_back(sounding(43.07 + k * 1e-7, -70.76, -40.0f));
    }
  }
  recon.add(batch, params);
  EXPECT_EQ(recon.soundingsSeen(), 40u);
  EXPECT_EQ(recon.soundingsSpilled(), 0u);
  EXPECT_EQ(recon.counts().level(), 14);

  const auto depths = recon.decisionDepths();
  ASSERT_EQ(depths.size(), 1u);
  // The histogram answers with its bin's shallow edge: never deeper than the
  // truth, never a full bin shallower.
  EXPECT_GT(depths.begin()->second, -40.0f);
  EXPECT_LE(depths.begin()->second, -40.0f + DepthHistogram::kInitialBinWidth);
  EXPECT_EQ(depths.begin()->first, gggs::Level(14).gridIndex(43.07, -70.76));

  // The spread radius is recorded per count tile: CONF_99PC * sqrt(0.01) ~ 0.26 m.
  const auto tile = gggs::Level(14).gridIndex(43.07, -70.76);
  EXPECT_NEAR(recon.counts().maxSpreadTerm(tile), params.maxSpreadRadius(batch.front().sounding),
      1e-9);
  EXPECT_NEAR(recon.counts().maxSpreadTerm(tile), 0.2576, 1e-3);

  const LevelPlan plan = recon.plan();
  EXPECT_FALSE(plan.tiles().empty());
  EXPECT_TRUE(plan.isEmitted(gggs::Level(8).gridIndex(43.07, -70.76)));

  // Degenerate soundings are ignored, not counted.
  std::vector<GeoSounding> bad{sounding(43.07, -70.76, std::nanf(""))};
  recon.add(bad, params);
  EXPECT_EQ(recon.soundingsSeen(), 40u);
}

// The ladder is a FOOTPRINT argument, so the recon decides on the water depth
// under the transducer -- not the stored value, which is a WGS84 ellipsoidal
// height (uma ADR-0002 D4) and at the UNH pier sits ~28 m below the water
// depth. Feeding the stored height coarsened every tile by one to two levels
// (cube#143 dry-run review).
TEST(ReconCollector, DecidesOnWaterDepthNotTheStoredEllipsoidalHeight)
{
  LevelPlanPolicy policy;
  ReconCollector recon(policy, "");  // no spill
  Parameters params{CellSizes(1.0f), "order1a"};

  // Stored depth -40 m (ellipsoidal), 12 m of water under the sonar.
  std::vector<GeoSounding> batch;
  for (int i = 0; i < 200; ++i) {
    batch.push_back(sounding(43.07 + i * 1e-8, -70.76, -40.0f, 0.01f, 12.0f));
  }
  recon.add(batch, params);

  const auto depths = recon.decisionDepths();
  ASSERT_EQ(depths.size(), 1u);
  const float d = depths.begin()->second;
  // 12 m of water, to within the histogram's bin (shallow side) -- not -40.
  EXPECT_GT(d, -12.0f);
  EXPECT_LE(d, -12.0f + DepthHistogram::kInitialBinWidth);

  // And the level follows the water: the ladder asks for a strictly finer tile
  // at 12 m than the stored -40 m would have bought.
  const LevelPlan plan = recon.plan();
  const auto root = gggs::Level(policy.depth.coarsest_level).gridIndex(43.07, -70.76);
  ASSERT_TRUE(plan.isEmitted(root));
  const auto & tile = plan.tiles().at(root);
  EXPECT_FLOAT_EQ(tile.decision_depth, d);
  EXPECT_EQ(
    tile.required_level,
    marine_bathymetry_store::depthAdaptiveLevel(d, policy.depth).level());
  EXPECT_GT(
    tile.required_level,
    marine_bathymetry_store::depthAdaptiveLevel(-40.0f, policy.depth).level());
}

// A producer that georeferences without carrying `sonar_relative_position`
// through leaves every z at 0. A histogram of zeros would answer "0 m of
// water" and ask for the finest level over the whole survey, so such
// soundings are counted but not histogrammed, and the caller is told.
TEST(ReconCollector, SoundingsWithNoSonarFrameRangeDoNotDecideALevel)
{
  LevelPlanPolicy policy;
  ReconCollector recon(policy, "");  // no spill
  Parameters params{CellSizes(1.0f), "order1a"};

  std::vector<GeoSounding> batch;
  for (int i = 0; i < 10; ++i) {
    batch.push_back(sounding(43.07, -70.76, -40.0f, 0.01f, 0.0f));  // z = 0
  }
  recon.add(batch, params);
  EXPECT_EQ(recon.soundingsSeen(), 10u);
  EXPECT_EQ(recon.soundingsWithoutRange(), 10u);
  // Nothing decided: no grid has a depth, so no tile is planned -- rather than
  // a plan built from 0 m of water.
  EXPECT_TRUE(recon.decisionDepths().empty());
  EXPECT_TRUE(recon.plan().tiles().empty());

  // One real sounding among them decides the grid on its own.
  recon.add({sounding(43.07, -70.76, -40.0f, 0.01f, 12.0f)}, params);
  EXPECT_EQ(recon.soundingsWithoutRange(), 10u);
  const auto depths = recon.decisionDepths();
  ASSERT_EQ(depths.size(), 1u);
  EXPECT_GT(depths.begin()->second, -12.0f);
  EXPECT_LE(depths.begin()->second, -12.0f + DepthHistogram::kInitialBinWidth);
}

// The recon must admit exactly what the estimator will (cube#143 triage).
// GeoGrid::insert rejects a non-finite uncertainty, a non-positive vertical
// error and a negative horizontal error on top of the position/depth checks;
// counting those soundings would make the achieved level read finer than the
// data supports and would call ground with no estimate surveyed, and spilling
// them replays them for nothing. A missing-attitude ping is exactly this case.
TEST(ReconCollector, RefusesWhatTheEstimatorWouldRefuse)
{
  const std::string dir = scratch("refused");
  LevelPlanPolicy policy;
  ReconCollector recon(policy, dir);
  Parameters params{CellSizes(1.0f), "order1a"};

  constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
  // A missing-attitude ping: NaN uncertainty in both terms.
  auto no_attitude = sounding(43.07, -70.76, -12.0f);
  no_attitude.sounding.vertical_error = kNaN;
  no_attitude.sounding.horizontal_error = kNaN;
  // A zero vertical error would divide by zero in the CUBE variance math.
  auto zero_vertical = sounding(43.07, -70.76, -12.0f);
  zero_vertical.sounding.vertical_error = 0.0f;
  // A negative horizontal error reintroduces NaN through influenceRadius' sqrt.
  auto negative_horizontal = sounding(43.07, -70.76, -12.0f);
  negative_horizontal.sounding.horizontal_error = -1.0f;

  recon.add({no_attitude, zero_vertical, negative_horizontal}, params);
  EXPECT_EQ(recon.soundingsRefused(), 3u);
  EXPECT_EQ(recon.soundingsSeen(), 0u) << "a refused sounding must not inflate the counts";
  EXPECT_EQ(recon.soundingsSpilled(), 0u) << "a refused sounding must not be replayed";
  EXPECT_EQ(recon.soundingsWithoutRange(), 0u) << "refused is not the same as no-range";
  EXPECT_TRUE(recon.plan().tiles().empty());

  // A good sounding at the same place still lands.
  recon.add({sounding(43.07, -70.76, -12.0f)}, params);
  EXPECT_EQ(recon.soundingsRefused(), 3u);
  EXPECT_EQ(recon.soundingsSeen(), 1u);
  EXPECT_EQ(recon.soundingsSpilled(), 1u);
  recon.cleanup();
}

TEST(ReconCollector, SpillRoundTripsEverySoundingFieldInChronologicalOrder)
{
  const std::string dir = scratch("spill");
  LevelPlanPolicy policy;
  Parameters params{CellSizes(1.0f), "order1a"};
  {
    ReconCollector recon(policy, dir);
    // Three soundings that straddle two level-10 grids, interleaved: an earlier
    // revision spilled one file per level-10 grid, which would replay these as
    // (0, 2, 1). One chronological file replays them as written.
    std::vector<GeoSounding> batch{
      sounding(43.07, -70.76, -12.0f, 0.02f),
      sounding(43.5, -70.2, -30.0f),
      sounding(43.0701, -70.7601, -12.5f, 0.03f)};
    recon.add(batch, params);
    EXPECT_EQ(recon.soundingsSpilled(), 3u);
    EXPECT_TRUE(std::filesystem::exists(recon.spillPath()));

    std::vector<GeoSounding> replayed;
    recon.forEachSpilled([&](const GeoSounding & s) {replayed.push_back(s);});
    ASSERT_EQ(replayed.size(), batch.size());
    for (std::size_t i = 0; i < batch.size(); ++i) {
      EXPECT_DOUBLE_EQ(replayed[i].latitude, batch[i].latitude) << "order at " << i;
      EXPECT_DOUBLE_EQ(replayed[i].longitude, batch[i].longitude) << "order at " << i;
      EXPECT_FLOAT_EQ(replayed[i].sounding.depth, batch[i].sounding.depth);
      EXPECT_FLOAT_EQ(replayed[i].sounding.vertical_error, batch[i].sounding.vertical_error);
      EXPECT_FLOAT_EQ(replayed[i].sounding.horizontal_error, batch[i].sounding.horizontal_error);
      EXPECT_FLOAT_EQ(replayed[i].sounding.intensity, batch[i].sounding.intensity);
      EXPECT_FLOAT_EQ(replayed[i].sounding.beam_angle, batch[i].sounding.beam_angle);
      EXPECT_FLOAT_EQ(replayed[i].sounding.slant_range, batch[i].sounding.slant_range);
      EXPECT_DOUBLE_EQ(replayed[i].sounding.sonar_relative_position.x,
        batch[i].sounding.sonar_relative_position.x);
      EXPECT_DOUBLE_EQ(replayed[i].sounding.sonar_relative_position.y,
        batch[i].sounding.sonar_relative_position.y);
      EXPECT_DOUBLE_EQ(replayed[i].sounding.sonar_relative_position.z,
        batch[i].sounding.sonar_relative_position.z);
    }
    // The replay is re-runnable (the file was closed, not consumed), and a
    // later add appends behind it.
    std::size_t again = 0;
    recon.forEachSpilled([&](const GeoSounding &) {++again;});
    EXPECT_EQ(again, 3u);
    recon.add({sounding(43.07, -70.76, -13.0f)}, params);
    std::size_t after = 0;
    recon.forEachSpilled([&](const GeoSounding &) {++after;});
    EXPECT_EQ(after, 4u);
  }
  // The destructor removed the spill file and the empty scratch dir.
  EXPECT_FALSE(std::filesystem::exists(dir));
}

TEST(ReconCollector, ATruncatedSpillIsReportedRatherThanReplayedShort)
{
  const std::string dir = scratch("truncated_spill");
  LevelPlanPolicy policy;
  Parameters params{CellSizes(1.0f), "order1a"};
  ReconCollector recon(policy, dir);
  recon.add(
    {sounding(43.07, -70.76, -12.0f), sounding(43.0701, -70.7601, -12.5f),
      sounding(43.0702, -70.7602, -13.0f), sounding(43.0703, -70.7603, -13.5f)},
    params);
  ASSERT_EQ(recon.soundingsSpilled(), 4u);

  // A clean replay reports exactly what phase one spilled -- the count the
  // import compares against soundingsSpilled().
  uint64_t seen = 0;
  EXPECT_EQ(recon.forEachSpilled([&](const GeoSounding &) {++seen;}), 4u);
  EXPECT_EQ(seen, recon.soundingsSpilled());

  const std::string path = recon.spillPath();
  const auto full_size = std::filesystem::file_size(path);

  // A PARTIAL final record (a disk-full part way through a write): the read
  // loop ends exactly as it ends at EOF, so without the stream-state check the
  // replay would stop silently mid-record and the store would be built from a
  // truncated survey.
  std::filesystem::resize_file(path, full_size - 8);
  EXPECT_THROW(recon.forEachSpilled([](const GeoSounding &) {}), std::runtime_error);

  // A WHOLE record lost at a record boundary leaves no trace in the stream
  // state: only the replayed count is short, which is why the caller must
  // compare it with soundingsSpilled().
  std::filesystem::resize_file(
    path, full_size - ReconCollector::kBytesPerSpilledSounding);
  uint64_t short_replay = 0;
  EXPECT_EQ(recon.forEachSpilled([&](const GeoSounding &) {++short_replay;}), 3u);
  EXPECT_NE(short_replay, recon.soundingsSpilled());
}

TEST(ReconCollector, RefusesAnOrphanedScratchDir)
{
  const std::string dir = scratch("orphan");
  std::filesystem::create_directories(dir);
  std::ofstream(std::filesystem::path(dir) / "soundings.spill") << "stale";
  LevelPlanPolicy policy;
  // An orphaned dir from a killed run would be appended to, replaying another
  // run's soundings: refuse it instead.
  EXPECT_THROW(ReconCollector(policy, dir), std::runtime_error);
  std::filesystem::remove_all(dir);
}

TEST(ReconCollector, FreeSpaceCheckNamesTheShortfall)
{
  const std::string dir = scratch("space");
  EXPECT_NO_THROW(ReconCollector::requireFreeSpace(dir, 1024));
  EXPECT_THROW(
    ReconCollector::requireFreeSpace(dir, std::numeric_limits<uint64_t>::max() / 2),
    std::runtime_error);
  std::filesystem::remove_all(dir);
  EXPECT_GE(ReconCollector::kBytesPerSpilledSounding, 64u);
  EXPECT_LE(ReconCollector::kBytesPerSpilledSounding, 80u);
}

TEST(ReconCollector, RejectsABadPolicy)
{
  LevelPlanPolicy policy;
  policy.depth.finest_level = 15;
  EXPECT_THROW(ReconCollector(policy, ""), std::invalid_argument);
}

}  // namespace cube
