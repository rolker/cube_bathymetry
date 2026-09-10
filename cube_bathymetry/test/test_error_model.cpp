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
#include <limits>
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

// Regression for #16: vessel speed enters every horizontal-latency term squared,
// so the horizontal error is identical for +v and -v. This documents why no
// negative-speed guard is needed in horizontal_latency() (the removed dead code
// claimed to "avoid negative speed" but the squaring already neutralizes sign).
TEST_F(ErrorModelTest, HorizontalErrorIsInsensitiveToVesselSpeedSign)
{
  ErrorModel em(vessel, device);
  auto det = makeDetections({0.0f}, 0.02f);  // nadir → cos_pitch term maximal

  auto p_pos = makePlatform();
  p_pos.vessel_speed = 5.0f;
  auto p_neg = makePlatform();
  p_neg.vessel_speed = -5.0f;
  auto p_zero = makePlatform();
  p_zero.vessel_speed = 0.0f;

  auto s_pos = em.compute(det, p_pos);
  auto s_neg = em.compute(det, p_neg);
  auto s_zero = em.compute(det, p_zero);
  ASSERT_EQ(s_pos.size(), 1u);
  ASSERT_EQ(s_neg.size(), 1u);
  ASSERT_EQ(s_zero.size(), 1u);

  // Sign-insensitive: +v and -v give the same horizontal error.
  EXPECT_FLOAT_EQ(s_pos[0].horizontal_error, s_neg[0].horizontal_error);
  // And the speed path is genuinely exercised: nonzero speed adds latency error
  // on top of the zero-speed baseline (otherwise the equality above is vacuous).
  EXPECT_GT(s_pos[0].horizontal_error, s_zero[0].horizontal_error);
}

// Regression for the offline-import empty-epoch bug (cube_bathymetry#63): a
// non-finite vessel_speed (offline replay supplies NaN; a corrupt odom twist
// could yield +/-inf) must floor to 0 rather than NaN/inf-poisoning
// horizontal_error -- which propagates through Node::insert into the depth
// variance and zeroes the whole epoch. The floored result is finite and equals
// the zero-speed baseline.
TEST_F(ErrorModelTest, HorizontalErrorFloorsNonFiniteVesselSpeed)
{
  ErrorModel em(vessel, device);
  auto det = makeDetections({0.0f}, 0.02f);  // nadir → speed terms maximal

  auto p_nan = makePlatform();
  p_nan.vessel_speed = std::numeric_limits<float>::quiet_NaN();
  auto p_inf = makePlatform();
  p_inf.vessel_speed = std::numeric_limits<float>::infinity();
  auto p_zero = makePlatform();
  p_zero.vessel_speed = 0.0f;

  auto s_nan = em.compute(det, p_nan);
  auto s_inf = em.compute(det, p_inf);
  auto s_zero = em.compute(det, p_zero);
  ASSERT_EQ(s_nan.size(), 1u);
  ASSERT_EQ(s_inf.size(), 1u);
  ASSERT_EQ(s_zero.size(), 1u);

  // Non-finite speed floors to 0: finite horizontal_error equal to the zero-speed
  // baseline (not NaN/inf, which would zero the epoch downstream).
  EXPECT_TRUE(std::isfinite(s_nan[0].horizontal_error));
  EXPECT_TRUE(std::isfinite(s_inf[0].horizontal_error));
  EXPECT_FLOAT_EQ(s_nan[0].horizontal_error, s_zero[0].horizontal_error);
  EXPECT_FLOAT_EQ(s_inf[0].horizontal_error, s_zero[0].horizontal_error);
}

