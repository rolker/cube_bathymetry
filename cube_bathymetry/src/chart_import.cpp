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

#include "chart_import.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace cube_bathymetry
{

marine_bathymetry_store::BathyCell chartCellFromContour(
  double depth_below_surface,
  double uncertainty,
  double legacy_timestamp_seconds,
  double chart_datum_z)
{
  marine_bathymetry_store::BathyCell cell;

  // Band 1 NaN is the no-data sentinel: leave the cell at its default no-data
  // state (depth NaN, hasData() == false) and do not invent an ellipsoidal
  // height for a cell the contour prior never observed.
  if (std::isnan(depth_below_surface)) {
    return cell;
  }

  // ADR-0002 §D7: convert chart datum -> WGS84 ellipsoidal at import. The
  // contour band is depth *below the lake surface*, negative-down; the lake
  // surface sits at chart_datum_z above the ellipsoid, so the seabed's
  // ellipsoidal height is the surface height plus that (negative) offset.
  cell.depth = chart_datum_z + depth_below_surface;

  // Uncertainty is carried through verbatim (may itself be NaN = unknown).
  cell.uncertainty = uncertainty;

  // Legacy timestamp is Float64 seconds; the store keeps int64 nanoseconds.
  // Treat 0 / NaN as "unset" and leave the default 0 so we never fabricate an
  // acquisition time for a cell that lacked one.
  if (!std::isnan(legacy_timestamp_seconds) && legacy_timestamp_seconds != 0.0) {
    cell.timestamp =
      static_cast<int64_t>(std::llround(legacy_timestamp_seconds * 1e9));
  }

  // The Chart prior is a single read-only source; no registry, source_index 0.
  cell.source_index = 0;

  return cell;
}

}  // namespace cube_bathymetry
