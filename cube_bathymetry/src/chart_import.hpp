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

#ifndef CHART_IMPORT_HPP_
#define CHART_IMPORT_HPP_

#include "marine_bathymetry_store/bathy_cell.hpp"

/// @file
/// @brief ROS-free, GDAL-free conversion core for the chart-prior importer
/// (cube_bathymetry #44). Turns one raw contour cell — depth **below the lake
/// surface** (negative-down, metres), its uncertainty, and a legacy Float64
/// seconds timestamp — into the ellipsoidal-height `BathyCell` the
/// `marine_bathymetry_store` Chart layer stores (ADR-0002 §D7: datum is applied
/// at import; the store stays datum-agnostic).
///
/// Keeping the arithmetic here (no GDAL, no file I/O, no ROS) makes it
/// unit-testable in isolation; the executable handles GDAL reads and the store.

namespace cube_bathymetry
{

/// @brief Convert one raw contour cell to an ellipsoidal-height `BathyCell`.
///
/// @param depth_below_surface Band 1: depth below the lake surface, negative
///   down, metres. `NaN` marks a no-data cell.
/// @param uncertainty Band 2: 1-sigma vertical uncertainty, metres (carried
///   through verbatim).
/// @param legacy_timestamp_seconds Band 3: legacy acquisition time, seconds
///   since the Unix epoch (Float64). `0` or `NaN` leaves the cell's timestamp
///   unset (`0`).
/// @param chart_datum_z Ellipsoidal height (WGS84, metres) of the lake surface
///   at this tile, from `mru_transform::resolve_datum`.
/// @return A `BathyCell` with `depth = chart_datum_z + depth_below_surface`
///   (ellipsoidal), `uncertainty` carried through, `timestamp` converted
///   seconds→nanoseconds, and `source_index = 0` (the Chart prior is a single
///   read-only source — no registry). When @p depth_below_surface is `NaN` the
///   returned cell is no-data (depth `NaN`, `!hasData()`).
marine_bathymetry_store::BathyCell chartCellFromContour(
  double depth_below_surface,
  double uncertainty,
  double legacy_timestamp_seconds,
  double chart_datum_z);

}  // namespace cube_bathymetry

#endif  // CHART_IMPORT_HPP_
