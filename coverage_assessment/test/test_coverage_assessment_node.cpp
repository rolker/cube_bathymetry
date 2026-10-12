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

// Node-level tests of coverage_assessment (#164): the real lifecycle node, a
// static/dynamic TF broadcaster, synthetic GridMap and SurveyArea publishers, and
// exact numbers on the two output topics. Each test builds its own node under a
// private namespace; the whole executable runs on its own ROS_DOMAIN_ID with
// localhost-only discovery (CMakeLists.txt).
//
// Geometry: the raster frame "map" is a local east/north/up frame at (43.07,
// -70.76), so a rectangle in map metres converts to latitude/longitude and back
// through the same chain the node uses. Rectangle edges sit on whole metres, so
// every cell centre is half a metre from any edge and the polygon rule never
// meets a tie.

#include <gtest/gtest.h>

#include <Eigen/Geometry>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "grid_map_msgs/msg/grid_map.hpp"
#include "grid_map_ros/grid_map_ros.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "marine_autonomy/gz4d_geo.h"
#include "marine_interfaces/msg/coverage_assessment.hpp"
#include "marine_interfaces/msg/survey_area.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rcutils/logging.h"
#include "tf2_eigen/tf2_eigen.hpp"
#include "tf2_ros/static_transform_broadcaster.h"
#include "tf2_ros/transform_broadcaster.h"

#include "coverage_assessment_node.hpp"

using coverage_assessment::CoverageAssessmentNode;
using lifecycle_msgs::msg::State;
using marine_interfaces::msg::CoverageAssessment;
using marine_interfaces::msg::SurveyArea;

namespace
{

constexpr double kLat0 = 43.07;
constexpr double kLon0 = -70.76;
// The water surface, ellipsoid height in metres, positive up. Depth = kSurface
// - elevation; whole numbers keep the float elevations exact.
constexpr double kSurface = 10.0;

// ---- log capture ------------------------------------------------------------

std::mutex g_log_mutex;
std::vector<std::string> g_log;
rcutils_logging_output_handler_t g_previous_handler = nullptr;

void captureHandler(
  const rcutils_log_location_t * location, int severity, const char * name,
  rcutils_time_point_value_t timestamp, const char * format, va_list * args)
{
  va_list copy;
  va_copy(copy, *args);
  char buffer[4096];
  vsnprintf(buffer, sizeof(buffer), format, copy);
  va_end(copy);
  // WARN and above, plus the periodic "Assessments:" status line (INFO).
  const bool status_line = std::string(buffer).rfind("Assessments:", 0) == 0;
  if ((severity >= RCUTILS_LOG_SEVERITY_WARN || status_line) &&
    std::string(name).find("coverage_assessment") != std::string::npos)
  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log.push_back(std::to_string(severity) + "|" + buffer);
  }
  if (g_previous_handler) {
    g_previous_handler(location, severity, name, timestamp, format, args);
  }
}

std::size_t countLog(const std::string & needle)
{
  std::lock_guard<std::mutex> lock(g_log_mutex);
  return std::count_if(g_log.begin(), g_log.end(), [&](const std::string & line) {
             return line.find(needle) != std::string::npos;
      });
}

void clearLog()
{
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_log.clear();
}

// ---- geometry helpers -------------------------------------------------------

// earth <- a local east/north/up frame whose origin is at (lat, lon, h).
Eigen::Isometry3d earthFromEnu(double lat_deg, double lon_deg, double h)
{
  const double lat = lat_deg * M_PI / 180.0;
  const double lon = lon_deg * M_PI / 180.0;
  const Eigen::Vector3d east(-std::sin(lon), std::cos(lon), 0.0);
  const Eigen::Vector3d north(
    -std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat));
  const Eigen::Vector3d up(
    std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat));
  gz4d::GeoPointLatLongDegrees ll(lat_deg, lon_deg, h);
  gz4d::GeoPointECEF ecef(ll);
  Eigen::Isometry3d t = Eigen::Isometry3d::Identity();
  t.linear().col(0) = east;
  t.linear().col(1) = north;
  t.linear().col(2) = up;
  t.translation() = Eigen::Vector3d(ecef.x(), ecef.y(), ecef.z());
  return t;
}

// parent <- child as a TransformStamped.
geometry_msgs::msg::TransformStamped transformMsg(
  const std::string & parent, const std::string & child,
  const Eigen::Isometry3d & parent_from_child)
{
  geometry_msgs::msg::TransformStamped t = tf2::eigenToTransform(parent_from_child);
  t.header.frame_id = parent;
  t.child_frame_id = child;
  return t;
}

// A pose in the map frame: translate by (x, y), then yaw (radians) about up.
Eigen::Isometry3d mapPose(double x, double y, double yaw)
{
  return Eigen::Translation3d(x, y, 0.0) * Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitZ());
}

struct Rect
{
  double x0;
  double y0;
  double x1;
  double y1;
};

// ---- messages ---------------------------------------------------------------

// A grid in `frame` whose cell edges sit on whole metres of that frame and whose
// values come from f(x, y) at each cell centre: {depth, uncertainty}.
grid_map_msgs::msg::GridMap makeGrid(
  const std::string & frame, const Rect & r, double resolution,
  const std::function<std::pair<float, float>(double, double)> & f)
{
  grid_map::GridMap g({"elevation", "uncertainty"});
  g.setFrameId(frame);
  g.setGeometry(
    grid_map::Length(r.x1 - r.x0, r.y1 - r.y0), resolution,
    grid_map::Position((r.x0 + r.x1) / 2.0, (r.y0 + r.y1) / 2.0));
  for (grid_map::GridMapIterator it(g); !it.isPastEnd(); ++it) {
    grid_map::Position p;
    g.getPosition(*it, p);
    const auto v = f(p.x(), p.y());
    g.at("elevation", *it) = static_cast<float>(kSurface) - v.first;
    g.at("uncertainty", *it) = v.second;
  }
  auto msg = grid_map::GridMapRosConverter::toMessage(g);
  return *msg;
}

SurveyArea makeArea(
  const std::string & id, const Rect & r_map, double a, double b, int stamp_sec)
{
  const Eigen::Isometry3d earth_from_map = earthFromEnu(kLat0, kLon0, 0.0);
  SurveyArea area;
  area.id = id;
  area.stamp.sec = stamp_sec;
  area.standard.name = "test";
  area.standard.a = a;
  area.standard.b = b;
  const double xs[4] = {r_map.x0, r_map.x1, r_map.x1, r_map.x0};
  const double ys[4] = {r_map.y0, r_map.y0, r_map.y1, r_map.y1};
  for (int i = 0; i < 4; ++i) {
    const Eigen::Vector3d e = earth_from_map * Eigen::Vector3d(xs[i], ys[i], 0.0);
    gz4d::GeoPointECEF ecef(e.x(), e.y(), e.z());
    gz4d::GeoPointLatLongDegrees ll(ecef);
    geographic_msgs::msg::GeoPoint p;
    p.latitude = ll.latitude();
    p.longitude = ll.longitude();
    area.polygon.points.push_back(p);
  }
  return area;
}

// ---- harness ----------------------------------------------------------------

int g_next_namespace = 0;

