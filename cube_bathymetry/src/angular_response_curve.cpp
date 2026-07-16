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


#include "cube_bathymetry/angular_response_curve.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cube
{

namespace
{
/// Empirical angular-response lookup: the curve's db_relative_to_nadir at
/// @p abs_angle_deg, linearly interpolated between adjacent bin centres. The
/// curve is ascending {abs_angle_deg, db_relative_to_nadir} pairs (loaded by
/// loadAngularResponseCurve). Returns 0 (identity) when the curve is empty or
/// @p abs_angle_deg lies beyond the curve's max angle; clamps to the first bin
/// below the curve's min angle (nadir bin ~0 -> ~identity near nadir).
///
/// Moved verbatim from node.cpp (cube_bathymetry#93): the correction now runs at
/// record (recordBeam) instead of extract, so this helper lives next to
/// correctBeamIntensity. Unchanged logic -> identical corrected values.
double curveRelativeDb(
  const std::vector<std::pair<float, float>> & curve, double abs_angle_deg)
{
  if (curve.empty()) {
    return 0.0;
  }
  // Below the first bin centre: clamp to it (the nadir bin's value, ~0).
  if (abs_angle_deg <= curve.front().first) {
    return curve.front().second;
  }
  // Beyond the last bin centre: clamp to the outermost bin's correction rather
  // than jump to identity. The empirical curve is bounded (unlike a cos/log model
  // that diverges near grazing), so continuing the edge value keeps the correction
  // continuous and avoids a swath-edge discontinuity / bright ring (#81 review).
  if (abs_angle_deg > curve.back().first) {
    return curve.back().second;
  }
  // Find the bracketing pair [lo, hi] and linearly interpolate.
  for (std::size_t i = 1; i < curve.size(); ++i) {
    if (abs_angle_deg <= curve[i].first) {
      const double a0 = curve[i - 1].first;
      const double d0 = curve[i - 1].second;
      const double a1 = curve[i].first;
      const double d1 = curve[i].second;
      const double span = a1 - a0;
      if (span <= 0.0) {
        return d1;  // duplicate angle: take the upper bin's value
      }
      const double t = (abs_angle_deg - a0) / span;
      return d0 + t * (d1 - d0);
    }
  }
  return 0.0;  // unreachable (guarded by the back() check above)
}
}  // namespace

double correctBeamIntensity(
  float raw_intensity, float beam_angle, float range, const Parameters & parameters)
{
  // Exact per-beam math formerly in Node::extractNodeRecord (cube#80/#81/#87),
  // moved to record time (cube#93). Apply order is TL add-back THEN angular
  // residual, accumulated in double -- identical to the old extract.
  const bool apply_ara =
    (parameters.backscatter_angle_correction ==
    BackscatterAngleCorrection::Empirical ||
    parameters.backscatter_angle_correction ==
    BackscatterAngleCorrection::Auto) &&
    !parameters.angular_response_curve.empty();
  const bool apply_tl = apply_ara && parameters.backscatter_tl_removed;
  const double alpha = parameters.backscatter_absorption_db_per_m;

  double corrected = raw_intensity;
  if (apply_tl) {
    const double range_d = static_cast<double>(range);
    // Skip the TL term for a missing / non-positive range (log10 undefined); the
    // beam is still corrected by the residual angular-response curve below.
    if (std::isfinite(range_d) && range_d > 0.0) {
      corrected += 40.0 * std::log10(range_d) + 2.0 * alpha * range_d;
    }
  }
  if (apply_ara && !std::isnan(beam_angle)) {
    const double abs_angle_deg =
      std::abs(static_cast<double>(beam_angle)) * 180.0 / M_PI;
    corrected -= curveRelativeDb(parameters.angular_response_curve, abs_angle_deg);
  }
  return corrected;
}

bool parseBackscatterAngleCorrection(
  const std::string & text, BackscatterAngleCorrection & out)
{
  std::string lower;
  lower.reserve(text.size());
  for (char c : text) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "none" || lower.empty()) {
    out = BackscatterAngleCorrection::None;
    return true;
  }
  if (lower == "empirical") {
    out = BackscatterAngleCorrection::Empirical;
    return true;
  }
  if (lower == "auto") {
    out = BackscatterAngleCorrection::Auto;
    return true;
  }
  return false;
}

