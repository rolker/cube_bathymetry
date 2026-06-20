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
#include "cube_bathymetry/parameters.h"

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

// --- #47: datum-aware tide terms ---------------------------------------------

// Build a Vessel in which the only surviving contributor to a nadir beam's
// vertical_error is the vertical_reduction (draft + tide) term. Everything else
// (heave, beamwidth, angle, svp, pitch, roll) is zeroed so the test isolates the
// tide-flag behaviour. At nadir with zero attitude, swath_depth's range/angle/
// pitch terms multiply out to range_error() + beamwidth, so we also zero the
// along-track beamwidth and the range floor/percent to leave vertical_reduction
// as the only term that differs between the two flag states.
static Vessel tideIsolatingVessel()
{
  Vessel v{};
  // Kill every vertical contributor except the draft + tide reduction.
  v.draft_sdev = 0.0;
  v.ddraft_sdev = 0.0;
  v.loading_sdev = 0.0;
  v.heave_fixed_sdev = 0.0;
  v.heave_var_percent = 0.0;
  v.imu_off_sdev = 0.0;
  v.imu_rp_align_sdev = 0.0;
  v.pitch_sdev = 0.0;
  v.pitch_stab_sdev = 0.0;
  v.roll_sdev = 0.0;
  v.svp_sdev = 0.0;
  v.surf_ss_sdev = 0.0;
  // Tide terms left at their 0.02 m defaults so they contribute when enabled.
  return v;
}

static Device rangeFreeDevice()
{
  Device d{};
  d.along_track_beamwidth = 0.0;  // kill beamwidth_err
  d.across_track_beamwidth = 0.0;  // kill the angle-measurement term
  d.range_error_percent = 0.0;     // kill the range term entirely
  d.range_error_floor_m = 0.0;
  return d;
}

TEST_F(ErrorModelTest, TideTermsOffByDefaultEllipsoidReferenced)
{
  auto platform = makePlatform();
  auto det = makeDetections({0.0f}, 0.02f);  // nadir beam, zero attitude

  Vessel ellipsoidal = tideIsolatingVessel();  // ellipsoidal_referenced=true (default)
  Vessel datum = tideIsolatingVessel();
  datum.ellipsoidal_referenced = false;

  Device d = rangeFreeDevice();

  ErrorModel em_ellipsoidal(ellipsoidal, d);
  ErrorModel em_datum(datum, d);

  auto s_ellipsoidal = em_ellipsoidal.compute(det, platform);
  auto s_datum = em_datum.compute(det, platform);

  ASSERT_EQ(s_ellipsoidal.size(), 1u);
  ASSERT_EQ(s_datum.size(), 1u);

  // With everything else zeroed, the ellipsoidal-referenced budget collapses to 0.
  EXPECT_NEAR(s_ellipsoidal[0].vertical_error, 0.0f, 1e-9);

  // Tidal-datum mode adds tide_measured_sdev^2 + tide_predicted_sdev^2.
  const double expected_tide = datum.tide_measured_sdev * datum.tide_measured_sdev +
    datum.tide_predicted_sdev * datum.tide_predicted_sdev;  // 0.02^2 + 0.02^2 = 8e-4
  EXPECT_NEAR(s_datum[0].vertical_error, expected_tide, 1e-9);

  // And datum mode is strictly larger (the tide variances add positively).
  EXPECT_GT(s_datum[0].vertical_error, s_ellipsoidal[0].vertical_error);
}

// --- #47: parameterized device range error -----------------------------------

// Isolate range_error() in the vertical budget at nadir with zero attitude, the
// same way tideIsolatingVessel() does, but keep the range term alive. Then
// vertical_error == range_error(depth) == max(percent*|depth|, floor)^2.
static Vessel rangeIsolatingVessel()
{
  Vessel v = tideIsolatingVessel();  // already zeroes everything but tide
  v.tide_measured_sdev = 0.0;
  v.tide_predicted_sdev = 0.0;
  return v;
}

TEST_F(ErrorModelTest, RangeErrorParameterizedByDevice)
{
  auto platform = makePlatform();
  // 0.02 s two-way at 1500 m/s -> range 15 m, nadir -> depth -15 m.
  auto det = makeDetections({0.0f}, 0.02f);
  const double depth = 15.0;  // |depth|

  Vessel v = rangeIsolatingVessel();

  auto verticalErrorFor = [&](double percent, double floor) {
      Device d{};
      d.along_track_beamwidth = 0.0;
      d.across_track_beamwidth = 0.0;
      d.range_error_percent = percent;
      d.range_error_floor_m = floor;
      ErrorModel em(v, d);
      return static_cast<double>(em.compute(det, platform).at(0).vertical_error);
    };

  // Percent dominates (0.01*15 = 0.15 m > 0.05 floor): error == (0.15)^2.
  EXPECT_NEAR(verticalErrorFor(0.01, 0.05), 0.01 * depth * 0.01 * depth, 1e-6);
  // Doubling the percent quadruples the contribution (it is squared).
  EXPECT_NEAR(verticalErrorFor(0.02, 0.05), 0.02 * depth * 0.02 * depth, 1e-6);
}