class Harness
{
public:
  struct Options
  {
    std::vector<std::string> sources;  // names; the topics are <prefix>/<name>/grid
    bool use_names = true;
    double max_age = 30.0;
    double max_distance = 1500.0;
    bool dynamic_tide = false;
    double boat_x = 10.0;
    int64_t max_areas = 0;  // 0: the node's default
    int64_t max_total_cells = 0;  // 0: the node's default
    bool dynamic_boat = false;      // base_link comes from publishBoatNow(), stamped
    bool tide_under_earth = false;  // map_tide hangs directly off earth
    bool use_sim_time = false;      // node clock follows /clock (nobody publishes it)
  };

  explicit Harness(const Options & opt)
  : opt_(opt), prefix_("/t" + std::to_string(g_next_namespace++))
  {
    io_ = std::make_shared<rclcpp::Node>("coverage_test_io");
    static_tf_ = std::make_unique<tf2_ros::StaticTransformBroadcaster>(io_);
    dynamic_tf_ = std::make_unique<tf2_ros::TransformBroadcaster>(io_);

    std::vector<std::string> topics;
    for (const auto & s : opt_.sources) {
      topics.push_back(gridTopic(s));
      grid_pubs_[s] = io_->create_publisher<grid_map_msgs::msg::GridMap>(gridTopic(s), 10);
    }
    area_pub_ = io_->create_publisher<SurveyArea>(
      prefix_ + "/survey_area", rclcpp::QoS(100).reliable().transient_local());
    assessment_sub_ = subscribeAssessments();
    grid_sub_ = io_->create_subscription<grid_map_msgs::msg::GridMap>(
      prefix_ + "/coverage_grid", rclcpp::QoS(1).reliable().transient_local(),
      [this](grid_map_msgs::msg::GridMap::ConstSharedPtr m) {last_coverage_grid_ = *m;});

    rclcpp::NodeOptions options;
    options.arguments({"--ros-args", "-r", "__ns:=" + prefix_});
    options.append_parameter_override("grid_topics", topics);
    if (opt_.use_names) {
      options.append_parameter_override("source_names", opt_.sources);
    }
    options.append_parameter_override("survey_area_topic", prefix_ + "/survey_area");
    options.append_parameter_override("assessment_period", 0.1);
    options.append_parameter_override("water_surface_max_age", opt_.max_age);
    options.append_parameter_override("water_surface_max_boat_distance", opt_.max_distance);
    if (opt_.use_sim_time) {
      options.append_parameter_override("use_sim_time", true);
    }
    if (opt_.max_total_cells > 0) {
      options.append_parameter_override("max_total_raster_cells", opt_.max_total_cells);
    }
    if (opt_.max_areas > 0) {
      options.append_parameter_override("max_areas", opt_.max_areas);
    }
    node_ = std::make_shared<CoverageAssessmentNode>(options);

    executor_.add_node(io_);
    executor_.add_node(node_->get_node_base_interface());
  }

  ~Harness()
  {
    executor_.remove_node(node_->get_node_base_interface());
    executor_.remove_node(io_);
  }

  std::string gridTopic(const std::string & source) const
  {
    return prefix_ + "/" + source + "/grid";
  }

  // earth <- map, earth <- (static) map_tide below it, and the tide boat.
  void publishCommonTf()
  {
    std::vector<geometry_msgs::msg::TransformStamped> statics;
    statics.push_back(transformMsg("earth", "map", earthFromEnu(kLat0, kLon0, 0.0)));
    if (!opt_.dynamic_boat) {
      statics.push_back(transformMsg("map", "base_link", mapPose(opt_.boat_x, 0.0, 0.0)));
    }
    if (opt_.tide_under_earth) {
      statics.push_back(transformMsg(
          "earth", "map_tide",
          earthFromEnu(kLat0, kLon0, 0.0) *
          Eigen::Isometry3d(Eigen::Translation3d(0, 0, kSurface))));
    } else if (!opt_.dynamic_tide) {
      statics.push_back(
        transformMsg("map", "map_tide", Eigen::Isometry3d(Eigen::Translation3d(0, 0, kSurface))));
    }
    static_tf_->sendTransform(statics);
  }

  // earth <- frame, the frame being `map` moved by mapPose(x, y, yaw).
  void publishSourceFrame(const std::string & frame, double x, double y, double yaw)
  {
    static_tf_->sendTransform(transformMsg("map", frame, mapPose(x, y, yaw)));
  }

  void publishTideNow()
  {
    geometry_msgs::msg::TransformStamped t = transformMsg(
      "map", "map_tide", Eigen::Isometry3d(Eigen::Translation3d(0, 0, kSurface)));
    t.header.stamp = io_->now();
    dynamic_tf_->sendTransform(t);
  }

  void publishBoatNow()
  {
    geometry_msgs::msg::TransformStamped t =
      transformMsg("map", "base_link", mapPose(opt_.boat_x, 0.0, 0.0));
    t.header.stamp = io_->now();
    dynamic_tf_->sendTransform(t);
  }

  void publishArea(const SurveyArea & area) {area_pub_->publish(area);}

  void publishGrid(const std::string & source, const grid_map_msgs::msg::GridMap & msg)
  {
    grid_pubs_.at(source)->publish(msg);
  }

  // Spin everything until `done` holds or `seconds` pass; `each` runs about every
  // 150 ms (resend the grids: the union is idempotent, and it covers the TF that
  // has not reached the node yet).
  bool spinUntil(
    const std::function<bool()> & done, double seconds,
    const std::function<void()> & each = nullptr)
  {
    const auto start = std::chrono::steady_clock::now();
    auto last_each = start - std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() - start < std::chrono::duration<double>(seconds)) {
      executor_.spin_some(std::chrono::milliseconds(10));
      const auto now = std::chrono::steady_clock::now();
      if (each && now - last_each >= std::chrono::milliseconds(150)) {
        each();
        last_each = now;
      }
      if (done && done()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done ? done() : true;
  }

  void spinFor(double seconds, const std::function<void()> & each = nullptr)
  {
    spinUntil(nullptr, seconds, each);
  }

  rclcpp::Subscription<CoverageAssessment>::SharedPtr subscribeAssessments()
  {
    return io_->create_subscription<CoverageAssessment>(
      prefix_ + "/coverage_assessment", rclcpp::QoS(100).reliable().transient_local(),
      [this](CoverageAssessment::ConstSharedPtr m) {
        ++received_;
        latest_[m->area.id] = *m;
        if (!first_) {
          first_ = *m;
        }
      });
  }

  // A second subscriber, as a scoreboard starting late would be; returns what the
  // latched topic hands it.
  std::map<std::string, CoverageAssessment> lateJoiner()
  {
    std::map<std::string, CoverageAssessment> got;
    auto sub = io_->create_subscription<CoverageAssessment>(
      prefix_ + "/coverage_assessment", rclcpp::QoS(100).reliable().transient_local(),
      [&got](CoverageAssessment::ConstSharedPtr m) {got[m->area.id] = *m;});
    spinFor(1.0);
    return got;
  }

  std::shared_ptr<rclcpp::Node> io_;
  std::shared_ptr<CoverageAssessmentNode> node_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  Options opt_;
  std::string prefix_;
  std::unique_ptr<tf2_ros::StaticTransformBroadcaster> static_tf_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> dynamic_tf_;
  std::map<std::string, rclcpp::Publisher<grid_map_msgs::msg::GridMap>::SharedPtr> grid_pubs_;
  rclcpp::Publisher<SurveyArea>::SharedPtr area_pub_;
  rclcpp::Subscription<CoverageAssessment>::SharedPtr assessment_sub_;
  rclcpp::Subscription<grid_map_msgs::msg::GridMap>::SharedPtr grid_sub_;
  std::size_t received_ = 0;
  std::map<std::string, CoverageAssessment> latest_;
  std::optional<CoverageAssessment> first_;
  grid_map_msgs::msg::GridMap last_coverage_grid_;
};

class NodeTest : public ::testing::Test
{
public:
  static void SetUpTestSuite()
  {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    g_previous_handler = rcutils_logging_get_output_handler();
    rcutils_logging_set_output_handler(captureHandler);
  }