// Rewritten for #144. This test previously held the two-way travel time fixed
// (so the DEPTH shrank as the beam swung out) and asserted the total vertical
// error rose monotonically nadir -> 30 deg -> 60 deg. That only held because
// the angular term was inflated ~3283x by the degrees-as-radians bug: it
// swamped every other term, so the budget tracked sin^2(angle) alone.
//
// With the angular term at its correct magnitude the picture is the real one:
// the measured-range error projects into depth as cos^2(angle) and so SHRINKS
// off nadir, while the angular term grows as sin^2(angle) * range^2. At shallow
// depths with a narrow beam the shrinking range projection wins at moderate
// angles, so the total dips at 30 deg before the angular term takes over. The
// genuine, model-correct property is that an oblique beam is worse than nadir
// once the angular term dominates -- asserted here at constant depth, which is
// the comparison that isolates beam obliquity from a shortening water column.
TEST_F(ErrorModelTest, VerticalErrorIsWorseAtObliqueAngleAtConstantDepth)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();

  // Hold the DEPTH at 15 m rather than the slant range: travel time scales as
  // 1/cos(angle) so depth = range*cos(angle) stays put.
  //
  // Three angles, not two -- and the third one says something the pair could
  // not. The nadir-vs-60-degree comparison is a claim about ENDPOINTS, and a
  // single pair cannot distinguish "rises monotonically" from "dips and then
  // recovers". Measured at 30 degrees, the corrected model does the latter:
  // holding depth fixed, the measured-range term projects into depth as
  // cos^2(angle) and shrinks faster than the angular term's tan^2(angle) grows,
  // until the angular term takes over somewhere past 30 degrees.
  //
  // So this test pins the SHAPE, dip included, rather than asserting a
  // monotonicity that is not true of the total budget. (It is true of the
  // angular contribution in isolation -- see
  // AngularContributionToVerticalErrorRisesWithBeamAngle -- which is the claim
  // the deleted VerticalErrorIncreasesWithBeamAngle was really reaching for.)
  auto det_nadir = makeDetections({0.0f}, 0.02f);
  auto det_30deg = makeDetections({0.5236f}, 0.0230940f);  // ~30 deg, same 15 m depth
  auto det_60deg = makeDetections({1.0472f}, 0.04f);       // ~60 deg, same 15 m depth

  auto s_nadir = em.compute(det_nadir, platform);
  auto s_30 = em.compute(det_30deg, platform);
  auto s_60 = em.compute(det_60deg, platform);

  // Same depth for all three, by construction.
  ASSERT_NEAR(s_nadir[0].depth, s_30[0].depth, 1e-3);
  ASSERT_NEAR(s_nadir[0].depth, s_60[0].depth, 1e-3);

  // The property this test is named for: an oblique beam is worse than a nadir
  // beam at the same depth.
  EXPECT_LT(s_nadir[0].vertical_error, s_60[0].vertical_error);

  // ...but not by a monotone climb. Pinned so a future change that flattens or
  // reverses the intermediate dip has to come here and say so deliberately.
  EXPECT_LT(s_30[0].vertical_error, s_nadir[0].vertical_error);
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

// --- #52: per-beam backscatter carried into the sounding ----------------------

// compute() must copy SonarDetections.intensities[i] onto each sounding so the
// downstream point cloud can publish a backscatter field (x,y,z,intensity,...).
TEST_F(ErrorModelTest, CarriesPerBeamIntensity)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();
  auto det = makeDetections({-0.2f, 0.0f, 0.2f}, 0.02f);
  det.intensities = {-12.5f, -20.0f, -8.0f};  // e.g. reflectivity, dB

  auto soundings = em.compute(det, platform);
  ASSERT_EQ(soundings.size(), 3u);
  EXPECT_FLOAT_EQ(soundings[0].intensity, -12.5f);
  EXPECT_FLOAT_EQ(soundings[1].intensity, -20.0f);
  EXPECT_FLOAT_EQ(soundings[2].intensity, -8.0f);
}

// When the source omits intensities the field must be NaN, never a fabricated
// value (so a missing measurement is distinguishable downstream).
TEST_F(ErrorModelTest, IntensityIsNaNWhenAbsent)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();
  auto det = makeDetections({0.0f}, 0.02f);  // no intensities populated
  ASSERT_TRUE(det.intensities.empty());

  auto soundings = em.compute(det, platform);
  ASSERT_EQ(soundings.size(), 1u);
  EXPECT_TRUE(std::isnan(soundings[0].intensity));
}

// --- #144: angular-measurement term units, validation ------------------------

