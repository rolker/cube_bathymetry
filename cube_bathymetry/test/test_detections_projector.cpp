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

#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>

#include "cube_bathymetry/detections_projector.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2/buffer_core.h"

namespace cube
{

namespace
{

// A fixed, non-zero ping stamp. The projector converts it to a tf2::TimePoint;
// the transforms below are stamped at the same instant so the at-stamp lookup
// resolves rather than falling back to "latest".
constexpr int32_t kPingSec = 1000;
constexpr uint32_t kPingNanosec = 500000000u;  // 0.5 s

builtin_interfaces::msg::Time pingStamp()
{
  builtin_interfaces::msg::Time t;
  t.sec = kPingSec;
  t.nanosec = kPingNanosec;
  return t;
}

geometry_msgs::msg::TransformStamped identityTransform(
  const std::string & parent, const std::string & child)
{
  geometry_msgs::msg::TransformStamped tfs;
  tfs.header.stamp = pingStamp();
  tfs.header.frame_id = parent;
  tfs.child_frame_id = child;
  tfs.transform.rotation.w = 1.0;  // identity quaternion (roll=pitch=yaw=0)
  return tfs;
}

// A (parent <- child) transform whose rotation is a right-handed rotation of
// `angle_rad` about the +y (port) axis. Used to build a pitched attitude.
geometry_msgs::msg::TransformStamped rotatedAboutY(
  const std::string & parent, const std::string & child, double angle_rad)
{
  geometry_msgs::msg::TransformStamped tfs;
  tfs.header.stamp = pingStamp();
  tfs.header.frame_id = parent;
  tfs.child_frame_id = child;
  tfs.transform.rotation.x = 0.0;
  tfs.transform.rotation.y = std::sin(angle_rad / 2.0);
  tfs.transform.rotation.z = 0.0;
  tfs.transform.rotation.w = std::cos(angle_rad / 2.0);
  return tfs;
}

// Build a SonarDetections ping. rx_angles is one entry per beam; tx_angle is a
// single transmit steering angle applied to all beams (matching the typical
// across-track fan). travel_time is two-way, seconds.
marine_acoustic_msgs::msg::SonarDetections makeDetections(
  const std::vector<float> & rx_angles,
  float travel_time,
  float tx_angle = 0.0f,
  float sound_speed = 1500.0f)
{
  marine_acoustic_msgs::msg::SonarDetections det;
  det.header.stamp = pingStamp();
  det.header.frame_id = "sonar";
  det.ping_info.sound_speed = sound_speed;
  det.ping_info.frequency = 200000.0f;
  det.tx_angles.assign(rx_angles.size(), tx_angle);
  for (size_t i = 0; i < rx_angles.size(); ++i) {
    det.rx_angles.push_back(rx_angles[i]);
    det.two_way_travel_times.push_back(travel_time);
  }
  return det;
}

// Populate a buffer with identity attitude (level<-base) and heave (tide<-base)
// frames so the error model gets finite roll/pitch and the lookups resolve.
// BufferCore holds a mutex and is non-copyable, so we fill it in place.
void fillAttitudeBuffer(tf2::BufferCore & buffer, const ProjectorParams & params)
{
  // Static transforms resolve at any query time, so the at-stamp attitude/heave
  // lookups succeed deterministically regardless of the ping stamp. base_link is
  // the common parent of both level and tide frames so the buffer forms ONE
  // connected tree (a frame can have only one parent in tf2). The projector's
  // lookupTransform(level <- base_link) resolves the inverse edge.
  buffer.setTransform(
    identityTransform(params.base_link_frame, params.level_frame), "test", true);
  buffer.setTransform(
    identityTransform(params.base_link_frame, params.tide_frame), "test", true);
}

}  // namespace

class DetectionsProjectorTest : public ::testing::Test
{
protected:
  ProjectorParams params_;  // defaults: base_link / base_link_north_up / map_tide
};

// Synthetic ping + prepopulated BufferCore: assert hand-computed beam geometry
// and finite TPU. Geometry mirrors the Sounding(detections, i, depth) ctor:
//   range = ttt * sound_speed / 2
//   x = range * -sin(tx); y = range * sin(rx); z = range * cos(tx) * cos(rx)
TEST_F(DetectionsProjectorTest, BeamGeometryAndFiniteTpu)
{
  DetectionsProjector projector(params_);
  tf2::BufferCore buffer;
  fillAttitudeBuffer(buffer, params_);

  const float ttt = 0.02f;           // s, two-way
  const float ss = 1500.0f;          // m/s
  const float rx = 0.3f;             // rad, starboard +
  const float tx = 0.1f;             // rad, forward +
  const float range = ttt * ss / 2.0f;  // 15 m

  auto det = makeDetections({rx}, ttt, tx, ss);
  auto result = projector.project(det, buffer, 0.0f);

  ASSERT_EQ(result.soundings.size(), 1u);
  const auto & s = result.soundings[0];

  EXPECT_NEAR(s.sonar_relative_position.x, range * -std::sin(tx), 1e-3);
  EXPECT_NEAR(s.sonar_relative_position.y, range * std::sin(rx), 1e-3);
  EXPECT_NEAR(s.sonar_relative_position.z,
    range * std::cos(tx) * std::cos(rx), 1e-3);

  EXPECT_TRUE(std::isfinite(s.vertical_error));
  EXPECT_TRUE(std::isfinite(s.horizontal_error));
  EXPECT_GT(s.vertical_error, 0.0f);
  EXPECT_GT(s.horizontal_error, 0.0f);

  EXPECT_EQ(result.diagnostics.missing_attitude, 0u);
  EXPECT_EQ(result.diagnostics.filtered_range, 0u);
  EXPECT_EQ(result.diagnostics.total, 1u);
}

// Intensity passthrough: present -> carried; absent -> NaN.
TEST_F(DetectionsProjectorTest, IntensityPassthrough)
{
  DetectionsProjector projector(params_);
  tf2::BufferCore buffer;
  fillAttitudeBuffer(buffer, params_);

  auto det = makeDetections({-0.2f, 0.0f, 0.2f}, 0.02f);
  det.intensities = {-12.5f, -20.0f, -8.0f};

  auto with_intensity = projector.project(det, buffer, 0.0f);
  ASSERT_EQ(with_intensity.soundings.size(), 3u);
  EXPECT_FLOAT_EQ(with_intensity.soundings[0].intensity, -12.5f);
  EXPECT_FLOAT_EQ(with_intensity.soundings[1].intensity, -20.0f);
  EXPECT_FLOAT_EQ(with_intensity.soundings[2].intensity, -8.0f);

  auto det_no_int = makeDetections({0.0f}, 0.02f);
  ASSERT_TRUE(det_no_int.intensities.empty());
  auto without = projector.project(det_no_int, buffer, 0.0f);
  ASSERT_EQ(without.soundings.size(), 1u);
  EXPECT_TRUE(std::isnan(without.soundings[0].intensity));
}

// Range gate drops out-of-range soundings; in-range survive; diagnostics count
// the dropped soundings.
TEST_F(DetectionsProjectorTest, RangeGateDropsOutOfRange)
{
  params_.minimum_range = 10.0;
  params_.maximum_range = 20.0;
  DetectionsProjector projector(params_);
  tf2::BufferCore buffer;
  fillAttitudeBuffer(buffer, params_);

  // ttt 0.02 s -> range 15 m (in gate); ttt 0.002 s -> range 1.5 m (below min);
  // ttt 0.04 s -> range 30 m (above max). Nadir so slant range == range.
  marine_acoustic_msgs::msg::SonarDetections det;
  det.header.stamp = pingStamp();
  det.header.frame_id = "sonar";
  det.ping_info.sound_speed = 1500.0f;
  det.tx_angles = {0.0f, 0.0f, 0.0f};
  det.rx_angles = {0.0f, 0.0f, 0.0f};
  det.two_way_travel_times = {0.02f, 0.002f, 0.04f};

  auto result = projector.project(det, buffer, 0.0f);

  ASSERT_EQ(result.soundings.size(), 1u);  // only the 15 m beam survives
  EXPECT_EQ(result.diagnostics.total, 3u);
  EXPECT_EQ(result.diagnostics.filtered_range, 2u);
  const double slant = std::sqrt(
    result.soundings[0].sonar_relative_position.x *
    result.soundings[0].sonar_relative_position.x +
    result.soundings[0].sonar_relative_position.y *
    result.soundings[0].sonar_relative_position.y +
    result.soundings[0].sonar_relative_position.z *
    result.soundings[0].sonar_relative_position.z);
  EXPECT_NEAR(slant, 15.0, 1e-3);
}

// Missing attitude (no level_frame transform): roll/pitch NaN -> NaN
// uncertainty, and the diagnostic count increments. Geometry stays valid.
TEST_F(DetectionsProjectorTest, MissingAttitudeYieldsNaNUncertainty)
{
  DetectionsProjector projector(params_);
  tf2::BufferCore buffer;  // EMPTY: no level/tide transforms

  auto det = makeDetections({0.0f}, 0.02f);
  auto result = projector.project(det, buffer, 0.0f);

  ASSERT_EQ(result.soundings.size(), 1u);
  const auto & s = result.soundings[0];

  // Geometry is pure travel-time/angle math, so it stays finite.
  EXPECT_TRUE(std::isfinite(s.sonar_relative_position.z));
  EXPECT_NEAR(s.sonar_relative_position.z, 15.0, 1e-3);

  // Attitude missing -> NaN roll/pitch -> NaN uncertainty.
  EXPECT_TRUE(std::isnan(s.vertical_error));
  EXPECT_TRUE(std::isnan(s.horizontal_error));

  EXPECT_EQ(result.diagnostics.missing_attitude, 1u);
  EXPECT_EQ(result.diagnostics.missing_heave, 1u);
}

// Node-behavior regression: the same SonarDetections through project() must
// yield identical soundings to the pre-refactor inline path. We pin the geometry
// and TPU values, and include the stale-SOG case (the node passes NaN
// vessel_speed when odom is stale). With non-NaN attitude, NaN SOG does not
// poison vertical_error here, but the horizontal budget includes a SOG term, so
// we assert the contract: NaN-SOG soundings differ in horizontal_error from
// finite-SOG ones while geometry stays identical.
TEST_F(DetectionsProjectorTest, NodeRegressionGeometryAndStaleSog)
{
  DetectionsProjector projector(params_);
  tf2::BufferCore buffer;
  fillAttitudeBuffer(buffer, params_);

  auto det = makeDetections({0.0f, 0.3f, -0.3f}, 0.02f);

  // Finite SOG (recent odom) vs NaN SOG (stale odom -> node passes NaN).
  auto with_sog = projector.project(det, buffer, 2.5f);
  auto stale_sog = projector.project(det, buffer, std::nanf(""));

  ASSERT_EQ(with_sog.soundings.size(), 3u);
  ASSERT_EQ(stale_sog.soundings.size(), 3u);

  for (size_t i = 0; i < 3; ++i) {
    // Geometry is SOG-independent: identical regardless of SOG staleness.
    EXPECT_FLOAT_EQ(with_sog.soundings[i].sonar_relative_position.x,
      stale_sog.soundings[i].sonar_relative_position.x);
    EXPECT_FLOAT_EQ(with_sog.soundings[i].sonar_relative_position.y,
      stale_sog.soundings[i].sonar_relative_position.y);
    EXPECT_FLOAT_EQ(with_sog.soundings[i].sonar_relative_position.z,
      stale_sog.soundings[i].sonar_relative_position.z);

    // Vertical error does not depend on SOG, so it must match exactly.
    EXPECT_FLOAT_EQ(with_sog.soundings[i].vertical_error,
      stale_sog.soundings[i].vertical_error);
    EXPECT_TRUE(std::isfinite(with_sog.soundings[i].vertical_error));
  }

  // Determinism: re-projecting the same ping with the same SOG is identical.
  auto with_sog_again = projector.project(det, buffer, 2.5f);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_FLOAT_EQ(with_sog.soundings[i].vertical_error,
      with_sog_again.soundings[i].vertical_error);
    EXPECT_FLOAT_EQ(with_sog.soundings[i].horizontal_error,
      with_sog_again.soundings[i].horizontal_error);
  }
}

