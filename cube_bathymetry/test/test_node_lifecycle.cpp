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

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "lifecycle_msgs/msg/state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "tf2_msgs/msg/tf_message.hpp"

#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry_node.hpp"
#include "marine_autonomy/gz4d_geo.h"

// Friend of CubeBathymetry (declared in the node header): dirties the per-session
// state a reconfigure must reset, and reads it back. Each field below is reset by
// on_configure (or on_cleanup); without a way to set them first, "starts empty"
// would be asserted against values that were never anything else.
struct CubeBathymetryTestAccess
{
  static gggs::GridIndex probeIndex()
  {
    return gggs::Level::fromCellSize(1.0f).gridIndex(43.07, -70.76);
  }

  static void dirtySessionState(CubeBathymetry & n)
  {
    n.last_grid_publish_time_ = rclcpp::Time(1234, 0, RCL_SYSTEM_TIME);
    n.have_publish_tf_ = true;
    n.last_publish_tf_.header.frame_id = "stale_frame";
    n.have_vessel_pos_ = true;
    n.last_vessel_lat_ = 43.0;
    n.last_vessel_lon_ = -70.0;
    n.saving_report_last_s_ = 99.0;
    n.report_window_cells_ = 7;
    n.report_full_cells_ = 8;
    n.report_refresh_cells_ = 9;
    n.evicted_indices_.insert(probeIndex());
    n.disk_serve_queue_.push_back(probeIndex());
    n.disk_serve_queued_.insert(probeIndex());
    // "went out whole": a fresh tracker would call this tile due (never sent).
    n.refresh_tracker_.notePublished(probeIndex(), 1.0, true);
    n.catalog_builder_.update(probeIndex(), 5);
  }

  /// Names of the fields that are NOT back at their fresh-node value.
  static std::vector<std::string> staleFields(const CubeBathymetry & n)
  {
    std::vector<std::string> stale;
    auto check = [&stale](bool is_fresh, const char * name) {
        if (!is_fresh) {
          stale.emplace_back(name);
        }
      };
    check(n.last_grid_publish_time_.nanoseconds() == 0, "last_grid_publish_time_");
    check(!n.have_publish_tf_, "have_publish_tf_");
    check(n.last_publish_tf_.header.frame_id.empty(), "last_publish_tf_");
    check(!n.have_vessel_pos_, "have_vessel_pos_");
    check(n.last_vessel_lat_ == 0.0 && n.last_vessel_lon_ == 0.0, "last_vessel_lat/lon_");
    check(n.saving_report_last_s_ == 0.0, "saving_report_last_s_");
    check(
      n.report_window_cells_ == 0 && n.report_full_cells_ == 0 &&
      n.report_refresh_cells_ == 0, "report_*_cells_");
    check(n.evicted_indices_.empty(), "evicted_indices_");
    check(n.disk_serve_queue_.empty() && n.disk_serve_queued_.empty(), "disk_serve_queue_");
    check(n.refresh_tracker_.refreshDue(probeIndex(), 2.0), "refresh_tracker_");
    check(n.catalog_builder_.size() == 0, "catalog_builder_");
    return stale;
  }
};

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

// The same retry with /tf traffic arriving from another node while the failed
// attempts are replaced. FailedConfigureCanBeRetried above passes without the
// `tf_listener_.reset()` before the buffer is replaced (the listener's thread has
// nothing to deliver, so nothing touches the freed buffer). With a publisher
// running, the old listener's subscription callback writes into the buffer that
// the next configure is destroying: under AddressSanitizer this is a
// heap-use-after-free on the unfixed code (confirmed in the pre-push review of
// #164). WITHOUT a sanitizer it usually stays quiet, so this test only has teeth
// in an ASan build (colcon build --cmake-args -DCMAKE_CXX_FLAGS="-fsanitize=address
// -fno-omit-frame-pointer"); it still pins the behaviour (the node recovers).
TEST_F(NodeLifecycle, FailedConfigureCanBeRetriedWhileTfIsPublished)
{
  auto helper = std::make_shared<rclcpp::Node>("tf_traffic_for_lifecycle_test");
  auto pub = helper->create_publisher<tf2_msgs::msg::TFMessage>("/tf", rclcpp::QoS(100));
  std::atomic<bool> stop{false};
  std::thread publisher([&]() {
      tf2_msgs::msg::TFMessage msg;
      msg.transforms.resize(1);
      msg.transforms[0].header.frame_id = "map";
      msg.transforms[0].child_frame_id = "base_link";
      msg.transforms[0].transform.rotation.w = 1.0;
      while (!stop.load()) {
        msg.transforms[0].header.stamp = helper->now();
        pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      }
    });

  auto node = std::make_shared<CubeBathymetry>(
    withParams({rclcpp::Parameter("sonar_info_topic", std::string("not a valid topic"))}));
  for (int attempt = 0; attempt < 300; ++attempt) {
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED)
      << "attempt " << attempt;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  const auto result =
    node->set_parameter(rclcpp::Parameter("sonar_info_topic", std::string("sonar_info")));
  ASSERT_TRUE(result.successful) << result.reason;
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);

  stop = true;
  publisher.join();
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