// Isolate swath_angle_error's beamwidth term (ang_meas). Everything else that
// could reach vertical_error or horizontal_error is zeroed, so at nadir with
// zero attitude:
//     horizontal_error == ang_meas * range^2
// and at beam angle A (roll = pitch = 0):
//     vertical_error   == ang_meas * range^2 * sin^2(A)
// where ang_meas = (beamwidth_rad / 12)^2.
static Vessel angleIsolatingVessel()
{
  Vessel v{};
  v.draft_sdev = 0.0;
  v.ddraft_sdev = 0.0;
  v.loading_sdev = 0.0;
  v.tide_measured_sdev = 0.0;
  v.tide_predicted_sdev = 0.0;
  v.heave_fixed_sdev = 0.0;
  v.heave_var_percent = 0.0;
  v.gps_off_sdev = 0.0;
  v.imu_off_sdev = 0.0;
  v.gps_drms = 0.0;
  v.gps_latency = 0.0;
  v.gps_latency_sdev = 0.0;
  v.imu_latency_sdev = 0.0;
  v.tx_latency_sdev = 0.0;
  v.sog_sdev = 0.0;
  v.imu_rp_align_sdev = 0.0;
  v.imu_g_align_sdev = 0.0;
  v.roll_sdev = 0.0;         // kills base_roll_variance
  v.pitch_sdev = 0.0;
  v.pitch_stab_sdev = 0.0;
  v.gyro_sdev = 0.0;
  v.svp_sdev = 0.0;          // kills ang_svp and profile_err
  v.surf_ss_sdev = 0.0;      // kills ang_surf_speed
  v.static_roll = 0.0;
  return v;
}

// Companion device: only the across-track beamwidth is left alive.
static Device angleIsolatingDevice(double across_track_beamwidth_deg)
{
  Device d{};
  d.across_track_beamwidth = across_track_beamwidth_deg;
  d.along_track_beamwidth = 0.0;   // kills beamwidth_err
  d.range_error_percent = 0.0;     // kills the range term
  d.range_error_floor_m = 0.0;
  return d;
}

// 0.02 s two-way at 1500 m/s -> 15 m range.
static constexpr double kIsolatedRange = 15.0;

TEST_F(ErrorModelTest, AngleErrorBranchesAgreeOnUnits)
{
  auto platform = makePlatform();
  const double bw_deg = 3.0;
  const double bw_rad = bw_deg * M_PI / 180.0;
  Vessel v = angleIsolatingVessel();

  // Fallback branch: no rx_beamwidths at all, device configured in degrees.
  auto det_fallback = makeDetections({0.0f}, 0.02f);
  ASSERT_TRUE(det_fallback.ping_info.rx_beamwidths.empty());
  ErrorModel em_fallback(v, angleIsolatingDevice(bw_deg));
  const double h_fallback =
    em_fallback.compute(det_fallback, platform).at(0).horizontal_error;

  // Per-beam branch: the same physical beamwidth, reported in radians as
  // marine_acoustic_msgs/PingInfo specifies. The device value is deliberately
  // different so a silent fallback would show up as a mismatch.
  auto det_perbeam = makeDetections({0.0f}, 0.02f);
  det_perbeam.ping_info.rx_beamwidths.push_back(static_cast<float>(bw_rad));
  ErrorModel em_perbeam(v, angleIsolatingDevice(bw_deg * 10.0));
  const double h_perbeam =
    em_perbeam.compute(det_perbeam, platform).at(0).horizontal_error;

  ASSERT_GT(h_fallback, 0.0);
  // The two branches must agree: both are radians by the time they are used.
  // Before #144 they disagreed by (180/pi)^2 == ~3283x.
  EXPECT_NEAR(h_perbeam, h_fallback, 1e-6 * h_fallback);
}

// Pins the absolute magnitude of the fallback term against an independently
// computed value. Deliberately NOT the stock 2-degree Device default: with the
// default this would be the same arithmetic as
// AngleErrorFallsBackOnEmptyBeamwidths and pin nothing that test does not, so
// it uses a distinct width and shows the pin tracks the configured value.
TEST_F(ErrorModelTest, AngleErrorFallbackPinnedAtNadir)
{
  auto platform = makePlatform();
  const double bw_deg = 7.5;
  Vessel v = angleIsolatingVessel();
  Device d = angleIsolatingDevice(bw_deg);
  auto det = makeDetections({0.0f}, 0.02f);

  ErrorModel em(v, d);
  const double h = em.compute(det, platform).at(0).horizontal_error;

  const double bw_rad = bw_deg * M_PI / 180.0;
  const double ang_meas = (bw_rad / 12.0) * (bw_rad / 12.0);
  const double expected = ang_meas * kIsolatedRange * kIsolatedRange;
  EXPECT_NEAR(h, expected, 1e-6 * expected);
}

