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


#ifndef CUBE_BATHYMETRY__SOUNDING_H_
#define CUBE_BATHYMETRY__SOUNDING_H_

#include "cube_bathymetry/common.h"
#include "geometry_msgs/msg/point.hpp"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"

namespace cube
{

  struct Sounding
  {
    explicit Sounding(float depth)
    : depth(depth)
  {
    }

    Sounding(const marine_acoustic_msgs::msg::SonarDetections & detections, size_t i, float depth)
      : depth(depth)
  {
    auto range = detections.two_way_travel_times[i] * detections.ping_info.sound_speed / 2.0;
    float tx_angle = 0.0;
    if(i < detections.tx_angles.size()) {
        tx_angle = detections.tx_angles[i];
    }
    sonar_relative_position.x = range * -sin(tx_angle);
    sonar_relative_position.y = range * sin(detections.rx_angles[i]);
    sonar_relative_position.z = range * cos(tx_angle) * cos(detections.rx_angles[i]);

    // Per-beam acoustic intensity (backscatter). Sonar-reported and usually
    // uncalibrated; for the Kongsberg M3 (via kongsberg_em_bridge) it is
    // reflectivity in dB. Left NaN when the source omits intensities so a
    // missing value is never mistaken for a real measurement.
    if(i < detections.intensities.size()) {
        intensity = detections.intensities[i];
    }
    }

  /// Depth relative to the sea surface. Positive is up above sea surface
  /// and negative is down below sea surface
    float depth = std::nan("");
    float vertical_error = 0.0;
    float horizontal_error = 0.0;

  /// Per-beam acoustic intensity / backscatter (NaN when not reported).
    float intensity = std::nan("");

  // Position relative to the sonar head, in meters.
  // For a typical down looking sonar, x is along the heading, y is to starboard, and z is down.
    geometry_msgs::msg::Point sonar_relative_position;
  };

  struct MapSounding : public MapPosition
  {
    MapSounding(double x, double y, float z)
      : MapPosition(x, y), sounding(z)
  {
    }
    Sounding sounding;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__SOUNDING_H_
