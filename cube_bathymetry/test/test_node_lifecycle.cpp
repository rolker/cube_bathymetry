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
//
// The `~/reset` service (cube_bathymetry#167) starts the same fresh session in
// RAM with no lifecycle round trip. Its tests call it through a real
// std_srvs/Trigger client against an in-process SingleThreadedExecutor and share
// one assertion helper with ReconfigureResetsTheSessionState, so a session field
// that one path forgets fails in both.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <utility>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "lifecycle_msgs/msg/state.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_interfaces/msg/sonar_info.hpp"
#include "marine_interfaces/msg/tile_catalog.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "tf2_msgs/msg/tf_message.hpp"

#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
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

  /// `~/reset` handler called directly (no service, no executor): what a null
  /// sheet answers, and what a released service cannot be asked.
  static std::pair<bool, std::string> callResetHandler(CubeBathymetry & n)
  {
    auto response = std::make_shared<std_srvs::srv::Trigger::Response>();
    n.resetService(std::make_shared<std_srvs::srv::Trigger::Request>(), response);
    return {response->success, response->message};
  }

  /// Make buildSession() throw once the candidate sheet exists.
  static void setBuildFault(CubeBathymetry & n, std::function<void()> fault)
  {
    n.build_fault_injector_ = std::move(fault);
  }

  /// Make resetSession() throw after the build, outside the build's own try block.
  static void setResetFault(CubeBathymetry & n, std::function<void()> fault)
  {
    n.reset_fault_injector_ = std::move(fault);
  }

  static void deliverSonarInfo(CubeBathymetry & n, const marine_interfaces::msg::SonarInfo & info)
  {
    n.sonarInfoCallback(info);
  }
  static bool curveApplied(const CubeBathymetry & n) {return n.bs_curve_applied_;}
  static std::size_t storedCurvePoints(const CubeBathymetry & n)
  {
    return n.bs_applied_curve_.points.size();
  }

  static double catalogInterval(const CubeBathymetry & n) {return n.catalog_interval_s_;}
  static double saveInterval(const CubeBathymetry & n) {return n.save_interval_s_;}

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

// Nothing of the previous session may be left (the 11 fields of staleFields),
// whichever way the fresh session was started. Shared by the lifecycle-walk test
// and the `~/reset` tests.
void expectNoStaleSessionState(const CubeBathymetry & node, const std::string & how)
{
  const auto stale = CubeBathymetryTestAccess::staleFields(node);
  for (const auto & name : stale) {
    ADD_FAILURE() << name << " survived " << how;
  }
  EXPECT_TRUE(stale.empty());
  EXPECT_EQ(node.catalogSize(), 0u) << "the catalog survived " << how;
}

// Files under @p dir as {path, size, mtime}: equal before and after means nothing
// on disk was created, written, touched or removed.
std::vector<std::string> listing(const std::filesystem::path & dir)
{
  std::vector<std::string> out;
  for (const auto & entry : std::filesystem::recursive_directory_iterator(dir)) {
    std::string line = entry.path().string();
    if (entry.is_regular_file()) {
      line += " " + std::to_string(entry.file_size());
    }
    line += " " + std::to_string(entry.last_write_time().time_since_epoch().count());
    out.push_back(line);
  }
  std::sort(out.begin(), out.end());
  return out;
}

// A real std_srvs/Trigger client against the node's `~/reset`, with the node and
// the client spun by one in-process SingleThreadedExecutor (what main() runs).
class ResetCaller
{
public:
  explicit ResetCaller(const std::shared_ptr<CubeBathymetry> & node)
  : node_(node),
    helper_(std::make_shared<rclcpp::Node>("reset_caller_" + std::to_string(counter()++))),
    client_(helper_->create_client<std_srvs::srv::Trigger>(
        std::string(node->get_fully_qualified_name()) + "/reset"))
  {
    exe_.add_node(node_->get_node_base_interface());
    exe_.add_node(helper_);
  }

  // The node's graph entry for `reset`, polled: graph updates arrive
  // asynchronously after a service is created or destroyed.
  bool serviceInGraph() const
  {
    const auto services = helper_->get_service_names_and_types_by_node(
      node_->get_name(), node_->get_namespace());
    return services.count(std::string(node_->get_fully_qualified_name()) + "/reset") > 0;
  }

  bool waitForGraph(bool present, std::chrono::milliseconds limit = std::chrono::seconds(5)) const
  {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
      if (serviceInGraph() == present) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return serviceInGraph() == present;
  }

  // {success, message}; fails the test when the service does not answer.
  std::pair<bool, std::string> call()
  {
    if (!client_->wait_for_service(std::chrono::seconds(5))) {
      ADD_FAILURE() << "~/reset is not available";
      return {false, ""};
    }
    auto future = client_->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
    if (exe_.spin_until_future_complete(future, std::chrono::seconds(30)) !=
      rclcpp::FutureReturnCode::SUCCESS)
    {
      ADD_FAILURE() << "~/reset did not answer";
      return {false, ""};
    }
    const auto response = future.get();
    return {response->success, response->message};
  }

private:
  static int & counter()
  {
    static int n = 0;
    return n;
  }

  std::shared_ptr<CubeBathymetry> node_;
  std::shared_ptr<rclcpp::Node> helper_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr client_;
  rclcpp::executors::SingleThreadedExecutor exe_;
};

