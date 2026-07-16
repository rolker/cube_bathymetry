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

#include "cube_bathymetry/sonar_info_curve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>

namespace cube
{

bool curveFromSonarInfo(
  const marine_interfaces::msg::SonarInfo & info,
  AngularResponseCurve & out, std::string & reject_reason)
{
  using marine_interfaces::msg::SonarInfo;
  const auto & angles = info.angular_response_angle_deg;
  const auto & dbs = info.angular_response_db_rel_nadir;

  if (angles.empty() && dbs.empty()) {
    reject_reason = "SonarInfo declares no angular-response curve";
    return false;
  }
  if (angles.size() != dbs.size()) {
    std::ostringstream ss;
    ss << "SonarInfo angular-response arrays disagree: " << angles.size()
       << " angles vs " << dbs.size() << " values (malformed producer)";
    reject_reason = ss.str();
    return false;
  }
  if (info.angular_response_tl == SonarInfo::ANGULAR_RESPONSE_TL_UNKNOWN) {
    reject_reason =
      "SonarInfo curve has UNKNOWN TL provenance -- refusing to guess the "
      "TL model (tier-1 vs tier-2 mis-application corrupts the store)";
    return false;
  }
  const bool tl_removed =
    info.angular_response_tl == SonarInfo::ANGULAR_RESPONSE_TL_REMOVED;
  const float alpha = info.angular_response_absorption_db_per_m;
  if (tl_removed && !std::isfinite(alpha)) {
    reject_reason =
      "SonarInfo curve is TL_REMOVED but its absorption is not finite "
      "(alpha unknown -- the TL add-back is impossible without it)";
    return false;
  }

  // Wire input: reject non-finite points outright. Beyond feeding NaN into
  // the interpolation, a NaN point would also break the consumer's
  // latch-first same-curve comparison (NaN != NaN) on every heartbeat.
  for (std::size_t i = 0; i < angles.size(); ++i) {
    if (!std::isfinite(angles[i]) || !std::isfinite(dbs[i])) {
      std::ostringstream ss;
      ss << "SonarInfo angular-response curve has a non-finite point at index "
         << i << " (malformed producer)";
      reject_reason = ss.str();
      return false;
    }
  }

  out.points.clear();
  out.points.reserve(angles.size());
  for (std::size_t i = 0; i < angles.size(); ++i) {
    out.points.emplace_back(angles[i], dbs[i]);
  }
  // Same postcondition as loadAngularResponseCurveWithHeader: ascending
  // angles so the interpolation can assume monotonic bin centres.
  std::sort(
    out.points.begin(), out.points.end(),
    [](const std::pair<float, float> & a, const std::pair<float, float> & b) {
      return a.first < b.first;
    });
  out.tl_removed = tl_removed;
  out.absorption_db_per_m = tl_removed ? alpha : 0.0f;
  reject_reason.clear();
  return true;
}

}  // namespace cube
