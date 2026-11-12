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

#ifndef CUBE_BATHYMETRY__SLOPE_INCIDENCE_H_
#define CUBE_BATHYMETRY__SLOPE_INCIDENCE_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cube_bathymetry/geo_sounding.h"
#include "marine_backscatter/seabed_geometry.hpp"

/// @file
/// @brief The incidence angle of each beam against the local seabed normal
///        (cube_bathymetry#173). The only code of this package that calls the
///        shared `marine_backscatter::seabed` geometry module.
///
/// ### The angle
/// `incidence = acos(dot(-beam_dir, normal))`, where `beam_dir` is
/// `Sounding::beam_dir_enu` (the unit ENU vector from the sensor to the
/// touchdown, straight ray, no refraction) and `normal` is the seabed normal at
/// the touchdown from a finished depth surface, or the vertical where the
/// surface has no usable stencil. The vertical normal makes the angle the
/// earth-frame angle from vertical, which includes the vessel's attitude; that
/// is what such a beam is stamped with and it is counted, never reported as a
/// slope-corrected one.
///
/// The beam vector is the chord from the sensor to the touchdown in the
/// sensor's ENU frame. Against the touchdown's own ENU frame it is off by about
/// range / R_earth (1e-5 rad at 100 m); the projector traces no refraction, so
/// the chord is also the direction of arrival.
///
/// ### The baseline
/// The finite-difference baseline subtends a fixed angle at the sonar, so it
/// scales with range: the full baseline is `slant_range * radians(angle)`, the
/// probes sit at plus and minus half of it, and the module floors that half at
/// one native cell of the surface.

namespace cube
{

/// Upper bound on `SlopeOptions::baseline_deg`. It bounds the half-baseline at
/// `range * tan(15 deg)`-ish and so the margin of surface tiles a tile's
/// record has to cover.
  constexpr double kMaxSlopeBaselineDeg = 30.0;

/// Default baseline angle. PROVISIONAL until measured on real data
/// (cube_bathymetry#173 decision 14).
  constexpr double kDefaultSlopeBaselineDeg = 5.0;

  struct SlopeOptions
  {
  /// The angle the full differencing baseline subtends at the sonar, degrees,
  /// in (0, kMaxSlopeBaselineDeg].
    double baseline_deg = kDefaultSlopeBaselineDeg;
  };

/// What `stampIncidence` did, counted per beam. Cheap to update (fixed-size
/// histograms), merged by nothing: one instance per run.
  class IncidenceCounters
  {
public:
  /// Number of `FallbackReason` values (kNone .. kBadPosition).
    static constexpr std::size_t kReasonCount = 7;
  /// Width of one bin of the baseline histogram, metres.
    static constexpr double kBaselineBinM = 0.25;
    static constexpr std::size_t kBaselineBins = 1024;
  /// Slope histogram: one bin per degree (nearest), 0 to 90.
    static constexpr std::size_t kSlopeBins = 91;

  /// Every beam handed to `stampIncidence`.
    uint64_t beams = 0;
  /// Beams that carried no beam vector (incidence left NaN).
    uint64_t no_beam_vector = 0;
  /// Beams whose angle the module flagged invalid (non-unit vector, ...); left NaN.
    uint64_t invalid_angle = 0;
  /// Beams whose normal came from the surface.
    uint64_t from_surface = 0;
  /// Beams stamped with the vertical normal, by `FallbackReason` (index = value).
    std::array < uint64_t, kReasonCount > fallback {};
  /// Beams with an incidence at or beyond 90 deg (the back of a slope). Stamped
  /// raw; the table's "at or beyond 90 deg" fallback decides what happens to them.
    uint64_t back_facing = 0;
  /// Beams whose half-baseline was raised to the one-cell floor.
    uint64_t at_floor = 0;
  /// Largest half-baseline actually used, metres.
    double max_half_baseline_m = 0.0;

    void addBaseline(double full_baseline_m);
    void addSlope(double slope_deg);

  /// The @p p quantile (0..1) of the full baseline used, metres; NaN when none
  /// was recorded. Resolution is `kBaselineBinM`; the top bin holds everything
  /// above its edge.
    double baselinePercentile(double p) const;

  /// Beams that carry a baseline (a stencil was attempted).
    uint64_t baselineCount() const {return baseline_count_;}
    const std::array < uint64_t, kSlopeBins > & slopeHistogram() const {
      return slope_hist_;
    }

  /// Total beams stamped with the vertical normal.
    uint64_t fallbackTotal() const;

  /// The pass report: baseline distribution, floor fraction, slope histogram,
  /// fallback counts per reason. Plain text, one fact per line.
    std::string report() const;

private:
    std::vector < uint64_t > baseline_hist_ = std::vector < uint64_t > (kBaselineBins, 0);
    std::array < uint64_t, kSlopeBins > slope_hist_ {};
    uint64_t baseline_count_ = 0;
  };

/// Stamp `Sounding::incidence_angle` on every sounding of @p soundings (radians,
/// in [0, pi]; above pi/2 means a back-facing slope). A beam with no vector gets
/// NaN. @p sampler is not thread-safe, so neither is this call.
///
/// @throws whatever @p sampler throws (a corrupt tile is a fault, not a fallback).
  void stampIncidence(
    std::vector < GeoSounding > &soundings,
    marine_backscatter::seabed::SeabedHeightSampler & sampler,
    const SlopeOptions & options, IncidenceCounters & counters);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__SLOPE_INCIDENCE_H_
