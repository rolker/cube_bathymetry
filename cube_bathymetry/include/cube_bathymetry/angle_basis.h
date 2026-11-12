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

#ifndef CUBE_BATHYMETRY__ANGLE_BASIS_H_
#define CUBE_BATHYMETRY__ANGLE_BASIS_H_

#include <cctype>
#include <string>

/// @file
/// @brief Which angle indexes the backscatter angular-response correction
///        (cube_bathymetry#173). Header-only so the curve and table loaders,
///        which `Parameters` includes, can name it without a cycle.

namespace cube
{

/// `Beam` (the default, and what every store built before #173 used) is the
/// sonar-frame receive angle `|rx_angle|` of the ping; `Incidence` is the angle
/// between the beam's earth-frame arrival direction and the seabed normal (the
/// vertical where there is no surface), stamped on each sounding as
/// `Sounding::incidence_angle`. A curve or table is only valid on the basis it
/// was fitted on.
  enum class BackscatterAngleBasis
  {
    Beam,
    Incidence
  };

/// Stable lower-case name: `beam` or `incidence`.
  inline const char * toString(BackscatterAngleBasis basis)
  {
    return basis == BackscatterAngleBasis::Incidence ? "incidence" : "beam";
  }

/// Parse `beam` or `incidence` (case-insensitive). Returns false for anything
/// else, including the empty string (out is left unchanged): unlike the
/// correction mode there is no legacy empty spelling to keep.
  inline bool parseBackscatterAngleBasis(const std::string & text, BackscatterAngleBasis & out)
  {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text) {
      lower.push_back(static_cast < char > (std::tolower(static_cast < unsigned char > (c))));
    }
    if (lower == "beam") {
      out = BackscatterAngleBasis::Beam;
      return true;
    }
    if (lower == "incidence") {
      out = BackscatterAngleBasis::Incidence;
      return true;
    }
    return false;
  }

}  // namespace cube

#endif  // CUBE_BATHYMETRY__ANGLE_BASIS_H_