TEST_F(ErrorModelTest, AngleErrorFallsBackOnEmptyBeamwidths)
{
  auto platform = makePlatform();
  // Today's real Kongsberg M3 behaviour: kongsberg_em_bridge leaves
  // rx_beamwidths empty on purpose.
  auto det = makeDetections({0.0f}, 0.02f);
  ASSERT_TRUE(det.ping_info.rx_beamwidths.empty());

  ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
  const double h = em.compute(det, platform).at(0).horizontal_error;

  const double bw_rad = 2.0 * M_PI / 180.0;
  const double expected = (bw_rad / 12.0) * (bw_rad / 12.0) *
    kIsolatedRange * kIsolatedRange;
  EXPECT_NEAR(h, expected, 1e-6 * expected);
}

TEST_F(ErrorModelTest, AngleErrorFallsBackOnZeroFilledBeamwidths)
{
  auto platform = makePlatform();
  // Today's real norbit behaviour: conversions.cpp resizes rx_beamwidths to a
  // zero-filled vector for a value it does not report. A length-only check
  // takes that 0.0 as a measurement and silently deletes the angular term.
  auto det = makeDetections({0.0f}, 0.02f);
  det.ping_info.rx_beamwidths.assign(1, 0.0f);

  ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
  const double h = em.compute(det, platform).at(0).horizontal_error;

  const double bw_rad = 2.0 * M_PI / 180.0;
  const double expected = (bw_rad / 12.0) * (bw_rad / 12.0) *
    kIsolatedRange * kIsolatedRange;
  EXPECT_GT(h, 0.0);  // not silently zeroed
  EXPECT_NEAR(h, expected, 1e-6 * expected);
}

// The 2-degree device fallback, as it reaches horizontal_error at nadir with
// every other term isolated away. Shared by the rejection tests below, all of
// which assert that a bad per-beam value lands exactly here.
static double nadirFallbackHorizontalError()
{
  const double bw_rad = 2.0 * M_PI / 180.0;
  return (bw_rad / 12.0) * (bw_rad / 12.0) * kIsolatedRange * kIsolatedRange;
}

TEST_F(ErrorModelTest, AngleErrorFallsBackOnNonFiniteBeamwidth)
{
  auto platform = makePlatform();
  const double expected = nadirFallbackHorizontalError();

  for (float bad : {std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity()})
  {
    auto det = makeDetections({0.0f}, 0.02f);
    det.ping_info.rx_beamwidths.assign(1, bad);

    ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
    const double h = em.compute(det, platform).at(0).horizontal_error;

    ASSERT_TRUE(std::isfinite(h));
    EXPECT_NEAR(h, expected, 1e-6 * expected);
  }
}

// Finite but not a measurement. Kept separate from the non-finite cases: -0.01
// used to sit in that test's list, where it was mislabelled -- it is perfectly
// finite, and it is the positivity check, not isfinite(), that rejects it.
TEST_F(ErrorModelTest, AngleErrorFallsBackOnNonPositiveBeamwidth)
{
  auto platform = makePlatform();
  const double expected = nadirFallbackHorizontalError();

  for (float bad : {-0.01f, -1.0f, -0.0f}) {
    auto det = makeDetections({0.0f}, 0.02f);
    det.ping_info.rx_beamwidths.assign(1, bad);

    ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
    const double h = em.compute(det, platform).at(0).horizontal_error;

    EXPECT_NEAR(h, expected, 1e-6 * expected);
  }
}

// The hard physical ceiling: a beam cannot subtend half a turn or more, so a
// reported width at or above pi rad is nonsense (unit mix-up, sentinel, corrupt
// field) and the device value is used instead.
TEST_F(ErrorModelTest, AngleErrorFallsBackOnPhysicallyImpossibleBeamwidth)
{
  auto platform = makePlatform();
  const double expected = nadirFallbackHorizontalError();

  for (float bad : {static_cast<float>(M_PI), 4.0f, 100.0f, 6.2831853f}) {
    auto det = makeDetections({0.0f}, 0.02f);
    det.ping_info.rx_beamwidths.assign(1, bad);

    ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
    const double h = em.compute(det, platform).at(0).horizontal_error;

    EXPECT_NEAR(h, expected, 1e-6 * expected) << "rejected value: " << bad;
  }
}

