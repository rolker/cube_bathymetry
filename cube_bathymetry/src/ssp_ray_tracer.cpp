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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

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
// The dy/dz forms above are the textbook ones and are NOT what advanceArc()
// evaluates. Written that way they catastrophically cancel exactly where the
// geometry matters most: near nadir with a near-flat gradient, R reaches
// 1e12 m while cos(theta0) and cos(theta1) are both within 1e-15 of 1, so the
// difference is pure rounding noise and the across-track offset comes back as
// a hard zero (reproduced at launch 1e-3 rad, g = 5e-9 1/s: 0.0 returned
// against a true 0.0100 m at 10 m range). The algebraically identical
// half-angle forms, with m = (theta0 + theta1)/2 and d = (theta1 - theta0)/2,
// keep every factor at its own scale:
//
//   dy = 2 * R * sin(m) * sin(d)
//   dz = 2 * R * cos(m) * sin(d)
//
// and the small quantity d is itself formed without subtracting two nearly
// equal angles wherever the caller knows it exactly (see advanceInclined()'s
// time-limited branch, which gets d from atan/expm1 on the tan-half ratio).
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
    if (i > 0) {
      if (profile[i].depth_below_surface <= profile[i - 1].depth_below_surface) {
        return false;
      }
      // Strictly increasing is not enough: a denormal-scale depth separation
      // makes the segment gradient overflow to +/-inf, which then propagates
      // NaN through the arc radius and the endpoint. Such a profile is
      // degenerate input, so reject it here rather than returning a NaN
      // "result" downstream.
      const double gradient =
        (profile[i].sound_speed - profile[i - 1].sound_speed) /
        (profile[i].depth_below_surface - profile[i - 1].depth_below_surface);
      if (!std::isfinite(gradient)) {
        return false;
      }
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
    // z_top is -inf, so c_top has no boundary to sit at; carry the shallowest
    // sample's speed (the anchor soundSpeedIn() uses for this region) rather
    // than a NaN whose safety would depend on another field's finiteness.
    seg = {-inf, profile[0].depth_below_surface, profile[0].sound_speed, g,
      true};
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
  const bool floor_limit = (c_target == kMinSoundSpeed && seg.is_extrapolated);
  if (floor_limit && !(t_exit > 0.0)) {
    // Already at or below the extrapolation floor. Advancing would run the
    // ray BACKWARDS (negative t_exit means a negative elapsed time and a
    // displacement toward the transducer), so stop here with nothing
    // consumed. traceRay() rejects a transducer placed below the floor, so
    // this is a defensive backstop, not a routine path.
    *status = RayTraceStatus::kExtrapolationLimit;
    return true;
  }
  if (state->time_left <= t_exit) {
    const double c1 = c0 * std::exp(g * state->time_left);
    state->z += (c1 - c0) / g;
    state->time_left = 0.0;
    return true;
  }
  state->z += (c_target - c0) / g;
  state->time_left -= t_exit;
  if (floor_limit) {
    *status = RayTraceStatus::kExtrapolationLimit;
    return true;  // trace ends here, time NOT fully consumed
  }
  return false;
}

