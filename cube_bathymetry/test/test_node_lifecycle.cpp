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

// Lifecycle cycle of the live node (cube_bathymetry#164).
//
// The Ocean Discovery Day demo needs a fresh CUBE sheet without restarting the
// node: deactivate -> cleanup -> configure -> activate. Two things had to hold
// and neither was tested:
//
//   1. A SECOND configure must succeed. on_configure declared ~18 parameters
//      with no has_parameter guard; rclcpp throws ParameterAlreadyDeclared on
//      the second declare, and rclcpp_lifecycle swallows exceptions out of
//      transition callbacks, so the failure shows up only as a transition that
//      lands in the wrong state.
//   2. The reconfigured node starts with an EMPTY sheet, catalog and cursor
//      state when nothing is persisted (draft_dir and prior_store_dir empty).
//      With draft_dir set the saved tiles reload, by design; that is pinned
//      here too so the README statement cannot drift.
//
// The tests drive the public LifecycleNode transitions directly (no executor
// needed: configure/activate create entities but no callback has to run).

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "lifecycle_msgs/msg/state.hpp"
#include "rclcpp/rclcpp.hpp"

#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry_node.hpp"
#include "marine_autonomy/gz4d_geo.h"

namespace
{

using lifecycle_msgs::msg::State;

// Repeat each location so CUBE forms a hypothesis from a single survey patch.
void addPatch(const std::shared_ptr<cube::GeoMapSheet> & sheet, double depth)
{
  for (int rep = 0; rep < 25; ++rep) {
    for (int iy = 0; iy < 3; ++iy) {
      for (int ix = 0; ix < 3; ++ix) {
        gz4d::GeoPointLatLongDegrees ll(
          43.07 + iy * 1.2e-5, -70.76 + ix * 1.6e-5, depth);
        cube::GeoSounding gs(ll);
        gs.sounding.vertical_error = 0.5f;
        gs.sounding.horizontal_error = 0.1f;
        sheet->addSoundings({gs});
      }
    }
  }
}

// True when the sheet holds at least one finite cell.
bool hasFiniteCell(const std::shared_ptr<cube::GeoMapSheet> & sheet)
{
  for (const auto & grid : sheet->grids()) {
    if (!grid) {
      continue;
    }
    for (const auto & v : grid->values()) {
      if (std::isfinite(v.depth)) {
        return true;
      }
    }
  }
  return false;
}

class NodeLifecycle : public ::testing::Test
{
public:
  static void SetUpTestSuite()
  {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  }

  static void TearDownTestSuite()
  {
    rclcpp::shutdown();
  }

  // Throw-away directory for draft persistence cases.
  static std::filesystem::path makeTempDir()
  {
    const auto base = std::filesystem::temp_directory_path();
    for (int i = 0; i < 100; ++i) {
      const auto p = base / ("cube_lifecycle_test_" + std::to_string(::getpid()) + "_" +
        std::to_string(i));
      if (std::filesystem::create_directory(p)) {
        return p;
      }
    }
    throw std::runtime_error("could not create a temp directory");
  }

  static rclcpp::NodeOptions withParams(const std::vector<rclcpp::Parameter> & params)
  {
    return rclcpp::NodeOptions().parameter_overrides(params);
  }

  static uint8_t stateId(const CubeBathymetry & node)
  {
    return node.get_current_state().id();
  }
};

}  // namespace

// The reproduction (cube_bathymetry#164): configure -> activate -> deactivate ->
// cleanup -> configure must land in INACTIVE again. Unfixed, the second
// configure re-declares parameters, rclcpp swallows the exception and the node
// does not reach INACTIVE.
TEST_F(NodeLifecycle, SecondConfigureReachesInactive)
{
  auto node = std::make_shared<CubeBathymetry>();
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);

  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE)
    << "second configure did not reach INACTIVE";
  EXPECT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);

  // And a third time: the guard must hold for every cycle, not just the second.
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// The fresh-sheet claim itself: nothing persisted -> the reconfigured node
// starts with a new, empty sheet.
TEST_F(NodeLifecycle, EmptyStartWithoutPersistence)
{
  auto node = std::make_shared<CubeBathymetry>();
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);

  const auto first_sheet = node->sheet();
  ASSERT_TRUE(first_sheet);
  addPatch(first_sheet, -12.5);
  ASSERT_TRUE(hasFiniteCell(first_sheet)) << "test setup: CUBE produced no estimate";

  ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);

  const auto second_sheet = node->sheet();
  ASSERT_TRUE(second_sheet);
  EXPECT_NE(second_sheet.get(), first_sheet.get()) << "configure must build a new sheet";
  EXPECT_TRUE(second_sheet->grids().empty());
  EXPECT_FALSE(hasFiniteCell(second_sheet));
  EXPECT_TRUE(second_sheet->dirtyGrids().empty());
  EXPECT_EQ(node->catalogSize(), 0u);
}

