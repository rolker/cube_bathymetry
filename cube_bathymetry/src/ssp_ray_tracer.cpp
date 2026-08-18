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

#include "cube_bathymetry/ssp_ray_tracer.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

// Constant-gradient (isogradient) ray tracing, theta-parametrized.
//
// Within a segment of gradient g, the Snell invariant p = sin(theta)/c makes
// the ray a circular arc of radius R = 1/(p*g) (theta from nadir). The closed
// forms used below, valid on 0 < theta < pi and therefore CONTINUOUS through
// a turning point (theta = pi/2):
//
//   theta(t)  : tan(theta/2) = tan(theta0/2) * exp(g*t)
//   t(theta)  = (1/g) * ln( tan(theta/2) / tan(theta0/2) )
//   dy        = R * (cos(theta0) - cos(theta1))
//   dz        = R * (sin(theta1) - sin(theta0))
//
// All internal math is double (plan decision: R reaches tens of km while
// offsets are metres — float arc differences catastrophically cancel).

namespace cube
{

namespace
{

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kHalfPi = 1.5707963267948966;
constexpr double kPi = 3.141592653589793;

// Below this ray parameter (sin(launch)/c) the ray is integrated as purely
// vertical: for real geometry (c ~ 1500 m/s) this is a launch angle below
// ~2e-9 rad, where the arc half-angle formulas lose precision to no purpose.
constexpr double kVerticalRayParameter = 1e-12;

// Gradient magnitude (1/s) below which a segment is integrated as a straight
// ray at the entry sound speed. Position error is bounded by the arc sagitta
// s = L^2/(8R) with R = 1/(p*g): at L = 100 m, p <= 1/1400 (a 90-degree beam
// at 1400 m/s), |g| = 1e-9 gives R >= 1.4e12 m and s <= 1e-9 m — well below
// any survey-relevant precision.
constexpr double kStraightGradientEpsilon = 1e-9;

// Safety cap on segment traversals. A ducted (sound-channel) profile can
// oscillate a ray legitimately, but each half-cycle consumes finite time;
// this cap only breaks pathological non-termination.
constexpr std::size_t kMaxSegmentSteps = 100000;

struct TraceState
{
  double z;            // depth below surface, positive down
  double y;            // across-track offset (starboard positive, pre-mirror)
  double theta;        // radians from nadir, (0, pi); > pi/2 == ascending
  double time_left;    // seconds of one-way travel time still to consume
  bool turned;
  bool extrapolated;
};

RayTraceResult invalidResult(RayTraceStatus status)
{
  return {status, kNaN, kNaN, kNaN, kNaN, false, false};
}

bool profileIsValid(const SoundSpeedProfile & profile)
{
  if (profile.empty()) {
    return false;
  }
  for (std::size_t i = 0; i < profile.size(); ++i) {
    if (!std::isfinite(profile[i].depth_below_surface) ||
      !std::isfinite(profile[i].sound_speed) || profile[i].sound_speed <= 0.0)
    {
      return false;
    }
    if (i > 0 &&
      profile[i].depth_below_surface <= profile[i - 1].depth_below_surface)
    {
      return false;
    }
  }
  return true;
}

// Segment index i covers [profile[i].depth, profile[i+1].depth). Index -1 is
// the extrapolated region above the first sample; index size()-1 is the
// extrapolated region below the last. For a single-point profile both
// extrapolated regions use gradient 0.
struct Segment
{
  double z_top;        // -inf for the above-profile region
  double z_bottom;     // +inf for the below-profile region
  double c_top;        // sound speed at z_top (at the first sample if -inf)
  double gradient;     // dc/dz within the segment
  bool is_extrapolated;
};

Segment segmentAt(const SoundSpeedProfile & profile, std::ptrdiff_t index)
{
  const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(profile.size());
  const double inf = std::numeric_limits<double>::infinity();
  Segment seg;
  if (n == 1) {
    seg = {-inf, inf, profile[0].sound_speed, 0.0, index != 0};
    // Single point: one unbounded zero-gradient segment either side.
    return seg;
  }
  if (index < 0) {
    const double g =
      (profile[1].sound_speed - profile[0].sound_speed) /
      (profile[1].depth_below_surface - profile[0].depth_below_surface);
    seg = {-inf, profile[0].depth_below_surface, kNaN, g, true};
    seg.c_top = kNaN;  // unused above the profile; c anchored at z_bottom
    return seg;
  }
  if (index >= n - 1) {
    const double g =
      (profile[n - 1].sound_speed - profile[n - 2].sound_speed) /
      (profile[n - 1].depth_below_surface - profile[n - 2].depth_below_surface);
    seg = {profile[n - 1].depth_below_surface, inf,
      profile[n - 1].sound_speed, g, true};
    return seg;
  }
  const double g =
    (profile[index + 1].sound_speed - profile[index].sound_speed) /
    (profile[index + 1].depth_below_surface -
    profile[index].depth_below_surface);
  seg = {profile[index].depth_below_surface,
    profile[index + 1].depth_below_surface,
    profile[index].sound_speed, g, false};
  return seg;
}

// Sound speed at depth z within segment seg (linear; anchored at whichever
// boundary is finite).
double soundSpeedIn(
  const SoundSpeedProfile & profile, const Segment & seg, double z)
{
  if (std::isfinite(seg.z_top)) {
    return seg.c_top + seg.gradient * (z - seg.z_top);
  }
  // Above-profile region: anchor at the first sample.
  return profile.front().sound_speed +
         seg.gradient * (z - profile.front().depth_below_surface);
}

std::ptrdiff_t segmentIndexFor(const SoundSpeedProfile & profile, double z)
{
  const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(profile.size());
  if (n == 1) {
    return 0;
  }
  if (z < profile.front().depth_below_surface) {
    return -1;
  }
  for (std::ptrdiff_t i = 0; i < n - 1; ++i) {
    if (z < profile[i + 1].depth_below_surface) {
      return i;
    }
  }
  return n - 1;
}

// Advance a purely vertical (p ~ 0) ray through one segment. Returns true if
// the travel time was exhausted (state.time_left == 0 and state.z is final).
bool advanceVertical(
  const SoundSpeedProfile & profile, const Segment & seg, TraceState * state,
  RayTraceStatus * status)
{
  const double c0 = soundSpeedIn(profile, seg, state->z);
  const double g = seg.gradient;
  // Vertical descent: dz/dt = c(z); with constant gradient, c(t) = c0*e^(g t).
  if (std::abs(g) < kStraightGradientEpsilon) {
    const double t_exit = (seg.z_bottom - state->z) / c0;  // inf if unbounded
    if (state->time_left <= t_exit) {
      state->z += c0 * state->time_left;
      state->time_left = 0.0;
      return true;
    }
    state->z = seg.z_bottom;
    state->time_left -= t_exit;
    return false;
  }
  // Time to reach the segment bottom (c there is c_bottom = c0 + g*dz).
  const double c_bottom = std::isfinite(seg.z_bottom) ?
    c0 + g * (seg.z_bottom - state->z) :
    (g < 0.0 ? kMinSoundSpeed : std::numeric_limits<double>::infinity());
  // In an extrapolated segment with a negative gradient the floor applies.
  const double c_target = (seg.is_extrapolated && g < 0.0 &&
    (!std::isfinite(seg.z_bottom) || c_bottom < kMinSoundSpeed)) ?
    kMinSoundSpeed : c_bottom;
  const double t_exit = std::isfinite(c_target) ?
    std::log(c_target / c0) / g : std::numeric_limits<double>::infinity();
  if (state->time_left <= t_exit) {
    const double c1 = c0 * std::exp(g * state->time_left);
    state->z += (c1 - c0) / g;
    state->time_left = 0.0;
    return true;
  }
  state->z += (c_target - c0) / g;
  state->time_left -= t_exit;
  if (c_target == kMinSoundSpeed && seg.is_extrapolated) {
    *status = RayTraceStatus::kExtrapolationLimit;
    return true;  // trace ends here, time NOT fully consumed
  }
  return false;
}

// Advance an inclined ray (p > 0) through one segment using the arc forms.
// Returns true when the trace is finished (time exhausted or limit hit).
bool advanceInclined(
  const SoundSpeedProfile & profile, const Segment & seg, double p,
  TraceState * state, RayTraceStatus * status)
{
  const double c0 = soundSpeedIn(profile, seg, state->z);
  const double g = seg.gradient;
  const bool moving_down = state->theta < kHalfPi ||
    (state->theta == kHalfPi && g <= 0.0);

  if (std::abs(g) < kStraightGradientEpsilon) {
    // Straight segment at constant c0; direction fixed.
    const double cos_th = std::cos(state->theta);
    double t_exit = std::numeric_limits<double>::infinity();
    double z_exit = kNaN;
    if (cos_th > 0.0 && std::isfinite(seg.z_bottom)) {
      z_exit = seg.z_bottom;
      t_exit = (z_exit - state->z) / (c0 * cos_th);
    } else if (cos_th < 0.0 && std::isfinite(seg.z_top)) {
      z_exit = seg.z_top;
      t_exit = (z_exit - state->z) / (c0 * cos_th);  // both negative -> > 0
    }
    if (state->time_left <= t_exit) {
      const double s = c0 * state->time_left;
      state->y += s * std::sin(state->theta);
      state->z += s * cos_th;
      state->time_left = 0.0;
      return true;
    }
    state->y += c0 * t_exit * std::sin(state->theta);
    state->z = z_exit;
    state->time_left -= t_exit;
    return false;
  }

  // Arc segment. Determine the exit angle at the boundary the ray is moving
  // toward; if sin(theta) would exceed 1 before that boundary, the ray turns
  // inside this segment and exits through the opposite boundary — the closed
  // forms are continuous through the turn, so the exit angle is simply on
  // the other branch (pi - asin).
  const double R = 1.0 / (p * g);
  double theta_target;
  bool will_turn = false;
  const double inf = std::numeric_limits<double>::infinity();

  double c_toward = moving_down ?
    (std::isfinite(seg.z_bottom) ?
    soundSpeedIn(profile, seg, seg.z_bottom) : (g > 0.0 ? inf : -inf)) :
    (std::isfinite(seg.z_top) ? seg.c_top : (g > 0.0 ? -inf : inf));
  // (Unbounded extrapolated segment: c runs to +/-inf along the gradient.)

  const double sin_toward = p * c_toward;
  if (sin_toward >= 1.0) {
    // Turns inside this segment; exits via the boundary it came from.
    will_turn = true;
    const double c_back = moving_down ?
      (std::isfinite(seg.z_top) ? seg.c_top :
      soundSpeedIn(profile, seg, state->z)) :
      (std::isfinite(seg.z_bottom) ?
      soundSpeedIn(profile, seg, seg.z_bottom) :
      soundSpeedIn(profile, seg, state->z));
    theta_target = moving_down ?
      kPi - std::asin(std::min(1.0, p * c_back)) :
      std::asin(std::min(1.0, p * c_back));
  } else if (sin_toward <= p * kMinSoundSpeed && seg.is_extrapolated) {
    // The gradient drives c toward the floor before any boundary.
    theta_target = moving_down ?
      std::asin(p * kMinSoundSpeed) : kPi - std::asin(p * kMinSoundSpeed);
    const double tan_half_target = std::tan(theta_target / 2.0);
    const double tan_half_0 = std::tan(state->theta / 2.0);
    const double t_exit = std::log(tan_half_target / tan_half_0) / g;
    if (state->time_left <= t_exit) {
      // Exhausts before reaching the floor — fall through to the common
      // partial-advance below by treating the floor angle as the target.
    } else {
      const double th1 = theta_target;
      state->y += R * (std::cos(state->theta) - std::cos(th1));
      state->z += R * (std::sin(th1) - std::sin(state->theta));
      state->theta = th1;
      state->time_left -= t_exit;
      *status = RayTraceStatus::kExtrapolationLimit;
      return true;
    }
  } else {
    theta_target = moving_down ?
      std::asin(sin_toward) : kPi - std::asin(sin_toward);
  }

  const double tan_half_0 = std::tan(state->theta / 2.0);
  const double tan_half_target = std::tan(theta_target / 2.0);
  const double t_exit = std::log(tan_half_target / tan_half_0) / g;

  if (state->time_left <= t_exit) {
    const double th1 =
      2.0 * std::atan(tan_half_0 * std::exp(g * state->time_left));
    if ((state->theta - kHalfPi) * (th1 - kHalfPi) < 0.0) {
      state->turned = true;
    }
    state->y += R * (std::cos(state->theta) - std::cos(th1));
    state->z += R * (std::sin(th1) - std::sin(state->theta));
    state->theta = th1;
    state->time_left = 0.0;
    return true;
  }

  if (will_turn ||
    (state->theta - kHalfPi) * (theta_target - kHalfPi) < 0.0)
  {
    state->turned = true;
  }
  state->y += R * (std::cos(state->theta) - std::cos(theta_target));
  state->z += R * (std::sin(theta_target) - std::sin(state->theta));
  state->theta = theta_target;
  state->time_left -= t_exit;
  return false;
}

}  // namespace

RayTraceResult traceRay(
  const SoundSpeedProfile & profile,
  double transducer_depth_below_surface,
  double launch_angle,
  double array_sound_speed,
  double one_way_travel_time)
{
  if (!profileIsValid(profile) ||
    !std::isfinite(transducer_depth_below_surface) ||
    !std::isfinite(launch_angle) || std::abs(launch_angle) >= kHalfPi ||
    !std::isfinite(array_sound_speed) || array_sound_speed <= 0.0 ||
    !std::isfinite(one_way_travel_time) || one_way_travel_time <= 0.0)
  {
    return invalidResult(RayTraceStatus::kInvalidInput);
  }

  // Trace to starboard internally; mirror a port launch at the end.
  const bool mirrored = launch_angle < 0.0;
  const double abs_launch = std::abs(launch_angle);

  // Array-face Snell correction: the sonar steered with array_sound_speed,
  // so that pair defines the invariant ray parameter.
  const double p = std::sin(abs_launch) / array_sound_speed;

  std::ptrdiff_t seg_index =
    segmentIndexFor(profile, transducer_depth_below_surface);
  Segment seg = segmentAt(profile, seg_index);
  const double c_start =
    soundSpeedIn(profile, seg, transducer_depth_below_surface);
  if (c_start <= 0.0) {
    // Transducer sits in an extrapolated region whose gradient has already
    // driven the extended profile unphysical at this depth.
    return invalidResult(RayTraceStatus::kInvalidInput);
  }

  TraceState state{transducer_depth_below_surface, 0.0, 0.0,
    one_way_travel_time, false, false};
  RayTraceStatus status = RayTraceStatus::kOk;

  if (p >= kVerticalRayParameter) {
    const double sin_start = p * c_start;
    if (sin_start >= 1.0) {
      return invalidResult(RayTraceStatus::kEvanescentLaunch);
    }
    state.theta = std::asin(sin_start);
  }

  for (std::size_t step = 0; step < kMaxSegmentSteps; ++step) {
    if (seg.is_extrapolated) {
      state.extrapolated = true;
    }
    bool finished;
    if (p < kVerticalRayParameter) {
      finished = advanceVertical(profile, seg, &state, &status);
    } else {
      finished = advanceInclined(profile, seg, p, &state, &status);
    }
    if (finished) {
      const double dz = state.z - transducer_depth_below_surface;
      const double consumed = one_way_travel_time - state.time_left;
      const double slant = std::sqrt(state.y * state.y + dz * dz);
      const double sign = mirrored ? -1.0 : 1.0;
      return {status, sign * state.y, state.z, sign * state.theta,
        consumed > 0.0 ? slant / consumed : kNaN,
        state.turned, state.extrapolated};
    }
    // Move to the adjacent segment in the current direction of travel.
    const bool going_down =
      p < kVerticalRayParameter || state.theta < kHalfPi;
    seg_index += going_down ? 1 : -1;
    seg = segmentAt(profile, seg_index);
  }
  return invalidResult(RayTraceStatus::kInvalidInput);
}

}  // namespace cube
