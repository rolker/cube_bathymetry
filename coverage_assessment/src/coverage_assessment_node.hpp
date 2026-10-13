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

#ifndef COVERAGE_ASSESSMENT_NODE_HPP_
#define COVERAGE_ASSESSMENT_NODE_HPP_

#include <Eigen/Geometry>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "grid_map_msgs/msg/grid_map.hpp"
#include "grid_map_ros/grid_map_ros.hpp"
#include "marine_interfaces/msg/coverage_assessment.hpp"
#include "marine_interfaces/msg/survey_area.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "tf2/exceptions.h"
#include "tf2/time.h"
#include "tf2_eigen/tf2_eigen.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

#include "coverage_assessment/area_registry.h"
#include "coverage_assessment/assessment_raster.h"
#include "coverage_assessment/coverage_assessor.h"
#include "coverage_assessment/polygon_raster.h"
#include "coverage_assessment/source_accumulator.h"
#include "frame_chain.hpp"

/// A log line at most once per kWarnPeriodSeconds PER NODE and call site: the rclcpp
/// *_THROTTLE macros keep their state per call site for the whole process, so a
/// second node (or test) in the process would hide this one's line.
#define COVERAGE_LOG_THROTTLED(level, ...) \
  do { \
    if (throttle(__LINE__)) { \
      RCLCPP_ ## level(get_logger(), __VA_ARGS__); \
    } \
  } while (0)

namespace coverage_assessment
{

/// When a throttled log line last went out. An explicit flag says "never logged":
/// a zero timestamp cannot, since a simulated clock legitimately reads zero.
struct LogThrottle
{
  bool logged = false;
  std::chrono::steady_clock::time_point at;
};

/// Counters for the node's diagnostics: logged with the periodic status line and
/// read back by tests. Reset on every configure; cumulative across a `~/reset`.
struct NodeStats
{
  /// grid messages received, and what became of them
  uint64_t grids_received = 0;
  uint64_t grids_placed = 0;
  /// no transform from the raster frame to the grid's frame
  uint64_t grids_unplaced = 0;
  /// not convertible, no elevation layer, or unusable geometry
  uint64_t grids_rejected = 0;
  uint64_t areas_received = 0;
  uint64_t areas_rejected = 0;
  /// assessment passes that published
  uint64_t assessments_published = 0;
  /// timer ticks that did not assess, by reason
  uint64_t withheld_unplaced_source = 0;
  uint64_t withheld_no_earth = 0;
  /// map_frame <- earth moved after the raster georeference was captured
  uint64_t withheld_map_moved = 0;
  /// the sources hold more than max_held_cells
  uint64_t withheld_held_limit = 0;
  /// placed grids not folded in because max_held_cells was reached
  uint64_t grids_over_held_limit = 0;
  uint64_t skipped_surface_unavailable = 0;
  uint64_t skipped_surface_stale = 0;
  uint64_t skipped_surface_too_far = 0;
  /// water_surface_frame's parent is `earth`: the distance bound cannot be measured
  uint64_t skipped_surface_misconfigured = 0;
  /// exceptions caught at the top of a callback (the node kept running)
  uint64_t callback_exceptions = 0;
  /// cells with depth below -kNegativeDepthMargin in the last assessment, over
  /// all areas (see AreaAssessment::cells_above_surface)
  std::size_t cells_above_surface = 0;
};

/// Test-only access to the node's private state (defined in the test).
struct CoverageAssessmentTestAccess;

/// Lifecycle node: scores how much of each SurveyArea the sources' grids cover
/// and how much of that meets the area's standard. The judging lives in the
/// plain-C++ core; this class converts messages and frames into the core's
/// structs and publishes the results. See README.md for the design.
class CoverageAssessmentNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  friend struct CoverageAssessmentTestAccess;

  using CallbackReturn =
    rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

  /// Depth of the survey_area subscription and of the coverage_assessment
  /// publisher (both transient_local). Fixed, not a parameter: a late consumer
  /// receives up to this many of the latest messages, which holds the newest
  /// assessment of every area as long as fewer areas than this are in flight.
  static constexpr std::size_t kLatchedDepth = 100;

  /// Interval of the status line (cells held, memory, counters).
  static constexpr double kStatusLogPeriodSeconds = 10.0;

  /// Interval of a repeating warning (per source where it concerns one).
  static constexpr double kWarnPeriodSeconds = 10.0;

  /// A change in the water-surface height smaller than this does not trigger a
  /// new assessment (metres).
  static constexpr double kSurfaceChangeThreshold = 0.01;

  /// map_frame <- earth may differ from the captured raster georeference by this
  /// much (metres, radians) before the node calls the map frame moved.
  static constexpr double kGeorefTranslationTolerance = 0.01;
  static constexpr double kGeorefRotationTolerance = 1.0e-6;

  /// Most definitions of one area id kept while the raster georeference is not
  /// available yet to project them (the lowest stamp is dropped first).
  static constexpr std::size_t kMaxPendingPerArea = 8;

  /// Upper bound of the max_areas parameter: the coverage_assessment publisher's
  /// transient_local history is kLatchedDepth messages, one per area per pass.
  static constexpr int64_t kMaxAreasLimit = 100;

  explicit CoverageAssessmentNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : rclcpp_lifecycle::LifecycleNode("coverage_assessment", options)
  {
    declareParameters();
  }

  /// Counters, for diagnostics and tests.
  const NodeStats & stats() const {return stats_;}

  /// Lattice cells the sources hold (all sources), for diagnostics and tests.
  std::size_t heldCellCount() const {return heldCells();}

  /// Test seam: called with the callback's name at the start of every guarded
  /// callback ("grid callback", "survey_area callback", "assessment timer") and
  /// with "area registration" (a new id, between its two containers), "area
  /// projection" (before the queued definitions are projected) and "grid fold"
  /// (before a placed grid is added to its union); a throw from it
  /// exercises the containment of an unexpected exception.
  void setFaultInjectorForTest(std::function<void(const char *)> injector)
  {
    fault_injector_ = std::move(injector);
  }

  CallbackReturn on_configure(const rclcpp_lifecycle::State & state)
  {
    bool ok = false;
    try {
      ok = configureEntities();
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "Configure failed: %s", e.what());
    }
    if (!ok) {
      // Whatever was created before the failure goes with it: the node returns
      // to UNCONFIGURED and a second configure starts from nothing.
      releaseConfiguredEntities();
      return CallbackReturn::FAILURE;
    }
    return LifecycleNode::on_configure(state);
  }

  CallbackReturn on_activate(const rclcpp_lifecycle::State & state)
  {
    // The first tick assesses even when nothing changed since configure.
    force_assessment_ = true;
    last_status_log_ = std::chrono::steady_clock::now();
    timer_ = create_wall_timer(
      std::chrono::nanoseconds(static_cast<int64_t>(assessment_period_s_ * 1.0e9)),
      [this]() {guarded("assessment timer", [this]() {onTimer();});});
    const auto result = LifecycleNode::on_activate(state);
    // A reset while inactive could not publish (the publishers were not activated),
    // so the old run's result is still latched; publish the emptied one now that
    // they are. After the base class: it is what activates the publishers.
    if (result == CallbackReturn::SUCCESS && publish_empty_on_activate_) {
      publish_empty_on_activate_ = false;
      publishEmptyAssessment();
    }
    return result;
  }

  CallbackReturn on_deactivate(const rclcpp_lifecycle::State & state)
  {
    if (timer_) {
      timer_->cancel();
      timer_.reset();
    }
    return LifecycleNode::on_deactivate(state);
  }

  CallbackReturn on_cleanup(const rclcpp_lifecycle::State & state)
  {
    releaseConfiguredEntities();
    return LifecycleNode::on_cleanup(state);
  }

  CallbackReturn on_shutdown(const rclcpp_lifecycle::State & state)
  {
    releaseConfiguredEntities();
    return LifecycleNode::on_shutdown(state);
  }

