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
#include <limits>
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

GeoSounding sounding(double lat, double lon, float depth, float horiz = 0.01f)
{
  GeoSounding s(gz4d::GeoPointLatLongDegrees(lat, lon, depth));
  s.sounding.vertical_error = 0.05f;
  s.sounding.horizontal_error = horiz;
  s.sounding.intensity = -20.0f;
  s.sounding.beam_angle = 0.3f;
  s.sounding.slant_range = 12.5f;
  s.sounding.sonar_relative_position.x = 1.0;
  s.sounding.sonar_relative_position.y = -2.0;
  s.sounding.sonar_relative_position.z = 10.0;
  return s;
}
}  // namespace

TEST(ShallowReservoir, KeepsTheShallowestAndReadsThePercentile)
{
  ShallowReservoir r;
  EXPECT_TRUE(std::isnan(r.decisionDepth(0.02)));
  // 100 soundings from -1 (shallowest) to -100 (deepest), added deep first.
  for (int i = 100; i >= 1; --i) {
    r.add(static_cast<float>(-i));
  }
  EXPECT_EQ(r.count, 100u);
  EXPECT_EQ(r.shallowest.size(), ShallowReservoir::kCapacity);
  EXPECT_FLOAT_EQ(r.shallowest.front(), -1.0f);
  EXPECT_FLOAT_EQ(r.shallowest.back(), -64.0f);
  // p = 0.02 of 100 -> rank 2 -> the second shallowest.
  EXPECT_FLOAT_EQ(r.decisionDepth(0.02), -2.0f);
  // p = 0 -> rank clamps to 1 -> the shallowest itself (no flier guard).
  EXPECT_FLOAT_EQ(r.decisionDepth(0.0), -1.0f);
  // A percentile past the retained window caps at the 64th shallowest.
  EXPECT_FLOAT_EQ(r.decisionDepth(0.9), -64.0f);

  // One flier at -0.1 among 101: ceil(0.02 * 101) = 3, the third shallowest
  // (-0.1, -1, -2 -> -2); the flier never becomes the decision depth.
  r.add(-0.1f);
  EXPECT_FLOAT_EQ(r.shallowest.front(), -0.1f);
  EXPECT_FLOAT_EQ(r.decisionDepth(0.02), -2.0f);
}

TEST(ShallowReservoir, FlierGuardAtSmallCounts)
{
  ShallowReservoir r;
  r.add(-10.0f);
  r.add(-0.2f);  // flier
  r.add(-10.5f);
  // ceil(0.02 * 3) = 1 -> the shallowest (the flier): with three soundings the
  // percentile cannot exclude anything -- the count grid is what keeps such a
  // grid from being refined (it cannot achieve a fine level).
  EXPECT_FLOAT_EQ(r.decisionDepth(0.02), -0.2f);
}

TEST(ReconCollector, CountsReservoirsAndPlans)
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
  EXPECT_FLOAT_EQ(depths.begin()->second, -40.0f);
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

TEST(ReconCollector, SpillRoundTripsEverySoundingFieldPerLevel10Grid)
{
  const std::string dir = scratch("spill");
  LevelPlanPolicy policy;
  Parameters params{CellSizes(1.0f), "order1a"};
  {
    ReconCollector recon(policy, dir);
    // Two soundings in one level-10 grid, one far away in another.
    std::vector<GeoSounding> batch{
      sounding(43.07, -70.76, -12.0f, 0.02f),
      sounding(43.0701, -70.7601, -12.5f, 0.03f),
      sounding(43.5, -70.2, -30.0f)};
    recon.add(batch, params);
    EXPECT_EQ(recon.soundingsSpilled(), 3u);
    ASSERT_EQ(recon.spilledGrids().size(), 2u);
    EXPECT_EQ(std::filesystem::directory_iterator(dir) != std::filesystem::directory_iterator(),
        true);

    const auto near = gggs::Level(ReconCollector::kSpillLevel).gridIndex(43.07, -70.76);
    std::vector<GeoSounding> replayed;
    recon.forEachSpilled(near, [&](const GeoSounding & s) {replayed.push_back(s);});
    ASSERT_EQ(replayed.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
      EXPECT_DOUBLE_EQ(replayed[i].latitude, batch[i].latitude);
      EXPECT_DOUBLE_EQ(replayed[i].longitude, batch[i].longitude);
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
    // A grid nothing was spilled into replays nothing.
    std::size_t none = 0;
    recon.forEachSpilled(gggs::Level(10).gridIndex(44.0, -69.0), [&](const GeoSounding &) {
        ++none;
        });
    EXPECT_EQ(none, 0u);
    // The replay is re-runnable (the file was closed, not consumed).
    std::size_t again = 0;
    recon.forEachSpilled(near, [&](const GeoSounding &) {++again;});
    EXPECT_EQ(again, 2u);
  }
  // The destructor removed the spill files and the empty scratch dir.
  EXPECT_FALSE(std::filesystem::exists(dir));
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
