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

#include <cmath>

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
    // Retain the per-beam slant range as a first-class field (not only baked into
    // sonar_relative_position) so the tier-2 backscatter TL correction can read it
    // downstream (cube_bathymetry#87). Same value used for the geometry below.
    slant_range = static_cast < float > (range);
    float tx_angle = 0.0;
    if(i < detections.tx_angles.size()) {
        tx_angle = detections.tx_angles[i];
    }
    // rx_angles is bounds-guarded like every other per-beam array here (#144).
    // A driver that reports fewer receive angles than travel times used to
    // send the two reads below off the end of the vector. Absent -> NaN, which
    // propagates into the position and the TPU, rather than being read as 0
    // (a nadir beam that was never measured) or as whatever follows in memory.
    const float rx_angle = (i < detections.rx_angles.size()) ?
        detections.rx_angles[i] : std::nan("");
    sonar_relative_position.x = range * -sin(tx_angle);
    sonar_relative_position.y = range * sin(rx_angle);
    sonar_relative_position.z = range * cos(tx_angle) * cos(rx_angle);

    // Per-beam acoustic intensity (backscatter). Sonar-reported and usually
    // uncalibrated; for the Kongsberg M3 (via kongsberg_em_bridge) it is
    // reflectivity in dB. Left NaN when the source omits intensities so a
    // missing value is never mistaken for a real measurement.
    if(i < detections.intensities.size()) {
        intensity = detections.intensities[i];
    }

    // Per-beam receive (steering) angle, captured here alongside intensity so
    // the {raw intensity, angle} sufficient-statistics pair stays bound to the
    // same beam (ADR-0007 D3). This is the beam/incidence angle relative to
    // nadir, NOT a true seafloor grazing angle (the latter needs local slope,
    // deferred to cube_bathymetry#15); it is the per-beam geometry the deferred
    // node-output GeoCoder correction reconstructs the grazing angle from. Left
    // NaN when the source omits rx_angles for this beam.
    beam_angle = rx_angle;
    }

  /// Depth relative to the sea surface. Positive is up above sea surface
  /// and negative is down below sea surface
    float depth = std::nan("");

  /// Vertical total propagated uncertainty as a VARIANCE in m^2, at one sigma.
  /// No confidence-interval scaling is applied: CUBE consumes this directly as
  /// the measurement variance in its depth update, and any 95%/99% figure is
  /// produced downstream at reporting time by scaling the square root
  /// (CONF_95PC / CONF_99PC). A one-dimensional error about `depth`.
    float vertical_error = 0.0;

  /// Horizontal total propagated uncertainty as a VARIANCE in m^2, at one
  /// sigma, likewise with no confidence-interval scaling.
  /// Not the same shape of quantity as `vertical_error`: this one is radial,
  /// derived from the drms convention, so its square root is a radius in the
  /// horizontal plane rather than an error along a single axis. See #144.
    float horizontal_error = 0.0;

  /// Per-beam acoustic intensity / backscatter (NaN when not reported).
    float intensity = std::nan("");

  /// Predicted-seabed-surface depth interpolated at this sounding's touchdown
  /// point, negative-down (same convention as `depth` and `Node`'s
  /// `predicted_depth_`). This is the port's analog of the original CUBE
  /// sounding's *overwritten* `range` element: in `mapsheet_cube.c:2434` the
  /// integration code fills `snd->range` with
  /// `cube_grid_interpolate(... de, dn ...)` — the bilinear blend of the four
  /// surrounding nodes' `pred_depth` at the touchdown `(de,dn)` — purely so
  /// that `cube_node_insert` (`cube_node.c:1846`) can use it for the
  /// slope correction `offset = node->pred_depth - snd->range`.
  ///
  /// It is deliberately NOT named `range` to avoid resurrecting the prior
  /// misread that equated it with the error-model slant range
  /// `depth/cos(angle)` (`sounding.c:1268`); that is a different quantity that
  /// is overwritten before the offset runs and is irrelevant here.
  ///
  /// Defaults to the no-correction sentinel `INVALID_DATA`. The original uses
  /// `range == 0.0` as its "no interpolation result" sentinel only because
  /// `cube_grid_interpolate` *returns* `0.0f` on a no-data corner
  /// (`cube_grid.c:2383`); `0.0` is an artifact of that return convention, not
  /// a deliberate semantic, and is safe there only because a real seabed
  /// `pred_depth` is never exactly `0.0` m. In this port a legitimately
  /// interpolated touchdown depth at the shoreline could be `0.0`, so we use
  /// `INVALID_DATA` to carry the same intent ("no interpolation result =>
  /// skip") without the value collision. Producers (#59):
  /// `Grid::insert` / `GeoGrid::insert` stamp this via
  /// `interpolatePredictedDepth` on a per-sounding copy (ADR-0008); it stays
  /// at the sentinel (offset 0) when no predicted surface is primed there.
    float predicted_depth_at_touchdown = INVALID_DATA;

  /// Per-beam receive (steering) angle in radians, positive to starboard
  /// (the detections.rx_angles convention). This is the beam/incidence angle
  /// relative to nadir, the per-beam geometry retained for the deferred
  /// node-output backscatter correction (ADR-0007 D3). NaN when not reported.
  ///
  /// SIGN-CONVENTION VERIFICATION REQUIRED (cube_bathymetry#15): before the
  /// deferred GeoCoder grazing-angle reconstruction consumes this field, the
  /// sign/zero convention of rx_angles[i] (e.g., "+= to starboard") MUST be
  /// cross-checked against the marine_acoustic_msgs producer. A sign error
  /// would bias the incidence correction and corrupt the settled backscatter.
    float beam_angle = std::nan("");

  /// Per-beam slant range R from the sonar head to the touchdown, in meters
  /// (`two_way_travel_times[i] * sound_speed / 2`). Retained for the tier-2
  /// backscatter 2-way transmission-loss correction (cube_bathymetry#87), which
  /// removes `40*log10(R) + 2*alpha*R` so the empirical angular-response curve
  /// becomes depth/range transferable. NaN when constructed without detections
  /// (the `explicit Sounding(float depth)` ctor); the TL term is then skipped.
    float slant_range = std::nan("");

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