marine_interfaces::msg::SonarInfo tier2Curve()
{
  marine_interfaces::msg::SonarInfo info;
  info.angular_response_angle_deg = {0.5f, 30.5f};
  info.angular_response_db_rel_nadir = {0.0f, -12.0f};
  info.angular_response_tl = marine_interfaces::msg::SonarInfo::ANGULAR_RESPONSE_TL_REMOVED;
  info.angular_response_absorption_db_per_m = 0.00025f;
  return info;
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

// A shutdown from ACTIVE releases what configure/activate created, as cleanup
// does. Before, the timers outlived it and a FINALIZED node went on publishing
// its coverage catalog every catalog_interval. Spun in an executor so the timers
// can actually fire; the control phase proves the listener would see them.
TEST_F(NodeLifecycle, ShutdownFromActiveStopsAllOutput)
{
  auto node = std::make_shared<CubeBathymetry>(
    withParams({rclcpp::Parameter("catalog_interval", 0.1)}));
  auto listener = std::make_shared<rclcpp::Node>("catalog_listener");
  int catalogs = 0;
  auto sub = listener->create_subscription<marine_interfaces::msg::TileCatalog>(
    std::string(node->get_fully_qualified_name()) + "/coverage_catalog",
    rclcpp::QoS(10).reliable(),
    [&catalogs](marine_interfaces::msg::TileCatalog::ConstSharedPtr) {++catalogs;});
  rclcpp::executors::SingleThreadedExecutor exe;
  exe.add_node(node->get_node_base_interface());
  exe.add_node(listener);
  // Spin until `done` or the deadline, on this thread: transitions below run
  // between spins, never concurrently with a callback.
  auto spinUntil = [&exe](std::chrono::milliseconds limit, auto done) {
      const auto end = std::chrono::steady_clock::now() + limit;
      while (std::chrono::steady_clock::now() < end && !done()) {
        exe.spin_some(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    };
  auto never = [] {return false;};

  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  spinUntil(std::chrono::milliseconds(5000), [&catalogs] {return catalogs >= 3;});
  ASSERT_GE(catalogs, 3) << "control: the listener must see the catalog while ACTIVE";

  ASSERT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
  spinUntil(std::chrono::milliseconds(200), never);  // drain anything in flight
  catalogs = 0;
  spinUntil(std::chrono::milliseconds(1000), never);  // ten catalog intervals
  EXPECT_EQ(catalogs, 0) << "a FINALIZED node must not keep publishing its catalog";
  EXPECT_FALSE(node->sheet()) << "shutdown must release the sheet, as cleanup does";
}

// SIGINT/SIGTERM (Ctrl-C, or ros2 launch stopping its nodes) shuts the context
// down and returns from spin() with no lifecycle transition, so on_shutdown never
// runs. spinUntilStopped() -- what main() runs -- must still write the tiles
// accumulated since the last periodic save. The context is shut down directly,
// which is what rclcpp's signal handler does; a private context keeps the suite's
// global one alive for the other tests.
TEST_F(NodeLifecycle, ContextShutdownFlushesUnsavedTiles)
{
  const auto dir = makeTempDir();
  {
    auto context = std::make_shared<rclcpp::Context>();
    context->init(0, nullptr);
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("draft_dir", dir.string())}).context(context));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    addPatch(node->sheet(), -12.5);
    ASSERT_FALSE(node->sheet()->dirtyGrids().empty());
    ASSERT_FALSE(std::filesystem::exists(dir / "draft")) << "test setup: nothing saved yet";

    rclcpp::ExecutorOptions options;
    options.context = context;
    rclcpp::executors::SingleThreadedExecutor exe(options);
    std::thread spinner([&node, &exe] {spinUntilStopped(node, exe);});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    context->shutdown("test: what the SIGINT handler does");
    spinner.join();

    EXPECT_TRUE(std::filesystem::exists(dir / "draft") &&
      !std::filesystem::is_empty(dir / "draft"))
      << "stopping on a signal must write the tiles accumulated since the last save";
    EXPECT_TRUE(node->sheet()->dirtyGrids().empty());
  }
  std::filesystem::remove_all(dir);
}

// A zero, negative, non-finite or sub-millisecond timer interval would make its
// wall timer fire continuously and starve the executor, and a huge one makes
// timer creation throw in on_activate; configure falls back to the default.
class TimerIntervalFallback : public NodeLifecycle,
  public ::testing::WithParamInterface<double>
{
};

