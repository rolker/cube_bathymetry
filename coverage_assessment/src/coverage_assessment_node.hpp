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
#include "tf2/exceptions.h"
#include "tf2/time.h"
#include "tf2_eigen/tf2_eigen.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

#include "coverage_assessment/area_registry.h"
#include "coverage_assessment/assessment_raster.h"
#include "coverage_assessment/coverage_assessor.h"
#include "coverage_assessment/source_accumulator.h"
#include "frame_chain.hpp"

namespace coverage_assessment
{

/// Counters for the node's diagnostics: logged with the periodic status line and
/// read back by tests. Reset on every configure.
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
  uint64_t skipped_surface_unavailable = 0;
  uint64_t skipped_surface_stale = 0;
  uint64_t skipped_surface_too_far = 0;
  /// cells with depth below -kNegativeDepthMargin in the last assessment, over
  /// all areas (see AreaAssessment::cells_above_surface)
  std::size_t cells_above_surface = 0;
};

/// Lifecycle node: scores how much of each SurveyArea the sources' grids cover
/// and how much of that meets the area's standard. The judging lives in the
/// plain-C++ core; this class converts messages and frames into the core's
/// structs and publishes the results. See README.md for the design.
class CoverageAssessmentNode : public rclcpp_lifecycle::LifecycleNode
{
public:
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

  explicit CoverageAssessmentNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : rclcpp_lifecycle::LifecycleNode("coverage_assessment", options)
  {
    declareParameters();
  }