// The same cycle with draft persistence on: the saved draft tiles reload into
// the new sheet. This is by design (ADR-0001: never lose survey data) and is
// what the README's "fresh sheet" section tells operators to avoid by leaving
// draft_dir empty.
TEST_F(NodeLifecycle, DraftTilesReloadWhenDraftDirIsSet)
{
  const auto dir = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("draft_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    addPatch(node->sheet(), -12.5);
    ASSERT_TRUE(hasFiniteCell(node->sheet()));
    // deactivate flushes the dirty tiles to <draft_dir>/draft/
    ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);

    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_TRUE(hasFiniteCell(node->sheet()))
      << "saved draft tiles must reload into the reconfigured sheet";
    EXPECT_GT(node->catalogSize(), 0u);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  std::filesystem::remove_all(dir);
}

// A parameter set between cycles is what the next configure reads, and the
// stale catalog/sheet from the previous cycle does not survive it: cycle one
// reloads tiles from draft_dir; clearing draft_dir before cycle two gives an
// empty sheet AND an empty catalog.
TEST_F(NodeLifecycle, ParameterSetBetweenCyclesIsHonoured)
{
  const auto dir = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("draft_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    addPatch(node->sheet(), -12.5);
    ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);

    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_TRUE(hasFiniteCell(node->sheet()));
    ASSERT_GT(node->catalogSize(), 0u);
    ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);

    // draft_dir is writable: the operator clears it between cycles.
    const auto result = node->set_parameter(rclcpp::Parameter("draft_dir", std::string("")));
    ASSERT_TRUE(result.successful) << result.reason;
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_TRUE(node->sheet()->grids().empty());
    EXPECT_FALSE(hasFiniteCell(node->sheet()));
    EXPECT_EQ(node->catalogSize(), 0u);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  std::filesystem::remove_all(dir);
}

// A configure that THROWS partway (here: an invalid sonar_info_topic, rejected
// when the subscription is created, after the TF listener exists) returns the
// node to UNCONFIGURED without on_cleanup. Fixing the parameter and configuring
// again must work, and the listener of the failed attempt must be released before
// the new buffer replaces the one it references (use-after-free window, #164).
TEST_F(NodeLifecycle, FailedConfigureCanBeRetried)
{
  auto node = std::make_shared<CubeBathymetry>(
    withParams({rclcpp::Parameter("sonar_info_topic", std::string("not a valid topic"))}));
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED)
    << "an invalid sonar_info_topic must fail the configure";

  // Retry without fixing anything: the failed attempt left a TF listener behind
  // each time, and each retry must replace it cleanly.
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);

  const auto result =
    node->set_parameter(rclcpp::Parameter("sonar_info_topic", std::string("sonar_info")));
  ASSERT_TRUE(result.successful) << result.reason;
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE)
    << "configure after a failed configure did not reach INACTIVE";
  EXPECT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// ADR-0001 (never lose survey data): when the final flush in cleanup fails, the
// transition must FAIL and leave the node INACTIVE with its sheet, rather than
// succeed and let the next configure replace tiles that never reached disk.
// The write failure is provoked by putting a regular file where the draft layer
// directory belongs (works for root too, unlike a permission bit).
TEST_F(NodeLifecycle, CleanupFailsWhenTheFinalFlushFails)
{
  const auto dir = makeTempDir();
  const auto layer_dir = dir / marine_bathymetry_store::layerDirName(
    marine_bathymetry_store::SourceLayer::Draft);
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("draft_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    const auto sheet = node->sheet();
    addPatch(sheet, -12.5);
    ASSERT_TRUE(hasFiniteCell(sheet));
    ASSERT_FALSE(sheet->dirtyGrids().empty());

    std::filesystem::remove_all(layer_dir);
    {
      std::ofstream blocker(layer_dir);
      blocker << "not a directory";
    }
    ASSERT_TRUE(std::filesystem::is_regular_file(layer_dir));

    // Deactivate flushes too and fails to write; it still succeeds (it only
    // cancels timers) and must keep the dirty set.
    ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_FALSE(sheet->dirtyGrids().empty()) << "a failed save must keep the dirty set";

    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_INACTIVE)
      << "cleanup must fail, not discard tiles that were never written";
    EXPECT_EQ(node->sheet().get(), sheet.get()) << "the sheet must survive a refused cleanup";
    EXPECT_TRUE(hasFiniteCell(node->sheet()));
    EXPECT_FALSE(node->sheet()->dirtyGrids().empty());

    // Remove the cause: the retry writes the tiles and the cleanup goes through.
    std::filesystem::remove(layer_dir);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
    EXPECT_FALSE(std::filesystem::is_empty(dir / "draft"))
      << "the retried flush must have written the tiles";

    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_TRUE(hasFiniteCell(node->sheet())) << "the tiles must reload after the retry";
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  std::filesystem::remove_all(dir);
}