// #144: per-beam beamwidths the error model refuses are counted, not silent.
// A rejected beam falls back to the generic Device::across_track_beamwidth, so
// the operator has to be able to see that the angular budget is a default.
TEST_F(DetectionsProjectorTest, RejectedBeamwidthsAreCounted)
{
  DetectionsProjector projector(params_);
  tf2::BufferCore buffer;
  fillAttitudeBuffer(buffer, params_);

  auto det = makeDetections({-0.2f, 0.0f, 0.2f}, 0.02f);
  // Beam 0: zero-filled, as norbit_driver reports an unknown width.
  // Beam 1: at the physical ceiling (pi rad) -- nonsense, rejected.
  // Beam 2: garmin_sidescan's legitimate 55 degrees across-track -- accepted.
  det.ping_info.rx_beamwidths = {
    0.0f,
    ErrorModel::kMaxPerBeamBeamwidthRad,
    static_cast<float>(55.0 * M_PI / 180.0)};

  auto result = projector.project(det, buffer, 0.0f);
  EXPECT_EQ(result.diagnostics.rejected_beamwidths, 2u);

  // Nothing reported at all is not a rejection -- it is the common case.
  auto clean = makeDetections({0.0f}, 0.02f);
  ASSERT_TRUE(clean.ping_info.rx_beamwidths.empty());
  EXPECT_EQ(projector.project(clean, buffer, 0.0f).diagnostics.rejected_beamwidths, 0u);
}