  static void TearDownTestSuite()
  {
    rcutils_logging_set_output_handler(g_previous_handler);
    rclcpp::shutdown();
  }

  void SetUp() override {clearLog();}
};

// Depth 12 and uncertainty `u` everywhere.
std::function<std::pair<float, float>(double, double)> flat(float depth, float u)
{
  return [depth, u](double, double) {return std::make_pair(depth, u);};
}

}  // namespace

// ---- (ii) + (iii a): exact numbers, two sources in different frames -----------

TEST_F(NodeTest, ScoresTwoSourcesInDifferentFramesWithExactNumbers)
{
  Harness::Options opt;
  opt.sources = {"A", "B"};
  Harness h(opt);
  h.publishCommonTf();
  // a/map is the raster frame moved 10 m east. b/map is moved (30, 5) and turned
  // 90 degrees: x_map = 30 - y_b, y_map = 5 + x_b.
  h.publishSourceFrame("a/map", 10.0, 0.0, 0.0);
  h.publishSourceFrame("b/map", 30.0, 5.0, M_PI / 2.0);

  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);

  // The area is map [5, 25] x [0, 10]: 200 cells at 1 m, standard a = 0.5 m, b = 0.
  h.publishArea(makeArea("area1", {5, 0, 25, 10}, 0.5, 0.0, 1));
  // A covers map [0, 20] x [0, 10] at depth 12: uncertainty 0.25 west of map x = 10
  // (meets) and 0.75 east of it (does not). In a/map that is x_a in [-10, 10].
  const auto grid_a = makeGrid(
    "a/map", {-10, 0, 10, 10}, 1.0,
    [](double x, double) {return std::make_pair(12.0f, x < 0.0 ? 0.25f : 0.75f);});
  // B covers map [15, 30] x [0, 10] at depth 15, uncertainty 0.25. In b/map that
  // is x_b in [-5, 5], y_b in [0, 15].
  const auto grid_b = makeGrid("b/map", {-5, 0, 5, 15}, 1.0, flat(15.0f, 0.25f));
  auto send = [&]() {
      h.publishGrid("A", grid_a);
      h.publishGrid("B", grid_b);
    };
  auto done = [&]() {
      const auto it = h.latest_.find("area1");
      return it != h.latest_.end() && it->second.sources.size() == 2 &&
             it->second.sources[0].area_covered == 150.0 &&
             it->second.sources[1].area_covered == 100.0;
    };
  ASSERT_TRUE(h.spinUntil(done, 20.0, send)) << "no complete assessment arrived";

  const CoverageAssessment & a = h.latest_.at("area1");
  EXPECT_EQ(a.header.frame_id, "map");
  EXPECT_GT(a.header.stamp.sec, 0);
  EXPECT_EQ(a.area.id, "area1");
  EXPECT_EQ(a.area.polygon.points.size(), 4u);
  EXPECT_DOUBLE_EQ(a.cell_size, 1.0);
  EXPECT_DOUBLE_EQ(a.area_total, 200.0);
  EXPECT_DOUBLE_EQ(a.area_covered, 200.0);
  EXPECT_DOUBLE_EQ(a.area_meeting_standard, 150.0);
  ASSERT_EQ(a.sources.size(), 2u);
  EXPECT_EQ(a.sources[0].source, "A");
  EXPECT_DOUBLE_EQ(a.sources[0].area_covered, 150.0);
  EXPECT_DOUBLE_EQ(a.sources[0].area_meeting_standard, 50.0);
  EXPECT_DOUBLE_EQ(a.sources[0].area_unique, 100.0);
  EXPECT_EQ(a.sources[1].source, "B");
  EXPECT_DOUBLE_EQ(a.sources[1].area_covered, 100.0);
  EXPECT_DOUBLE_EQ(a.sources[1].area_meeting_standard, 100.0);
  EXPECT_DOUBLE_EQ(a.sources[1].area_unique, 50.0);

  // coverage_grid: the bounding box of the area, layer meets_standard.
  ASSERT_TRUE(h.spinUntil([&]() {return !h.last_coverage_grid_.data.empty();}, 5.0));
  grid_map::GridMap g;
  ASSERT_TRUE(grid_map::GridMapRosConverter::fromMessage(h.last_coverage_grid_, g));
  EXPECT_EQ(g.getFrameId(), "map");
  EXPECT_EQ(h.last_coverage_grid_.header.frame_id, "map");
  EXPECT_DOUBLE_EQ(g.getResolution(), 1.0);
  EXPECT_DOUBLE_EQ(g.getLength().x(), 20.0);
  EXPECT_DOUBLE_EQ(g.getLength().y(), 10.0);
  ASSERT_TRUE(g.exists("meets_standard"));
  EXPECT_EQ(g.getLayers().size(), 1u);
  int ones = 0;
  int zeros = 0;
  int nans = 0;
  for (grid_map::GridMapIterator it(g); !it.isPastEnd(); ++it) {
    const float v = g.at("meets_standard", *it);
    if (std::isnan(v)) {
      ++nans;
    } else if (v == 1.0f) {
      ++ones;
    } else if (v == 0.0f) {
      ++zeros;
    }
  }
  EXPECT_EQ(ones, 150);
  EXPECT_EQ(zeros, 50);
  EXPECT_EQ(nans, 0);
  EXPECT_FLOAT_EQ(g.atPosition("meets_standard", grid_map::Position(7.5, 5.5)), 1.0f);
  EXPECT_FLOAT_EQ(g.atPosition("meets_standard", grid_map::Position(12.5, 5.5)), 0.0f);
  EXPECT_FLOAT_EQ(g.atPosition("meets_standard", grid_map::Position(17.5, 5.5)), 1.0f);
  EXPECT_FLOAT_EQ(g.atPosition("meets_standard", grid_map::Position(22.5, 0.5)), 1.0f);

  // transient_local: a consumer that starts late still gets the latest of the area.
  const auto late = h.lateJoiner();
  ASSERT_EQ(late.count("area1"), 1u);
  EXPECT_DOUBLE_EQ(late.at("area1").area_meeting_standard, 150.0);

  // The sim's tide came from one static transform, so nothing was refused, and the
  // counters say what happened.
  const auto & s = h.node_->stats();
  EXPECT_GT(s.assessments_published, 0u);
  EXPECT_EQ(s.skipped_surface_stale + s.skipped_surface_too_far + s.skipped_surface_unavailable,
    0u);
  EXPECT_EQ(s.cells_above_surface, 0u);
}

// ---- (iii b): a source that cannot be placed withholds everything -------------