// ...and the ceiling is a PHYSICAL bound, not a plausibility clamp.
// garmin_sidescan reports 55 degrees across-track for SideVu (46 for ClearVu),
// which is correct data in the right field -- a sidescan does no across-track
// beamforming, so its receive fan genuinely is that wide. A clamp tight enough
// to reject an R2Sonic driver's misplaced 2.27 rad transmit fan would throw
// this away, which is exactly why no such clamp exists.
TEST_F(ErrorModelTest, AngleErrorAcceptsWideButLegitimateSidescanBeamwidth)
{
  auto platform = makePlatform();
  const double garmin_sidevu_rad = 55.0 * M_PI / 180.0;
  auto det = makeDetections({0.0f}, 0.02f);
  det.ping_info.rx_beamwidths.assign(1, static_cast<float>(garmin_sidevu_rad));

  ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
  const double h = em.compute(det, platform).at(0).horizontal_error;

  const double expected = (garmin_sidevu_rad / 12.0) * (garmin_sidevu_rad / 12.0) *
    kIsolatedRange * kIsolatedRange;
  EXPECT_NEAR(h, expected, 1e-5 * expected);
  // Used, not silently swapped for the 2-degree device default.
  EXPECT_GT(h, 10.0 * nadirFallbackHorizontalError());
}

// The predicate the projector reuses to count rejections must agree with what
// the model actually does, boundary included: pi is out, just below pi is in.
TEST_F(ErrorModelTest, PerBeamBeamwidthUsablePredicateMatchesTheCeiling)
{
  EXPECT_TRUE(ErrorModel::per_beam_beamwidth_usable(0.96f));
  EXPECT_TRUE(ErrorModel::per_beam_beamwidth_usable(2.27f));  // an R2Sonic driver's tx fan
  EXPECT_TRUE(
    ErrorModel::per_beam_beamwidth_usable(
      std::nextafter(ErrorModel::kMaxPerBeamBeamwidthRad, 0.0f)));
  EXPECT_FALSE(ErrorModel::per_beam_beamwidth_usable(ErrorModel::kMaxPerBeamBeamwidthRad));
  EXPECT_FALSE(ErrorModel::per_beam_beamwidth_usable(0.0f));
  EXPECT_FALSE(ErrorModel::per_beam_beamwidth_usable(-1.0f));
  EXPECT_FALSE(ErrorModel::per_beam_beamwidth_usable(std::numeric_limits<float>::quiet_NaN()));
}

TEST_F(ErrorModelTest, AngleErrorUsesReportedBeamwidthAsRadians)
{
  auto platform = makePlatform();
  // A real, usable per-beam value: 4 degrees expressed in radians, twice the
  // device's 2-degree fallback.
  const double reported_rad = 4.0 * M_PI / 180.0;
  auto det = makeDetections({0.0f}, 0.02f);
  det.ping_info.rx_beamwidths.assign(1, static_cast<float>(reported_rad));

  ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
  const double h = em.compute(det, platform).at(0).horizontal_error;

  const double expected = (reported_rad / 12.0) * (reported_rad / 12.0) *
    kIsolatedRange * kIsolatedRange;
  EXPECT_NEAR(h, expected, 1e-6 * expected);

  // And specifically NOT the pre-#144 double conversion, which would have
  // scaled the reported radians by another pi/180.
  const double double_converted = reported_rad * (M_PI / 180.0);
  const double old_wrong = (double_converted / 12.0) * (double_converted / 12.0) *
    kIsolatedRange * kIsolatedRange;
  EXPECT_GT(h, 100.0 * old_wrong);
}

// The monotone property the old test was reaching for, stated over the term it
// actually applies to: the angular-measurement contribution to the vertical
// budget rises with beam angle. Isolated so no other term can mask it.
TEST_F(ErrorModelTest, AngularContributionToVerticalErrorRisesWithBeamAngle)
{
  auto platform = makePlatform();
  ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));

  auto vertical = [&](float rx_angle) {
      auto det = makeDetections({rx_angle}, 0.02f);
      return static_cast<double>(em.compute(det, platform).at(0).vertical_error);
    };

  const double v_nadir = vertical(0.0f);
  const double v_30 = vertical(0.5236f);
  const double v_60 = vertical(1.0472f);

  EXPECT_NEAR(v_nadir, 0.0, 1e-12);  // sin^2(0) == 0
  EXPECT_LT(v_nadir, v_30);
  EXPECT_LT(v_30, v_60);
}

// --- #147: Platform::roll / Platform::pitch are RADIANS -------------------
//
// Every other test in this file zeroes attitude, which is exactly why nothing
// caught the mismatch: DetectionsProjector fed tf2::getEulerYPR's radians into
// fields ErrorModel converted as degrees, understating attitude by 57.3x. These
// tests exercise NON-ZERO roll and pitch, and each one fails against the
// pre-#147 code.

