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

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "cube_bathymetry/build_fingerprint.h"

namespace cube
{

namespace
{
std::string tempStore(const std::string & tag)
{
  const auto dir = std::filesystem::temp_directory_path() /
    ("cube_fingerprint_" + tag + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(dir);
  return dir.string();
}

BuildFingerprint adaptive()
{
  BuildFingerprint f;
  f.mode = BuildFingerprint::Mode::DepthAdaptive;
  f.iho_order = "order1a";
  f.policy.depth_adaptive_scale = 0.05;
  f.policy.coarsest_level = 8;
  f.policy.finest_level = 14;
  f.policy.count_level = 14;
  f.policy.decision_depth_percentile = 0.02;
  f.policy.achieved_percentile = 0.95;
  f.levels_used = {8, 9, 10, 13};
  return f;
}

BuildFingerprint fixed(double cell)
{
  BuildFingerprint f;
  f.mode = BuildFingerprint::Mode::Fixed;
  f.cell_size_m = cell;
  f.iho_order = "order1a";
  f.levels_used = {10};
  return f;
}
}  // namespace

TEST(BuildFingerprint, RoundTripsBothModes)
{
  for (const auto & original : {adaptive(), fixed(1.0)}) {
    const auto back = BuildFingerprint::fromJson(original.toJson());
    EXPECT_EQ(back.schema_version, 2);
    EXPECT_EQ(back.mode, original.mode);
    EXPECT_EQ(back.cell_size_m.has_value(), original.cell_size_m.has_value());
    if (original.cell_size_m) {
      EXPECT_DOUBLE_EQ(*back.cell_size_m, *original.cell_size_m);
    }
    EXPECT_DOUBLE_EQ(back.policy.capture_distance_scale, original.policy.capture_distance_scale);
    EXPECT_DOUBLE_EQ(back.policy.capture_spacing_scale, original.policy.capture_spacing_scale);
    EXPECT_EQ(back.iho_order, original.iho_order);
    if (original.mode == BuildFingerprint::Mode::DepthAdaptive) {
      EXPECT_DOUBLE_EQ(back.policy.depth_adaptive_scale, original.policy.depth_adaptive_scale);
      EXPECT_DOUBLE_EQ(
        back.policy.decision_depth_percentile, original.policy.decision_depth_percentile);
      EXPECT_DOUBLE_EQ(back.policy.achieved_percentile, original.policy.achieved_percentile);
    }
    EXPECT_EQ(back.levels_used, original.levels_used);
    EXPECT_FALSE(back.isStale(original));
    EXPECT_FALSE(original.isStale(back));
  }
}

TEST(BuildFingerprint, PolicyIsWrittenInFixedModeToo)
{
  // The capture gate changes fixed-level output, so a fixed store's fingerprint
  // must carry the capture policy or a gate change could not mark it stale.
  auto a = fixed(1.0);
  auto b = fixed(1.0);
  b.policy.capture_spacing_scale = 0.5;
  EXPECT_TRUE(a.isStale(b));
  EXPECT_NE(a.toJson().find("capture_spacing_scale"), std::string::npos);
  // Depth-adaptive-only fields are null in fixed mode and do not affect staleness.
  auto c = fixed(1.0);
  c.policy.finest_level = 12;
  EXPECT_FALSE(a.isStale(c));
  EXPECT_NE(a.toJson().find("\"finest_level\": null"), std::string::npos);
  EXPECT_NE(a.toJson().find("\"achieved_percentile\": null"), std::string::npos);
  EXPECT_NE(a.toJson().find("\"depth_adaptive_scale\": null"), std::string::npos);
  // The IHO order is written in fixed mode (not null): it picks the error model.
  EXPECT_NE(a.toJson().find("\"iho_order\": \"order1a\""), std::string::npos);
}

TEST(BuildFingerprint, StalenessRules)
{
  const auto base = adaptive();
  EXPECT_FALSE(base.isStale(base));

  auto mode = base;
  mode.mode = BuildFingerprint::Mode::Fixed;
  mode.cell_size_m = 0.906;
  EXPECT_TRUE(base.isStale(mode));
  EXPECT_TRUE(mode.isStale(base));

  auto finest = base;
  finest.policy.finest_level = 12;
  EXPECT_TRUE(base.isStale(finest));
  auto count = base;
  count.policy.count_level = 15;
  EXPECT_TRUE(base.isStale(count));
  auto obs = base;
  obs.policy.min_obs_per_node = 7;
  EXPECT_TRUE(base.isStale(obs));
  auto blunder = base;
  blunder.policy.blunder_allowance = 0.3;
  EXPECT_TRUE(base.isStale(blunder));
  auto scale = base;
  scale.policy.capture_distance_scale = 0.06;
  EXPECT_TRUE(base.isStale(scale));

  // Every input the level plan rests on moves tiles between levels, so each one
  // must make a store stale (the plan-deciding keys, #143 review must-fix 4).
  auto ladder = base;
  ladder.policy.depth_adaptive_scale = 0.08;
  EXPECT_TRUE(base.isStale(ladder));
  auto decision = base;
  decision.policy.decision_depth_percentile = 0.05;
  EXPECT_TRUE(base.isStale(decision));
  auto achieved = base;
  achieved.policy.achieved_percentile = 0.99;
  EXPECT_TRUE(base.isStale(achieved));

  // The IHO order picks the error model, so it decides output in both modes.
  auto order = base;
  order.iho_order = "special";
  EXPECT_TRUE(base.isStale(order));
  auto fixed_order = fixed(1.0);
  fixed_order.iho_order = "special";
  EXPECT_TRUE(fixed(1.0).isStale(fixed_order));

  // levels_used is informational.
  auto levels = base;
  levels.levels_used = {8};
  EXPECT_FALSE(base.isStale(levels));

  // Fixed: the cell size decides.
  EXPECT_TRUE(fixed(1.0).isStale(fixed(0.5)));
  EXPECT_FALSE(fixed(1.0).isStale(fixed(1.0)));

  // A schema-1 file reads as stale, whatever else it says.
  const auto v1 = BuildFingerprint::fromJson("{\"schema_version\": 1, \"cell_size_m\": 0.25}");
  EXPECT_EQ(v1.schema_version, 1);
  EXPECT_TRUE(base.isStale(v1));
  EXPECT_TRUE(fixed(0.25).isStale(v1));
}

TEST(BuildFingerprint, ReadWriteAtomically)
{
  const auto dir = tempStore("rw");
  EXPECT_FALSE(BuildFingerprint::read(dir).has_value());  // absent directory: absent file

  const auto f = adaptive();
  f.write(dir);
  const auto back = BuildFingerprint::read(dir);
  ASSERT_TRUE(back.has_value());
  EXPECT_FALSE(back->isStale(f));
  EXPECT_FALSE(std::filesystem::exists(
      std::filesystem::path(dir) / (std::string(BuildFingerprint::kFilename) + ".tmp")));

  // Overwrite: the new content replaces the old.
  auto g = fixed(0.5);
  g.write(dir);
  const auto again = BuildFingerprint::read(dir);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(again->mode, BuildFingerprint::Mode::Fixed);
  EXPECT_TRUE(again->isStale(f));

  // Malformed file: read throws rather than reporting "absent".
  {
    std::ofstream bad(std::filesystem::path(dir) / BuildFingerprint::kFilename);
    bad << "{ this is not json";
  }
  EXPECT_THROW(BuildFingerprint::read(dir), std::runtime_error);
  EXPECT_THROW(BuildFingerprint::fromJson("{\"schema_version\": 2}"), std::runtime_error);
  EXPECT_THROW(BuildFingerprint::fromJson(
      "{\"schema_version\": 2, \"tiling\": {\"mode\": \"weird\", "
      "\"policy\": {}, \"levels_used\": []}}"),
    std::runtime_error);
  std::filesystem::remove_all(dir);
}

TEST(BuildFingerprint, ReportsAFailedDirectoryFsyncInsteadOfClaimingSuccess)
{
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root bypasses the directory permissions this test relies on";
  }
  const std::string dir = tempStore("nodirsync");
  std::filesystem::create_directories(dir);
  auto f = adaptive();
  f.write(dir);
  ASSERT_TRUE(BuildFingerprint::read(dir).has_value());

  // Write-and-search but not readable: the temp file, the fsync of it and the
  // rename all still succeed, and only the post-rename fsync of the DIRECTORY
  // fails. Swallowed, that returns success for a durability that did not
  // happen -- so write() must throw.
  std::filesystem::permissions(
    dir, std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec,
    std::filesystem::perm_options::replace);
  try {
    f.write(dir);
    ADD_FAILURE() << "write() reported success without syncing the directory";
  } catch (const std::runtime_error & e) {
    // Pin the failure to the directory fsync, not an earlier step.
    EXPECT_NE(std::string(e.what()).find("fsync"), std::string::npos) << e.what();
  }

  std::filesystem::permissions(
    dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
  std::filesystem::remove_all(dir);
}

}  // namespace cube
