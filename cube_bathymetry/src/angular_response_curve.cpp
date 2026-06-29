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
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cube
{

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