// Roll steers the beam: the reported depth is -range * cos(roll + beam angle).
// With the isolating config every other contributor is switched off, so this
// pins the geometry directly.
TEST_F(ErrorModelTest, PlatformRollIsRadiansAndSteersTheBeam)
{
  auto platform = makePlatform();
  const double roll_rad = 0.25;      // ~14.3 degrees, a real sea state
  platform.roll = static_cast<float>(roll_rad);

  const float rx_angle = 0.2f;       // rad, starboard +  ->  meas_angle = -0.2
  const double meas_angle = -static_cast<double>(rx_angle);

  ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
  auto det = makeDetections({rx_angle}, 0.02f);
  const double depth = em.compute(det, platform).at(0).depth;

  const double expected = -kIsolatedRange * std::cos(roll_rad + meas_angle);
  EXPECT_NEAR(depth, expected, 1e-4);

  // And specifically NOT the pre-#147 reading, which took the radians for
  // degrees and shrank the roll by 57.3x.
  const double old_wrong =
    -kIsolatedRange * std::cos(roll_rad * M_PI / 180.0 + meas_angle);
  EXPECT_GT(std::abs(depth - old_wrong), 0.05);
}

// Roll and pitch together, against the closed form the isolating config leaves:
//   vertical_error = (beamwidth_rad/12)^2 * range^2
//                    * sin^2(roll + beam angle) * cos^2(pitch)
// Both attitude angles therefore have to be radians for this to hold.
TEST_F(ErrorModelTest, PlatformAttitudeEntersTheVerticalBudgetInRadians)
{
  auto platform = makePlatform();
  const double roll_rad = 0.25;
  const double pitch_rad = 0.35;
  platform.roll = static_cast<float>(roll_rad);
  platform.pitch = static_cast<float>(pitch_rad);

  const float rx_angle = 0.2f;
  const double meas_angle = -static_cast<double>(rx_angle);
  const double bw_rad = 2.0 * M_PI / 180.0;

  ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
  auto det = makeDetections({rx_angle}, 0.02f);
  const double v = em.compute(det, platform).at(0).vertical_error;

  const double sinT = std::sin(roll_rad + meas_angle);
  const double cos_pitch = std::cos(pitch_rad);
  const double expected = (bw_rad / 12.0) * (bw_rad / 12.0) *
    kIsolatedRange * kIsolatedRange * sinT * sinT * cos_pitch * cos_pitch;

  EXPECT_NEAR(v, expected, 1e-5 * expected);

  // The pre-#147 model read both angles as degrees. Distinguish the two: the
  // sin^2 factor alone differs by more than a factor of ten here.
  const double old_sinT = std::sin(roll_rad * M_PI / 180.0 + meas_angle);
  const double old_cos_pitch = std::cos(pitch_rad * M_PI / 180.0);
  const double old_wrong = (bw_rad / 12.0) * (bw_rad / 12.0) *
    kIsolatedRange * kIsolatedRange * old_sinT * old_sinT *
    old_cos_pitch * old_cos_pitch;
  EXPECT_GT(old_wrong, 10.0 * v);
}

// Sign convention, confirmed rather than assumed. Platform documents "+ve is
// port side up", and beam_angle() makes meas_angle +ve to PORT (it negates the
// starboard-positive rx angle). Rolling the boat port-side-up therefore steers
// a port-side beam further from vertical and a starboard-side beam towards it,
// which the depths must show: at equal and opposite beam angles, the port beam
// reads shallower (its |cos| is smaller) than the starboard one.
TEST_F(ErrorModelTest, PositiveRollIsPortSideUp)
{
  auto platform = makePlatform();
  platform.roll = 0.25f;  // rad, port side up

  ErrorModel em(angleIsolatingVessel(), angleIsolatingDevice(2.0));
  // rx_angle +0.3 is starboard, -0.3 is port (rx_angles are starboard-positive).
  auto det = makeDetections({0.3f, -0.3f}, 0.02f);
  const auto soundings = em.compute(det, platform);
  ASSERT_EQ(soundings.size(), 2u);

  const double starboard_depth = soundings.at(0).depth;
  const double port_depth = soundings.at(1).depth;

  // Depths are negative; the port beam is swung further off vertical, so its
  // magnitude is the smaller of the two.
  EXPECT_LT(std::abs(port_depth), std::abs(starboard_depth));

  // Pin both against the closed form so the direction is not just a ratio.
  EXPECT_NEAR(starboard_depth, -kIsolatedRange * std::cos(0.25 - 0.3), 1e-4);
  EXPECT_NEAR(port_depth, -kIsolatedRange * std::cos(0.25 + 0.3), 1e-4);
}

