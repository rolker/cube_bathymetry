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


#ifndef CUBE_BATHYMETRY__SSP_RAY_TRACER_H_
#define CUBE_BATHYMETRY__SSP_RAY_TRACER_H_

#include <vector>

// Forward constant-gradient sound-speed-profile ray tracer (#126).
//
// The single shared forward model agreed on unh_marine_autonomy#300: consumed
// by the phase-2 sound-speed inversion (iterating candidate profiles) and by
// marine_perception_tools#28 (CUBE lab re-projection). Pure geometry — no ROS
// types, no file I/O (cast loading is deferred to unh_marine_autonomy#299).
//
// ===== Sign and unit conventions (the contract both consumers build on) =====
//
// Angles   : radians from nadir (straight down = 0), POSITIVE TO STARBOARD —
//            the same convention as marine_acoustic_msgs' rx_angles and
//            Sounding::beam_angle (sounding.h), so rx_angles[i] passes in
//            unmodified.
// Depths   : metres, POSITIVE DOWN, measured below the same surface datum for
//            the profile samples, the transducer, and the result — the caller
//            owes all three on one shared datum. Positive-down matches the
//            oceanographic cast convention and Sounding::sonar_relative_
//            position.z; the original CUBE library documents the same
//            positive-down-inside / positive-up-outside split
//            (original_cube/docs/CUBE_Development_Notes.md).
//            *** Sounding::depth (sounding.h) is the OPPOSITE sign (it is an
//            elevation, positive up). The result field is deliberately named
//            depth_below_surface, never bare "depth", so wiring it into
//            Sounding::depth without the required negation reads wrong at the
//            call site. ***
// Across-track offset: metres, positive to starboard, relative to the
//            transducer (same sign family as rx_angles and
//            Sounding::sonar_relative_position.y).

namespace cube
{

/// One sample of a sound speed profile. Depth in metres below the surface
/// datum (positive down), sound speed in m/s (> 0, finite).
  struct SoundSpeedProfilePoint
  {
    double depth_below_surface;
    double sound_speed;
  };

/// Monotonic profile: depths strictly increasing. A single-point profile is
/// valid and degenerates to a straight ray at that point's sound speed.
  using SoundSpeedProfile = std::vector < SoundSpeedProfilePoint >;

  enum class RayTraceStatus
  {
  /// Trace completed; all result fields valid.
    kOk,
  /// Input violated the contract (see traceRay()); all result fields NaN.
    kInvalidInput,
  /// The array-face Snell correction has no real solution: the profile's
  /// sound speed at the transducer is high enough, relative to the applied
  /// array_sound_speed, that sin(theta) would exceed 1 — the steered beam
  /// cannot refract into the water column. Reachable in practice: startup
  /// pings carry a stale applied sound speed (#121). All result fields NaN.
    kEvanescentLaunch,
  /// Extrapolation past the profile boundary drove the local sound speed
  /// down to kMinSoundSpeed before the travel time was exhausted. The result
  /// fields hold the endpoint at that limit (extrapolated == true), so a
  /// consumer can see how far the trace got; travel time is NOT fully
  /// consumed.
    kExtrapolationLimit,
  };

/// Floor for extrapolated sound speed (m/s); reaching it ends the trace with
/// kExtrapolationLimit.
  constexpr double kMinSoundSpeed = 1.0;

  struct RayTraceResult
  {
    RayTraceStatus status;
  /// Metres to starboard of the transducer (negative = port).
    double across_track_offset;
  /// Metres below the surface datum, positive down. NOT relative to the
  /// transducer, and NOT Sounding::depth (opposite sign — see file header).
    double depth_below_surface;
  /// Ray direction at the endpoint: radians from nadir, positive starboard,
  /// CONTINUOUS THROUGH A TURNING POINT — |end_angle| grows monotonically
  /// along a turning arc, so an ascending ray has |end_angle| > pi/2; the
  /// sign always gives the across-track direction of travel.
    double end_angle;
  /// Straight-line transducer-to-endpoint distance divided by the one-way
  /// travel time: the constant speed that reproduces the same SLANT RANGE
  /// for this time. Substituting it into range = twtt*c/2 with the original
  /// rx angle does NOT reproduce the endpoint position — the ray bent.
    double effective_sound_speed;
  /// A turning point (total internal refraction) was passed; the endpoint is
  /// still valid.
    bool turned;
  /// The trace ran above the shallowest or below the deepest profile sample;
  /// that boundary segment's gradient was extended.
    bool extrapolated;
  };

/// Trace one beam through a sound speed profile.
///
/// @param profile  Strictly-increasing-depth samples; every sound speed
///                 finite and > 0. Between samples the gradient is constant
///                 (piecewise-linear profile => circular-arc ray segments);
///                 beyond either end the boundary segment's gradient is
///                 extended (extrapolated flag set).
/// @param transducer_depth_below_surface  Metres below the surface datum,
///                 positive down, same datum as the profile. Finite; may lie
///                 outside the profile's depth span (extrapolation).
/// @param launch_angle  Beam steering angle as the sonar reported it
///                 (rx_angles[i]): radians from nadir, positive starboard,
///                 |launch_angle| < pi/2, finite.
/// @param array_sound_speed  The sound speed the sonar APPLIED when steering
///                 (ping_info.sound_speed), m/s, finite, > 0. The sonar
///                 formed the beam using this value, so the Snell invariant
///                 is p = sin(launch_angle) / array_sound_speed; the true
///                 initial water-column angle is asin(p * c_profile(z_tx))
///                 (kEvanescentLaunch when that has no real solution).
/// @param one_way_travel_time  Seconds (two_way_travel_times[i] / 2),
///                 finite, > 0.
///
/// Violations of the finiteness/range constraints above return
/// kInvalidInput with NaN fields.
  RayTraceResult traceRay(
    const SoundSpeedProfile & profile,
    double transducer_depth_below_surface,
    double launch_angle,
    double array_sound_speed,
    double one_way_travel_time);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__SSP_RAY_TRACER_H_
