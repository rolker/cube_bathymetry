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

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "cube_bathymetry/ssp_ray_tracer.h"

namespace
{

using cube::RayTraceResult;
using cube::RayTraceStatus;
using cube::SoundSpeedProfile;
using cube::traceRay;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

// Piecewise-linear sound speed with boundary-gradient extension — the
// documented profile contract, restated independently for the reference
// integrator.
double soundSpeedAt(const SoundSpeedProfile & profile, double z)
{
  if (profile.size() == 1) {
    return profile.front().sound_speed;
  }
  size_t hi = 1;
  while (hi < profile.size() - 1 &&
    z >= profile[hi].depth_below_surface) {++hi;}
  const auto & a = profile[hi - 1];
  const auto & b = profile[hi];
  const double g = (b.sound_speed - a.sound_speed) /
    (b.depth_below_surface - a.depth_below_surface);
  return a.sound_speed + g * (z - a.depth_below_surface);
}

struct ReferenceEndpoint
{
  double y;
  double z;
  double theta;
};

// Independent ground truth: RK4 integration of the ray equations
//   dy/ds = sin(theta), dz/ds = cos(theta),
//   dtheta/ds = p * g(z), dt/ds = 1 / c(z)
// (from differentiating the Snell invariant p = sin(theta)/c along arc
// length). Shares no closed-form algebra with the implementation under test.
ReferenceEndpoint integrateReference(
  const SoundSpeedProfile & profile, double z0, double theta0, double p,
  double travel_time, double ds = 0.001)
{
  double y = 0.0;
  double z = z0;
  double theta = theta0;
  double t = 0.0;
  while (true) {
    // One RK4 step of (y, z, theta, t) in arc length.
    auto deriv = [&](double zz, double th) {
        const double c = soundSpeedAt(profile, zz);
        struct {double dy, dz, dth, dt;} d{
          std::sin(th), std::cos(th),
          p * ((soundSpeedAt(profile, zz + 1e-4) -
          soundSpeedAt(profile, zz - 1e-4)) / 2e-4),
          1.0 / c};
        return d;
      };
    const auto k1 = deriv(z, theta);
    const auto k2 = deriv(z + 0.5 * ds * k1.dz, theta + 0.5 * ds * k1.dth);
    const auto k3 = deriv(z + 0.5 * ds * k2.dz, theta + 0.5 * ds * k2.dth);
    const auto k4 = deriv(z + ds * k3.dz, theta + ds * k3.dth);
    const double dt =
      ds * (k1.dt + 2 * k2.dt + 2 * k3.dt + k4.dt) / 6.0;
    if (t + dt >= travel_time) {
      // Final partial step, linear in the remaining time.
      const double frac = (travel_time - t) / dt;
      y += frac * ds * (k1.dy + 2 * k2.dy + 2 * k3.dy + k4.dy) / 6.0;
      z += frac * ds * (k1.dz + 2 * k2.dz + 2 * k3.dz + k4.dz) / 6.0;
      theta += frac * ds * (k1.dth + 2 * k2.dth + 2 * k3.dth + k4.dth) / 6.0;
      return {y, z, theta};
    }
    y += ds * (k1.dy + 2 * k2.dy + 2 * k3.dy + k4.dy) / 6.0;
    z += ds * (k1.dz + 2 * k2.dz + 2 * k3.dz + k4.dz) / 6.0;
    theta += ds * (k1.dth + 2 * k2.dth + 2 * k3.dth + k4.dth) / 6.0;
    t += dt;
  }
}

TEST(SspRayTracer, UniformProfileMatchesStraightRay)
{
  const SoundSpeedProfile profile{{0.0, 1500.0}};
  const double transducer_depth = 0.35;
  const double launch = 0.6;
  const double t = 0.004;
  const auto r = traceRay(profile, transducer_depth, launch, 1500.0, t);
  ASSERT_EQ(r.status, RayTraceStatus::kOk);
  // Exactly today's straight-ray geometry (sounding.h): range = c*t.
  EXPECT_NEAR(r.across_track_offset, 1500.0 * t * std::sin(launch), 1e-9);
  EXPECT_NEAR(r.depth_below_surface,
    transducer_depth + 1500.0 * t * std::cos(launch), 1e-9);
  EXPECT_NEAR(r.end_angle, launch, 1e-12);
  EXPECT_NEAR(r.effective_sound_speed, 1500.0, 1e-9);
  EXPECT_FALSE(r.turned);

  // Nadir sub-case: range = twtt*c/2 straight down.
  const auto nadir = traceRay(profile, 0.0, 0.0, 1500.0, t);
  ASSERT_EQ(nadir.status, RayTraceStatus::kOk);
  EXPECT_NEAR(nadir.depth_below_surface, 1500.0 * t, 1e-9);
  EXPECT_NEAR(nadir.across_track_offset, 0.0, 1e-12);
}

TEST(SspRayTracer, TwoLayerMatchesNumericalIntegration)
{
  const SoundSpeedProfile profile{{0.0, 1500.0}, {100.0, 1520.0}};
  const double launch = M_PI / 4.0;
  const double array_c = 1500.0;  // equals c at the transducer: no correction
  const double p = std::sin(launch) / array_c;
  for (const double t : {0.01, 0.05, 0.12}) {
    const auto r = traceRay(profile, 0.0, launch, array_c, t);
    ASSERT_EQ(r.status, RayTraceStatus::kOk) << "t=" << t;
    const auto ref = integrateReference(profile, 0.0, launch, p, t);
    EXPECT_NEAR(r.across_track_offset, ref.y, 1e-5) << "t=" << t;
    EXPECT_NEAR(r.depth_below_surface, ref.z, 1e-5) << "t=" << t;
    EXPECT_NEAR(r.end_angle, ref.theta, 1e-7) << "t=" << t;
  }
}

TEST(SspRayTracer, SnellInvariantHoldsAtArbitraryTimes)
{
  const SoundSpeedProfile profile{
    {0.0, 1490.0}, {20.0, 1500.0}, {60.0, 1495.0}, {120.0, 1510.0}};
  const double launch = 0.9;
  const double array_c = 1490.0;
  const double p = std::sin(launch) / array_c;
  for (const double t : {0.005, 0.02, 0.05, 0.09, 0.15}) {
    const auto r = traceRay(profile, 0.0, launch, array_c, t);
    ASSERT_EQ(r.status, RayTraceStatus::kOk) << "t=" << t;
    const double c_end = soundSpeedAt(profile, r.depth_below_surface);
    EXPECT_NEAR(std::sin(r.end_angle) / c_end, p, 1e-12) << "t=" << t;
  }
}

TEST(SspRayTracer, PortStarboardMirror)
{
  const SoundSpeedProfile profile{
    {0.0, 1500.0}, {30.0, 1512.0}, {80.0, 1505.0}};
  const double t = 0.05;
  const auto stbd = traceRay(profile, 1.0, 0.7, 1500.0, t);
  const auto port = traceRay(profile, 1.0, -0.7, 1500.0, t);
  ASSERT_EQ(stbd.status, RayTraceStatus::kOk);
  ASSERT_EQ(port.status, RayTraceStatus::kOk);
  EXPECT_NEAR(port.across_track_offset, -stbd.across_track_offset, 1e-12);
  EXPECT_NEAR(port.end_angle, -stbd.end_angle, 1e-12);
  EXPECT_NEAR(port.depth_below_surface, stbd.depth_below_surface, 1e-12);
  EXPECT_GT(stbd.across_track_offset, 0.0);
  EXPECT_LT(port.across_track_offset, 0.0);
}

TEST(SspRayTracer, PositiveDownSignConvention)
{
  // Locks the depth_below_surface direction: a downward ray from a shallow
  // transducer ends DEEPER (larger positive) than where it started. Guards
  // against a silent flip toward Sounding::depth's positive-up convention.
  const SoundSpeedProfile profile{{0.0, 1500.0}, {50.0, 1510.0}};
  const auto r = traceRay(profile, 0.4, 0.2, 1500.0, 0.01);
  ASSERT_EQ(r.status, RayTraceStatus::kOk);
  EXPECT_GT(r.depth_below_surface, 0.4);
  EXPECT_GT(r.depth_below_surface, 0.0);
}

TEST(SspRayTracer, TurningRayComesBackUp)
{
  // Strong (unrealistically steep, but valid) downward-increasing gradient
  // and a wide beam: c_turn = 1/p = array_c/sin(launch) is reached inside
  // the layer, the ray turns, and the endpoint is shallower than the
  // turning depth.
  const SoundSpeedProfile profile{{0.0, 1500.0}, {50.0, 1600.0}};  // g = 2/s
  const double launch = 1.4;  // ~80 degrees
  const double array_c = 1500.0;
  const double p = std::sin(launch) / array_c;
  const double z_turn = (1.0 / p - 1500.0) / 2.0;  // c(z_turn) = 1/p
  ASSERT_LT(z_turn, 50.0);
  // Time to the turn is ~0.086 s (arc length ~130 m at ~1510 m/s); 0.12 s
  // turns and ascends but stays inside the layer.
  const double t = 0.12;
  const auto r = traceRay(profile, 0.0, launch, array_c, t);
  ASSERT_EQ(r.status, RayTraceStatus::kOk);
  EXPECT_TRUE(r.turned);
  EXPECT_GT(r.end_angle, M_PI / 2.0);  // ascending, continuous convention
  EXPECT_LT(r.depth_below_surface, z_turn + 1e-9);
  // Cross-check the endpoint against the independent integrator.
  const auto ref = integrateReference(profile, 0.0, launch, p, t);
  EXPECT_NEAR(r.across_track_offset, ref.y, 1e-4);
  EXPECT_NEAR(r.depth_below_surface, ref.z, 1e-4);
}

TEST(SspRayTracer, ExtrapolatesPastProfileBottom)
{
  const SoundSpeedProfile profile{{0.0, 1500.0}, {10.0, 1502.0}};
  const double launch = 0.5;
  const double array_c = 1500.0;
  const double p = std::sin(launch) / array_c;
  const double t = 0.03;  // runs well past 10 m
  const auto r = traceRay(profile, 0.0, launch, array_c, t);
  ASSERT_EQ(r.status, RayTraceStatus::kOk);
  EXPECT_TRUE(r.extrapolated);
  EXPECT_GT(r.depth_below_surface, 10.0);
  const auto ref = integrateReference(profile, 0.0, launch, p, t);
  EXPECT_NEAR(r.across_track_offset, ref.y, 1e-5);
  EXPECT_NEAR(r.depth_below_surface, ref.z, 1e-5);
}

TEST(SspRayTracer, EvanescentLaunchIsDistinct)
{
  // Stale applied sound speed well below the cast's surface value at a wide
  // angle: sin(theta) in the water column would exceed 1 (the #121 startup-
  // transient case). p * c = (sin 1.2 / 1400) * 1520 = 1.0119... >= 1.
  const SoundSpeedProfile profile{{0.0, 1520.0}, {50.0, 1500.0}};
  const auto r = traceRay(profile, 0.0, 1.2, 1400.0, 0.01);
  EXPECT_EQ(r.status, RayTraceStatus::kEvanescentLaunch);
  EXPECT_TRUE(std::isnan(r.depth_below_surface));
  EXPECT_TRUE(std::isnan(r.across_track_offset));
}

TEST(SspRayTracer, ExtrapolationLimitStopsAtSoundSpeedFloor)
{
  // Steep negative gradient extended below the profile drives c to the
  // documented floor before the (huge) travel time is exhausted.
  const SoundSpeedProfile profile{{0.0, 1500.0}, {10.0, 1400.0}};  // g = -10
  const auto r = traceRay(profile, 0.0, 0.0, 1500.0, 10.0);
  EXPECT_EQ(r.status, RayTraceStatus::kExtrapolationLimit);
  EXPECT_TRUE(r.extrapolated);
  // Endpoint is where the extended profile reaches kMinSoundSpeed:
  // z = 10 + (kMin - 1400)/(-10).
  EXPECT_NEAR(r.depth_below_surface,
    10.0 + (cube::kMinSoundSpeed - 1400.0) / -10.0, 1e-6);
}

TEST(SspRayTracer, EffectiveSoundSpeedReproducesSlantRange)
{
  const SoundSpeedProfile profile{{0.0, 1490.0}, {40.0, 1515.0}};
  const double t = 0.02;
  const auto r = traceRay(profile, 0.5, 0.8, 1490.0, t);
  ASSERT_EQ(r.status, RayTraceStatus::kOk);
  const double dz = r.depth_below_surface - 0.5;
  const double slant =
    std::sqrt(r.across_track_offset * r.across_track_offset + dz * dz);
  EXPECT_NEAR(r.effective_sound_speed * t, slant, 1e-9);
}

TEST(SspRayTracer, InvalidInputsAreRejected)
{
  const SoundSpeedProfile good{{0.0, 1500.0}, {10.0, 1502.0}};
  const auto expectInvalid = [](const RayTraceResult & r) {
      EXPECT_EQ(r.status, RayTraceStatus::kInvalidInput);
      EXPECT_TRUE(std::isnan(r.across_track_offset));
      EXPECT_TRUE(std::isnan(r.depth_below_surface));
      EXPECT_TRUE(std::isnan(r.end_angle));
      EXPECT_TRUE(std::isnan(r.effective_sound_speed));
    };
  // Empty profile.
  expectInvalid(traceRay({}, 0.0, 0.1, 1500.0, 0.01));
  // Zero and negative travel time.
  expectInvalid(traceRay(good, 0.0, 0.1, 1500.0, 0.0));
  expectInvalid(traceRay(good, 0.0, 0.1, 1500.0, -0.01));
  // Non-finite scalars.
  expectInvalid(traceRay(good, kNan, 0.1, 1500.0, 0.01));
  expectInvalid(traceRay(good, 0.0, kNan, 1500.0, 0.01));
  expectInvalid(traceRay(good, 0.0, 0.1, kNan, 0.01));
  expectInvalid(traceRay(good, 0.0, 0.1, 1500.0, kNan));
  // Launch angle at/H beyond horizontal.
  expectInvalid(traceRay(good, 0.0, M_PI / 2.0, 1500.0, 0.01));
  expectInvalid(traceRay(good, 0.0, -1.6, 1500.0, 0.01));
  // Non-positive array sound speed.
  expectInvalid(traceRay(good, 0.0, 0.1, 0.0, 0.01));
  expectInvalid(traceRay(good, 0.0, 0.1, -1500.0, 0.01));
  // Non-monotonic / duplicate profile depths.
  expectInvalid(
    traceRay({{0.0, 1500.0}, {0.0, 1501.0}}, 0.0, 0.1, 1500.0, 0.01));
  expectInvalid(
    traceRay({{5.0, 1500.0}, {2.0, 1501.0}}, 0.0, 0.1, 1500.0, 0.01));
  // Non-positive / non-finite profile sound speed.
  expectInvalid(
    traceRay({{0.0, 1500.0}, {10.0, 0.0}}, 0.0, 0.1, 1500.0, 0.01));
  expectInvalid(
    traceRay({{0.0, 1500.0}, {10.0, kNan}}, 0.0, 0.1, 1500.0, 0.01));
}

}  // namespace
