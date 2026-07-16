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

#ifndef CUBE_BATHYMETRY__SONAR_INFO_CURVE_H_
#define CUBE_BATHYMETRY__SONAR_INFO_CURVE_H_

#include <string>
#include "cube_bathymetry/angular_response_curve.h"
#include "marine_interfaces/msg/sonar_info.hpp"

namespace cube
{

/// Extract + validate the angular-response curve carried in a SonarInfo
/// (cube#102; wire format uma#268, producer marine_tools#71). On success,
/// fills @p out (points sorted ascending by angle, TL provenance mapped) and
/// returns true. Returns false -- with a human-readable @p reject_reason --
/// when the message carries no usable curve:
///  - empty curve arrays (nothing declared; not an error, reason says so);
///  - parallel-array length mismatch (malformed producer);
///  - angular_response_tl == ANGULAR_RESPONSE_TL_UNKNOWN with a non-empty
///    curve (never guess the TL model -- mis-application corrupts the store);
///  - TL_REMOVED with a non-finite absorption (alpha unknown: the producer
///    publishes NaN when the curve file's header was missing/unparseable,
///    and the TL add-back 40*log10(R) + 2*alpha*R is impossible without it).
/// TL_IN maps to {tl_removed=false, absorption=0}; TL_REMOVED to
/// {tl_removed=true, absorption=angular_response_absorption_db_per_m}.
  bool curveFromSonarInfo(
    const marine_interfaces::msg::SonarInfo & info,
    AngularResponseCurve & out, std::string & reject_reason);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__SONAR_INFO_CURVE_H_
