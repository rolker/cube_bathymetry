// Copyright 2026 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "cube_bathymetry/detections_projector.h"
#include "cube_bathymetry/ping_georeference.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2/LinearMath/Matrix3x3.h"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2/buffer_core.h"

/// @file
/// @brief Beam geometry of a georeferenced ping (cube_bathymetry#173): the sign
///        of `rx_angle` (the check #15 left open) and the earth-frame beam
///        vector stamped by `georeferencePing`.

namespace cube
{
namespace
{

constexpr int32_t kPingSec = 1000;
using Vec3 = std::array<double, 3>;

builtin_interfaces::msg::Time pingStamp()
{
  builtin_interfaces::msg::Time t;
  t.sec = kPingSec;
  return t;
}

geometry_msgs::msg::TransformStamped staticIdentity(
  const std::string & parent, const std::string & child)
{
  geometry_msgs::msg::TransformStamped tfs;
  tfs.header.stamp = pingStamp();
  tfs.header.frame_id = parent;
  tfs.child_frame_id = child;
  tfs.transform.rotation.w = 1.0;
  return tfs;
}

// WGS84 geodetic -> ECEF, metres.
Vec3 ecef(double lat_deg, double lon_deg, double h)
{
  const double a = 6378137.0;
  const double f = 1.0 / 298.257223563;
  const double e2 = f * (2.0 - f);
  const double lat = lat_deg * M_PI / 180.0;
  const double lon = lon_deg * M_PI / 180.0;
  const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
  return {(n + h) * std::cos(lat) * std::cos(lon), (n + h) * std::cos(lat) * std::sin(lon),
    (n * (1.0 - e2) + h) * std::sin(lat)};
}

// East, north and up unit vectors at (lat, lon), as ECEF components.
struct Enu3
{
  Vec3 east, north, up;
};

Enu3 enuAxes(double lat_deg, double lon_deg)
{
  const double lat = lat_deg * M_PI / 180.0;
  const double lon = lon_deg * M_PI / 180.0;
  return {
    {-std::sin(lon), std::cos(lon), 0.0},
    {-std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat)},
    {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)}};
}

// The earth <- sonar transform of a vessel at (lat, lon) heading @p heading_deg
// (clockwise from north), the sonar frame being the documented one: x forward,
// y to starboard, z down.
geometry_msgs::msg::TransformStamped earthToSonar(
  double lat_deg, double lon_deg, double heading_deg)
{
  const Enu3 axes = enuAxes(lat_deg, lon_deg);
  const double h = heading_deg * M_PI / 180.0;
  Vec3 forward, starboard, down;
  for (int k = 0; k < 3; ++k) {
    forward[k] = std::sin(h) * axes.east[k] + std::cos(h) * axes.north[k];
    starboard[k] = std::cos(h) * axes.east[k] - std::sin(h) * axes.north[k];
    down[k] = -axes.up[k];
  }
  // Columns of the rotation matrix are the sonar axes written in ECEF.
  const tf2::Matrix3x3 m(
    forward[0], starboard[0], down[0],
    forward[1], starboard[1], down[1],
    forward[2], starboard[2], down[2]);
  tf2::Quaternion q;
  m.getRotation(q);
  const Vec3 origin = ecef(lat_deg, lon_deg, 0.0);
  geometry_msgs::msg::TransformStamped tfs;
  tfs.header.stamp = pingStamp();
  tfs.header.frame_id = "earth";
  tfs.child_frame_id = "sonar";
  tfs.transform.translation.x = origin[0];
  tfs.transform.translation.y = origin[1];
  tfs.transform.translation.z = origin[2];
  tfs.transform.rotation.x = q.x();
  tfs.transform.rotation.y = q.y();
  tfs.transform.rotation.z = q.z();
  tfs.transform.rotation.w = q.w();
  return tfs;
}

marine_acoustic_msgs::msg::SonarDetections ping(const std::vector<float> & rx_angles)
{
  marine_acoustic_msgs::msg::SonarDetections det;
  det.header.stamp = pingStamp();
  det.header.frame_id = "sonar";
  det.ping_info.sound_speed = 1500.0f;
  det.ping_info.frequency = 200000.0f;
  for (const float rx : rx_angles) {
    det.rx_angles.push_back(rx);
    det.tx_angles.push_back(0.0f);
    det.two_way_travel_times.push_back(0.02f);  // 15 m
  }
  return det;
}

