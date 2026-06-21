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


#ifndef CUBE_BATHYMETRY__DETECTIONS_PROJECTOR_H_
#define CUBE_BATHYMETRY__DETECTIONS_PROJECTOR_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "cube_bathymetry/error_model.h"
#include "cube_bathymetry/sounding.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"
#include "tf2/buffer_core.h"
#include "tf2/time.h"

// This header is intentionally rclcpp-free. It is part of the no-rclcpp
// cube_bathymetry library target so the projection math can be unit-tested and
// reused offline (bag replay) without a ROS node. Do NOT add rclcpp,
// rclcpp_lifecycle, tf2_ros, or any node-bound include here.

namespace cube
{

/// Plain-data projector configuration: TF frame names, range gates, and the
/// error-model Vessel/Device. No ROS node, no rclcpp.
  struct ProjectorParams
  {
  /// TF frames. Defaults are the unprefixed mru_transform names; namespaced
  /// deployments must override them (see the package README "Configuring frames").
    std::string base_link_frame = "base_link";
    std::string level_frame = "base_link_north_up";  // level, north-aligned
    std::string tide_frame = "map_tide";

  /// Range gate, meters. A sounding is kept only when its slant range from the
  /// sonar head is in [minimum_range, maximum_range].
    double minimum_range = 0.0;
    double maximum_range = 12000.0;

  /// Error-model tuning. Vessel::ellipsoidal_referenced and Device range-error
  /// parameters are configured by the caller before constructing the projector.
    cube::Vessel vessel;
    cube::Device device;
  };

/// Counts of soundings/pings handled by a single project() call. The projector
/// itself never logs; the caller (node or offline tool) reports these.
  struct ProjectionDiagnostics
  {
  /// Soundings dropped because their slant range fell outside the range gate.
    size_t filtered_range = 0;

  /// Total soundings produced by the error model before range filtering.
    size_t total = 0;

  /// 1 when the attitude TF (level_frame <- base_link_frame) was missing for
  /// this ping, so roll/pitch (and hence per-sounding uncertainty) are NaN.
    size_t missing_attitude = 0;

  /// 1 when the heave TF (tide_frame <- base_link_frame) was missing; heave
  /// defaults to 0 (it enters the budget only squared, so this is non-critical).
    size_t missing_heave = 0;
  };

/// Result of projecting one SonarDetections message: the (range-filtered)
/// soundings plus the diagnostics the caller logs.
  struct ProjectionResult
  {
    std::vector < cube::Sounding > soundings;
    ProjectionDiagnostics diagnostics;
  };

/// Turns a SonarDetections ping into per-beam soundings (sonar-frame x/y/z plus
/// per-sounding TPU from cube::ErrorModel), drawing roll/pitch/heave from a TF
/// buffer at the ping stamp. This is the node-free extraction of the projection
/// that previously lived inline in detections_to_pointcloud's callback; the node
/// and bag_to_geotiff are both thin adapters over it.
  class DetectionsProjector
  {
public:
    explicit DetectionsProjector(const ProjectorParams & params);

  /// Project one ping into soundings.
  ///
  /// @param detections   the ping. Per-beam geometry comes from
  ///        two_way_travel_times / tx_angles / rx_angles and ping_info.sound_speed;
  ///        Sounding::intensity is copied from detections.intensities[i] and is
  ///        NaN when intensities is absent or shorter than the beam index --
  ///        consumers must NOT interpret that NaN as zero backscatter.
  /// @param tf  must have been populated with /tf + /tf_static covering (or near)
  ///        the ping stamp. Attitude/heave are looked up at the ping stamp with a
  ///        fall-back to the latest available transform (lookupAtOrLatest). When
  ///        the attitude transform is missing, roll/pitch are NaN and the
  ///        resulting uncertainty is NaN (diagnostics.missing_attitude is set).
  /// @param vessel_speed_mps  speed-over-ground. Pass NaN when SOG is unavailable
  ///        (e.g. a detections-only bag); the error model accepts NaN.
    ProjectionResult project(
      const marine_acoustic_msgs::msg::SonarDetections & detections,
      const tf2::BufferCore & tf,
      float vessel_speed_mps) const;

    const ProjectorParams & params() const {return params_;}

private:
  /// Look up target<-source at the ping stamp; if TF is momentarily behind (an
  /// "extrapolation into the future", common under bag replay), fall back to the
  /// latest available transform. Attitude/heave vary slowly, so tens of ms of
  /// staleness is harmless. Returns false only if no transform is available.
    static bool lookupAtOrLatest(
      const tf2::BufferCore & tf,
      const std::string & target, const std::string & source,
      const tf2::TimePoint & stamp, geometry_msgs::msg::TransformStamped & out);

    ProjectorParams params_;
    double minimum_range_sq_;
    double maximum_range_sq_;
    std::shared_ptr < cube::ErrorModel > error_model_;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__DETECTIONS_PROJECTOR_H_