TEST_F(NodeTest, UnplacedSourceWithholdsEveryAssessmentUntilItIsPlaced)
{
  Harness::Options opt;
  opt.sources = {"A", "C"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  // c/map is NOT in TF yet.

  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));

  const auto grid_a = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  const auto grid_c = makeGrid("c/map", {0, 0, 10, 5}, 1.0, flat(12.0f, 0.25f));
  auto send = [&]() {
      h.publishGrid("A", grid_a);
      h.publishGrid("C", grid_c);
    };
  // Positive: the node reports having withheld on several ticks, then nothing
  // may have been published.
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().withheld_unplaced_source >= 3;}, 20.0, send));
  EXPECT_EQ(h.received_, 0u) << "an assessment was published while source C was unplaced";
  EXPECT_GT(h.node_->stats().grids_unplaced, 0u);
  EXPECT_GE(countLog("Source 'C'"), 1u) << "the WARN must name the source";
  EXPECT_GE(countLog("c/map"), 1u) << "the WARN must name the frame";
  EXPECT_NE(h.latest_.count("area1"), 1u);

  // c/map appears (its origin is 0, 0): C's next grid is placed and scoring resumes.
  h.publishSourceFrame("c/map", 0.0, 0.0, 0.0);
  ASSERT_TRUE(h.spinUntil([&]() {return h.received_ > 0;}, 20.0, send));
  // The first assessment ever published already credits C: nothing was published
  // with C as zeros.
  ASSERT_TRUE(h.first_.has_value());
  ASSERT_EQ(h.first_->sources.size(), 2u);
  EXPECT_EQ(h.first_->sources[1].source, "C");
  EXPECT_DOUBLE_EQ(h.first_->area_total, 100.0);
  EXPECT_DOUBLE_EQ(h.first_->sources[0].area_covered, 100.0);
  EXPECT_DOUBLE_EQ(h.first_->sources[1].area_covered, 50.0);
  EXPECT_DOUBLE_EQ(h.first_->sources[1].area_unique, 0.0);
  EXPECT_DOUBLE_EQ(h.first_->area_meeting_standard, 100.0);
}

// ---- startup: waiting for earth is reported as such ----------------------------------

TEST_F(NodeTest, GridsBeforeEarthReportTheWaitNotAnUnplacedSource)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  // No TF at all: `earth` is absent, as for the first ~10 s of a simulation.
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().withheld_no_earth >= 3;}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  EXPECT_GE(h.node_->stats().grids_received, 1u);
  EXPECT_EQ(h.node_->stats().grids_unplaced, 0u) << "waiting for earth is not an unplaced grid";
  EXPECT_EQ(h.node_->stats().withheld_unplaced_source, 0u);
  EXPECT_GE(countLog("waiting for 'map' <- 'earth'"), 1u);
  EXPECT_EQ(countLog("cannot place its grid"), 0u);
  EXPECT_EQ(h.received_, 0u);

  // earth appears; the grid kept meanwhile is placed (one send is enough).
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 100.0;
      }, 20.0));
  EXPECT_EQ(h.node_->stats().grids_unplaced, 0u);
}

TEST_F(NodeTest, ARejectedKeptGridIsLoggedWithItsFrame)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  grid.layers[0] = "not_elevation";  // kept while unplaced, then rejected when folded
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().withheld_no_earth >= 1;}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_TRUE(h.spinUntil([&]() {return h.node_->stats().grids_rejected >= 1;}, 20.0));
  EXPECT_GE(countLog("grid in frame 'a/map' not used: it has no 'elevation' layer"), 1u);
}

TEST_F(NodeTest, ARepeatingConditionLogsOncePerWindowWhileTheNodeClockIsStuckAtZero)
{
  Harness::Options opt;
  opt.sources = {"A"};
  opt.use_sim_time = true;  // nobody publishes /clock: the node clock stays at zero
  Harness h(opt);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  ASSERT_EQ(h.node_->get_clock()->now().nanoseconds(), 0);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  // No TF: every 0.1 s tick finds `earth` missing. Many ticks, one line.
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().withheld_no_earth >= 8;}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(countLog("waiting for 'map' <- 'earth'"), 1u);
}

TEST_F(NodeTest, AFailedFoldOfAKeptGridKeepsItForTheNextTick)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  int folds = 0;
  h.node_->setFaultInjectorForTest([&folds](const char * what) {
      if (std::string(what) == "grid fold" && folds++ == 0) {
        throw std::runtime_error("injected grid fold");
      }
    });
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  // One send only, before earth exists: the node keeps it. The first fold, on
  // the retry once earth is there, throws; the grid must survive that.
  bool sent = false;
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().grids_received >= 1;}, 20.0,
      [&]() {
        if (!sent) {
          sent = true;
          h.publishGrid("A", grid);
        }
      }));
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 100.0;
      }, 20.0));
  EXPECT_EQ(h.node_->stats().callback_exceptions, 1u);
  EXPECT_GE(folds, 2);
}

// ---- (iii c): a source that has sent nothing scores zero ----------------------

TEST_F(NodeTest, SourceThatHasSentNothingScoresZeroAndKeepsItsSlot)
{
  Harness::Options opt;
  opt.sources = {"A", "D"};
  opt.use_names = false;  // the topic is then the source name
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));

  const auto grid_a = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.received_ > 0;}, 20.0, [&]() {h.publishGrid("A", grid_a);}));
  const CoverageAssessment & a = h.latest_.at("area1");
  ASSERT_EQ(a.sources.size(), 2u);
  EXPECT_EQ(a.sources[0].source, h.gridTopic("A")) << "no source_names: the topic is the name";
  EXPECT_EQ(a.sources[1].source, h.gridTopic("D"));
  EXPECT_DOUBLE_EQ(a.area_covered, 100.0);
  EXPECT_DOUBLE_EQ(a.sources[1].area_covered, 0.0);
  EXPECT_DOUBLE_EQ(a.sources[1].area_meeting_standard, 0.0);
  EXPECT_DOUBLE_EQ(a.sources[1].area_unique, 0.0);
  EXPECT_DOUBLE_EQ(a.sources[0].area_unique, 100.0);
  // The silent source was never flagged as unplaced (A may have been, briefly,
  // before its transform reached the node).
  EXPECT_EQ(countLog(h.gridTopic("D")), 0u);
}

// ---- (iv): the water surface is refused when stale or when the boat is far ----

TEST_F(NodeTest, StaleTideSkipsScoringForEveryoneAndLeavesTheLastAssessment)
{
  Harness::Options opt;
  opt.sources = {"A"};
  opt.dynamic_tide = true;
  // Generous against a CI stall: the tide is republished every 150 ms, so a bound
  // of a second would be crossed by a slow first assessment alone.
  opt.max_age = 4.0;
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));

  bool tide_alive = true;
  const auto small = makeGrid("a/map", {0, 0, 10, 4}, 1.0, flat(12.0f, 0.25f));
  const auto big = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  bool send_big = false;
  auto each = [&]() {
      if (tide_alive) {
        h.publishTideNow();
      }
      h.publishGrid("A", send_big ? big : small);
    };
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 40.0;
      }, 20.0, each));

  // The tide stops. Until it is older than max_age it is still good, so keep
  // sending the small grid and wait for the refusal; only then does the grid grow,
  // and nothing may be scored with the old surface.
  tide_alive = false;
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().skipped_surface_stale > 0;}, 30.0, each));
  h.spinFor(0.3, each);  // let an assessment already published drain
  const std::size_t before_growth = h.received_;
  const uint64_t stale_before = h.node_->stats().skipped_surface_stale;
  send_big = true;
  // Positive: the node refuses on several more ticks while the bigger grid is in.
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().skipped_surface_stale >= stale_before + 3;}, 30.0, each));
  EXPECT_GT(h.node_->stats().grids_placed, 0u);
  EXPECT_EQ(h.received_, before_growth) << "scored with a stale water surface";
  EXPECT_GE(countLog("stale"), 1u);
  EXPECT_GE(countLog("map_tide"), 1u) << "the WARN names the frame";
  const auto late = h.lateJoiner();
  ASSERT_EQ(late.count("area1"), 1u);
  EXPECT_DOUBLE_EQ(late.at("area1").area_covered, 40.0) << "the last assessment stays latched";

  // The tide returns: the accumulated grid is scored at once.
  tide_alive = true;
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.latest_.at("area1").area_covered == 100.0;}, 20.0, each));
}

