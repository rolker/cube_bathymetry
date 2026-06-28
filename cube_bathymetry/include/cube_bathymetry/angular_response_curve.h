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

/// Load an empirical angular-response curve from a CSV file matching the seed at
/// ~/data/logs/analysis/m3_angular_response_curve.csv: a header line then rows
/// `abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir`. Reads the first
/// (abs_angle_deg_center) and fourth (db_relative_to_nadir) columns into ascending
/// {abs_angle_deg, db_relative_to_nadir} pairs (sorted by angle).
///
/// Comment lines (`#`...), blank lines, the header row, and rows that fail to
/// parse are skipped. A missing/unreadable file yields an empty vector (the
/// Empirical correction then degrades to a no-op -- callers should warn).
  std::vector < std::pair < float, float >> loadAngularResponseCurve(const std::string & path);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__ANGULAR_RESPONSE_CURVE_H_