namespace
{
// Parse a `# key: value` provenance comment. Returns true and fills `value`
// (trimmed) when `line` (already known to start with '#') matches `key`.
bool matchHeaderComment(
  const std::string & line, const std::string & key, std::string & value)
{
  // Strip the leading '#' and surrounding whitespace, then split on the first
  // ':'. Tolerant of arbitrary leading whitespace / spacing around the colon.
  const auto hash = line.find('#');
  std::string body = line.substr(hash + 1);
  const auto colon = body.find(':');
  if (colon == std::string::npos) {
    return false;
  }
  std::string k = body.substr(0, colon);
  std::string v = body.substr(colon + 1);
  auto trim = [](std::string & s) {
      const auto b = s.find_first_not_of(" \t\r\n");
      const auto e = s.find_last_not_of(" \t\r\n");
      if (b == std::string::npos) {
        s.clear();
      } else {
        s = s.substr(b, e - b + 1);
      }
    };
  trim(k);
  trim(v);
  if (k != key) {
    return false;
  }
  value = v;
  return true;
}
}  // namespace

// NOTE: kongsberg_em_bridge/angular_response.py (marine_tools) is a Python
// mirror of this loader -- it parses the same CSVs to publish the curve in
// SonarInfo (marine_tools#71). Keep parsing-semantics changes in sync.
AngularResponseCurve loadAngularResponseCurveWithHeader(const std::string & path)
{
  AngularResponseCurve result;
  std::vector<std::pair<float, float>> & curve = result.points;
  if (path.empty()) {
    return result;
  }

  std::ifstream in(path);
  if (!in) {
    return result;
  }

  std::string line;
  while (std::getline(in, line)) {
    // Skip blank lines and comments -- but first mine comments for the optional
    // self-describing TL header (cube_bathymetry#87).
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || line[first] == '#') {
      if (first != std::string::npos) {
        std::string value;
        if (matchHeaderComment(line, "tl_removed", value)) {
          // Case-insensitive "true"/"1" -> true; anything else -> false.
          std::string lower;
          for (char c : value) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
          }
          result.tl_removed = (lower == "true" || lower == "1");
        } else if (matchHeaderComment(line, "absorption_db_per_m", value)) {
          try {
            std::size_t used = 0;
            const float a = std::stof(value, &used);
            if (used > 0) {
              result.absorption_db_per_m = a;
            }
          } catch (const std::exception &) {
            // Malformed absorption value -- leave at the tier-1 default (0).
          }
        }
      }
      continue;
    }

    // Split on commas. We need column 0 (abs_angle_deg_center) and column 3
    // (db_relative_to_nadir). A header row or any malformed row fails the float
    // parse and is skipped.
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
      fields.push_back(field);
    }
    if (fields.size() < 4) {
      continue;
    }

    try {
      std::size_t a_used = 0;
      std::size_t d_used = 0;
      const float angle = std::stof(fields[0], &a_used);
      const float db_rel = std::stof(fields[3], &d_used);
      // std::stof throws on a field with no leading number (e.g. the header's
      // "abs_angle_deg_center"), handled by the catch below. The used==0 check is
      // a belt-and-suspenders guard for an empty parse. Note a value like "1deg"
      // parses its numeric prefix (1.0) and IS accepted -- the derive tool writes
      // clean numeric columns, so trailing garbage is tolerated, not rejected.
      if (a_used == 0 || d_used == 0) {
        continue;
      }
      curve.emplace_back(angle, db_rel);
    } catch (const std::exception &) {
      // Header row or malformed numeric -- skip.
      continue;
    }
  }

  // Ensure ascending order by angle so the interpolation can assume monotonic
  // bin centres regardless of file ordering.
  std::sort(
    curve.begin(), curve.end(),
    [](const std::pair<float, float> & a, const std::pair<float, float> & b) {
      return a.first < b.first;
    });

  return result;
}

std::vector<std::pair<float, float>> loadAngularResponseCurve(const std::string & path)
{
  return loadAngularResponseCurveWithHeader(path).points;
}

}  // namespace cube