TEST_F(NodeTest, TideBoatBeyondTheDistanceBoundSkipsScoring)
{
  Harness::Options opt;
  opt.sources = {"A"};
  opt.max_distance = 50.0;
  opt.boat_x = 100.0;  // the tide boat is 100 m from the origin of map_tide's parent
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().skipped_surface_too_far >= 3;}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(h.received_, 0u);
  EXPECT_GE(countLog("water_surface_max_boat_distance"), 1u);
  EXPECT_GT(h.node_->stats().grids_placed, 0u) << "grids keep accumulating while scoring waits";

  // The boat comes back inside the bound: the same static transform, re-sent.
  h.static_tf_->sendTransform(transformMsg("map", "base_link", mapPose(10.0, 0.0, 0.0)));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.received_ > 0;}, 20.0, [&]() {h.publishGrid("A", grid);}));
  EXPECT_DOUBLE_EQ(h.latest_.at("area1").area_covered, 100.0);
}

TEST_F(NodeTest, StaleTideBoatTransformSkipsScoringToo)
{
  Harness::Options opt;
  opt.sources = {"A"};
  opt.dynamic_boat = true;
  opt.max_age = 4.0;
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  bool boat_alive = true;
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  auto each = [&]() {
      if (boat_alive) {
        h.publishBoatNow();
      }
      h.publishGrid("A", grid);
    };
  ASSERT_TRUE(h.spinUntil([&]() {return h.received_ > 0;}, 20.0, each));
  EXPECT_EQ(h.node_->stats().skipped_surface_stale, 0u);

  // The boat's transform stops: its distance from the tide frame's origin is
  // unknown, which is refused like a stale tide.
  boat_alive = false;
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().skipped_surface_stale >= 1;}, 30.0, each));
  EXPECT_GE(countLog("the transform to the tide boat 'base_link'"), 1u);

  // It returns: scoring resumes (a bigger grid forces a new assessment).
  boat_alive = true;
  const std::size_t received = h.received_;
  const auto bigger = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(14.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.received_ > received;}, 20.0, [&]() {
        h.publishBoatNow();
        h.publishGrid("A", bigger);
      }));
}

TEST_F(NodeTest, TideFrameDirectlyUnderEarthIsReportedAsMisconfigured)
{
  Harness::Options opt;
  opt.sources = {"A"};
  opt.tide_under_earth = true;
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().skipped_surface_misconfigured >= 3;}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(h.node_->stats().skipped_surface_too_far, 0u) << "not the generic distance refusal";
  EXPECT_EQ(h.received_, 0u);
  EXPECT_GE(countLog("hangs directly off 'earth'"), 1u);
  EXPECT_GE(countLog("map_tide"), 1u);
}

// ---- (v): deactivate -> cleanup -> configure empties the union ---------------

TEST_F(NodeTest, ReconfigureEmptiesTheUnion)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 100.0;
      }, 20.0, [&]() {h.publishGrid("A", grid);}));

  // The reset sequence of reset_cube_nodes.sh, on the assessor.
  ASSERT_EQ(h.node_->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_EQ(h.node_->stats().grids_placed, 0u) << "configure starts the counters afresh";
  // The cleanup dropped the old subscriptions; the old test subscriber keeps its
  // last value, so look at what arrives next.
  h.latest_.clear();
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);

  // No grid is sent: the area comes back from the latched topic and scores zero.
  ASSERT_TRUE(h.spinUntil([&]() {return h.latest_.count("area1") > 0;}, 20.0));
  EXPECT_DOUBLE_EQ(h.latest_.at("area1").area_total, 100.0);
  EXPECT_DOUBLE_EQ(h.latest_.at("area1").area_covered, 0.0) << "the union survived the reset";
  EXPECT_DOUBLE_EQ(h.latest_.at("area1").area_meeting_standard, 0.0);
  ASSERT_EQ(h.latest_.at("area1").sources.size(), 1u);
  EXPECT_DOUBLE_EQ(h.latest_.at("area1").sources[0].area_covered, 0.0);

  // And the node works again afterwards.
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.latest_.at("area1").area_covered == 100.0;}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
}

// ---- the raster georeference is captured once ---------------------------------

TEST_F(NodeTest, MapFrameMovingAfterTheCaptureWithholdsUntilReconfigured)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  auto send = [&]() {h.publishGrid("A", grid);};
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 100.0;
      }, 20.0, send));
  EXPECT_EQ(h.node_->stats().withheld_map_moved, 0u);

  // The map frame's origin jumps 5 m east (its sources' frames hang off it and move
  // with it). Nothing may be re-projected silently: the node says so and withholds.
  const Eigen::Isometry3d moved =
    earthFromEnu(kLat0, kLon0, 0.0) * Eigen::Isometry3d(Eigen::Translation3d(5.0, 0.0, 0.0));
  h.static_tf_->sendTransform(transformMsg("earth", "map", moved));
  ASSERT_TRUE(h.spinUntil([&]() {return h.node_->stats().withheld_map_moved >= 1;}, 20.0, send));
  EXPECT_GE(countLog("The map frame 'map' <- 'earth' moved by 5.000 m"), 1u);
  h.spinFor(0.3, send);  // let anything already published drain
  const std::size_t received = h.received_;
  const uint64_t withheld = h.node_->stats().withheld_map_moved;
  h.spinFor(1.5, send);
  EXPECT_EQ(h.received_, received) << "an assessment was published after the map frame moved";
  EXPECT_GE(h.node_->stats().withheld_map_moved, withheld + 5);

  // A reconfigure recaptures. The area (latitude/longitude) now sits at map
  // x in [-5, 5]; the grid, in a frame that moved with the map, covers x in [0, 10]:
  // the overlap is 5 x 10 m.
  ASSERT_EQ(h.node_->deactivate().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.latest_.clear();
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 50.0;
      }, 20.0, send));
  EXPECT_DOUBLE_EQ(h.latest_.at("area1").area_total, 100.0);
  EXPECT_EQ(h.node_->stats().withheld_map_moved, 0u);
}

TEST_F(NodeTest, ASmallMotionOfTheMapFrameWithinTheToleranceIsNotMovement)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  auto send = [&]() {h.publishGrid("A", grid);};
  ASSERT_TRUE(h.spinUntil([&]() {return h.received_ > 0;}, 20.0, send));
  // 5 mm: inside the 1 cm tolerance.
  h.static_tf_->sendTransform(transformMsg(
      "earth", "map",
      earthFromEnu(kLat0, kLon0, 0.0) * Eigen::Isometry3d(Eigen::Translation3d(0.005, 0.0, 0.0))));
  const std::size_t received = h.received_;
  h.spinFor(1.0, send);
  EXPECT_EQ(h.node_->stats().withheld_map_moved, 0u);
  // ... still assessing: force a new assessment by changing the grid.
  const auto bigger = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(14.0f, 0.25f));
  EXPECT_TRUE(h.spinUntil(
      [&]() {return h.received_ > received;}, 20.0, [&]() {h.publishGrid("A", bigger);}));
  EXPECT_EQ(h.node_->stats().withheld_map_moved, 0u);
}