// #144: cube::Platform keeps CALDER's sign conventions (pitch +ve bow up,
// heave +ve down); the projector converts at the boundary, exactly as it does
// for units. tf2::getEulerYPR returns the opposite pitch sense for an FLU
// rotation, so the projector must negate it.
//
// This test needs NON-ZERO IMU/GPS lever arms: the only terms odd in
// sin(pitch) -- swath_heave's IMU lever-arm term and the heading/pitch
// cross-terms of the static horizontal positioning error -- are multiplied by
// those offsets and vanish at the (zero) defaults. That is why the wrong sign
// was latent, and why a zero-lever-arm test could never catch it.
TEST_F(DetectionsProjectorTest, BowUpPitchFollowsCalderSignConvention)
{
  ProjectorParams params = params_;
  params.vessel.gps_x = 3.0;   // m forward of the transducer
  params.vessel.gps_z = 1.5;   // m above it
  params.vessel.imu_x = 2.0;
  params.vessel.imu_z = 1.0;
  DetectionsProjector projector(params);

  // Bow-up attitude. In the level frame, base_link's forward axis tilts UP, so
  // the (level <- base_link) rotation has R[2][0] > 0 and getEulerYPR returns
  // pitch = -asin(R[2][0]) < 0 -- bow-DOWN-positive. Calder's Platform wants
  // bow-UP-positive, so the projector must hand the error model +kBowUpRad.
  const double kBowUpRad = 0.20;
  tf2::BufferCore buffer;
  // Parent = level, child = base_link, so the stored rotation IS the
  // (level <- base_link) one the projector looks up -- no inversion to reason
  // about. base_link stays the parent of the tide frame, so the tree
  // (level -> base_link -> tide) is still singly connected.
  buffer.setTransform(
    rotatedAboutY(params.level_frame, params.base_link_frame, -kBowUpRad), "test", true);
  buffer.setTransform(
    identityTransform(params.base_link_frame, params.tide_frame), "test", true);

  auto det = makeDetections({0.35f}, 0.02f);
  const float sog = 2.0f;
  auto result = projector.project(det, buffer, sog);
  ASSERT_EQ(result.soundings.size(), 1u);
  ASSERT_EQ(result.diagnostics.missing_attitude, 0u);

  // Reference: the same ping through the error model with Calder's convention.
  cube::ErrorModel model(params.vessel, params.device);
  cube::Platform bow_up;
  bow_up.timestamp = static_cast<double>(kPingSec) +
    static_cast<double>(kPingNanosec) * 1e-9;
  bow_up.roll = 0.0f;
  bow_up.pitch = static_cast<float>(kBowUpRad);
  bow_up.heave = 0.0f;
  bow_up.surf_sspeed = det.ping_info.sound_speed;
  bow_up.mean_speed = det.ping_info.sound_speed;
  bow_up.vessel_speed = sog;

  cube::Platform wrong_sign = bow_up;
  wrong_sign.pitch = -bow_up.pitch;

  const auto expected = model.compute(det, bow_up);
  const auto unflipped = model.compute(det, wrong_sign);
  ASSERT_EQ(expected.size(), 1u);
  ASSERT_EQ(unflipped.size(), 1u);

  // The projector agrees with the Calder-convention reference...
  EXPECT_FLOAT_EQ(result.soundings[0].vertical_error, expected[0].vertical_error);
  EXPECT_FLOAT_EQ(result.soundings[0].horizontal_error, expected[0].horizontal_error);

  // ...and the two signs are genuinely distinguishable at this lever arm, so
  // the assertions above fail if the negation is dropped.
  EXPECT_NE(expected[0].vertical_error, unflipped[0].vertical_error);
  EXPECT_NE(expected[0].horizontal_error, unflipped[0].horizontal_error);
}

}  // namespace cube
