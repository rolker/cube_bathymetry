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


#ifndef CUBE_BATHYMETRY__ANGULAR_RESPONSE_CURVE_H_
#define CUBE_BATHYMETRY__ANGULAR_RESPONSE_CURVE_H_

#include <string>
#include <utility>
#include <vector>
#include "cube_bathymetry/parameters.h"

namespace cube
{

/// Parse a backscatter angular-response correction mode string ("none" or
/// "empirical", case-insensitive). Returns false on an unrecognized value
/// (out is left unchanged); the caller decides how to report the error.
  bool parseBackscatterAngleCorrection(
    const std::string & text, BackscatterAngleCorrection & out);

/// A loaded angular-response curve plus its self-describing TL provenance
/// (cube_bathymetry#87). `points` is the ascending {abs_angle_deg,
/// db_relative_to_nadir} curve. When `tl_removed` is true the curve is a TL-
/// REMOVED residual (tier-2) and the estimator must remove the per-beam 2-way
/// transmission loss `40*log10(R) + 2*alpha*R` (alpha == `absorption_db_per_m`)
/// before subtracting the residual. A tier-1 curve has `tl_removed == false`
/// and `absorption_db_per_m == 0`, fully backward compatible.
  struct AngularResponseCurve
  {
    std::vector < std::pair < float, float >> points;
    bool tl_removed = false;
    float absorption_db_per_m = 0.0f;
  };

/// Load an empirical angular-response curve from a CSV file matching the seed at
/// ~/data/logs/analysis/m3_angular_response_curve.csv: a header line then rows
/// `abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir`. Reads the first
/// (abs_angle_deg_center) and fourth (db_relative_to_nadir) columns into ascending
/// {abs_angle_deg, db_relative_to_nadir} pairs (sorted by angle).
///
/// Also parses the optional self-describing TL header comments written by the
/// derive tool (cube_bathymetry#87):
///   `# tl_removed: true|false`
///   `# absorption_db_per_m: <float>`
/// Their absence yields tier-1 defaults (tl_removed=false, absorption=0), so a
/// tier-1 curve keeps loading unchanged. The scalar `absorption_db_per_m` is read
/// verbatim (the C++ estimator never recomputes alpha).
///
/// Comment lines (`#`...), blank lines, the header row, and rows that fail to
/// parse are skipped. A missing/unreadable file yields an empty curve (the
/// Empirical correction then degrades to a no-op -- callers should warn).
  AngularResponseCurve loadAngularResponseCurveWithHeader(const std::string & path);

/// Backward-compatible convenience: the curve `points` only (drops the TL
/// provenance). Equivalent to `loadAngularResponseCurveWithHeader(path).points`.
  std::vector < std::pair < float, float >> loadAngularResponseCurve(const std::string & path);

/// @brief Apply the node-output backscatter correction to one raw beam
///        (ADR-0007 D3, cube_bathymetry#81/#87).
///
/// Returns the corrected backscatter (dB) for a single beam, computed at RECORD
/// time (cube_bathymetry#93) so the CUBE hypothesis can stream a Welford of the
/// CORRECTED intensity instead of retaining every raw beam. This is the exact
/// per-beam math that used to run inside `Node::extractNodeRecord`, factored out
/// verbatim so correct-at-record reproduces correct-at-extract bit-for-bit:
///   - tier-2 TL add-back (cube#87): when @c backscatter_tl_removed and the range
///     is finite and > 0, add `40*log10(R) + 2*alpha*R` (alpha ==
///     @c backscatter_absorption_db_per_m). A NaN / non-positive range skips it.
///   - angular-response residual (cube#81): when the mode is Empirical with a
///     non-empty curve and the beam angle is not NaN, subtract
///     `curve(|beam_angle| in degrees)` (linearly interpolated, edge-clamped).
/// Identity (returns @p raw_intensity) when the mode is None or the curve is
/// empty, and for the angle term when @p beam_angle is NaN. The caller gates a
/// NaN @p raw_intensity (a NaN beam is never a sample). Computed and returned in
/// `double` to match the estimator's accumulation precision.
  double correctBeamIntensity(
    float raw_intensity, float beam_angle, float range, const Parameters & parameters);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__ANGULAR_RESPONSE_CURVE_H_