// Apply one circular-arc segment traversal from state->theta to theta1, in
// the numerically stable half-angle form (see the file-header note). The
// caller supplies half_delta == (theta1 - state->theta) / 2 — separately,
// because near nadir it can often be formed to far better relative accuracy
// than the subtraction itself allows.
void advanceArc(TraceState * state, double R, double theta1, double half_delta)
{
  const double mid = state->theta + half_delta;
  const double sin_half = std::sin(half_delta);
  state->y += 2.0 * R * std::sin(mid) * sin_half;
  state->z += 2.0 * R * std::cos(mid) * sin_half;
  state->theta = theta1;
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
  bool floor_limit = false;
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
    // The gradient drives c toward the floor before any boundary. The target
    // angle is the floor angle; reaching it ends the trace (below), while
    // exhausting the travel time first is just the ordinary partial advance.
    floor_limit = true;
    theta_target = moving_down ?
      std::asin(p * kMinSoundSpeed) : kPi - std::asin(p * kMinSoundSpeed);
  } else {
    theta_target = moving_down ?
      std::asin(sin_toward) : kPi - std::asin(sin_toward);
  }

  const double tan_half_0 = std::tan(state->theta / 2.0);
  const double tan_half_target = std::tan(theta_target / 2.0);
  const double t_exit = std::log(tan_half_target / tan_half_0) / g;

  if (floor_limit && !(t_exit > 0.0)) {
    // Already at or past the floor: advancing would run the ray backwards in
    // time. Stop here with nothing consumed (defensive — traceRay() rejects a
    // transducer placed where the extended profile is already at the floor).
    *status = RayTraceStatus::kExtrapolationLimit;
    return true;
  }

  if (state->time_left <= t_exit) {
    const double growth = std::exp(g * state->time_left);
    const double th1 = 2.0 * std::atan(tan_half_0 * growth);
    // half_delta = atan(tan_half_0*growth) - atan(tan_half_0), via the atan
    // difference identity so the small angle is never formed by subtracting
    // two nearly equal ones. expm1 keeps the numerator exact for tiny g*t.
    double half_delta = std::atan(
      tan_half_0 * std::expm1(g * state->time_left) /
      (1.0 + tan_half_0 * tan_half_0 * growth));
    if (!std::isfinite(half_delta)) {
      half_delta = 0.5 * (th1 - state->theta);  // grazing theta -> pi
    }
    if ((state->theta - kHalfPi) * (th1 - kHalfPi) < 0.0) {
      state->turned = true;
    }
    advanceArc(state, R, th1, half_delta);
    state->time_left = 0.0;
    return true;
  }

  if (will_turn ||
    (state->theta - kHalfPi) * (theta_target - kHalfPi) < 0.0)
  {
    state->turned = true;
  }
  advanceArc(state, R, theta_target, 0.5 * (theta_target - state->theta));
  state->time_left -= t_exit;
  if (floor_limit) {
    *status = RayTraceStatus::kExtrapolationLimit;
    return true;  // trace ends here, time NOT fully consumed
  }
  return false;
}

}  // namespace

RayTraceResult traceRay(
  const SoundSpeedProfile & profile,
  double transducer_depth_below_surface,
  double launch_angle,
  double array_sound_speed,
  double one_way_travel_time) noexcept
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
  if (!std::isfinite(c_start) || c_start < kMinSoundSpeed) {
    // Transducer sits in an extrapolated region whose gradient has already
    // driven the extended profile down to (or below) the sound speed floor at
    // this depth: there is no water column to launch into. The documented
    // contract makes this a joint (profile, transducer depth) constraint, so
    // kInvalidInput is the honest status. Note the >= kMinSoundSpeed test
    // rather than > 0: admitting a sliver above zero let the trace start
    // already below the extrapolation floor, which ran the floor branches
    // backwards (negative consumed time, endpoint above the transducer).
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
      // A very long trace on an upward-gradient extrapolation can run the
      // exponential depth growth to overflow: kOk must never carry a
      // non-finite endpoint (the inversion differentiates through these
      // fields), so a blown-up geometry is reported as kStepLimit — "the
      // tracer gave up before consuming the time" — rather than success.
      if (!std::isfinite(state.y) || !std::isfinite(state.z)) {
        return invalidResult(RayTraceStatus::kStepLimit);
      }
      const double dz = state.z - transducer_depth_below_surface;
      const double consumed = one_way_travel_time - state.time_left;
      const double slant = std::hypot(state.y, dz);
      const double sign = mirrored ? -1.0 : 1.0;
      // effective_sound_speed is per the header: slant range over the time
      // actually CONSUMED (== one_way_travel_time on kOk, less when the
      // extrapolation floor cut the trace short), NaN when nothing was
      // consumed at all.
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
  // The input was well formed; the tracer is declining to keep integrating.
  // Reporting that as kInvalidInput blamed the caller for the tracer's own
  // safety cap, so it gets its own status.
  return invalidResult(RayTraceStatus::kStepLimit);
}

}  // namespace cube
