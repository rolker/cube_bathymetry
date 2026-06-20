// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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
#include <vector>
#include "cube_bathymetry/error_model.h"

namespace cube
{

class ErrorModelTest : public ::testing::Test
{
protected:
  Platform makePlatform()
  {
    Platform p{};
    p.timestamp = 0.0;
    p.roll = 0.0f;
    p.pitch = 0.0f;
    p.heave = 0.0f;
    p.surf_sspeed = 1500.0f;
    p.mean_speed = 1500.0f;
    p.vessel_speed = 0.0f;
    return p;
  }

  marine_acoustic_msgs::msg::SonarDetections makeDetections(
    const std::vector<float> & rx_angles,
    float travel_time,
    float sound_speed = 1500.0f)
  {
    marine_acoustic_msgs::msg::SonarDetections det;
    det.ping_info.sound_speed = sound_speed;
    det.ping_info.frequency = 200000.0f;

    for (size_t i = 0; i < rx_angles.size(); ++i) {
      det.rx_angles.push_back(rx_angles[i]);
      det.two_way_travel_times.push_back(travel_time);
    }
    return det;
  }

  Vessel vessel{};
  Device device{};
};

TEST_F(ErrorModelTest, ConstructorWithDefaults)
{
  EXPECT_NO_THROW(ErrorModel(vessel, device));
}

TEST_F(ErrorModelTest, ComputeNadirBeamReturnsSingleSounding)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();
  auto det = makeDetections({0.0f}, 0.02f);

  auto soundings = em.compute(det, platform);
  EXPECT_EQ(soundings.size(), 1u);
}

TEST_F(ErrorModelTest, ComputeReturnsCorrectCount)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();
  auto det = makeDetections({-0.5f, -0.25f, 0.0f, 0.25f, 0.5f}, 0.02f);

  auto soundings = em.compute(det, platform);
  EXPECT_EQ(soundings.size(), 5u);
}

TEST_F(ErrorModelTest, DepthIsNegative)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();
  // 0.02s two-way at 1500 m/s → ~15m range, nadir beam → depth ~ -15m
  auto det = makeDetections({0.0f}, 0.02f);

  auto soundings = em.compute(det, platform);
  ASSERT_EQ(soundings.size(), 1u);
  EXPECT_LT(soundings[0].depth, 0.0f);
}

TEST_F(ErrorModelTest, ErrorsAreNonNegative)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();
  auto det = makeDetections({0.0f, 0.3f, -0.3f}, 0.02f);

  auto soundings = em.compute(det, platform);
  for (const auto & s  : soundings) {
    EXPECT_GE(s.vertical_error, 0.0f);
    EXPECT_GE(s.horizontal_error, 0.0f);
  }
}

TEST_F(ErrorModelTest, VerticalErrorIncreasesWithBeamAngle)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();

  auto det_nadir = makeDetections({0.0f}, 0.02f);
  auto det_30deg = makeDetections({0.5236f}, 0.02f);  // ~30 degrees
  auto det_60deg = makeDetections({1.0472f}, 0.02f);  // ~60 degrees

  auto s_nadir = em.compute(det_nadir, platform);
  auto s_30 = em.compute(det_30deg, platform);
  auto s_60 = em.compute(det_60deg, platform);

  EXPECT_LT(s_nadir[0].vertical_error, s_30[0].vertical_error);
  EXPECT_LT(s_30[0].vertical_error, s_60[0].vertical_error);
}

TEST_F(ErrorModelTest, HorizontalErrorVariesWithBeamAngle)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();

  auto det_nadir = makeDetections({0.0f}, 0.02f);
  auto det_45deg = makeDetections({0.7854f}, 0.02f);  // ~45 degrees

  auto s_nadir = em.compute(det_nadir, platform);
  auto s_45 = em.compute(det_45deg, platform);

  // Horizontal error changes with beam angle (not constant)
  EXPECT_NE(s_nadir[0].horizontal_error, s_45[0].horizontal_error);
}