ProjectionResult project(const std::vector<float> & rx_angles)
{
  ProjectorParams params;
  DetectionsProjector projector(params);
  tf2::BufferCore buffer;
  buffer.setTransform(staticIdentity(params.base_link_frame, params.level_frame), "test", true);
  buffer.setTransform(staticIdentity(params.base_link_frame, params.tide_frame), "test", true);
  return projector.project(ping(rx_angles), buffer, 0.0f);
}

double dot(const Vec3 & a, const Vec3 & b) {return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];}

}  // namespace

// rx_angle > 0 is starboard: the touchdown is on the +y side of the sonar head.
TEST(BeamGeometry, PositiveRxAngleIsStarboardInTheSonarFrame)
{
  const auto result = project({-0.5f, 0.0f, 0.5f});
  ASSERT_EQ(result.soundings.size(), 3u);
  EXPECT_LT(result.soundings[0].sonar_relative_position.y, 0.0);
  EXPECT_NEAR(result.soundings[1].sonar_relative_position.y, 0.0, 1e-9);
  EXPECT_GT(result.soundings[2].sonar_relative_position.y, 0.0);
  for (const auto & s : result.soundings) {
    EXPECT_GT(s.sonar_relative_position.z, 0.0) << "z is down";
  }
}

// Through a rotated earth transform the beam lands on the starboard side of the
// heading, for several headings and a southern latitude: the cross-track
// component of the earth-frame vector, taken against the starboard direction,
// has the sign of rx_angle, and the vector equals the closed form
// sin(rx) * starboard + cos(rx) * down.
TEST(BeamGeometry, EarthFrameBeamVectorLandsOnTheStarboardSideOfTheHeading)
{
  const std::vector<float> rx = {-0.6f, -0.2f, 0.0f, 0.3f, 0.7f};
  const auto result = project(rx);
  ASSERT_EQ(result.soundings.size(), rx.size());
  for (const double lat : {43.0, -35.0}) {
    for (const double heading : {0.0, 90.0, 200.0, 315.0}) {
      const auto soundings = georeferencePing(result, earthToSonar(lat, -70.6, heading));
      ASSERT_EQ(soundings.size(), rx.size());
      const double h = heading * M_PI / 180.0;
      const Vec3 starboard_enu = {std::cos(h), -std::sin(h), 0.0};
      for (std::size_t i = 0; i < rx.size(); ++i) {
        const auto & v = soundings[i].sounding.beam_dir_enu;
        const double norm = std::sqrt(dot(v, v));
        EXPECT_NEAR(norm, 1.0, 1e-12) << "unit vector";
        const double cross = v[0] * starboard_enu[0] + v[1] * starboard_enu[1];
        if (rx[i] > 0.0f) {
          EXPECT_GT(cross, 0.0) << "lat " << lat << " heading " << heading;
        } else if (rx[i] < 0.0f) {
          EXPECT_LT(cross, 0.0) << "lat " << lat << " heading " << heading;
        }
        EXPECT_NEAR(v[0], std::sin(rx[i]) * std::cos(h), 1e-6) << "east";
        EXPECT_NEAR(v[1], -std::sin(rx[i]) * std::sin(h), 1e-6) << "north";
        EXPECT_NEAR(v[2], -std::cos(rx[i]), 1e-6) << "up";
      }
    }
  }
}

// The beam index rides through the georeferencing.
TEST(BeamGeometry, BeamIndexSurvivesGeoreferencing)
{
  const auto result = project({0.1f, 0.2f, 0.3f});
  const auto soundings = georeferencePing(result, earthToSonar(43.0, -70.6, 0.0));
  ASSERT_EQ(soundings.size(), 3u);
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(soundings[i].sounding.beam_index, i);
  }
  EXPECT_EQ(Sounding(10.0f).beam_index, Sounding::kNoBeamIndex);
}

// A sounding nobody stamped carries no vector and no incidence.
TEST(BeamGeometry, DefaultSoundingHasNoBeamVectorOrIncidence)
{
  const Sounding s(10.0f);
  EXPECT_TRUE(std::isnan(s.beam_dir_enu[0]));
  EXPECT_TRUE(std::isnan(s.beam_dir_enu[1]));
  EXPECT_TRUE(std::isnan(s.beam_dir_enu[2]));
  EXPECT_TRUE(std::isnan(s.incidence_angle));
}

}  // namespace cube