TEST_P(TimerIntervalFallback, UnusableIntervalsFallBackToDefaults)
{
  const auto dir = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({
      rclcpp::Parameter("draft_dir", dir.string()),
      rclcpp::Parameter("catalog_interval", GetParam()),
      rclcpp::Parameter("save_interval", GetParam())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_EQ(CubeBathymetryTestAccess::catalogInterval(*node), 5.0);
    EXPECT_EQ(CubeBathymetryTestAccess::saveInterval(*node), 30.0);
    // The parameter store reports the value in use, not the rejected one.
    EXPECT_EQ(node->get_parameter("catalog_interval").as_double(), 5.0);
    EXPECT_EQ(node->get_parameter("save_interval").as_double(), 30.0);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    EXPECT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
  }
  std::filesystem::remove_all(dir);
}

INSTANTIATE_TEST_SUITE_P(
  NodeLifecycle, TimerIntervalFallback,
  ::testing::Values(
    0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
    std::numeric_limits<double>::infinity(), 1e300, 1e-10));

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
  expectNoStaleSessionState(*node, "the reconfigure");
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

// ---- `~/reset` (cube_bathymetry#167) ----

// The service starts the same fresh session as a reconfigure: every one of the
// session fields the lifecycle test dirties is back at its fresh value, the sheet
// and catalog are new, and the node stays active. Same assertion helper as
// ReconfigureResetsTheSessionState.
TEST_F(NodeLifecycle, ResetServiceResetsTheSessionState)
{
  auto node = std::make_shared<CubeBathymetry>();
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  ResetCaller caller(node);

  std::weak_ptr<cube::GeoMapSheet> old_sheet = node->sheet();
  const auto old_ptr = node->sheet().get();
  addPatch(node->sheet(), -12.5);
  ASSERT_TRUE(hasFiniteCell(node->sheet())) << "test setup: CUBE produced no estimate";
  CubeBathymetryTestAccess::dirtySessionState(*node);
  ASSERT_EQ(CubeBathymetryTestAccess::staleFields(*node).size(), 11u)
    << "test setup: every field should be dirty before the reset";
  ASSERT_EQ(node->catalogSize(), 1u);

  const auto [ok, message] = caller.call();
  EXPECT_TRUE(ok) << message;
  EXPECT_NE(message.find("tile(s) dropped from RAM"), std::string::npos) << message;
  EXPECT_NE(message.find("node is active"), std::string::npos) << message;
  EXPECT_NE(message.find("no tile written to disk"), std::string::npos) << message;
  EXPECT_NE(message.find("renames a legacy survey/ directory"), std::string::npos)
    << "the claim must carry its qualification: " << message;

  expectNoStaleSessionState(*node, "the reset service");
  EXPECT_EQ(stateId(*node), State::PRIMARY_STATE_ACTIVE);
  ASSERT_TRUE(node->sheet());
  EXPECT_NE(node->sheet().get(), old_ptr) << "the service must build a new sheet";
  EXPECT_TRUE(old_sheet.expired()) << "nothing may keep the old sheet alive";
  // The fresh-sheet claim of EmptyStartWithoutPersistence, through the service.
  EXPECT_TRUE(node->sheet()->grids().empty());
  EXPECT_FALSE(hasFiniteCell(node->sheet()));
  EXPECT_TRUE(node->sheet()->dirtyGrids().empty());

  // The new sheet accumulates only new data.
  addPatch(node->sheet(), -20.0);
  ASSERT_TRUE(hasFiniteCell(node->sheet()));
  for (const auto & grid : node->sheet()->grids()) {
    for (const auto & v : grid->values()) {
      if (std::isfinite(v.depth)) {
        EXPECT_LT(v.depth, -15.0f) << "a cell of the old session came back";
      }
    }
  }
  EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// An inactive node has a sheet too. Resetting it is harmless, and the response
// says the node publishes nothing, so a caller cannot read "reset" as "publishing".
TEST_F(NodeLifecycle, ResetWhileInactiveSaysSo)
{
  auto node = std::make_shared<CubeBathymetry>();
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ResetCaller caller(node);
  const auto [ok, message] = caller.call();
  EXPECT_TRUE(ok) << message;
  EXPECT_NE(message.find("node is inactive: no grid until activated"), std::string::npos)
    << message;
  EXPECT_EQ(stateId(*node), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// The sheet-level settings come from the stored configuration, not from a bare
// GeoMapSheet: the capture-spacing scale (a launch override here), and the
// backscatter curve that arrived on the latched sonar_info topic and is NOT
// redelivered to the surviving subscription. Twice in a row, so a curve that is
// moved out of the stored member instead of copied is caught on the second reset.
TEST_F(NodeLifecycle, ResetKeepsSheetLevelSettings)
{
  auto node = std::make_shared<CubeBathymetry>(
    withParams({rclcpp::Parameter("capture_spacing_scale", 0.9)}));
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  CubeBathymetryTestAccess::deliverSonarInfo(*node, tier2Curve());
  ASSERT_TRUE(CubeBathymetryTestAccess::curveApplied(*node));

  const auto before = node->sheet()->parameters();
  ASSERT_FLOAT_EQ(before.capture_spacing_scale, 0.9f);
  ASSERT_EQ(before.angular_response_curve.size(), 2u) << "test setup: the curve was not applied";
  ASSERT_TRUE(before.backscatter_tl_removed);

  ResetCaller caller(node);
  for (int round = 1; round <= 2; ++round) {
    const auto [ok, message] = caller.call();
    ASSERT_TRUE(ok) << message;
    const auto after = node->sheet()->parameters();
    EXPECT_FLOAT_EQ(after.capture_spacing_scale, 0.9f) << "reset " << round;
    EXPECT_EQ(after.backscatter_angle_correction, before.backscatter_angle_correction)
      << "reset " << round;
    EXPECT_EQ(after.angular_response_curve, before.angular_response_curve) << "reset " << round;
    EXPECT_EQ(after.backscatter_tl_removed, before.backscatter_tl_removed) << "reset " << round;
    EXPECT_FLOAT_EQ(
      after.backscatter_absorption_db_per_m, before.backscatter_absorption_db_per_m)
      << "reset " << round;
    // The stored curve is intact, so the next heartbeat with the same curve is
    // recognised as the same (silent) and the next reset can use it again.
    EXPECT_TRUE(CubeBathymetryTestAccess::curveApplied(*node)) << "reset " << round;
    EXPECT_EQ(CubeBathymetryTestAccess::storedCurvePoints(*node), 2u) << "reset " << round;
  }
  CubeBathymetryTestAccess::deliverSonarInfo(*node, tier2Curve());  // a heartbeat
  EXPECT_EQ(node->sheet()->parameters().angular_response_curve, before.angular_response_curve);
  EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// With draft_dir set the saved tiles would reload (ADR-0001), so the sheet would
// not be fresh, and dropping the unsaved ones would break "persist before drop":
// the service refuses, says why, and changes NOTHING -- not the sheet, not the
// resident tiles, not a byte on disk.
TEST_F(NodeLifecycle, ResetIsRefusedWhileDraftDirIsSet)
{
  const auto dir = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("draft_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    addPatch(node->sheet(), -12.5);
    ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);  // flushes to disk
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    const auto saved = listing(dir);
    ASSERT_FALSE(saved.empty()) << "test setup: nothing was written to the draft store";
    const auto sheet_before = node->sheet();
    const auto resident_before = sheet_before->residentTileCount();
    ASSERT_GT(resident_before, 0u);
    CubeBathymetryTestAccess::dirtySessionState(*node);

    ResetCaller caller(node);
    auto [ok, message] = caller.call();
    EXPECT_FALSE(ok);
    EXPECT_NE(message.find(dir.string()), std::string::npos) << "names the directory: " << message;
    EXPECT_NE(message.find("saved tiles would reload"), std::string::npos) << message;
    EXPECT_NE(message.find("--allow-persistence"), std::string::npos) << message;
    EXPECT_EQ(node->sheet().get(), sheet_before.get()) << "a refusal must keep the sheet";
    EXPECT_EQ(node->sheet()->residentTileCount(), resident_before);
    EXPECT_TRUE(hasFiniteCell(node->sheet()));
    EXPECT_EQ(CubeBathymetryTestAccess::staleFields(*node).size(), 11u)
      << "a refusal must not touch the session state";
    EXPECT_EQ(listing(dir), saved) << "a refusal must not change the draft directory";

    // The member decides, not the writable parameter: clearing the parameter after
    // configure does not turn persistence off, so the refusal stands.
    ASSERT_TRUE(node->set_parameter(rclcpp::Parameter("draft_dir", std::string(""))).successful);
    std::tie(ok, message) = caller.call();
    EXPECT_FALSE(ok) << "draft_dir cleared at runtime must not unlock the reset: " << message;
    EXPECT_EQ(node->sheet().get(), sheet_before.get());
    EXPECT_EQ(listing(dir), saved);
    EXPECT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
  }
  std::filesystem::remove_all(dir);

  // The other way round: configured with no draft_dir, a path set afterwards is a
  // configuration the node is not running with, so it does not refuse -- and the
  // reset creates nothing there.
  const auto later = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>();
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    ASSERT_TRUE(
      node->set_parameter(rclcpp::Parameter("draft_dir", later.string())).successful);
    ResetCaller caller(node);
    const auto [ok, message] = caller.call();
    EXPECT_TRUE(ok) << message;
    EXPECT_TRUE(std::filesystem::is_empty(later)) << "the reset must not write to disk";
    EXPECT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
  }
  std::filesystem::remove_all(later);
}

// "Service absent" reads "not configured": the service is created by configure,
// answers in inactive and active, and is gone after cleanup and after shutdown.
// A service is not state-gated, so the handler itself refuses on a null sheet.
TEST_F(NodeLifecycle, ResetServiceExistsOnlyWhileConfigured)
{
  auto node = std::make_shared<CubeBathymetry>();
  ResetCaller caller(node);
  EXPECT_TRUE(caller.waitForGraph(false)) << "no service before configure";

  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_TRUE(caller.waitForGraph(true)) << "inactive";
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  EXPECT_TRUE(caller.waitForGraph(true)) << "active";
  ASSERT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  EXPECT_TRUE(caller.waitForGraph(false)) << "no service after cleanup";

  const auto [ok, message] = CubeBathymetryTestAccess::callResetHandler(*node);
  EXPECT_FALSE(ok);
  EXPECT_NE(message.find("not configured"), std::string::npos) << message;

  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_TRUE(caller.waitForGraph(true)) << "back after a second configure";
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  ASSERT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
  EXPECT_TRUE(caller.waitForGraph(false)) << "no service after shutdown";
}

// A configure that throws after the service would have been created returns the
// node to UNCONFIGURED without on_cleanup. The service is created last, so the
// failed configure leaves none behind and "no service" still means "not configured".
TEST_F(NodeLifecycle, ConfigureThatThrowsLeavesNoResetService)
{
  auto node = std::make_shared<CubeBathymetry>(
    withParams({rclcpp::Parameter("sonar_info_topic", std::string("not a valid topic"))}));
  ResetCaller caller(node);
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);
  EXPECT_FALSE(caller.waitForGraph(true, std::chrono::milliseconds(500)))
    << "a failed configure must not leave ~/reset in the graph";
}

// The session is built aside and swapped in only when complete: when building it
// fails, the running session is exactly as it was and the service says so.
TEST_F(NodeLifecycle, FailedResetLeavesTheOldSessionIntact)
{
  auto node = std::make_shared<CubeBathymetry>();
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  addPatch(node->sheet(), -12.5);
  CubeBathymetryTestAccess::dirtySessionState(*node);
  const auto sheet_before = node->sheet();
  const auto resident_before = sheet_before->residentTileCount();
  ASSERT_GT(resident_before, 0u);

  CubeBathymetryTestAccess::setBuildFault(*node, [] {throw std::runtime_error("disk on fire");});
  ResetCaller caller(node);
  auto [ok, message] = caller.call();
  EXPECT_FALSE(ok);
  EXPECT_NE(message.find("disk on fire"), std::string::npos) << message;
  EXPECT_NE(message.find("unchanged"), std::string::npos) << message;
  EXPECT_EQ(node->sheet().get(), sheet_before.get());
  EXPECT_EQ(node->sheet()->residentTileCount(), resident_before);
  EXPECT_TRUE(hasFiniteCell(node->sheet()));
  EXPECT_EQ(CubeBathymetryTestAccess::staleFields(*node).size(), 11u)
    << "a failed reset must leave every session field as it was";
  EXPECT_EQ(node->catalogSize(), 1u);
  EXPECT_EQ(stateId(*node), State::PRIMARY_STATE_ACTIVE);

  // And the node is not wedged: without the fault the same call succeeds.
  CubeBathymetryTestAccess::setBuildFault(*node, nullptr);
  std::tie(ok, message) = caller.call();
  EXPECT_TRUE(ok) << message;
  expectNoStaleSessionState(*node, "the retried reset");
  EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// A throw that is not a std::exception while building the session must not escape
// the service callback (that would end the process, with the boat's CUBE node on
// it): the service answers success=false, the session is untouched, and the next
// call works.
TEST_F(NodeLifecycle, ANonStdThrowWhileBuildingTheResetIsContained)
{
  auto node = std::make_shared<CubeBathymetry>();
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  addPatch(node->sheet(), -12.5);
  CubeBathymetryTestAccess::dirtySessionState(*node);
  const auto sheet_before = node->sheet();

  CubeBathymetryTestAccess::setBuildFault(*node, [] {throw 42;});
  ResetCaller caller(node);
  auto [ok, message] = caller.call();
  EXPECT_FALSE(ok);
  EXPECT_NE(message.find("unknown exception"), std::string::npos) << message;
  EXPECT_NE(message.find("unchanged"), std::string::npos) << message;
  EXPECT_EQ(node->sheet().get(), sheet_before.get());
  EXPECT_EQ(CubeBathymetryTestAccess::staleFields(*node).size(), 11u);

  CubeBathymetryTestAccess::setBuildFault(*node, nullptr);
  std::tie(ok, message) = caller.call();
  EXPECT_TRUE(ok) << message;
  EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// The prior store is read again by every reset: the new sheet's predicted surface
// is seeded, so the blunder gate is active from the first ping of the new session.
// Control: the same false-deep patch is accepted at those cells by a sheet with no
// prior. (Only the cells the prior covers are gated; the deep patch's wide capture
// radius also reaches cells it does not cover, so the check is per primed cell.)
TEST_F(NodeLifecycle, ResetReprimesPriorStore)
{
  const auto dir = makeTempDir();
  auto source = std::make_shared<cube::GeoMapSheet>(1.0f);
  addPatch(source, -10.0);
  auto tiles = cube::mapSheetToTiles(*source);
  ASSERT_FALSE(tiles.empty());
  {
    auto store = marine_bathymetry_store::BathymetryStore::fromCellSize(
      1.0f, /*reference_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Reference, tiles);
    ASSERT_GT(marine_bathymetry_store::save(store, dir.string()), 0u);
  }
  // Cells of the prior that end with a settled deep (< -100 m) estimate.
  auto deepAtPrimedCells = [&tiles](const std::shared_ptr<cube::GeoMapSheet> & sheet,
    std::size_t & primed_cells) {
      std::size_t deep = 0;
      primed_cells = 0;
      for (const auto & gt : tiles) {
        auto grid = sheet->gridAt(gt.first);
        if (!grid) {
          continue;
        }
        const auto vals = grid->values();
        const auto & depth = gt.second.depthBand();
        gggs::CellAreaIterator it(gt.first);
        std::size_t k = 0;
        for (; it.valid() && k < depth.size() && k < vals.size(); it.next(), ++k) {
          if (std::isnan(depth[k])) {
            continue;
          }
          ++primed_cells;
          if (std::isfinite(vals[k].depth) && vals[k].depth < -100.0f) {
            ++deep;
          }
        }
      }
      return deep;
    };
  std::size_t primed_cells = 0;
  {
    // Control: no prior, so the deep blunder is accepted at the same cells.
    auto bare = std::make_shared<CubeBathymetry>();
    ASSERT_EQ(bare->configure().id(), State::PRIMARY_STATE_INACTIVE);
    addPatch(bare->sheet(), -150.0);
    std::size_t accepted = 0;
    for (const auto & grid : bare->sheet()->grids()) {
      for (const auto & v : grid->values()) {
        accepted += (std::isfinite(v.depth) && v.depth < -100.0f) ? 1 : 0;
      }
    }
    ASSERT_GT(accepted, 0u) << "control: without a prior the deep patch must be accepted";
    EXPECT_EQ(bare->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("prior_store_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    ASSERT_FALSE(node->sheet()->grids().empty()) << "test setup: the prior was not primed";
    addPatch(node->sheet(), -10.0);

    ResetCaller caller(node);
    const auto [ok, message] = caller.call();
    ASSERT_TRUE(ok) << message;
    EXPECT_NE(message.find("prior_store_dir primed"), std::string::npos) << message;
    EXPECT_NE(message.find("Reference"), std::string::npos) << message;

    EXPECT_FALSE(node->sheet()->grids().empty()) << "the prior tiles are primed again";
    EXPECT_TRUE(node->sheet()->dirtyGrids().empty()) << "priming must not mark tiles dirty";
    EXPECT_FALSE(hasFiniteCell(node->sheet())) << "the prior gates; it never fills";
    addPatch(node->sheet(), -150.0);
    const auto deep = deepAtPrimedCells(node->sheet(), primed_cells);
    EXPECT_GT(primed_cells, 0u) << "the re-primed sheet must carry the prior's cells";
    EXPECT_EQ(deep, 0u)
      << "the false-deep patch must be rejected at every cell the prior covers";
    EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  std::filesystem::remove_all(dir);
}

// The response says no TILE is written, not "nothing on disk": loading the prior
// store renames a legacy `survey/` layer directory to `processed/` (ADR-0010 D8),
// exactly as configure does. This pins that the qualification is true, so the
// wording cannot drift back to an unconditional claim while the behaviour stays.
TEST_F(NodeLifecycle, ResetMayRenameALegacyPriorSurveyDirLikeConfigure)
{
  const auto dir = makeTempDir();
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("prior_store_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    // A pre-D8 store appears after configure, so only the reset's load meets it.
    std::filesystem::create_directories(dir / "survey");
    ASSERT_FALSE(std::filesystem::exists(dir / "processed"));

    ResetCaller caller(node);
    const auto [ok, message] = caller.call();
    EXPECT_TRUE(ok) << message;
    EXPECT_FALSE(std::filesystem::exists(dir / "survey")) << "the legacy dir was not renamed";
    EXPECT_TRUE(std::filesystem::exists(dir / "processed"));
    EXPECT_NE(message.find("renames a legacy survey/ directory"), std::string::npos)
      << "the response must not claim that nothing on disk changed: " << message;
    EXPECT_EQ(message.find("nothing on disk"), std::string::npos) << message;
    EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  std::filesystem::remove_all(dir);
}

// A prior store that cannot be loaded does not fail a CONFIGURE (the node runs
// ungated and warns: a misconfigured prior must not take down live perception at
// start-up), but a RESET would replace a gated running session with an ungated
// one. So the reset fails and keeps the old session; once the prior reads again,
// the reset works.
TEST_F(NodeLifecycle, ResetWithAnUnloadablePriorKeepsTheGatedSession)
{
  const auto dir = makeTempDir();
  auto source = std::make_shared<cube::GeoMapSheet>(1.0f);
  addPatch(source, -10.0);
  auto tiles = cube::mapSheetToTiles(*source);
  ASSERT_FALSE(tiles.empty());
  {
    auto store = marine_bathymetry_store::BathymetryStore::fromCellSize(
      1.0f, /*reference_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Reference, tiles);
    ASSERT_GT(marine_bathymetry_store::save(store, dir.string()), 0u);
  }
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("prior_store_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    ASSERT_FALSE(node->sheet()->grids().empty()) << "test setup: the prior was not primed";
    CubeBathymetryTestAccess::dirtySessionState(*node);
    const auto sheet_before = node->sheet();
    const auto resident_before = sheet_before->residentTileCount();

    // A file load() cannot parse: no level in its name.
    const auto bogus = dir / "reference" / "bogus.tif";
    {
      std::ofstream(bogus) << "not a tile";
    }
    ResetCaller caller(node);
    auto [ok, message] = caller.call();
    EXPECT_FALSE(ok) << "an ungated session replaced a gated one: " << message;
    EXPECT_NE(message.find("could not be loaded"), std::string::npos) << message;
    EXPECT_NE(message.find(dir.string()), std::string::npos) << message;
    EXPECT_NE(message.find("unchanged"), std::string::npos) << message;
    EXPECT_EQ(node->sheet().get(), sheet_before.get()) << "the old session must be kept";
    EXPECT_EQ(node->sheet()->residentTileCount(), resident_before);
    EXPECT_EQ(CubeBathymetryTestAccess::staleFields(*node).size(), 11u)
      << "a failed reset must leave every session field as it was";

    // Configure keeps its warn-and-continue behaviour for the same prior.
    auto fresh_node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("prior_store_dir", dir.string())}));
    EXPECT_EQ(fresh_node->configure().id(), State::PRIMARY_STATE_INACTIVE)
      << "configure must still run ungated on an unreadable prior";
    EXPECT_EQ(fresh_node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);

    // The prior reads again: the reset works.
    std::filesystem::remove(bogus);
    std::tie(ok, message) = caller.call();
    EXPECT_TRUE(ok) << message;
    EXPECT_NE(message.find("prior_store_dir primed"), std::string::npos) << message;
    EXPECT_NE(node->sheet().get(), sheet_before.get());
    EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  std::filesystem::remove_all(dir);
}

// load() does not throw for a prior directory that is missing, unmounted, empty or
// of the wrong level: it primes zero tiles. A reset must not replace a session that
// HAS the blunder gate with one that has none, so it refuses; once the prior is back
// the reset works. A node whose prior never primed anything is not locked out.
TEST_F(NodeLifecycle, ResetWithAPriorThatPrimesNothingKeepsTheGatedSession)
{
  const auto dir = makeTempDir();
  auto source = std::make_shared<cube::GeoMapSheet>(1.0f);
  addPatch(source, -10.0);
  auto tiles = cube::mapSheetToTiles(*source);
  ASSERT_FALSE(tiles.empty());
  {
    auto store = marine_bathymetry_store::BathymetryStore::fromCellSize(
      1.0f, /*reference_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Reference, tiles);
    ASSERT_GT(marine_bathymetry_store::save(store, dir.string()), 0u);
  }
  const auto moved = std::filesystem::path(dir.string() + ".moved");
  {
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("prior_store_dir", dir.string())}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    ASSERT_FALSE(node->sheet()->grids().empty()) << "test setup: the prior was not primed";
    CubeBathymetryTestAccess::dirtySessionState(*node);
    const auto sheet_before = node->sheet();
    const auto resident_before = sheet_before->residentTileCount();

    // The prior goes away after configure (an unmounted disk): no throw, zero tiles.
    std::filesystem::rename(dir, moved);
    ResetCaller caller(node);
    auto [ok, message] = caller.call();
    EXPECT_FALSE(ok) << "an ungated session replaced a gated one: " << message;
    EXPECT_NE(message.find("primed no tiles"), std::string::npos) << message;
    EXPECT_NE(message.find(dir.string()), std::string::npos) << message;
    EXPECT_NE(message.find("unchanged"), std::string::npos) << message;
    EXPECT_EQ(node->sheet().get(), sheet_before.get()) << "the old session must be kept";
    EXPECT_EQ(node->sheet()->residentTileCount(), resident_before);
    EXPECT_EQ(CubeBathymetryTestAccess::staleFields(*node).size(), 11u)
      << "a failed reset must leave every session field as it was";

    // The prior is back: the reset works.
    std::filesystem::rename(moved, dir);
    std::tie(ok, message) = caller.call();
    EXPECT_TRUE(ok) << message;
    EXPECT_NE(node->sheet().get(), sheet_before.get());
    EXPECT_FALSE(node->sheet()->grids().empty());
    EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }

  // A node whose prior never primed anything has no gate to lose, so it is not
  // locked out of resetting.
  {
    const auto missing = dir.string() + ".never_existed";
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("prior_store_dir", missing)}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    const auto sheet_before = node->sheet();
    ResetCaller caller(node);
    const auto [ok, message] = caller.call();
    EXPECT_TRUE(ok) << "a node that never had a gate was locked out: " << message;
    EXPECT_NE(node->sheet().get(), sheet_before.get());
    EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  std::filesystem::remove_all(dir);
  std::filesystem::remove_all(moved);
}

// The handler is contained as a whole, not only its build: a throw after the build
// (the copies, the message, the swap preparation run at the RAM peak) must answer
// success=false with the old session intact instead of escaping the service callback
// and ending the process.
TEST_F(NodeLifecycle, AThrowAfterTheBuildIsContainedByTheHandler)
{
  auto node = std::make_shared<CubeBathymetry>();
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
  addPatch(node->sheet(), -12.5);
  CubeBathymetryTestAccess::dirtySessionState(*node);
  const auto sheet_before = node->sheet();

  ResetCaller caller(node);
  CubeBathymetryTestAccess::setResetFault(*node, [] {throw std::bad_alloc();});
  auto [ok, message] = caller.call();
  EXPECT_FALSE(ok);
  EXPECT_NE(message.find("unchanged"), std::string::npos) << message;
  EXPECT_EQ(node->sheet().get(), sheet_before.get());
  EXPECT_EQ(CubeBathymetryTestAccess::staleFields(*node).size(), 11u);

  CubeBathymetryTestAccess::setResetFault(*node, [] {throw 42;});
  std::tie(ok, message) = caller.call();
  EXPECT_FALSE(ok);
  EXPECT_NE(message.find("unknown exception"), std::string::npos) << message;
  EXPECT_EQ(node->sheet().get(), sheet_before.get());

  CubeBathymetryTestAccess::setResetFault(*node, nullptr);
  std::tie(ok, message) = caller.call();
  EXPECT_TRUE(ok) << message;
  EXPECT_NE(node->sheet().get(), sheet_before.get());
  EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}

// The swap in the service handler has no mutex. That is safe because every
// callback of the node's own entities (pings, timers, subscriptions, the service)
// is in the ONE default mutually exclusive callback group that the node's
// SingleThreadedExecutor spins, so none can run while the handler swaps the sheet.
// The only other groups are made by libraries for their own threads and are not
// added to the node's executor: the tf2_ros::TransformListener's (/tf, /tf_static),
// and, under use_sim_time, rclcpp's clock thread (/clock). Each only feeds a buffer
// or the clock, neither of which a reset replaces. This pins both halves: the
// executor spins exactly one mutually exclusive group, and every group NOT added to
// the executor holds no service, client or timer and subscribes only to one of those
// three topics (its waitables are those subscriptions' QoS event handlers), so a
// callback group that is spun on a thread of its own and touches the sheet fails here
// instead of racing a ping against the swap. Run with and without use_sim_time,
// because the clock group exists only under it.
// What the callback groups of a node hold, split by whether the node's own executor
// spins them.
struct GroupAudit
{
  int executor_groups = 0;       // spun with the node (automatically added)
  int unspun_groups = 0;         // created for a thread of their own
  bool all_exclusive = true;
  bool executor_group_is_default = true;
  std::set<std::string> unspun_topics;  // subscriptions in unspun groups
  std::size_t unspun_services_clients_timers = 0;
  /// Waitables in groups outside the executor that are not a QoS event handler of one
  /// of the subscriptions in those groups.
  std::size_t unspun_foreign_waitables = 0;
};

GroupAudit auditGroups(CubeBathymetry & node)
{
  GroupAudit audit;
  const auto base = node.get_node_base_interface();
  std::set<const rclcpp::Waitable *> event_handlers;
  std::vector<const rclcpp::Waitable *> waitables;
  base->for_each_callback_group(
    [&](rclcpp::CallbackGroup::SharedPtr group) {
      audit.all_exclusive = audit.all_exclusive &&
      group->type() == rclcpp::CallbackGroupType::MutuallyExclusive;
      if (group->automatically_add_to_executor_with_node()) {
        ++audit.executor_groups;
        audit.executor_group_is_default = audit.executor_group_is_default &&
        group == base->get_default_callback_group();
        return;
      }
      ++audit.unspun_groups;
      group->collect_all_ptrs(
        [&](const rclcpp::SubscriptionBase::SharedPtr & sub) {
          audit.unspun_topics.insert(sub->get_topic_name());
          for (const auto & entry : sub->get_event_handlers()) {
            event_handlers.insert(entry.second.get());
          }
        },
        [&](const rclcpp::ServiceBase::SharedPtr &) {++audit.unspun_services_clients_timers;},
        [&](const rclcpp::ClientBase::SharedPtr &) {++audit.unspun_services_clients_timers;},
        [&](const rclcpp::TimerBase::SharedPtr &) {++audit.unspun_services_clients_timers;},
        // A waitable (an action server, say) is told apart from the QoS event handlers
        // of the subscriptions in the same groups, which are the only ones allowed.
        [&](const rclcpp::Waitable::SharedPtr & waitable) {
          waitables.push_back(waitable.get());
        });
    });
  for (const auto * waitable : waitables) {
    if (event_handlers.count(waitable) == 0) {
      ++audit.unspun_foreign_waitables;
    }
  }
  return audit;
}

TEST_F(NodeLifecycle, AllCallbacksShareTheDefaultGroup)
{
  for (const bool sim_time : {false, true}) {
    SCOPED_TRACE(sim_time ? "use_sim_time" : "wall clock");
    auto node = std::make_shared<CubeBathymetry>(
      withParams({rclcpp::Parameter("use_sim_time", sim_time)}));
    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
    const auto audit = auditGroups(*node);
    EXPECT_EQ(audit.executor_groups, 1)
      << "the node's executor must spin exactly one callback group";
    EXPECT_TRUE(audit.executor_group_is_default)
      << "a group other than the default is spun by the node's executor";
    EXPECT_TRUE(audit.all_exclusive) << "no reentrant callback group";
    EXPECT_GE(audit.unspun_groups, 1) << "test setup: the TF listener's group was not found";
    EXPECT_EQ(audit.unspun_services_clients_timers, 0u)
      << "a group that is not in the executor holds a service, client or timer";
    EXPECT_EQ(audit.unspun_foreign_waitables, 0u)
      << "a group that is not in the executor holds a waitable (an action server, say) "
         "that is not a QoS event handler of one of its subscriptions";
    const std::set<std::string> allowed = {"/tf", "/tf_static", "/clock"};
    for (const auto & topic : audit.unspun_topics) {
      EXPECT_EQ(allowed.count(topic), 1u)
        << "a callback group outside the executor holds a subscription to '" << topic
        << "'; only /tf, /tf_static and /clock may be there";
    }
    EXPECT_EQ(audit.unspun_topics.count("/tf"), 1u);
    if (sim_time) {
      EXPECT_EQ(audit.unspun_topics.count("/clock"), 1u)
        << "test setup: under use_sim_time the clock group should be present";
    }
    EXPECT_EQ(node->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
}

// A waitable of no use: stands for an action server or any other waitable that a
// callback group outside the executor could hold.
class InertWaitable : public rclcpp::Waitable
{
public:
  std::shared_ptr<void> take_data() override {return nullptr;}
  void execute(const std::shared_ptr<void> &) override {}
};

// The control for the audit above: groups of the kinds it exists to catch are
// reported, so a clean result means something. A reentrant group and an extra
// default-spun one are counted; a group outside the executor with a timer or a
// subscription to another topic is named.
TEST_F(NodeLifecycle, TheCallbackGroupAuditNoticesWhatItIsFor)
{
  auto node = std::make_shared<CubeBathymetry>();
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  const auto clean = auditGroups(*node);
  ASSERT_EQ(clean.executor_groups, 1);

  auto reentrant = node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  auto own_thread = node->create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive, false);
  auto timer = node->create_wall_timer(std::chrono::hours(1), [] {}, own_thread);
  rclcpp::SubscriptionOptions options;
  options.callback_group = own_thread;
  auto sub = node->create_subscription<marine_interfaces::msg::SonarInfo>(
    "/pings_on_my_own_thread", 1, [](marine_interfaces::msg::SonarInfo::ConstSharedPtr) {},
    options);

  const auto audit = auditGroups(*node);
  EXPECT_EQ(audit.executor_groups, 2) << "an extra group spun by the executor is counted";
  EXPECT_FALSE(audit.all_exclusive) << "a reentrant group is noticed";
  EXPECT_EQ(audit.unspun_groups, clean.unspun_groups + 1);
  EXPECT_GE(audit.unspun_services_clients_timers, 1u) << "a timer outside the executor is noticed";
  EXPECT_EQ(audit.unspun_topics.count("/pings_on_my_own_thread"), 1u)
    << "a subscription outside the executor is named";
  EXPECT_EQ(audit.unspun_foreign_waitables, 0u)
    << "the QoS event handlers of a subscription are not foreign";

  // A waitable that is not a subscription's event handler is reported.
  auto waitable = std::make_shared<InertWaitable>();
  node->get_node_waitables_interface()->add_waitable(waitable, own_thread);
  EXPECT_EQ(auditGroups(*node).unspun_foreign_waitables, 1u)
    << "a waitable outside the executor was not noticed";
  EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
}