TEST_F(ErrorModelTest, DepthIncreasesWithTravelTime)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();

  auto det_shallow = makeDetections({0.0f}, 0.01f);
  auto det_deep = makeDetections({0.0f}, 0.04f);

  auto s_shallow = em.compute(det_shallow, platform);
  auto s_deep = em.compute(det_deep, platform);

  // Depth is negative, so deeper means more negative
  EXPECT_GT(s_shallow[0].depth, s_deep[0].depth);
}

TEST_F(ErrorModelTest, SymmetricAnglesProduceSimilarErrors)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();

  auto det_port = makeDetections({-0.5f}, 0.02f);
  auto det_stbd = makeDetections({0.5f}, 0.02f);

  auto s_port = em.compute(det_port, platform);
  auto s_stbd = em.compute(det_stbd, platform);

  // Errors should be very similar for symmetric angles
  EXPECT_NEAR(s_port[0].vertical_error, s_stbd[0].vertical_error,
    s_port[0].vertical_error * 0.01);
  EXPECT_NEAR(s_port[0].horizontal_error, s_stbd[0].horizontal_error,
    s_port[0].horizontal_error * 0.01);
}

TEST_F(ErrorModelTest, EmptyDetectionsReturnsEmpty)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();
  auto det = makeDetections({}, 0.02f);

  auto soundings = em.compute(det, platform);
  EXPECT_TRUE(soundings.empty());
}

// Regression for #46 bug 1: the horizontal sound-speed-profile term (Eqn 3.77)
// must scale as svp_sdev^2 (a variance), not svp_sdev^4. The original double-
// squared it (used sound_speed_profile_variance = svp_sdev^2 and then squared the
// whole expression). Every horizontal term that depends on the SVP error scales as
// svp_sdev^2 in the correct model, so the SVP-dependent part of horizontal_error
// must be exactly quadratic: net(2s) == 4*net(s). The bug makes it grow faster.
TEST_F(ErrorModelTest, ProfileErrorScalesQuadraticallyWithSvp)
{
  auto platform = makePlatform();
  // Off-nadir beam so the profile and ang_svp terms are active (both vanish at
  // nadir). pitch=0 keeps the geometry simple.
  auto det = makeDetections({0.6f}, 0.02f);

  // Isolate svp_sdev: zero every other horizontal-error contributor so the only
  // svp-dependent terms left are profile_err (the fixed one) and ang_svp (inside
  // swath_angle_error), and float cancellation in the net() subtraction stays
  // negligible.
  auto horizontalError = [&](double svp) {
      Vessel v{};
      v.gps_off_sdev = 0.0; v.gps_latency_sdev = 0.0; v.gps_drms = 0.0;
      v.gps_latency = 0.0; v.imu_off_sdev = 0.0; v.imu_rp_align_sdev = 0.0;
      v.imu_g_align_sdev = 0.0; v.imu_latency_sdev = 0.0; v.tx_latency_sdev = 0.0;
      v.roll_sdev = 0.0; v.pitch_sdev = 0.0; v.gyro_sdev = 0.0;
      v.surf_ss_sdev = 0.0; v.sog_sdev = 0.0;
      v.svp_sdev = svp;
      Device d{};
      d.across_track_beamwidth = 0.0;  // kill the beamwidth angle term (ang_meas)
      ErrorModel em(v, d);
      auto soundings = em.compute(det, platform);
      return static_cast<double>(soundings.at(0).horizontal_error);
    };

  const double he0 = horizontalError(0.0);  // svp-independent baseline
  const double net1 = horizontalError(1.0) - he0;
  const double net2 = horizontalError(2.0) - he0;

  ASSERT_GT(net1, 0.0);  // the svp terms genuinely contribute at this beam angle
  // Both surviving svp terms are quadratic in svp_sdev in the correct model, so
  // their sum is too: net2 == 4*net1. The bug makes profile_err quartic, which
  // pushes the ratio to ~13.6.
  EXPECT_NEAR(net2, 4.0 * net1, 1e-3 * net1);
}

}  // namespace cube
