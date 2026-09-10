// Copyright 2025 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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


#include "cube_bathymetry/detections_projector.h"

#include <chrono>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "tf2/exceptions.h"
// tf2/utils.hpp's getEulerYPR resolves the quaternion via tf2::fromMsg, whose
// definition lives in tf2_geometry_msgs (header-only). Include it so the symbol
// is emitted in this TU, and BEFORE tf2/utils.hpp (ODR for fromMsg). This source
// stays free of rclcpp/tf2_ros *includes* -- what makes the projector node-free
// and offline-testable. (The cube_bathymetry .so still links rclcpp transitively
// via marine_autonomy and tf2_geometry_msgs->tf2_ros; that is unchanged here.)
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2/utils.hpp"

namespace cube
{

namespace
{

// Convert a builtin_interfaces stamp (sec/nanosec) to a tf2::TimePoint WITHOUT
// going through tf2_ros::fromMsg -- that keeps this translation unit, and the
// library as a whole, free of any rclcpp/tf2_ros header that might pull rclcpp.
tf2::TimePoint stampToTimePoint(const builtin_interfaces::msg::Time & stamp)
{
  // tf2::TimePoint is a std::chrono time_point with nanosecond duration.
  const auto seconds = std::chrono::seconds(stamp.sec);
  const auto nanoseconds = std::chrono::nanoseconds(stamp.nanosec);
  return tf2::TimePoint(
    std::chrono::duration_cast<tf2::Duration>(seconds + nanoseconds));
}

}  // namespace

DetectionsProjector::DetectionsProjector(const ProjectorParams & params)
: params_(params),
  minimum_range_sq_(params.minimum_range * params.minimum_range),
  maximum_range_sq_(params.maximum_range * params.maximum_range),
  error_model_(std::make_shared<cube::ErrorModel>(params.vessel, params.device))
{
}

bool DetectionsProjector::lookupAtOrLatest(
  const tf2::BufferCore & tf,
  const std::string & target, const std::string & source,
  const tf2::TimePoint & stamp, geometry_msgs::msg::TransformStamped & out)
{
  try {
    out = tf.lookupTransform(target, source, stamp);
    return true;
  } catch (const tf2::ExtrapolationException &) {
    try {
      out = tf.lookupTransform(target, source, tf2::TimePointZero);
      return true;
    } catch (const tf2::TransformException &) {
      return false;
    }
  } catch (const tf2::TransformException &) {
    return false;
  }
}

ProjectionResult DetectionsProjector::project(
  const marine_acoustic_msgs::msg::SonarDetections & detections,
  const tf2::BufferCore & tf,
  float vessel_speed_mps) const
{
  ProjectionResult result;

  // Convert the ping stamp to tf2::TimePoint once. BufferCore::lookupTransform
  // takes a tf2::TimePoint, not an rclcpp::Time.
  const tf2::TimePoint stamp = stampToTimePoint(detections.header.stamp);

  cube::Platform platform;
  platform.timestamp =
    static_cast<double>(detections.header.stamp.sec) +
    static_cast<double>(detections.header.stamp.nanosec) * 1e-9;

  // Attitude (roll/pitch) from TF at the ping stamp -- the SAME pose used
  // downstream to place the soundings, so the error budget is coherent.
  // Position and heading are not needed by the error model.
  geometry_msgs::msg::TransformStamped level;
  if (lookupAtOrLatest(tf, params_.level_frame, params_.base_link_frame, stamp, level)) {
    double y, p, r;
    tf2::getEulerYPR(level.transform.rotation, y, p, r);
    platform.roll = static_cast<float>(r);
    platform.pitch = static_cast<float>(p);
    (void)y;  // yaw/heading unused by the error model
  } else {
    platform.roll = std::nan("");
    platform.pitch = std::nan("");
    result.diagnostics.missing_attitude = 1;
  }

  // Heave = boat vertical offset from the tide-corrected surface. It enters the
  // budget only squared, so it is non-critical; default to 0 if absent.
  geometry_msgs::msg::TransformStamped tide;
  if (lookupAtOrLatest(tf, params_.tide_frame, params_.base_link_frame, stamp, tide)) {
    platform.heave = static_cast<float>(tide.transform.translation.z);
  } else {
    platform.heave = 0.0f;
    result.diagnostics.missing_heave = 1;
  }

  // Speed over ground is caller-supplied (NaN when unavailable).
  platform.vessel_speed = vessel_speed_mps;

  platform.mean_speed = detections.ping_info.sound_speed;
  platform.surf_sspeed = detections.ping_info.sound_speed;

  // Count per-beam beamwidths the error model will refuse, using the model's
  // own predicate so the two can never drift apart. The projector never logs;
  // the caller reports this (#144).
  for (const float reported : detections.ping_info.rx_beamwidths) {
    if (!cube::ErrorModel::per_beam_beamwidth_usable(reported)) {
      ++result.diagnostics.rejected_beamwidths;
    }
  }

  auto soundings = error_model_->compute(detections, platform);
  result.diagnostics.total = soundings.size();

  result.soundings.reserve(soundings.size());
  for (const auto & sounding : soundings) {
    const double range_sq =
      sounding.sonar_relative_position.x * sounding.sonar_relative_position.x +
      sounding.sonar_relative_position.y * sounding.sonar_relative_position.y +
      sounding.sonar_relative_position.z * sounding.sonar_relative_position.z;
    if (range_sq >= minimum_range_sq_ && range_sq <= maximum_range_sq_) {
      result.soundings.push_back(sounding);
    }
  }
  result.diagnostics.filtered_range = soundings.size() - result.soundings.size();

  return result;
}

}  // namespace cube