TEST_F(NodeTest, ATinyRotationOfTheMapFrameAboutItsOriginIsNotMovement)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  auto send = [&]() {h.publishGrid("A", grid);};
  ASSERT_TRUE(h.spinUntil([&]() {return h.received_ > 0;}, 20.0, send));
  // 1.6e-9 rad about the map's own origin: the frame stays where it is, but the
  // translation of map <- earth (the Earth-radius vector, turned) changes by ~1 cm.
  const Eigen::Isometry3d earth_from_map = earthFromEnu(kLat0, kLon0, 0.0) *
    Eigen::Isometry3d(Eigen::AngleAxisd(1.6e-9, Eigen::Vector3d::UnitX()));
  h.static_tf_->sendTransform(transformMsg("earth", "map", earth_from_map));
  const std::size_t received = h.received_;
  h.spinFor(2.0, send);
  EXPECT_EQ(h.node_->stats().withheld_map_moved, 0u);
  // ... still assessing: a changed grid is scored.
  const auto bigger = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(14.0f, 0.25f));
  EXPECT_TRUE(h.spinUntil(
      [&]() {return h.received_ > received;}, 20.0, [&]() {h.publishGrid("A", bigger);}));
  EXPECT_EQ(h.node_->stats().withheld_map_moved, 0u);
}

// ---- a source that went silent while unplaced does not halt scoring for ever -----

TEST_F(NodeTest, SilentUnplacedSourceIsRetriedAndItsKeptGridIsFoldedIn)
{
  Harness::Options opt;
  opt.sources = {"A", "C"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);  // c/map is not in TF
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid_a = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  const auto grid_c = makeGrid("c/map", {0, 0, 10, 5}, 1.0, flat(12.0f, 0.25f));
  // C's grid is sent until the node has seen (and flagged) it, then C goes silent.
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.node_->stats().grids_unplaced >= 1 && h.node_->stats().grids_placed >= 1;
      }, 20.0, [&]() {
        h.publishGrid("A", grid_a);
        if (h.node_->stats().grids_unplaced == 0) {
          h.publishGrid("C", grid_c);
        }
      }));
  h.spinFor(0.5);
  EXPECT_GT(h.node_->stats().withheld_unplaced_source, 0u);
  EXPECT_EQ(h.received_, 0u);

  // Its frame appears. Nothing more is sent by anybody: the retry finds the
  // frame, folds in the grid it kept, and the first assessment credits C.
  h.publishSourceFrame("c/map", 0.0, 0.0, 0.0);
  ASSERT_TRUE(h.spinUntil([&]() {return h.received_ > 0;}, 20.0));
  ASSERT_TRUE(h.first_.has_value());
  EXPECT_DOUBLE_EQ(h.first_->sources[0].area_covered, 100.0);
  EXPECT_EQ(h.first_->sources[1].source, "C");
  EXPECT_DOUBLE_EQ(h.first_->sources[1].area_covered, 50.0);
}

// ---- a polygon beyond the coordinate bound is refused with a reason ----------------

TEST_F(NodeTest, APolygonBeyondTheRasterCoordinateBoundIsRefusedWithAReason)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  // The raster frame's origin is 15000 km from the area (a map frame defined far
  // off the survey: Earth's own geometry never gets a lat/lon this far in the
  // tangent plane), so every vertex lands beyond the 1e7 m bound.
  h.static_tf_->sendTransform(transformMsg(
      "earth", "map",
      earthFromEnu(kLat0, kLon0, 0.0) * Eigen::Isometry3d(Eigen::Translation3d(1.5e7, 0.0, 0.0))));
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  SurveyArea area = makeArea("far", {0, 0, 10, 10}, 0.5, 0.0, 1);
  // makeArea places the rectangle through the unshifted frame; the shifted map
  // frame therefore sees it 1.5e7 m away.
  h.publishArea(area);
  ASSERT_TRUE(h.spinUntil([&]() {return h.node_->stats().areas_rejected >= 1;}, 20.0));
  EXPECT_GE(countLog("area 'far': polygon has a coordinate"), 1u);
  EXPECT_GE(countLog("beyond the limit of"), 1u);
  EXPECT_EQ(h.node_->stats().areas_rejected, 1u);
  EXPECT_EQ(h.node_->stats().assessments_published, 0u);
}

// ---- survey areas: arrival ordering, unset standards, max_areas -------------------

namespace
{

// A SurveyArea whose polygon has too few vertices to be one.
SurveyArea invalidArea(const std::string & id, int stamp_sec)
{
  SurveyArea area = makeArea(id, {0, 0, 10, 10}, 0.5, 0.0, stamp_sec);
  area.polygon.points.resize(2);
  return area;
}

}  // namespace

TEST_F(NodeTest, ARejectedNewerAreaNeverDisplacesNorOutranksAValidOne)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);

  // No TF yet, so nothing can be projected: all four messages are in the node
  // before the first assessment. Valid (stamp 1), invalid and newer (stamp 9),
  // valid with a stamp below the invalid one (stamp 5, a 10 x 6 rectangle), and
  // valid with a stamp below that (stamp 4). The invalid one must not outrank
  // stamp 5, and stamp 5 must win over the later-arriving stamp 4.
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  h.publishArea(invalidArea("area1", 9));
  h.publishArea(makeArea("area1", {0, 0, 10, 6}, 0.5, 0.0, 5));
  h.publishArea(makeArea("area1", {0, 0, 10, 8}, 0.5, 0.0, 4));
  ASSERT_TRUE(h.spinUntil([&]() {return h.node_->stats().areas_received == 4;}, 10.0));
  EXPECT_EQ(h.node_->stats().areas_rejected, 1u);
  EXPECT_EQ(h.received_, 0u);

  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.received_ > 0;}, 20.0, [&]() {h.publishGrid("A", grid);}));
  ASSERT_TRUE(h.first_.has_value());
  EXPECT_DOUBLE_EQ(h.first_->area_total, 60.0) << "the stamp-5 definition must be the one scored";
  EXPECT_EQ(h.first_->area.stamp.sec, 5);

  // Now that stamp 5 is accepted, an older arrival is ignored and an invalid
  // newer one is rejected, and neither changes what is scored.
  const std::size_t before = h.node_->stats().areas_rejected;
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 3));
  h.publishArea(invalidArea("area1", 20));
  ASSERT_TRUE(h.spinUntil([&]() {return h.node_->stats().areas_rejected > before;}, 10.0));
  h.spinFor(1.0, [&]() {h.publishGrid("A", grid);});
  EXPECT_DOUBLE_EQ(h.latest_.at("area1").area_total, 60.0);
  EXPECT_EQ(h.latest_.at("area1").area.stamp.sec, 5);
  EXPECT_GE(countLog("older than the definition held"), 1u);
}