// A shutdown requested from ACTIVE (or INACTIVE after a refused cleanup) goes
// through on_shutdown, not on_cleanup, and must flush the unsaved tiles too.
TEST_F(NodeLifecycle, ShutdownFromActiveFlushesUnsavedTiles)
{
  const auto dir = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("draft_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    addPatch(node->sheet(), -12.5);
    ASSERT_FALSE(node->sheet()->dirtyGrids().empty());
    ASSERT_FALSE(std::filesystem::exists(dir / "draft")) << "test setup: nothing saved yet";

    EXPECT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
    EXPECT_TRUE(std::filesystem::exists(dir / "draft") &&
      !std::filesystem::is_empty(dir / "draft"))
      << "shutdown must write the tiles accumulated since the last save";
  }
  std::filesystem::remove_all(dir);
}

// Shutdown cannot be refused: with the flush failing it still reaches FINALIZED
// (the error is logged), instead of wedging the node in ERRORPROCESSING.
TEST_F(NodeLifecycle, ShutdownCompletesWhenTheFlushFails)
{
  const auto dir = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("draft_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    addPatch(node->sheet(), -12.5);
    {
      std::ofstream blocker(dir / "draft");
      blocker << "not a directory";
    }
    EXPECT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
  }
  std::filesystem::remove_all(dir);
}

// Cleanup releases the sheet and the catalog instead of holding them until the
// next configure: a reset on a boat that has run for hours should not keep the
// old survey in RAM while the new one is built.
TEST_F(NodeLifecycle, CleanupReleasesTheSheetAndCatalog)
{
  const auto dir = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("draft_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    std::weak_ptr<cube::GeoMapSheet> weak = node->sheet();
    addPatch(node->sheet(), -12.5);
    ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);

    EXPECT_FALSE(node->sheet()) << "cleanup must drop the sheet";
    EXPECT_TRUE(weak.expired()) << "nothing else may keep the old sheet alive";
    EXPECT_EQ(node->catalogSize(), 0u);

    // The release must not cost the saved tiles: they reload on the next configure.
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_TRUE(hasFiniteCell(node->sheet()));
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  std::filesystem::remove_all(dir);
}

// Everything that describes the previous sheet's session is reset by the next
// configure: the publish throttle and cached transform/vessel fix (a stale one
// would suppress or misplace the first window), the saving ledger, evicted-tile
// markers, the disk-serve queue, the refresh tracker and the catalog. The state
// is dirtied first through the test accessor, so each reset is what the
// assertion observes rather than a default that was never changed.
TEST_F(NodeLifecycle, ReconfigureResetsTheSessionState)
{
  auto node = std::make_shared<CubeBathymetry>();
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);

  CubeBathymetryTestAccess::dirtySessionState(*node);
  EXPECT_EQ(CubeBathymetryTestAccess::staleFields(*node).size(), 11u)
    << "test setup: every field should be dirty before the cycle";
  EXPECT_EQ(node->catalogSize(), 1u);

  ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  EXPECT_EQ(node->catalogSize(), 0u) << "cleanup releases the catalog";

  // Dirty it again: cleanup does not reset the cursor fields, configure must.
  CubeBathymetryTestAccess::dirtySessionState(*node);
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  const auto stale = CubeBathymetryTestAccess::staleFields(*node);
  for (const auto & name : stale) {
    ADD_FAILURE() << name << " survived the reconfigure";
  }
  EXPECT_TRUE(stale.empty());
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// read_only parameters (capture_spacing_scale, publish_dirty_subwindow) take their
// launch/YAML override at the first configure, keep it across a cycle, are applied
// to the NEW sheet, and keep refusing runtime sets before and after the cycle.
TEST_F(NodeLifecycle, ReadOnlyOverridesSurviveACycle)
{
  auto node = std::make_shared<CubeBathymetry>(
    withParams(
      {rclcpp::Parameter("capture_spacing_scale", 0.9),
        rclcpp::Parameter("publish_dirty_subwindow", false)}));
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_FLOAT_EQ(node->sheet()->parameters().capture_spacing_scale, 0.9f);
  EXPECT_FALSE(node->get_parameter("publish_dirty_subwindow").as_bool());
  EXPECT_FALSE(
    node->set_parameter(rclcpp::Parameter("capture_spacing_scale", 0.5)).successful);
  EXPECT_FALSE(
    node->set_parameter(rclcpp::Parameter("publish_dirty_subwindow", true)).successful);

  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);

  EXPECT_FLOAT_EQ(node->sheet()->parameters().capture_spacing_scale, 0.9f)
    << "the override must reach the NEW sheet";
  EXPECT_DOUBLE_EQ(node->get_parameter("capture_spacing_scale").as_double(), 0.9);
  EXPECT_FALSE(node->get_parameter("publish_dirty_subwindow").as_bool());
  EXPECT_FALSE(
    node->set_parameter(rclcpp::Parameter("capture_spacing_scale", 0.5)).successful)
    << "read_only must still refuse runtime sets after a cycle";
  EXPECT_FALSE(
    node->set_parameter(rclcpp::Parameter("publish_dirty_subwindow", true)).successful);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}