TEST_F(ErrorModelTest, RangeErrorFloorDominatesAtShallowDepth)
{
  auto platform = makePlatform();
  // Very shallow: 0.0001 s two-way at 1500 m/s -> range 0.075 m, depth -0.075 m.
  // 0.005*0.075 = 3.75e-4 m << floor, so the floor must dominate.
  auto det = makeDetections({0.0f}, 0.0001f);

  Vessel v = rangeIsolatingVessel();

  auto verticalErrorFor = [&](double floor) {
      Device d{};
      d.along_track_beamwidth = 0.0;
      d.across_track_beamwidth = 0.0;
      d.range_error_percent = 0.005;
      d.range_error_floor_m = floor;
      ErrorModel em(v, d);
      return static_cast<double>(em.compute(det, platform).at(0).vertical_error);
    };

  const double small_floor = verticalErrorFor(0.05);
  const double large_floor = verticalErrorFor(0.2);

  // The floor feeds range_error which feeds swath_depth -> vertical_error, so a
  // larger floor must yield a larger vertical_error (distinguishes a floor bug).
  EXPECT_GT(large_floor, small_floor);
  // Pin the magnitudes: each equals floor^2 since the floor dominates here.
  EXPECT_NEAR(small_floor, 0.05 * 0.05, 1e-9);
  EXPECT_NEAR(large_floor, 0.2 * 0.2, 1e-9);
}

// --- #47: defaults --- tide OFF by default, range default pinned --------------

// The new defaults deliberately CHANGE Calder's range placeholder (5% -> 0.5%),
// so this is NOT a "behaviour preserved" test for range. It pins the two things
// the defaults must guarantee: (a) tide terms are off by default, and (b) the
// default range behaviour follows max(0.005*|depth|, 0.05).
TEST_F(ErrorModelTest, DefaultsTideOffAndRangeBehaviourPinned)
{
  auto platform = makePlatform();

  // (a) A default Vessel must behave identically to one that is explicitly
  // ellipsoidal-referenced -- i.e. the tide terms are off by default.
  {
    auto det = makeDetections({0.0f}, 0.02f);
    Vessel default_vessel{};                 // ellipsoidal_referenced defaults true
    Vessel explicit_ellipsoidal{};
    explicit_ellipsoidal.ellipsoidal_referenced = true;
    Device d{};
    ErrorModel em_default(default_vessel, d);
    ErrorModel em_explicit(explicit_ellipsoidal, d);
    auto s_default = em_default.compute(det, platform);
    auto s_explicit = em_explicit.compute(det, platform);
    ASSERT_EQ(s_default.size(), 1u);
    ASSERT_EQ(s_explicit.size(), 1u);
    EXPECT_FLOAT_EQ(s_default[0].vertical_error, s_explicit[0].vertical_error);
  }

  // (b) Pin the new default range behaviour by isolating range_error in the
  // vertical budget. The default device has percent=0.005, floor=0.05.
  Vessel v = rangeIsolatingVessel();
  auto verticalErrorAt = [&](float travel_time) {
      Device d{};                       // defaults: 0.005 percent, 0.05 floor
      d.along_track_beamwidth = 0.0;    // isolate the range term
      d.across_track_beamwidth = 0.0;
      auto det = makeDetections({0.0f}, travel_time);
      ErrorModel em(v, d);
      return static_cast<double>(em.compute(det, platform).at(0).vertical_error);
    };

  // Deep beam: 0.04 s -> range 30 m -> depth -30 m. 0.005*30 = 0.15 m > floor,
  // so the percent dominates: error == (0.005*30)^2.
  const double deep = 30.0;
  EXPECT_NEAR(verticalErrorAt(0.04f), 0.005 * deep * 0.005 * deep, 1e-6);

  // Shallow beam: 0.0001 s -> range 0.075 m. 0.005*0.075 << 0.05 floor, so the
  // floor dominates: error == 0.05^2.
  EXPECT_NEAR(verticalErrorAt(0.0001f), 0.05 * 0.05, 1e-9);
}

// --- #47: defaults stay inside the IHO Order 1a vertical budget ---------------

// Pin the defaults against the issue's acceptance criterion: at representative
// survey depths the default per-sounding vertical TPU must sit inside the IHO
// Order 1a allowance. Parameters::maxVarianceAllowed gives the CUBE propagation
// budget (the IHO numerator divided by CONF_95PC^2); the per-sounding error must
// be no larger than the full IHO numerator (the 1-sigma budget), which is the
// quantity a single sounding is compared against. We use the 95% numerator =
// maxVarianceAllowed * CONF_95PC^2.
TEST_F(ErrorModelTest, DefaultsInsideIHOOrder1aBudget)
{
  auto platform = makePlatform();
  Vessel v{};   // all defaults (ellipsoidal-referenced, no tide)
  Device d{};   // defaults: 0.005 percent, 0.05 floor
  ErrorModel em(v, d);

  Parameters params{CellSizes(1.0f), "order1a"};

  // Travel times chosen so the nadir depth lands near 2, 10 and 20 m.
  // depth = range = ttt * 1500 / 2 -> ttt = depth * 2 / 1500.
  for (double depth : {2.0, 10.0, 20.0}) {
    float travel_time = static_cast<float>(depth * 2.0 / 1500.0);
    auto det = makeDetections({0.0f}, travel_time);
    auto soundings = em.compute(det, platform);
    ASSERT_EQ(soundings.size(), 1u);

    // The IHO 1-sigma variance budget (numerator before the CONF_95PC^2 divide).
    const double iho_budget = params.maxVarianceAllowed(depth) * CONF_95PC * CONF_95PC;
    EXPECT_GT(soundings[0].vertical_error, 0.0f);
    EXPECT_LT(soundings[0].vertical_error, iho_budget)
      << "default vertical TPU exceeds IHO Order 1a budget at depth " << depth;
  }
}

}  // namespace cube