  /// Counters, for diagnostics and tests.
  const NodeStats & stats() const {return stats_;}

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
    last_status_log_ = get_clock()->now();
    timer_ = create_wall_timer(
      std::chrono::nanoseconds(static_cast<int64_t>(assessment_period_s_ * 1.0e9)),
      [this]() {onTimer();});
    return LifecycleNode::on_activate(state);
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
    bool resolution_warned = false;
    bool uncertainty_warned = false;
    /// Last time each repeating warning was logged for this source: the throttle
    /// is per source, so one noisy source cannot hide another's warning.
    rclcpp::Time last_unplaced_warn{0, 0, RCL_ROS_TIME};
    rclcpp::Time last_convert_warn{0, 0, RCL_ROS_TIME};
    rclcpp::Time last_rejected_warn{0, 0, RCL_ROS_TIME};
    rclcpp::Subscription<grid_map_msgs::msg::GridMap>::SharedPtr subscription;
  };

  /// A SurveyArea as received, kept so its polygon can be projected again when
  /// the earth -> map transform changes.
  struct RawArea
  {
    marine_interfaces::msg::SurveyArea msg;
    int64_t stamp_ns = 0;
  };

  struct AreaEntry
  {
    RawArea current;
    /// The last definition the registry accepted, to fall back to when a newer
    /// one is rejected.
    std::optional<RawArea> last_good;
  };

  enum class SurfaceStatus
  {
    kOk,
    kUnavailable,
    kStale,
    kTooFar
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
  }

  /// True (and `last` updated) when the warning behind `last` may be logged now.
  bool mayWarn(rclcpp::Time * last)
  {
    const rclcpp::Time now = get_clock()->now();
    if (last->nanoseconds() != 0 && (now - *last).seconds() < kWarnPeriodSeconds) {
      return false;
    }
    *last = now;
    return true;
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

    if (topics.empty()) {
      RCLCPP_ERROR(get_logger(),
        "Configure refused: grid_topics is empty; give one CUBE grid topic per source.");
      return false;
    }
    std::set<std::string> seen;
    for (const auto & topic : topics) {
      if (topic.empty()) {
        RCLCPP_ERROR(get_logger(), "Configure refused: grid_topics holds an empty entry.");
        return false;
      }
      if (!seen.insert(topic).second) {
        RCLCPP_ERROR(get_logger(),
          "Configure refused: grid_topics lists '%s' twice; each source needs its own topic.",
          topic.c_str());
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
    registry_ = std::make_unique<AreaRegistry>(cell_size_, max_raster_cells_);
    areas_.clear();
    area_order_.clear();
    areas_dirty_ = false;
    grids_changed_ = false;
    force_assessment_ = false;
    have_map_from_earth_ = false;
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
      [this](marine_interfaces::msg::SurveyArea::ConstSharedPtr msg) {onArea(*msg);});

    // Reliable, depth 10: what the cube_bathymetry `grid` publisher offers.
    for (std::size_t i = 0; i < sources_.size(); ++i) {
      sources_[i]->subscription = create_subscription<grid_map_msgs::msg::GridMap>(
        sources_[i]->topic, rclcpp::QoS(10),
        [this, i](grid_map_msgs::msg::GridMap::ConstSharedPtr msg) {onGrid(i, *msg);});
    }

    RCLCPP_INFO(get_logger(),
      "Configured: %zu source(s), cell_size=%g m, raster frame '%s', water surface '%s' "
      "(boat frame '%s', max distance %g m, max age %g s), survey areas on '%s'.",
      sources_.size(), cell_size_, map_frame_.c_str(), water_surface_frame_.c_str(),
      boat_frame_.c_str(), max_boat_distance_, max_surface_age_s_, survey_area_topic_.c_str());
    return true;
  }

  /// Release everything configure and activate created, so a reconfigure builds
  /// the node from nothing (the union included) and no callback of the old
  /// configuration can run. Shared by cleanup, shutdown and a failed configure.
  void releaseConfiguredEntities()
  {
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

  // ---- inputs ---------------------------------------------------------------

  void onArea(const marine_interfaces::msg::SurveyArea & msg)
  {
    ++stats_.areas_received;
    const auto & pts = msg.polygon.points;
    if (pts.size() < 3) {
      reject(msg, "fewer than 3 vertices");
      return;
    }
    for (const auto & p : pts) {
      if (!std::isfinite(p.latitude) || !std::isfinite(p.longitude) ||
        std::fabs(p.latitude) > 90.0 || std::fabs(p.longitude) > 180.0)
      {
        reject(msg, "a vertex is not a finite latitude/longitude in range");
        return;
      }
    }
    const double a = msg.standard.a;
    const double b = msg.standard.b;
    if (!std::isfinite(a) || !std::isfinite(b) || a < 0.0 || b < 0.0) {
      reject(msg, "the standard's a or b is negative or not finite");
      return;
    }
    RawArea raw;
    raw.msg = msg;
    raw.stamp_ns = static_cast<int64_t>(msg.stamp.sec) * 1000000000LL + msg.stamp.nanosec;
    auto it = areas_.find(msg.id);
    if (it != areas_.end() && raw.stamp_ns < it->second.current.stamp_ns) {
      RCLCPP_WARN(get_logger(),
        "Ignoring SurveyArea '%s': its stamp is older than the definition held.", msg.id.c_str());
      return;
    }
    if (it == areas_.end()) {
      area_order_.push_back(msg.id);
      areas_[msg.id] = AreaEntry{raw, std::nullopt};
    } else {
      it->second.current = raw;
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
    Eigen::Isometry3d source_from_map;
    try {
      const auto t = tf_buffer_->lookupTransform(frame, map_frame_, tf2::TimePointZero);
      source_from_map = tf2::transformToEigen(t);
    } catch (const std::exception & e) {
      ++stats_.grids_unplaced;
      source.placement_failed = true;
      source.failed_frame = frame;
      if (mayWarn(&source.last_unplaced_warn)) {
        RCLCPP_WARN(get_logger(),
          "Source '%s' (%s): cannot place its grid in frame '%s' (no transform from raster frame "
          "'%s': %s). No assessment is published until one of its grids is placed; 'earth' is "
          "absent from TF for about the first 10 s after a simulation starts, which is normal.",
          source.name.c_str(), source.topic.c_str(), frame.c_str(), map_frame_.c_str(), e.what());
      }
      return;
    }

    const AddResult added = source.accumulator.add(grid, rasterToSourceAffine(source_from_map));
    if (added.status != AddStatus::kAdded) {
      ++stats_.grids_rejected;
      if (mayWarn(&source.last_rejected_warn)) {
        RCLCPP_ERROR(get_logger(),
          "Source '%s' (%s): grid in frame '%s' not used: %s", source.name.c_str(),
          source.topic.c_str(), frame.c_str(),
          added.status == AddStatus::kMissingElevationLayer ?
          "it has no 'elevation' layer" : "its geometry or placement is unusable");
      }
      return;
    }
    ++stats_.grids_placed;
    source.have_grid = true;
    source.placement_failed = false;
    source.failed_frame.clear();
    grids_changed_ = true;
    if (!added.has_uncertainty_layer && !source.uncertainty_warned) {
      source.uncertainty_warned = true;
      RCLCPP_WARN(get_logger(),
        "Source '%s' (%s): its grids have no 'uncertainty' layer, so it can cover cells but "
        "never meet a standard.", source.name.c_str(), source.topic.c_str());
    }
    if (!source.resolution_warned && std::fabs(grid.getResolution() - cell_size_) > 1.0e-6) {
      source.resolution_warned = true;
      RCLCPP_WARN(get_logger(),
        "Source '%s' (%s): grid resolution %g m differs from cell_size %g m; cells are resampled "
        "onto the assessment lattice.", source.name.c_str(), source.topic.c_str(),
        grid.getResolution(), cell_size_);
    }
  }

  // ---- assessment -----------------------------------------------------------

  static bool sameTransform(const Eigen::Isometry3d & x, const Eigen::Isometry3d & y)
  {
    return (x.translation() - y.translation()).norm() < 1.0e-6 &&
           (x.linear() - y.linear()).cwiseAbs().maxCoeff() < 1.0e-9;
  }

  /// Project every held polygon with `map_from_earth` into the registry.
  void projectAreas(const Eigen::Isometry3d & map_from_earth)
  {
    std::vector<std::string> dropped;
    for (const auto & id : area_order_) {
      AreaEntry & entry = areas_.at(id);
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
      const RegistryResult result = submit(entry.current);
      if (result.outcome == RegistryOutcome::kAdded ||
        result.outcome == RegistryOutcome::kReplaced)
      {
        entry.last_good = entry.current;
        continue;
      }
      if (result.outcome == RegistryOutcome::kRejected && !(entry.last_good &&
        entry.last_good->stamp_ns == entry.current.stamp_ns &&
        entry.last_good->msg == entry.current.msg))
      {
        ++stats_.areas_rejected;
        RCLCPP_ERROR(get_logger(), "Rejecting SurveyArea '%s': %s%s.", id.c_str(),
          result.reason.c_str(),
          entry.last_good ? "; the earlier definition of that id is kept" : "");
      }
      if (entry.last_good) {
        // Keep the earlier definition, projected with the current transform.
        entry.current = *entry.last_good;
        submit(entry.current);
      } else {
        dropped.push_back(id);
      }
    }
    for (const auto & id : dropped) {
      areas_.erase(id);
      area_order_.erase(std::remove(area_order_.begin(), area_order_.end(), id),
        area_order_.end());
    }
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
    // A static transform carries stamp 0; there is no age to refuse.
    const rclcpp::Time stamp(earth_from_surface.header.stamp, get_clock()->get_clock_type());
    if (stamp.nanoseconds() != 0) {
      const double age = (get_clock()->now() - stamp).seconds();
      if (age > max_surface_age_s_) {
        *detail = "the transform to '" + water_surface_frame_ + "' is " + std::to_string(age) +
          " s old (water_surface_max_age " + std::to_string(max_surface_age_s_) + " s)";
        return SurfaceStatus::kStale;
      }
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
    double distance = 0.0;
    try {
      const auto t = tf_buffer_->lookupTransform(parent, boat_frame_, tf2::TimePointZero);
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

  void logStatus(const rclcpp::Time & now)
  {
    if ((now - last_status_log_).seconds() < kStatusLogPeriodSeconds) {
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
      "Holding %zu cell(s) with elevation over %zu source(s), %.1f MB; grids %lu received, %lu "
      "placed, %lu unplaced, %lu rejected; areas %lu received, %lu rejected, %zu held.",
      cells, sources_.size(), static_cast<double>(bytes) / 1.0e6, n(stats_.grids_received),
      n(stats_.grids_placed), n(stats_.grids_unplaced), n(stats_.grids_rejected),
      n(stats_.areas_received), n(stats_.areas_rejected),
      registry_ ? registry_->areas().size() : static_cast<std::size_t>(0));
    RCLCPP_INFO(get_logger(),
      "Assessments: %lu published; ticks without one: %lu waiting for an unplaced source, %lu "
      "without earth, %lu water surface unavailable, %lu stale, %lu tide boat too far; "
      "%zu cell(s) above the surface in the last assessment.",
      n(stats_.assessments_published), n(stats_.withheld_unplaced_source),
      n(stats_.withheld_no_earth), n(stats_.skipped_surface_unavailable),
      n(stats_.skipped_surface_stale), n(stats_.skipped_surface_too_far),
      stats_.cells_above_surface);
  }

  void onTimer()
  {
    if (!registry_ || !tf_buffer_ || !assessment_publisher_) {
      return;
    }
    const rclcpp::Time now = get_clock()->now();
    logStatus(now);

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
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
        "Not assessing: the latest grid of %s could not be placed. Assessment resumes once "
        "each of them has a grid that is placed.", unplaced.c_str());
      return;
    }
    if (areas_.empty()) {
      return;
    }

    Eigen::Isometry3d map_from_earth;
    try {
      map_from_earth = tf2::transformToEigen(
        tf_buffer_->lookupTransform(map_frame_, "earth", tf2::TimePointZero));
    } catch (const std::exception & e) {
      ++stats_.withheld_no_earth;
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
        "Not assessing: cannot project the survey areas, no transform '%s' <- 'earth' (%s). "
        "'earth' is absent from TF for about the first 10 s after a simulation starts, which is "
        "normal.", map_frame_.c_str(), e.what());
      return;
    }
    if (areas_dirty_ || !have_map_from_earth_ || !sameTransform(map_from_earth, map_from_earth_)) {
      map_from_earth_ = map_from_earth;
      have_map_from_earth_ = true;
      areas_dirty_ = false;
      projectAreas(map_from_earth);
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
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
          "Not scoring: water surface unavailable (%s). Grids keep accumulating; the last "
          "assessment stays published.", detail.c_str());
        return;
      case SurfaceStatus::kStale:
        ++stats_.skipped_surface_stale;
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
          "Not scoring: water surface is stale (%s). Grids keep accumulating; the last "
          "assessment stays published.", detail.c_str());
        return;
      case SurfaceStatus::kTooFar:
        ++stats_.skipped_surface_too_far;
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
          "Not scoring: %s. Grids keep accumulating; the last assessment stays published.",
          detail.c_str());
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
      assessed = assessAll(*registry_, accumulators, h_surface);
    } catch (const std::exception & e) {
      RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 10000, "Assessment failed: %s", e.what());
      return;
    }

    std::size_t above_surface = 0;
    for (const auto & a : assessed) {
      above_surface += a.cells_above_surface;
      marine_interfaces::msg::CoverageAssessment out;
      out.header.stamp = now;
      out.header.frame_id = map_frame_;
      out.area = areas_.at(a.area_id).current.msg;
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
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
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
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000, "No coverage_grid published: %s",
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

  // State, all rebuilt by a configure.
  std::vector<std::unique_ptr<SourceState>> sources_;
  std::unique_ptr<AreaRegistry> registry_;
  std::map<std::string, AreaEntry> areas_;
  std::vector<std::string> area_order_;
  bool areas_dirty_ = false;
  bool grids_changed_ = false;
  bool force_assessment_ = false;
  Eigen::Isometry3d map_from_earth_ = Eigen::Isometry3d::Identity();
  bool have_map_from_earth_ = false;
  double last_surface_ = 0.0;
  bool have_last_surface_ = false;
  rclcpp::Time last_status_log_{0, 0, RCL_ROS_TIME};
  NodeStats stats_;

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp_lifecycle::LifecyclePublisher<marine_interfaces::msg::CoverageAssessment>::SharedPtr
    assessment_publisher_;
  rclcpp_lifecycle::LifecyclePublisher<grid_map_msgs::msg::GridMap>::SharedPtr grid_publisher_;
  rclcpp::Subscription<marine_interfaces::msg::SurveyArea>::SharedPtr area_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace coverage_assessment

#endif  // COVERAGE_ASSESSMENT_NODE_HPP_