TEST_F(NodeTest, DegenerateAreasBeforeTfNeverEvictTheOnlyValidDefinition)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);

  // No TF yet: one valid definition, then eight zero-area ones (three vertices
  // each, valid coordinates, collinear) of the same id and newer stamps. The
  // queue holds eight per id, so queuing them all evicted the valid one and every
  // survivor then failed projection.
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  for (int k = 0; k < 8; ++k) {
    SurveyArea flat_area = makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 10 + k);
    flat_area.polygon.points.resize(3);
    flat_area.polygon.points[1].latitude = flat_area.polygon.points[0].latitude;
    flat_area.polygon.points[1].longitude = flat_area.polygon.points[0].longitude + 1.0e-4;
    flat_area.polygon.points[2].latitude = flat_area.polygon.points[0].latitude;
    flat_area.polygon.points[2].longitude = flat_area.polygon.points[0].longitude + 2.0e-4;
    h.publishArea(flat_area);
  }
  ASSERT_TRUE(h.spinUntil([&]() {return h.node_->stats().areas_received == 9;}, 10.0));
  EXPECT_EQ(h.node_->stats().areas_rejected, 8u);

  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.received_ > 0;}, 20.0, [&]() {h.publishGrid("A", grid);}));
  ASSERT_TRUE(h.first_.has_value());
  EXPECT_DOUBLE_EQ(h.first_->area_total, 100.0);
  EXPECT_EQ(h.first_->area.stamp.sec, 1);
}

TEST_F(NodeTest, QueueOverflowDropsTheLowestStampAndSaysSo)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);

  // Before TF: the highest stamp (a 10 x 6 rectangle) arrives first, then eight
  // more with lower stamps: nine in all, one over the queue bound. The lowest
  // stamp goes (and is named); the arrival-order policy used to drop stamp 100.
  h.publishArea(makeArea("area1", {0, 0, 10, 6}, 0.5, 0.0, 100));
  for (int k = 1; k <= 8; ++k) {
    h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, k));
  }
  ASSERT_TRUE(h.spinUntil([&]() {return h.node_->stats().areas_received == 9;}, 10.0));
  EXPECT_GE(countLog("dropping the one with the lowest stamp (1000000000 ns)"), 1u);

  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.received_ > 0;}, 20.0, [&]() {h.publishGrid("A", grid);}));
  ASSERT_TRUE(h.first_.has_value());
  EXPECT_EQ(h.first_->area.stamp.sec, 100);
  EXPECT_DOUBLE_EQ(h.first_->area_total, 60.0);
}

TEST_F(NodeTest, AnUnsetStandardIsScoredAndPublishedNotRefused)
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("nan_a", {0, 0, 10, 10}, nan, 0.0, 1));
  h.publishArea(makeArea("negative_b", {0, 0, 10, 10}, 0.5, -1.0, 1));
  h.publishArea(makeArea("zero", {0, 0, 10, 10}, 0.0, 0.0, 1));
  h.publishArea(makeArea("real", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.0f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        for (const char * id : {"nan_a", "negative_b", "zero", "real"}) {
          if (!h.latest_.count(id) || h.latest_.at(id).area_covered != 100.0) {
            return false;
          }
        }
        return true;
      }, 20.0, [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(h.node_->stats().areas_rejected, 0u);
  for (const char * id : {"nan_a", "negative_b", "zero"}) {
    EXPECT_DOUBLE_EQ(h.latest_.at(id).area_meeting_standard, 0.0) << id;
    EXPECT_DOUBLE_EQ(h.latest_.at(id).sources[0].area_meeting_standard, 0.0) << id;
  }
  EXPECT_DOUBLE_EQ(h.latest_.at("real").area_meeting_standard, 100.0);
  EXPECT_EQ(countLog("Rejecting SurveyArea"), 0u);
}

TEST_F(NodeTest, MaxTotalRasterCellsRefusesTheAreaThatWouldExceedItAndNamesTheBudget)
{
  Harness::Options opt;
  opt.sources = {"A"};
  opt.max_total_cells = 250;  // two 10 x 10 squares fit, a third does not
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("a1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  h.publishArea(makeArea("a2", {0, 0, 10, 10}, 0.5, 0.0, 1));
  h.publishArea(makeArea("a3", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.latest_.count("a1") && h.latest_.count("a2");}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(h.node_->stats().areas_rejected, 1u);
  EXPECT_EQ(countLog("max_total_raster_cells"), 1u);
  EXPECT_EQ(h.latest_.count("a3"), 0u);
}

TEST_F(NodeTest, StatusLineReportsTheLastAssessmentPassDuration)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("a1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  auto send = [&]() {h.publishGrid("A", grid);};
  ASSERT_TRUE(h.spinUntil([&]() {return h.received_ > 0;}, 20.0, send));
  // The status line comes every 10 s after activation.
  ASSERT_TRUE(h.spinUntil([&]() {return countLog("last assessment pass") >= 1;}, 20.0));
  EXPECT_GE(countLog(" s over "), 1u);
}

TEST_F(NodeTest, ConfigureRefusesAMaxTotalRasterCellsBelowOne)
{
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "-r", "__ns:=/t" + std::to_string(g_next_namespace++)});
  options.append_parameter_override("grid_topics", std::vector<std::string>{"/g"});
  options.append_parameter_override("max_total_raster_cells", static_cast<int64_t>(0));
  auto node = std::make_shared<CoverageAssessmentNode>(options);
  EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);
  EXPECT_EQ(countLog("max_total_raster_cells=0 must be at least 1"), 1u);
}

TEST_F(NodeTest, MaxAreasRefusesANewIdButStillUpdatesAHeldOne)
{
  Harness::Options opt;
  opt.sources = {"A"};
  opt.max_areas = 2;
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("a1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  h.publishArea(makeArea("a2", {0, 0, 10, 10}, 0.5, 0.0, 1));
  h.publishArea(makeArea("a3", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.latest_.count("a1") && h.latest_.count("a2");}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(h.node_->stats().areas_rejected, 1u);
  EXPECT_EQ(countLog("max_areas (2) areas are already held"), 1u);
  EXPECT_EQ(h.latest_.count("a3"), 0u);

  // A held id is still updated.
  h.publishArea(makeArea("a1", {0, 0, 10, 5}, 0.5, 0.0, 2));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.latest_.at("a1").area_total == 50.0;}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(h.latest_.count("a3"), 0u);
}

// ---- exception containment ----------------------------------------------------

TEST_F(NodeTest, ExceptionsInCallbacksAreContainedCountedAndTheNodeKeepsWorking)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  // The first call of each callback throws: a std::exception from the grid, a
  // non-std one from the survey area, a std::exception from the timer.
  std::map<std::string, int> calls;
  h.node_->setFaultInjectorForTest([&calls](const char * what) {
      const std::string name(what);
      if (calls[name]++ > 0) {
        return;
      }
      if (name == "survey_area callback") {
        throw 42;
      }
      throw std::runtime_error("injected " + name);
    });
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);

  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  const SurveyArea area = makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1);
  // The transient_local survey_area is delivered once; resend it so the second
  // delivery (which is not injected) defines the area.
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 100.0;
      }, 20.0, [&]() {
        h.publishArea(area);
        h.publishGrid("A", grid);
      }));
  EXPECT_GE(h.node_->stats().callback_exceptions, 3u)
    << "grid, survey_area and timer each threw once";
  EXPECT_GE(calls["grid callback"], 2);
  EXPECT_GE(calls["survey_area callback"], 2);
  EXPECT_GE(calls["assessment timer"], 2);
  EXPECT_GE(countLog("Unhandled exception"), 1u);
  EXPECT_GE(countLog("The node keeps running"), 1u);
}