// #144: Eqn. 3.49's pitch term is scaled by cos^2 T -- cos(roll + beam angle),
// the swath-geometry factor -- not by cos^2(pitch). The port had cos_pitch^2
// here while the two sibling terms in the same function were faithful
// (original_cube/libsrc/errmod/errmod_full.c:376-377). Pinned in closed form
// at an oblique beam, where the two factors are furthest apart.
TEST_F(ErrorModelTest, DepthPitchTermUsesSwathAngleNotPitchCosine)
{
  auto platform = makePlatform();
  const double pitch_rad = 0.35;
  platform.pitch = static_cast<float>(pitch_rad);
  platform.roll = 0.0f;

  // Only pitch variance is left alive, so vertical_error IS the Eqn. 3.49
  // term: the angular, range, along-track-beamwidth, heave and reduction
  // terms are all zeroed by the isolating vessel/device.
  Vessel v = angleIsolatingVessel();
  const double pitch_sdev_deg = 0.5;
  v.pitch_sdev = pitch_sdev_deg;
  ErrorModel em(v, angleIsolatingDevice(0.0));

  const float rx_angle = 1.0472f;  // ~60 deg to starboard
  const double meas_angle = -static_cast<double>(rx_angle);
  auto det = makeDetections({rx_angle}, 0.02f);
  const double vertical = em.compute(det, platform).at(0).vertical_error;

  const double pitch_var = (pitch_sdev_deg * M_PI / 180.0) * (pitch_sdev_deg * M_PI / 180.0);
  const double cosT = std::cos(0.0 + meas_angle);
  const double expected = kIsolatedRange * kIsolatedRange * cosT * cosT *
    std::sin(pitch_rad) * std::sin(pitch_rad) * pitch_var;

  EXPECT_NEAR(vertical, expected, 1e-6 * expected);

  // The pre-fix form would have used cos^2(pitch) here, which at a 60-degree
  // beam is 3.5x larger -- the assertion above fails if the slip comes back.
  const double cos_pitch = std::cos(pitch_rad);
  const double slipped = kIsolatedRange * kIsolatedRange * cos_pitch * cos_pitch *
    std::sin(pitch_rad) * std::sin(pitch_rad) * pitch_var;
  EXPECT_GT(slipped, 3.0 * expected);
}

// #144: a driver may report fewer receive angles than travel times. Both the
// Sounding geometry and beam_angle() used to index rx_angles unguarded, which
// is an out-of-bounds read; the surrounding per-beam arrays (tx_angles,
// intensities) were already guarded. The short beam now yields NaN -- honest,
// and NaN-safe downstream -- rather than undefined behaviour or a silent 0
// (which would read as a nadir beam that was never measured).
TEST_F(ErrorModelTest, ShortRxAnglesYieldNaNRatherThanReadingOffTheEnd)
{
  ErrorModel em(vessel, device);
  auto platform = makePlatform();

  auto det = makeDetections({0.2f, -0.2f}, 0.02f);
  det.rx_angles.resize(1);  // two beams, one reported angle

  const auto soundings = em.compute(det, platform);
  ASSERT_EQ(soundings.size(), 2u);

  // Beam 0 is fully reported and unaffected.
  EXPECT_TRUE(std::isfinite(soundings[0].depth));
  EXPECT_TRUE(std::isfinite(soundings[0].vertical_error));
  EXPECT_FLOAT_EQ(soundings[0].beam_angle, 0.2f);

  // Beam 1 has no angle: position, depth, TPU and beam_angle are all NaN.
  EXPECT_TRUE(std::isnan(soundings[1].beam_angle));
  EXPECT_TRUE(std::isnan(soundings[1].depth));
  EXPECT_TRUE(std::isnan(soundings[1].vertical_error));
  EXPECT_TRUE(std::isnan(soundings[1].horizontal_error));
  EXPECT_TRUE(std::isnan(soundings[1].sonar_relative_position.y));
  EXPECT_TRUE(std::isnan(soundings[1].sonar_relative_position.z));
}

}  // namespace cube