private:
  /// Everything known about one source (one grid topic).
  struct SourceState
  {
    SourceState(std::string topic_in, std::string name_in, double cell_size)
    : topic(std::move(topic_in)), name(std::move(name_in)), accumulator(cell_size) {}

    std::string topic;
    /// What CoverageContribution.source carries: the configured name, else the topic.
    std::string name;
    SourceAccumulator accumulator;
    /// True once a grid of this source has been folded in.
    bool have_grid = false;
    /// True while the LATEST grid of this source could not be placed (no
    /// transform to its frame). While any source has it, nothing is assessed.
    bool placement_failed = false;
    /// The frame of the grid that could not be placed, for the log.
    std::string failed_frame;
    /// That grid, kept so it can be folded in when its frame resolves (the
    /// source may never send another one). Only the latest is kept.
    std::optional<grid_map::GridMap> unplaced_grid;
    bool resolution_warned = false;
    bool uncertainty_warned = false;
    /// Last time each repeating warning was logged for this source: the throttle
    /// is per source, so one noisy source cannot hide another's warning.
    LogThrottle last_unplaced_warn;
    LogThrottle last_convert_warn;
    LogThrottle last_rejected_warn;
    rclcpp::Subscription<grid_map_msgs::msg::GridMap>::SharedPtr subscription;
  };

  /// A SurveyArea as received.
  struct RawArea
  {
    marine_interfaces::msg::SurveyArea msg;
    int64_t stamp_ns = 0;
  };

  /// One area id. `pending` holds definitions that passed the checks possible on
  /// arrival and wait to be projected into the registry; `accepted` is the last
  /// definition the registry accepted, which a rejected or older update never
  /// displaces and which stamp ordering is judged against.
  struct AreaEntry
  {
    std::vector<RawArea> pending;
    std::optional<RawArea> accepted;
  };

  enum class SurfaceStatus
  {
    kOk,
    kUnavailable,
    kStale,
    kTooFar,
    kMisconfigured
  };

  void declareParameters()
  {
    // Declared once, in the constructor, so a second configure never meets the
    // ParameterAlreadyDeclared exception rclcpp_lifecycle would swallow. Every one
    // is read at configure only, so every one is read_only: a runtime set is
    // rejected by rclcpp instead of being accepted and ignored. A launch/YAML
    // override still applies at declaration.
    rcl_interfaces::msg::ParameterDescriptor ro;
    ro.read_only = true;
    auto desc = [&ro](const std::string & text) {
        rcl_interfaces::msg::ParameterDescriptor d = ro;
        d.description = text;
        return d;
      };
    declare_parameter<std::vector<std::string>>(
      "grid_topics", std::vector<std::string>{},
      desc("CUBE grid topics, one per source; the order defines the order of "
      "CoverageAssessment.sources. Required: configure fails when empty or duplicated."));
    declare_parameter<std::vector<std::string>>(
      "source_names", std::vector<std::string>{},
      desc("Per-source label (platform name), same length as grid_topics; an empty entry, "
      "or an empty list, falls back to the topic."));
    declare_parameter<std::string>(
      "survey_area_topic", "survey_area", desc("SurveyArea input (transient_local)."));
    declare_parameter<double>(
      "assessment_period", 1.0, desc("Assessment timer period, seconds."));
    declare_parameter<double>(
      "cell_size", 1.0,
      desc("Assessment lattice spacing, metres; should equal the sources' cell size."));
    declare_parameter<std::string>(
      "map_frame", "map", desc("Frame of the assessment raster (and of both outputs)."));
    declare_parameter<std::string>(
      "water_surface_frame", "map_tide",
      desc("Frame whose origin is the water surface; one boat's, e.g. boat1/map_tide."));
    declare_parameter<std::string>(
      "water_surface_boat_frame", "base_link",
      desc("The tide-publishing boat, for the distance bound."));
    declare_parameter<double>(
      "water_surface_max_boat_distance", 1500.0,
      desc("Metres; beyond this the one-surface assumption is refused: warn and skip scoring."));
    declare_parameter<double>(
      "water_surface_max_age", 30.0,
      desc("Seconds; oldest acceptable water-surface transform."));
    declare_parameter<int64_t>(
      "max_raster_cells", 1000000,
      desc("Per-area raster cap, lattice cells (guards a swapped latitude/longitude)."));
    declare_parameter<int64_t>(
      "max_pass_cell_checks", 16000000,
      desc("Work budget of one assessment pass, which checks every held survey area's whole "
      "bounding box once per source: the boxes of ALL held areas, in lattice cells, times the "
      "number of sources may not exceed it (16e6 = 4e6 cells with 4 sources). An area that "
      "would take a pass over it is refused with an ERROR (an update of a held id is judged "
      "with its old size removed). At least the number of sources."));
    declare_parameter<int64_t>(
      "max_held_cells", 100000000,
      desc("Most lattice cells the node holds for all sources together (allocated in 64 x 64 "
      "blocks, 8 bytes per cell: 1e8 is about 800 MB). Each source keeps every cell it has "
      "mapped until reset, inside the survey areas or not. Once over it, no further grid is "
      "folded in and nothing is published (coverage would be under-reported), with an ERROR, "
      "until the node is reset (the ~/reset service, or deactivate, cleanup, configure, "
      "activate)."));
    declare_parameter<int64_t>(
      "max_area_vertices", 10000,
      desc("Most vertices in one SurveyArea polygon, at least 3; a definition with more is "
      "refused on arrival with an ERROR (filling a polygon scans every edge on every row, and "
      "queued definitions keep every vertex)."));
    declare_parameter<int64_t>(
      "max_areas", 20,
      desc("Most survey areas held at once, 1 to 100; a new id beyond it is refused with an "
      "ERROR. Keeps the transient_local history (100) holding the newest assessment of every "
      "area, and bounds the cost of an assessment pass."));
  }

  /// True (and `last` updated) when the warning behind `last` may be logged now.
  /// Throttled on the steady clock, not the node clock: under use_sim_time the
  /// node clock reads zero until /clock advances (and stands still while paused),
  /// which would let a repeating condition log on every tick.
  bool mayWarn(LogThrottle * last)
  {
    const auto now = std::chrono::steady_clock::now();
    if (last->logged &&
      std::chrono::duration<double>(now - last->at).count() < kWarnPeriodSeconds)
    {
      return false;
    }
    last->logged = true;
    last->at = now;
    return true;
  }

  /// True (and the call site `line` marked) when its throttled line may be logged.
  bool throttle(int line)
  {
    return mayWarn(&throttle_times_[line]);
  }

  static bool isUsableDuration(double seconds)
  {
    return std::isfinite(seconds) && seconds >= 0.001 && seconds <= 86400.0;
  }

  /// Read and validate the parameters, then create every entity. Returns false
  /// (after logging the reason) when the configuration is unusable; the caller
  /// releases whatever was already created.
  bool configureEntities()
  {
    const auto topics = get_parameter("grid_topics").as_string_array();
    const auto names = get_parameter("source_names").as_string_array();
    survey_area_topic_ = get_parameter("survey_area_topic").as_string();
    assessment_period_s_ = get_parameter("assessment_period").as_double();
    cell_size_ = get_parameter("cell_size").as_double();
    map_frame_ = get_parameter("map_frame").as_string();
    water_surface_frame_ = get_parameter("water_surface_frame").as_string();
    boat_frame_ = get_parameter("water_surface_boat_frame").as_string();
    max_boat_distance_ = get_parameter("water_surface_max_boat_distance").as_double();
    max_surface_age_s_ = get_parameter("water_surface_max_age").as_double();
    max_raster_cells_ = get_parameter("max_raster_cells").as_int();
    max_pass_cell_checks_ = get_parameter("max_pass_cell_checks").as_int();
    max_held_cells_ = get_parameter("max_held_cells").as_int();
    max_area_vertices_ = get_parameter("max_area_vertices").as_int();
    max_areas_ = get_parameter("max_areas").as_int();

    if (topics.empty()) {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: grid_topics is empty; give one CUBE grid topic per source.");
      return false;
    }
    // Duplicates are judged on the RESOLVED names (namespace expansion and
    // remapping): 'grid' and '/<ns>/grid' are one topic, which would otherwise be
    // subscribed twice and its coverage credited to two sources.
    std::map<std::string, std::string> seen;  // resolved -> as configured
    for (const auto & topic : topics) {
      if (topic.empty()) {
        RCLCPP_ERROR(get_logger(), "Configure refused: grid_topics holds an empty entry.");
        return false;
      }
      const std::string resolved_topic =
        get_node_topics_interface()->resolve_topic_name(topic);
      const auto inserted = seen.emplace(resolved_topic, topic);
      if (!inserted.second) {
        RCLCPP_ERROR(get_logger(),
          "Configure refused: grid_topics lists '%s' and '%s', which both resolve to '%s'; each "
          "source needs its own topic.", inserted.first->second.c_str(), topic.c_str(),
          resolved_topic.c_str());
        return false;
      }
    }
    if (!names.empty() && names.size() != topics.size()) {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: source_names has %zu entries but grid_topics has %zu; give one name "
        "per topic (an empty entry falls back to the topic) or leave source_names empty.",
        names.size(), topics.size());
      return false;
    }
    std::vector<std::string> resolved;
    std::set<std::string> resolved_seen;
    for (std::size_t i = 0; i < topics.size(); ++i) {
      const std::string name = (names.empty() || names[i].empty()) ? topics[i] : names[i];
      if (!resolved_seen.insert(name).second) {
        RCLCPP_ERROR(get_logger(),
          "Configure refused: source name '%s' is used by two sources; CoverageContribution.source "
          "must be unique within a message.", name.c_str());
        return false;
      }
      resolved.push_back(name);
    }
    if (!std::isfinite(cell_size_) || cell_size_ <= 0.0) {
      RCLCPP_ERROR(get_logger(), "Configure refused: cell_size=%g must be positive and finite.",
        cell_size_);
      return false;
    }
    if (!isUsableDuration(assessment_period_s_)) {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: assessment_period=%g is not a duration from 0.001 s to 86400 s.",
        assessment_period_s_);
      return false;
    }
    if (max_raster_cells_ < 1) {
      RCLCPP_ERROR(get_logger(), "Configure refused: max_raster_cells=%ld must be at least 1.",
        static_cast<long>(max_raster_cells_));  // NOLINT(runtime/int)
      return false;
    }
    if (max_pass_cell_checks_ < static_cast<int64_t>(topics.size())) {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: max_pass_cell_checks=%ld must be at least the number of sources "
        "(%zu): a pass checks every cell once per source.",
        static_cast<long>(max_pass_cell_checks_), topics.size());  // NOLINT(runtime/int)
      return false;
    }
    if (max_area_vertices_ < 3) {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: max_area_vertices=%ld must be at least 3.",
        static_cast<long>(max_area_vertices_));  // NOLINT(runtime/int)
      return false;
    }
    if (max_held_cells_ < 1) {
      RCLCPP_ERROR(get_logger(), "Configure refused: max_held_cells=%ld must be at least 1.",
        static_cast<long>(max_held_cells_));  // NOLINT(runtime/int)
      return false;
    }
    if (max_areas_ < 1 || max_areas_ > kMaxAreasLimit) {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: max_areas=%ld must be from 1 to %ld (the coverage_assessment history "
        "is %zu messages deep, so more areas could not all stay latched).",
        static_cast<long>(max_areas_), static_cast<long>(kMaxAreasLimit),  // NOLINT(runtime/int)
        kLatchedDepth);
      return false;
    }
    if (!std::isfinite(max_surface_age_s_) || max_surface_age_s_ <= 0.0 ||
      !std::isfinite(max_boat_distance_) || max_boat_distance_ <= 0.0)
    {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: water_surface_max_age=%g and water_surface_max_boat_distance=%g must "
        "be positive and finite.", max_surface_age_s_, max_boat_distance_);
      return false;
    }
    if (map_frame_.empty() || water_surface_frame_.empty() || boat_frame_.empty()) {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: map_frame, water_surface_frame and water_surface_boat_frame must not "
        "be empty.");
      return false;
    }

    stats_ = NodeStats{};
    throttle_times_.clear();
    registry_ = std::make_unique<AreaRegistry>(
      cell_size_, max_raster_cells_, max_pass_cell_checks_,
      static_cast<int64_t>(topics.size()));
    areas_.clear();
    area_order_.clear();
    areas_dirty_ = false;
    grids_changed_ = false;
    force_assessment_ = false;
    have_georef_ = false;
    georef_moved_ = false;
    held_limit_reached_ = false;
    have_last_surface_ = false;
    for (std::size_t i = 0; i < topics.size(); ++i) {
      sources_.push_back(std::make_unique<SourceState>(topics[i], resolved[i], cell_size_));
    }

    // The listener holds a reference to the buffer and feeds it from the node's
    // /tf subscription: it is destroyed first on every release path, and created
    // after the buffer here.
    tf_listener_.reset();
    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_, this);

    assessment_publisher_ = create_publisher<marine_interfaces::msg::CoverageAssessment>(
      "coverage_assessment", rclcpp::QoS(kLatchedDepth).reliable().transient_local());
    grid_publisher_ = create_publisher<grid_map_msgs::msg::GridMap>(
      "coverage_grid", rclcpp::QoS(1).reliable().transient_local());

    area_subscription_ = create_subscription<marine_interfaces::msg::SurveyArea>(
      survey_area_topic_, rclcpp::QoS(kLatchedDepth).reliable().transient_local(),
      [this](marine_interfaces::msg::SurveyArea::ConstSharedPtr msg) {
        guarded("survey_area callback", [&]() {onArea(*msg);});
      },
      qosOptions(survey_area_topic_, "reliable, transient_local"));

    // Reliable, depth 10: what the cube_bathymetry `grid` publisher offers.
    for (std::size_t i = 0; i < sources_.size(); ++i) {
      sources_[i]->subscription = create_subscription<grid_map_msgs::msg::GridMap>(
        sources_[i]->topic, rclcpp::QoS(10),
        [this, i](grid_map_msgs::msg::GridMap::ConstSharedPtr msg) {
          guarded("grid callback", [&]() {onGrid(i, *msg);});
        },
        qosOptions(sources_[i]->topic, "reliable"));
    }

    // `~/reset`, created last and released first with the other entities, so
    // "no service" reads "not configured".
    reset_service_.reset();
    reset_service_ = create_service<std_srvs::srv::Trigger>(
      "~/reset",
      [this](
        const std::shared_ptr<std_srvs::srv::Trigger::Request>,
        std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
        onReset(response.get());
      });

    RCLCPP_INFO(get_logger(),
      "Configured: %zu source(s), cell_size=%g m, raster frame '%s', water surface '%s' "
      "(boat frame '%s', max distance %g m, max age %g s), survey areas on '%s' (at most %ld).",
      sources_.size(), cell_size_, map_frame_.c_str(), water_surface_frame_.c_str(),
      boat_frame_.c_str(), max_boat_distance_, max_surface_age_s_, survey_area_topic_.c_str(),
      static_cast<long>(max_areas_));  // NOLINT(runtime/int)
    return true;
  }

  /// Release everything configure and activate created, so a reconfigure builds
  /// the node from nothing (the union included) and no callback of the old
  /// configuration can run. Shared by cleanup, shutdown and a failed configure.
  void releaseConfiguredEntities()
  {
    // First: its handler reads the registry and the sources released below.
    reset_service_.reset();
    publish_empty_on_activate_ = false;
    if (timer_) {
      timer_->cancel();
      timer_.reset();
    }
    area_subscription_.reset();
    for (auto & source : sources_) {
      source->subscription.reset();
    }
    assessment_publisher_.reset();
    grid_publisher_.reset();
    // The listener references the buffer and feeds it from the /tf subscription:
    // destroy it first.
    tf_listener_.reset();
    tf_buffer_.reset();
    sources_.clear();
    registry_.reset();
    areas_.clear();
    area_order_.clear();
  }

  /// Subscription options whose incompatible-QoS event logs an ERROR naming the
  /// topic: without it a publisher with a QoS the subscription cannot match (a
  /// volatile survey_area, say) fails silently and nothing is ever received.
  rclcpp::SubscriptionOptions qosOptions(const std::string & topic, const std::string & requested)
  {
    rclcpp::SubscriptionOptions options;
    const std::string resolved = get_node_topics_interface()->resolve_topic_name(topic);
    options.event_callbacks.incompatible_qos_callback =
      [this, resolved, requested](rclcpp::QOSRequestedIncompatibleQoSInfo &) {
        RCLCPP_ERROR(get_logger(),
          "Topic '%s': a publisher offers a QoS incompatible with this subscription (it requests "
          "%s); nothing will be received from that publisher until it offers at least that.",
          resolved.c_str(), requested.c_str());
      };
    return options;
  }

  /// Run one callback body so that no exception can leave the callback: it is
  /// logged (throttled), counted, and the node keeps running.
  template<typename F>
  void guarded(const char * what, F && body)
  {
    try {
      injectFault(what);
      body();
    } catch (const std::exception & e) {
      noteCallbackFailure(what, e.what());
    } catch (...) {
      noteCallbackFailure(what, "unknown exception");
    }
  }

  /// Test seam: lets a test throw at a named point inside a callback.
  void injectFault(const char * where)
  {
    if (fault_injector_) {
      fault_injector_(where);
    }
  }

  void noteCallbackFailure(const char * what, const char * why)
  {
    ++stats_.callback_exceptions;
    COVERAGE_LOG_THROTTLED(ERROR,
      "Unhandled exception in the %s: %s. The node keeps running.", what, why);
  }

  // ---- inputs ---------------------------------------------------------------

  /// Checks what can be checked on arrival with no transform and queues the
  /// definition for projection: vertex count, finite coordinates in range, and the
  /// SHAPE (zero area, collinear) on the plane tangent to the ellipsoid at the
  /// polygon's first vertex (localGeoPolygon). A definition that fails them never
  /// touches what is held, and stamp ordering is judged against the last definition
  /// the registry ACCEPTED, so neither a rejected update nor an unvalidated one can
  /// displace or outrank a good one; only shape-valid definitions are queued, so a
  /// burst of degenerate ones cannot push a valid one out of the queue. The
  /// standard is not checked: SurveyStandard.msg defines a negative, NaN or infinite
  /// a or b as "not set", which is a state to score and publish, not to refuse.
  void onArea(const marine_interfaces::msg::SurveyArea & msg)
  {
    ++stats_.areas_received;
    const auto & pts = msg.polygon.points;
    if (pts.size() < 3) {
      reject(msg, "fewer than 3 vertices");
      return;
    }
    if (pts.size() > static_cast<std::size_t>(max_area_vertices_)) {
      reject(msg, std::to_string(pts.size()) + " vertices, over max_area_vertices (" +
        std::to_string(max_area_vertices_) + ")");
      return;
    }
    std::vector<std::pair<double, double>> lat_lon;
    for (const auto & p : pts) {
      if (!std::isfinite(p.latitude) || !std::isfinite(p.longitude) ||
        std::fabs(p.latitude) > 90.0 || std::fabs(p.longitude) > 180.0)
      {
        reject(msg, "a vertex is not a finite latitude/longitude in range");
        return;
      }
      lat_lon.emplace_back(p.latitude, p.longitude);
    }
    // Shape only: the cell cap depends on the map frame, so the real projection
    // applies it once the georeference is captured (see geoPolygonShapeError).
    const std::string shape_error = geoPolygonShapeError(lat_lon);
    if (!shape_error.empty()) {
      reject(msg, "its shape is unusable (" + shape_error + ")");
      return;
    }
    RawArea raw;
    raw.msg = msg;
    raw.stamp_ns = static_cast<int64_t>(msg.stamp.sec) * 1000000000LL + msg.stamp.nanosec;
    auto it = areas_.find(msg.id);
    if (it == areas_.end()) {
      if (static_cast<int64_t>(areas_.size()) >= max_areas_) {
        reject(msg, "max_areas (" + std::to_string(max_areas_) + ") areas are already held");
        return;
      }
      // The entry first, the order list second: a throw between them must not
      // leave an id in area_order_ that areas_.at() then fails on every tick.
      it = areas_.emplace(msg.id, AreaEntry{}).first;
      try {
        injectFault("area registration");
        area_order_.push_back(msg.id);
      } catch (...) {
        areas_.erase(it);
        throw;
      }
    } else if (it->second.accepted && raw.stamp_ns < it->second.accepted->stamp_ns) {
      COVERAGE_LOG_THROTTLED(WARN,
        "Ignoring SurveyArea '%s': its stamp is older than the definition held.",
        msg.id.c_str());
      return;
    }
    auto & pending = it->second.pending;
    pending.push_back(std::move(raw));
    if (pending.size() > kMaxPendingPerArea) {
      // Over the bound: drop the definition with the LOWEST stamp (the earliest
      // arrival among equals), the one stamp ordering would rank last anyway. The
      // accepted definition is held apart and is never a candidate.
      const auto lower_stamp =
        [](const RawArea & x, const RawArea & y) {return x.stamp_ns < y.stamp_ns;};
      const auto victim = std::min_element(pending.begin(), pending.end(), lower_stamp);
      COVERAGE_LOG_THROTTLED(WARN,
        "SurveyArea '%s': more than %zu definitions arrived before they could be "
        "projected; dropping the one with the lowest stamp (%lld ns).",
        victim->msg.id.c_str(), kMaxPendingPerArea,
        static_cast<long long>(victim->stamp_ns));  // NOLINT(runtime/int)
      pending.erase(victim);
    }
    areas_dirty_ = true;
  }

  void reject(const marine_interfaces::msg::SurveyArea & msg, const std::string & why)
  {
    ++stats_.areas_rejected;
    RCLCPP_ERROR(get_logger(), "Rejecting SurveyArea '%s': %s%s.", msg.id.c_str(), why.c_str(),
      areas_.count(msg.id) ? "; the earlier definition of that id is kept" : "");
  }

  void onGrid(std::size_t index, const grid_map_msgs::msg::GridMap & msg)
  {
    if (index >= sources_.size()) {
      return;
    }
    SourceState & source = *sources_[index];
    ++stats_.grids_received;

    grid_map::GridMap grid;
    try {
      if (!grid_map::GridMapRosConverter::fromMessage(msg, grid)) {
        throw std::runtime_error("not a valid GridMap message");
      }
    } catch (const std::exception & e) {
      ++stats_.grids_rejected;
      if (mayWarn(&source.last_convert_warn)) {
        RCLCPP_ERROR(get_logger(),
          "Source '%s' (%s): grid message could not be converted: %s",
          source.name.c_str(), source.topic.c_str(), e.what());
      }
      return;
    }

    const std::string frame = msg.header.frame_id;
    Eigen::Isometry3d source_from_raster;
    std::string why;
    if (!sourceFromRaster(frame, &source_from_raster, &why)) {
      // Kept either way: the source may never send another grid. Before the
      // raster georeference is captured nothing can be placed at all, which is
      // the startup wait for `earth`, reported by the timer: such a grid is
      // neither counted as unplaced nor flagged, so it cannot shadow that notice.
      source.failed_frame = frame;
      source.unplaced_grid = std::move(grid);
      if (!have_georef_) {
        return;
      }
      ++stats_.grids_unplaced;
      source.placement_failed = true;
      if (mayWarn(&source.last_unplaced_warn)) {
        RCLCPP_WARN(get_logger(),
          "Source '%s' (%s): cannot place its grid in frame '%s' (%s). No assessment is published "
          "until a grid of that source is placed.",
          source.name.c_str(), source.topic.c_str(), frame.c_str(), why.c_str());
      }
      return;
    }
    foldGrid(source, grid, frame, source_from_raster);
  }

  /// Fold a placed grid into its source's union. The source counts as placed from
  /// here on, whether or not the grid itself turns out usable; but only once the
  /// accumulator has taken it: if add() throws, the kept grid and the unplaced
  /// flag stay, and the next tick tries again. `grid` may be the kept grid
  /// itself, so everything needed from it is read before it is released.
  void foldGrid(
    SourceState & source, const grid_map::GridMap & grid, std::string frame,
    const Eigen::Isometry3d & source_from_raster)
  {
    injectFault("grid fold");
    if (held_limit_reached_) {
      // Over max_held_cells: nothing more is folded in (the tick withholds and
      // says why). The grid is placed, so the source does not hold the
      // assessment as unplaced on top of that.
      ++stats_.grids_over_held_limit;
      source.placement_failed = false;
      source.failed_frame.clear();
      source.unplaced_grid.reset();
      return;
    }
    const double resolution = grid.getResolution();
    const AddResult added = source.accumulator.add(grid, rasterToSourceAffine(source_from_raster));
    source.placement_failed = false;
    source.failed_frame.clear();
    source.unplaced_grid.reset();  // `grid` may refer into it: not used again below
    if (added.status != AddStatus::kAdded) {
      ++stats_.grids_rejected;
      if (mayWarn(&source.last_rejected_warn)) {
        const std::string why = added.status == AddStatus::kMissingElevationLayer ?
          "it has no 'elevation' layer" : added.reason;
        // A footprint over the cap is a warning (it drops one message, the next
        // may be fine); the rest are unusable input.
        if (added.status == AddStatus::kFootprintTooLarge) {
          RCLCPP_WARN(get_logger(), "Source '%s' (%s): grid in frame '%s' not used: %s",
            source.name.c_str(), source.topic.c_str(), frame.c_str(), why.c_str());
        } else {
          RCLCPP_ERROR(get_logger(), "Source '%s' (%s): grid in frame '%s' not used: %s",
            source.name.c_str(), source.topic.c_str(), frame.c_str(), why.c_str());
        }
      }
      return;
    }
    ++stats_.grids_placed;
    source.have_grid = true;
    grids_changed_ = true;
    // Checked after the fold, so memory stops at most one message footprint over
    // the limit; from here on nothing more is folded in.
    if (heldCells() > static_cast<std::size_t>(max_held_cells_)) {
      held_limit_reached_ = true;
    }
    if (!added.has_uncertainty_layer && !source.uncertainty_warned) {
      source.uncertainty_warned = true;
      RCLCPP_WARN(get_logger(),
        "Source '%s' (%s): its grids have no 'uncertainty' layer, so it can cover cells but "
        "never meet a standard.", source.name.c_str(), source.topic.c_str());
    }
    if (!source.resolution_warned && std::fabs(resolution - cell_size_) > 1.0e-6) {
      source.resolution_warned = true;
      RCLCPP_WARN(get_logger(),
        "Source '%s' (%s): grid resolution %g m differs from cell_size %g m; cells are resampled "
        "onto the assessment lattice.", source.name.c_str(), source.topic.c_str(),
        resolution, cell_size_);
    }
  }

  // ---- reset ------------------------------------------------------------------

  /// `~/reset` handler: the exception containment of `guarded`, with the reason
  /// returned to the caller as well as logged.
  ///
  /// The reset takes no mutex: every callback of this node's own entities (grids,
  /// areas, the tick timer, this service) is in the one default mutually exclusive
  /// callback group, which main() spins on a SingleThreadedExecutor, so none runs while
  /// the handler empties the unions. The only other callback groups belong to
  /// libraries on their own threads (the tf2_ros::TransformListener's /tf and
  /// /tf_static, and under use_sim_time the clock's /clock); they feed the TF buffer
  /// and the clock only. CallbackGroupsHoldNothingButTfAndClockOutsideTheExecutor
  /// pins that.
  void onReset(std_srvs::srv::Trigger::Response * response)
  {
    response->success = false;
    try {
      injectFault("reset service");
      response->success = resetSession(&response->message);
    } catch (const std::exception & e) {
      response->success = false;
      response->message = std::string("failed with an exception: ") + e.what();
      noteCallbackFailure("reset service", e.what());
    } catch (...) {
      response->success = false;
      response->message = "failed with an unknown exception";
      noteCallbackFailure("reset service", "unknown exception");
    }
    if (response->success) {
      RCLCPP_INFO(get_logger(), "Reset: %s", response->message.c_str());
    } else {
      RCLCPP_WARN(get_logger(), "Reset not done: %s", response->message.c_str());
    }
  }

  /// Start a new assessment session in RAM (cube_bathymetry#167), for the run after
  /// the CUBE nodes were reset: every source's union, its kept unplaced grid and
  /// flags, the held-limit latch and the change flags go back to what a fresh
  /// configure would build. Kept on purpose: the survey areas, their projections,
  /// the registry, the subscriptions, TF, and the raster georeference (the map must
  /// not move under the areas), and the counters, which stay cumulative -- they
  /// also count areas received and rejected, which a reset that keeps the areas must
  /// not zero, and callback_exceptions is evidence of a fault. When active and the
  /// areas are projected, the emptied result is published at once
  /// (publishEmptyAssessment); when inactive, it does so on the next activation.
  /// Refuses, changing nothing, when the map frame has moved since the georeference was
  /// captured: the service keeps the georeference, so that state needs the lifecycle walk,
  /// which captures it again.
  bool resetSession(std::string * message)
  {
    if (!registry_ || !tf_buffer_) {
      *message = "refused: the node is not configured";
      return false;
    }
    // Only once a georeference exists: before the first capture raster_from_earth_
    // is still the identity, so the comparison would latch a false "map moved".
    // A node that has captured nothing has nothing to protect.
    if (have_georef_ && checkGeorefMoved()) {
      *message = "refused: the map frame '" + map_frame_ + "' <- 'earth' moved since the raster "
        "georeference was captured. This service keeps the georeference, so only the "
        "lifecycle walk (deactivate, cleanup, configure, activate), which captures it "
        "again, recovers this. Nothing was changed.";
      return false;
    }
    const std::size_t cells = heldCells();
    for (auto & source : sources_) {
      source->accumulator.clear();
      source->have_grid = false;
      // A kept unplaced grid would be folded in on the next tick and bring the old
      // session's cells back.
      source->placement_failed = false;
      source->failed_frame.clear();
      source->unplaced_grid.reset();
      source->resolution_warned = false;
      source->uncertainty_warned = false;
      source->last_unplaced_warn = LogThrottle{};
      source->last_convert_warn = LogThrottle{};
      source->last_rejected_warn = LogThrottle{};
    }
    held_limit_reached_ = false;
    grids_changed_ = false;
    force_assessment_ = true;       // the first tick assesses even if the surface did not change
    have_last_surface_ = false;
    throttle_times_.clear();
    const bool active = get_current_state().label() == "active";
    if (active) {
      publishEmptyAssessment();
    } else {
      // Nothing can be published while inactive; the next activation does it.
      publish_empty_on_activate_ = true;
    }
    *message = std::to_string(cells) + " cell(s) dropped from " +
      std::to_string(sources_.size()) + " source(s); " + std::to_string(areas_.size()) +
      " area(s), the georeference and the counters kept; node is " +
      (active ? "active" : "inactive: nothing is assessed until activated");
    return true;
  }

  /// Publish the result for the emptied unions now (the assessment and the
  /// `coverage_grid`, both latched), instead of waiting for a tick. A tick returns
  /// before assessing whenever the water surface is unavailable, stale or too far, so
  /// the old run's coverage would stay latched on both topics, and a scoreboard would
  /// keep showing it, for as long as that lasts. With nothing held, the result does not
  /// depend on the surface height, so none is needed. Only possible once the areas are
  /// projected (the georeference exists and no definition is queued); otherwise it
  /// returns without publishing. The next tick projects the queued definitions, but then
  /// meets the same water surface check as any tick, and returns before assessing while
  /// the surface is unavailable, stale or too far: the old result stays latched until a
  /// tick can assess, so for a queued definition the empty result is NOT guaranteed
  /// to be published. The change flags are put back so that tick still assesses with a
  /// real surface.
  void publishEmptyAssessment()
  {
    if (!have_georef_ || areas_dirty_ || !registry_ || registry_->areas().empty() ||
      !assessment_publisher_ || !grid_publisher_)
    {
      return;
    }
    try {
      assessAndPublish(0.0, get_clock()->now());
    } catch (const std::exception & e) {
      RCLCPP_WARN(get_logger(), "Reset: could not publish the empty assessment now: %s", e.what());
    }
    force_assessment_ = true;
    have_last_surface_ = false;
  }

  // ---- raster georeference ----------------------------------------------------

  /// Capture the raster georeference, map_frame <- earth, at the first successful
  /// lookup after configure. Polygons and sources are placed through this one
  /// transform ever after, so the target and the accumulated history cannot drift
  /// apart; the tick compares the live transform with it (checkGeorefMoved).
  bool ensureGeoref(std::string * why)
  {
    if (have_georef_) {
      return true;
    }
    try {
      raster_from_earth_ = tf2::transformToEigen(
        tf_buffer_->lookupTransform(map_frame_, "earth", tf2::TimePointZero));
    } catch (const std::exception & e) {
      *why = "no transform '" + map_frame_ + "' <- 'earth': " + e.what();
      return false;
    }
    have_georef_ = true;
    const Eigen::Vector3d o = raster_from_earth_.inverse().translation();
    gz4d::GeoPointLatLongDegrees ll(gz4d::GeoPointECEF(o.x(), o.y(), o.z()));
    RCLCPP_INFO(get_logger(),
      "Raster georeference captured: '%s' <- earth, the frame origin at %.7f, %.7f (altitude "
      "%.3f m). Survey areas and sources are placed through it until the node is reconfigured.",
      map_frame_.c_str(), ll.latitude(), ll.longitude(), ll.altitude());
    return true;
  }

  /// source_from_raster for a grid frame: frame <- earth <- captured raster.
  bool sourceFromRaster(
    const std::string & frame, Eigen::Isometry3d * source_from_raster, std::string * why)
  {
    if (!ensureGeoref(why)) {
      return false;
    }
    try {
      const Eigen::Isometry3d source_from_earth = tf2::transformToEigen(
        tf_buffer_->lookupTransform(frame, "earth", tf2::TimePointZero));
      *source_from_raster = source_from_earth * raster_from_earth_.inverse();
    } catch (const std::exception & e) {
      *why = "no transform '" + frame + "' <- 'earth': " + e.what();
      return false;
    }
    return true;
  }

  /// True while the map frame has moved since the capture. Latches: once moved,
  /// nothing is published until a reconfigure recaptures. A live transform that
  /// cannot be looked up is not evidence of movement; the captured one stays in use.
  bool checkGeorefMoved()
  {
    if (!georef_moved_) {
      Eigen::Isometry3d now;
      try {
        now = tf2::transformToEigen(
          tf_buffer_->lookupTransform(map_frame_, "earth", tf2::TimePointZero));
      } catch (const std::exception &) {
        return false;
      }
      mapFrameMovement(raster_from_earth_, now, &moved_shift_m_, &moved_angle_rad_);
      georef_moved_ = moved_shift_m_ > kGeorefTranslationTolerance ||
        moved_angle_rad_ > kGeorefRotationTolerance;
    }
    if (georef_moved_) {
      COVERAGE_LOG_THROTTLED(ERROR,
        "The map frame '%s' <- 'earth' moved by %.3f m and %.2e rad since the raster georeference "
        "was captured. The accumulated coverage and the survey areas are laid out in the captured "
        "frame and no longer line up with the moved one, so nothing is published until the node "
        "is reconfigured (deactivate, cleanup, configure, activate); the ~/reset service refuses "
        "in this state because it keeps the captured georeference.",
        map_frame_.c_str(), moved_shift_m_, moved_angle_rad_);
    }
    return georef_moved_;
  }

  /// Retry the placement of every source that holds a grid it could not place, so
  /// a source that has gone silent does not hold the assessment for everyone for
  /// ever: when its frame resolves, the kept grid is folded in and scoring resumes.
  /// A grid kept only because the georeference was missing becomes a genuinely
  /// unplaced one here when the georeference is there but its frame is not.
  void retryUnplacedSources()
  {
    for (auto & source : sources_) {
      if (!source->placement_failed && !source->unplaced_grid) {
        continue;
      }
      Eigen::Isometry3d source_from_raster;
      std::string why;
      if (!sourceFromRaster(source->failed_frame, &source_from_raster, &why)) {
        if (have_georef_ && !source->placement_failed) {
          source->placement_failed = true;
          ++stats_.grids_unplaced;
        }
        continue;
      }
      if (source->unplaced_grid) {
        foldGrid(*source, *source->unplaced_grid, source->failed_frame, source_from_raster);
      } else {
        source->placement_failed = false;
        source->failed_frame.clear();
      }
    }
  }

  // ---- assessment -----------------------------------------------------------

  /// Project the queued definitions with `map_from_earth` into the registry, in
  /// stamp order (arrival order within one stamp, so the last received wins), and
  /// A definition the registry refuses is reported and never displaces the
  /// accepted one; an id with no accepted definition left is forgotten, which
  /// frees its max_areas slot.
  void projectAreas(const Eigen::Isometry3d & map_from_earth)
  {
    auto submit = [&](const RawArea & raw) {
        AreaDefinition def;
        def.id = raw.msg.id;
        def.stamp_ns = raw.stamp_ns;
        def.standard.name = raw.msg.standard.name;
        def.standard.a = raw.msg.standard.a;
        def.standard.b = raw.msg.standard.b;
        for (const auto & p : raw.msg.polygon.points) {
          def.polygon.push_back(projectGeoPoint(p.latitude, p.longitude, map_from_earth));
        }
        return registry_->update(def);
      };
    std::vector<std::string> dropped;
    for (const auto & id : area_order_) {
      AreaEntry & entry = areas_.at(id);
      std::stable_sort(entry.pending.begin(), entry.pending.end(),
        [](const RawArea & x, const RawArea & y) {return x.stamp_ns < y.stamp_ns;});
      for (const RawArea & raw : entry.pending) {
        const RegistryResult result = submit(raw);
        if (result.outcome == RegistryOutcome::kAdded ||
          result.outcome == RegistryOutcome::kReplaced)
        {
          entry.accepted = raw;
        } else if (result.outcome == RegistryOutcome::kRejected) {
          ++stats_.areas_rejected;
          RCLCPP_ERROR(get_logger(), "Rejecting SurveyArea '%s': %s%s.", id.c_str(),
            result.reason.c_str(),
            entry.accepted ? "; the earlier definition of that id is kept" : "");
        }
      }
      entry.pending.clear();
      if (!entry.accepted) {
        dropped.push_back(id);
      }
    }
    for (const auto & id : dropped) {
      areas_.erase(id);
      area_order_.erase(std::remove(area_order_.begin(), area_order_.end(), id),
        area_order_.end());
    }
  }

  /// True when a transform is older than water_surface_max_age (`age` set). A
  /// static transform carries stamp 0; there is no age to refuse.
  bool tooOld(const geometry_msgs::msg::TransformStamped & t, double * age)
  {
    const rclcpp::Time stamp(t.header.stamp, get_clock()->get_clock_type());
    if (stamp.nanoseconds() == 0) {
      return false;
    }
    *age = (get_clock()->now() - stamp).seconds();
    return *age > max_surface_age_s_;
  }

  /// h_surface (geodetic altitude of the water-surface frame's origin) with the
  /// age and boat-distance refusals. `detail` carries the reason for the log.
  SurfaceStatus waterSurface(double * h_surface, std::string * detail)
  {
    geometry_msgs::msg::TransformStamped earth_from_surface;
    try {
      earth_from_surface =
        tf_buffer_->lookupTransform("earth", water_surface_frame_, tf2::TimePointZero);
    } catch (const std::exception & e) {
      *detail = std::string("no transform earth <- ") + water_surface_frame_ + ": " + e.what();
      return SurfaceStatus::kUnavailable;
    }
    double age = 0.0;
    if (tooOld(earth_from_surface, &age)) {
      *detail = "the transform to '" + water_surface_frame_ + "' is " + std::to_string(age) +
        " s old (water_surface_max_age " + std::to_string(max_surface_age_s_) + " s)";
      return SurfaceStatus::kStale;
    }
    const Eigen::Isometry3d earth_from_frame = tf2::transformToEigen(earth_from_surface);
    const double h = geodeticAltitude(earth_from_frame.translation());
    if (!std::isfinite(h)) {
      *detail = "the water surface height is not finite";
      return SurfaceStatus::kUnavailable;
    }

    // The curvature bound: the one-surface assumption holds only while the boat
    // that supplies the tide is near the origin of the tide's parent frame.
    std::string parent;
    if (!tf_buffer_->_getParent(water_surface_frame_, tf2::TimePointZero, parent)) {
      *detail = "'" + water_surface_frame_ +
        "' has no parent frame to measure the boat distance from";
      return SurfaceStatus::kUnavailable;
    }
    if (parent == "earth") {
      *detail = "'" + water_surface_frame_ + "' hangs directly off 'earth', so the boat distance "
        "would be measured from the centre of the Earth (about 6.4e6 m) and always refused; "
        "point water_surface_frame at a tide frame under a local frame (<boat>/map_tide under "
        "<boat>/map) and water_surface_boat_frame at that boat's base_link";
      return SurfaceStatus::kMisconfigured;
    }
    double distance = 0.0;
    try {
      const auto t = tf_buffer_->lookupTransform(parent, boat_frame_, tf2::TimePointZero);
      if (tooOld(t, &age)) {
        *detail = "the transform to the tide boat '" + boat_frame_ + "' is " +
          std::to_string(age) + " s old (water_surface_max_age " +
          std::to_string(max_surface_age_s_) + " s), so its distance from '" + parent +
          "' is unknown";
        return SurfaceStatus::kStale;
      }
      distance = std::hypot(t.transform.translation.x, t.transform.translation.y);
    } catch (const std::exception & e) {
      *detail = "cannot measure the distance of boat frame '" + boat_frame_ + "' from '" +
        parent + "': " + e.what();
      return SurfaceStatus::kUnavailable;
    }
    if (!std::isfinite(distance) || distance > max_boat_distance_) {
      *detail = "the tide boat '" + boat_frame_ + "' is " + std::to_string(distance) +
        " m from the origin of '" + parent + "' (water_surface_max_boat_distance " +
        std::to_string(max_boat_distance_) + " m)";
      return SurfaceStatus::kTooFar;
    }
    *h_surface = h;
    return SurfaceStatus::kOk;
  }

  /// Lattice cells allocated for all sources together (max_held_cells counts these).
  std::size_t heldCells() const
  {
    std::size_t cells = 0;
    for (const auto & source : sources_) {
      cells += source->accumulator.allocatedCells();
    }
    return cells;
  }

  /// The 10 s status lines. Timed on the steady clock, like the warning
  /// throttles and the wall timer that calls this: under use_sim_time the node
  /// clock stands still before /clock starts and while a simulation is paused,
  /// which silenced the status line exactly when it was needed.
  void logStatus()
  {
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - last_status_log_).count() <
      kStatusLogPeriodSeconds)
    {
      return;
    }
    last_status_log_ = now;
    std::size_t cells = 0;
    std::size_t bytes = 0;
    for (const auto & source : sources_) {
      cells += source->accumulator.elevationCellCount();
      bytes += source->accumulator.memoryBytes();
    }
    auto n = [](uint64_t v) {return static_cast<unsigned long>(v);};  // NOLINT(runtime/int)
    RCLCPP_INFO(get_logger(),
      "Holding %zu cell(s) with elevation over %zu source(s), %zu allocated of max_held_cells "
      "%ld, %.1f MB; grids %lu received, %lu placed, %lu unplaced, %lu rejected, %lu over "
      "max_held_cells; areas %lu received, %lu rejected, %zu held.",
      cells, sources_.size(), heldCells(),
      static_cast<long>(max_held_cells_),  // NOLINT(runtime/int)
      static_cast<double>(bytes) / 1.0e6, n(stats_.grids_received),
      n(stats_.grids_placed), n(stats_.grids_unplaced), n(stats_.grids_rejected),
      n(stats_.grids_over_held_limit), n(stats_.areas_received), n(stats_.areas_rejected),
      registry_ ? registry_->areas().size() : static_cast<std::size_t>(0));
    RCLCPP_INFO(get_logger(),
      "Assessments: %lu published; ticks without one: %lu waiting for an unplaced source, %lu "
      "without earth, %lu with the map frame moved, %lu over max_held_cells, %lu water surface "
      "unavailable, %lu stale, %lu tide boat too far, %lu tide frame under earth; "
      "%zu cell(s) above the surface in the last assessment; %lu callback exception(s) caught; "
      "last assessment pass %.3f s over %ld raster cell(s) x %zu source(s).",
      n(stats_.assessments_published), n(stats_.withheld_unplaced_source),
      n(stats_.withheld_no_earth), n(stats_.withheld_map_moved), n(stats_.withheld_held_limit),
      n(stats_.skipped_surface_unavailable),
      n(stats_.skipped_surface_stale), n(stats_.skipped_surface_too_far),
      n(stats_.skipped_surface_misconfigured), stats_.cells_above_surface,
      n(stats_.callback_exceptions), last_pass_seconds_,
      static_cast<long>(registry_ ? registry_->totalCells() : 0),  // NOLINT(runtime/int)
      sources_.size());
  }

  void onTimer()
  {
    if (!registry_ || !tf_buffer_ || !assessment_publisher_) {
      return;
    }
    const rclcpp::Time now = get_clock()->now();
    logStatus();

    std::string no_earth;
    const bool have_georef = ensureGeoref(&no_earth);
    if (!have_georef) {
      // Checked before anything about sources: until the georeference exists no
      // grid can be placed, and "waiting for earth" is the whole story.
      ++stats_.withheld_no_earth;
      COVERAGE_LOG_THROTTLED(WARN,
        "Not assessing: waiting for '%s' <- 'earth' (%s). Grids and survey areas are kept "
        "meanwhile; 'earth' is absent from TF for about the first 10 s after a simulation "
        "starts, which is normal.", map_frame_.c_str(), no_earth.c_str());
      return;
    }
    if (checkGeorefMoved()) {
      ++stats_.withheld_map_moved;
      return;
    }
    if (held_limit_reached_) {
      // Fail closed: grids are no longer folded in, so a score would only ever
      // under-report coverage from here on.
      ++stats_.withheld_held_limit;
      COVERAGE_LOG_THROTTLED(ERROR,
        "Not assessing: the sources hold %zu lattice cells (%.0f MB), over max_held_cells=%ld. "
        "No further grids are folded in, so coverage would be under-reported; nothing is "
        "published until the node is reset with the ~/reset service (or deactivate, cleanup, "
        "configure, activate) or restarted with a larger max_held_cells.", heldCells(),
        static_cast<double>(heldCells()) * 8.0 / 1.0e6,
        static_cast<long>(max_held_cells_));  // NOLINT(runtime/int)
      return;
    }
    retryUnplacedSources();

    // A source whose latest grid could not be placed withholds everything: the
    // message cannot represent a failed source, and zeros would read as uncovered.
    // A source that has sent nothing yet is different: it scores zero, truthfully.
    std::string unplaced;
    for (const auto & source : sources_) {
      if (source->placement_failed) {
        unplaced += (unplaced.empty() ? "" : ", ") + std::string("source '") + source->name +
          "' (" + source->topic + ", frame '" + source->failed_frame + "')";
      }
    }
    if (!unplaced.empty()) {
      ++stats_.withheld_unplaced_source;
      COVERAGE_LOG_THROTTLED(WARN,
        "Not assessing: the latest grid of %s could not be placed. Assessment resumes once "
        "each of them has a grid that is placed.", unplaced.c_str());
      return;
    }
    if (areas_.empty()) {
      return;
    }
    if (areas_dirty_) {
      // Cleared only once projected: a throw leaves the queued definitions
      // flagged, so the next tick tries again instead of never projecting them.
      injectFault("area projection");
      projectAreas(raster_from_earth_);
      areas_dirty_ = false;
      force_assessment_ = true;
    }
    if (registry_->areas().empty()) {
      return;
    }

    double h_surface = 0.0;
    std::string detail;
    switch (waterSurface(&h_surface, &detail)) {
      case SurfaceStatus::kOk:
        break;
      case SurfaceStatus::kUnavailable:
        ++stats_.skipped_surface_unavailable;
        COVERAGE_LOG_THROTTLED(WARN,
          "Not scoring: water surface unavailable (%s). Grids keep accumulating; the last "
          "assessment stays published.", detail.c_str());
        return;
      case SurfaceStatus::kStale:
        ++stats_.skipped_surface_stale;
        COVERAGE_LOG_THROTTLED(WARN,
          "Not scoring: water surface is stale (%s). Grids keep accumulating; the last "
          "assessment stays published.", detail.c_str());
        return;
      case SurfaceStatus::kTooFar:
        ++stats_.skipped_surface_too_far;
        COVERAGE_LOG_THROTTLED(WARN,
          "Not scoring: %s. Grids keep accumulating; the last assessment stays published.",
          detail.c_str());
        return;
      case SurfaceStatus::kMisconfigured:
        ++stats_.skipped_surface_misconfigured;
        COVERAGE_LOG_THROTTLED(ERROR,
          "Not scoring: the water surface is misconfigured: %s. Grids keep accumulating; the last "
          "assessment stays published.", detail.c_str());
        return;
    }

    const bool surface_changed = !have_last_surface_ ||
      std::fabs(h_surface - last_surface_) > kSurfaceChangeThreshold;
    if (!(grids_changed_ || force_assessment_ || surface_changed)) {
      return;
    }
    assessAndPublish(h_surface, now);
  }

  void assessAndPublish(double h_surface, const rclcpp::Time & now)
  {
    std::vector<const SourceAccumulator *> accumulators;
    for (const auto & source : sources_) {
      accumulators.push_back(&source->accumulator);
    }
    std::vector<AreaAssessment> assessed;
    try {
      const auto started = std::chrono::steady_clock::now();
      assessed = assessAll(*registry_, accumulators, h_surface);
      last_pass_seconds_ = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    } catch (const std::exception & e) {
      COVERAGE_LOG_THROTTLED(ERROR, "Assessment failed: %s", e.what());
      return;
    }

    std::size_t above_surface = 0;
    for (const auto & a : assessed) {
      above_surface += a.cells_above_surface;
      marine_interfaces::msg::CoverageAssessment out;
      out.header.stamp = now;
      out.header.frame_id = map_frame_;
      out.area = areas_.at(a.area_id).accepted->msg;
      out.cell_size = a.cell_size;
      out.area_total = a.area_total;
      out.area_covered = a.area_covered;
      out.area_meeting_standard = a.area_meeting_standard;
      for (std::size_t i = 0; i < a.sources.size(); ++i) {
        marine_interfaces::msg::CoverageContribution c;
        c.source = sources_[i]->name;
        c.area_covered = a.sources[i].area_covered;
        c.area_meeting_standard = a.sources[i].area_meeting_standard;
        c.area_unique = a.sources[i].area_unique;
        out.sources.push_back(c);
      }
      assessment_publisher_->publish(out);
    }
    stats_.cells_above_surface = above_surface;
    if (above_surface > 0) {
      COVERAGE_LOG_THROTTLED(WARN,
        "%zu cell(s) have a depth below -%g m (the seabed above the water surface) with the "
        "surface at %.3f m ellipsoid height; they are not counted as covered. A wrong sign, "
        "datum or a stale tide looks like this.", above_surface, kNegativeDepthMargin, h_surface);
    }

    const CoverageGridResult grid =
      buildCoverageGrid(assessed, cell_size_, map_frame_, max_raster_cells_);
    if (grid.ok) {
      auto msg = grid_map::GridMapRosConverter::toMessage(grid.grid);
      msg->header.stamp = now;
      msg->header.frame_id = map_frame_;
      grid_publisher_->publish(std::move(msg));
    } else {
      COVERAGE_LOG_THROTTLED(WARN, "No coverage_grid published: %s",
        grid.error.c_str());
    }

    ++stats_.assessments_published;
    grids_changed_ = false;
    force_assessment_ = false;
    last_surface_ = h_surface;
    have_last_surface_ = true;
  }

  // Parameters read at configure.
  std::string survey_area_topic_ = "survey_area";
  double assessment_period_s_ = 1.0;
  double cell_size_ = 1.0;
  std::string map_frame_ = "map";
  std::string water_surface_frame_ = "map_tide";
  std::string boat_frame_ = "base_link";
  double max_boat_distance_ = 1500.0;
  double max_surface_age_s_ = 30.0;
  int64_t max_raster_cells_ = 1000000;
  int64_t max_pass_cell_checks_ = 16000000;
  int64_t max_held_cells_ = 100000000;
  int64_t max_area_vertices_ = 10000;
  int64_t max_areas_ = 20;
  /// Wall time of the last assessment pass (assessAll), seconds; in the status line.
  double last_pass_seconds_ = 0.0;

  // State, all rebuilt by a configure.
  std::vector<std::unique_ptr<SourceState>> sources_;
  std::unique_ptr<AreaRegistry> registry_;
  std::map<std::string, AreaEntry> areas_;
  std::vector<std::string> area_order_;
  bool areas_dirty_ = false;
  bool grids_changed_ = false;
  bool force_assessment_ = false;
  /// The raster georeference, map_frame <- earth, captured at the first
  /// successful lookup after configure.
  Eigen::Isometry3d raster_from_earth_ = Eigen::Isometry3d::Identity();
  bool have_georef_ = false;
  /// The live map_frame <- earth has moved off the captured one: latched until a
  /// reconfigure.
  bool georef_moved_ = false;
  /// Set when the sources' held cells pass max_held_cells; cleared by configure.
  bool held_limit_reached_ = false;
  /// A reset ran while the node was inactive: publish the emptied result on the next
  /// activation, because the old run's is still latched on both topics.
  bool publish_empty_on_activate_ = false;
  double moved_shift_m_ = 0.0;
  double moved_angle_rad_ = 0.0;
  double last_surface_ = 0.0;
  bool have_last_surface_ = false;
  std::chrono::steady_clock::time_point last_status_log_{};
  NodeStats stats_;
  std::function<void(const char *)> fault_injector_;
  /// Last time each throttled call site logged (see COVERAGE_LOG_THROTTLED).
  std::map<int, LogThrottle> throttle_times_;

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp_lifecycle::LifecyclePublisher<marine_interfaces::msg::CoverageAssessment>::SharedPtr
    assessment_publisher_;
  rclcpp_lifecycle::LifecyclePublisher<grid_map_msgs::msg::GridMap>::SharedPtr grid_publisher_;
  rclcpp::Subscription<marine_interfaces::msg::SurveyArea>::SharedPtr area_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr reset_service_;
};

}  // namespace coverage_assessment

#undef COVERAGE_LOG_THROTTLED

#endif  // COVERAGE_ASSESSMENT_NODE_HPP_