TEST_F(NodeTest, AThrowWhileRegisteringANewAreaLeavesNoHalfRegisteredId)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  int registrations = 0;
  h.node_->setFaultInjectorForTest([&registrations](const char * what) {
      if (std::string(what) == "area registration" && registrations++ == 0) {
        throw std::runtime_error("injected area registration");
      }
    });
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  const SurveyArea area = makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1);
  // The first (transient_local) delivery throws in the middle of registering the
  // id; the resend defines it. A half-registered id would make every later tick throw.
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 100.0;
      }, 20.0, [&]() {
        h.publishArea(area);
        h.publishGrid("A", grid);
      }));
  EXPECT_EQ(h.node_->stats().callback_exceptions, 1u);
}

TEST_F(NodeTest, AThrowWhileProjectingAreasIsRetriedOnTheNextTick)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  int projections = 0;
  h.node_->setFaultInjectorForTest([&projections](const char * what) {
      if (std::string(what) == "area projection" && projections++ == 0) {
        throw std::runtime_error("injected area projection");
      }
    });
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  // The area is sent once only: if the failed projection cleared the dirty flag
  // the queued definition would never be projected and nothing would be scored.
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 1.0, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 100.0;
      }, 20.0, [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(h.node_->stats().callback_exceptions, 1u);
  EXPECT_GE(projections, 2);
}

// ---- incompatible QoS is an ERROR naming the topic -------------------------------

TEST_F(NodeTest, IncompatibleQosPublishersAreReportedByTopic)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);

  // A volatile survey_area publisher cannot reach a transient_local subscription,
  // and a best-effort grid publisher cannot reach a reliable one.
  auto volatile_area = h.io_->create_publisher<SurveyArea>(
    h.prefix_ + "/survey_area", rclcpp::QoS(10).reliable().durability_volatile());
  auto best_effort_grid = h.io_->create_publisher<grid_map_msgs::msg::GridMap>(
    h.gridTopic("A"), rclcpp::QoS(10).best_effort());
  ASSERT_TRUE(h.spinUntil(
      [&]() {
        return countLog("'" + h.prefix_ + "/survey_area': a publisher offers a QoS incompatible") >=
               1 &&
               countLog("'" + h.gridTopic("A") + "': a publisher offers a QoS incompatible") >= 1;
      }, 20.0));
}

// ---- configure refusals and parameters ---------------------------------------

namespace
{

std::shared_ptr<CoverageAssessmentNode> makeNode(
  const std::vector<std::string> & topics, const std::vector<std::string> & names)
{
  rclcpp::NodeOptions options;
  options.arguments({"--ros-args", "-r", "__ns:=/t" + std::to_string(g_next_namespace++)});
  options.append_parameter_override("grid_topics", topics);
  options.append_parameter_override("source_names", names);
  return std::make_shared<CoverageAssessmentNode>(options);
}

}  // namespace

TEST_F(NodeTest, ConfigureFailsWithAReasonOnAnUnusableSourceList)
{
  {
    auto node = makeNode({}, {});
    EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);
    EXPECT_EQ(countLog("grid_topics is empty"), 1u);
  }
  {
    auto node = makeNode({"/x/grid", "/x/grid"}, {});
    EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);
    EXPECT_EQ(countLog("lists '/x/grid' twice"), 1u);
    // A second attempt fails the same way and leaves nothing behind.
    EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
  {
    auto node = makeNode({"/x/grid", "/y/grid"}, {"only_one"});
    EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);
    EXPECT_EQ(countLog("source_names has 1 entries but grid_topics has 2"), 1u);
  }
  {
    auto node = makeNode({"/x/grid", "/y/grid"}, {"same", "same"});
    EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED);
    EXPECT_EQ(countLog("source name 'same' is used by two sources"), 1u);
  }
  {
    // The control: the same shape with a usable list configures.
    auto node = makeNode({"/x/grid", "/y/grid"}, {"x", ""});
    EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
    EXPECT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  }
}

TEST_F(NodeTest, ConfigureRefusesAMaxAreasOutsideOneToAHundred)
{
  for (const int64_t bad : {0, -3, 101, 100000}) {
    rclcpp::NodeOptions options;
    options.arguments({"--ros-args", "-r", "__ns:=/t" + std::to_string(g_next_namespace++)});
    options.append_parameter_override("grid_topics", std::vector<std::string>{"/x/grid"});
    options.append_parameter_override("max_areas", bad);
    auto node = std::make_shared<CoverageAssessmentNode>(options);
    clearLog();
    EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_UNCONFIGURED) << bad;
    EXPECT_EQ(countLog("max_areas=" + std::to_string(bad) + " must be from 1 to 100"), 1u) << bad;
  }
  for (const int64_t good : {1, 20, 100}) {
    rclcpp::NodeOptions options;
    options.arguments({"--ros-args", "-r", "__ns:=/t" + std::to_string(g_next_namespace++)});
    options.append_parameter_override("grid_topics", std::vector<std::string>{"/x/grid"});
    options.append_parameter_override("max_areas", good);
    auto node = std::make_shared<CoverageAssessmentNode>(options);
    EXPECT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE) << good;
  }
}

TEST_F(NodeTest, ParametersReadAtConfigureAreReadOnly)
{
  auto node = makeNode({"/x/grid"}, {});
  for (const char * name : {"cell_size", "map_frame", "grid_topics", "source_names",
      "survey_area_topic", "assessment_period", "water_surface_frame", "water_surface_boat_frame",
      "water_surface_max_boat_distance", "water_surface_max_age", "max_raster_cells",
      "max_total_raster_cells", "max_areas"})
  {
    ASSERT_TRUE(node->has_parameter(name)) << name;
    EXPECT_TRUE(node->describe_parameter(name).read_only) << name;
  }
  EXPECT_FALSE(node->set_parameter(rclcpp::Parameter("cell_size", 2.0)).successful);
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_FALSE(node->set_parameter(rclcpp::Parameter("cell_size", 2.0)).successful);
  EXPECT_DOUBLE_EQ(node->get_parameter("cell_size").as_double(), 1.0);
  ASSERT_EQ(node->cleanup().id(), State::PRIMARY_STATE_UNCONFIGURED);
  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
  EXPECT_FALSE(node->set_parameter(rclcpp::Parameter("cell_size", 2.0)).successful);
}

TEST_F(NodeTest, ResolutionDifferingFromCellSizeWarnsOncePerSource)
{
  Harness::Options opt;
  opt.sources = {"A"};
  Harness h(opt);
  h.publishCommonTf();
  h.publishSourceFrame("a/map", 0.0, 0.0, 0.0);
  ASSERT_EQ(h.node_->configure().id(), State::PRIMARY_STATE_INACTIVE);
  ASSERT_EQ(h.node_->activate().id(), State::PRIMARY_STATE_ACTIVE);
  h.publishArea(makeArea("area1", {0, 0, 10, 10}, 0.5, 0.0, 1));
  const auto grid = makeGrid("a/map", {0, 0, 10, 10}, 0.5, flat(12.0f, 0.25f));
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.node_->stats().grids_placed >= 3;}, 20.0,
      [&]() {h.publishGrid("A", grid);}));
  EXPECT_EQ(countLog("differs from cell_size"), 1u);
  // The half-metre grid still scores on the 1 m lattice.
  ASSERT_TRUE(h.spinUntil(
      [&]() {return h.latest_.count("area1") && h.latest_.at("area1").area_covered == 100.0;},
      20.0, [&]() {h.publishGrid("A", grid);}));
}
