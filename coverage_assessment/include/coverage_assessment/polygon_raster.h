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

#ifndef COVERAGE_ASSESSMENT__POLYGON_RASTER_H_
#define COVERAGE_ASSESSMENT__POLYGON_RASTER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coverage_assessment
{

/// Largest raster-frame coordinate, metres, in x or y, that a polygon vertex or a
/// grid footprint may have. Beyond it the double -> lattice-index conversions lose
/// their meaning (a huge finite value overflows int64, and above 2^53 the cell
/// loops cannot step by one), so such input is refused with a reason before any
/// conversion. 1e7 m is ten thousand kilometres from the raster origin: far beyond
/// any survey, still well inside double precision.
constexpr double kMaxRasterCoordinate = 1.0e7;

/// Largest lattice index (coordinate / cell size) accepted, for the same reason:
/// a tiny cell size must not push the indices toward 2^53.
constexpr double kMaxLatticeIndex = 1.0e12;

/// A point in the assessment raster's frame, metres.
  struct Point2
  {
    double x = 0.0;
    double y = 0.0;
  };

// The assessment lattice: cell (i, j) covers [i*cs, (i+1)*cs) x [j*cs, (j+1)*cs)
// in the raster frame, and its centre is ((i+0.5)*cs, (j+0.5)*cs). Every
// component of the package shares this one lattice, so a cell index means the
// same cell everywhere.

/// The cells of a polygon, as a dense mask over its bounding box of lattice
/// cells. A cell belongs to the polygon when its CENTRE is inside it, with the
/// half-open rule documented at rasterisePolygon().
  struct PolygonMask
  {
    /// lattice index of the mask's first column
    int64_t i0 = 0;
    /// lattice index of the mask's first row
    int64_t j0 = 0;
    /// columns (0 when the polygon holds no cell centre)
    int64_t ni = 0;
    /// rows
    int64_t nj = 0;
    /// row-major (j * ni + i), 1 = in the polygon
    std::vector < uint8_t > inside;
    /// number of cells set in `inside`
    std::size_t count = 0;

  /// True when lattice cell (i, j) is inside the bounding box and the polygon.
    bool contains(int64_t i, int64_t j) const
    {
      if (i < i0 || j < j0 || i >= i0 + ni || j >= j0 + nj) {
        return false;
      }
      return inside[static_cast < std::size_t > ((j - j0) * ni + (i - i0))] != 0;
    }
  };

/// Success flag, the mask, and (on failure) the reason, ready for a log line.
  struct PolygonRasterResult
  {
    bool ok = false;
    PolygonMask mask;
    std::string error;
  };

/// Rasterise a polygon (raster-frame metres, ring closed implicitly; a repeated
/// first vertex at the end is accepted and dropped) onto the lattice of
/// `cell_size`.
///
/// Rule: scanline even-odd fill at each cell-centre row. A cell is in the
/// polygon when its centre lies in the half-open span [x_left, x_right) of a
/// crossing pair, and an edge counts for a row when (a.y <= cy) != (b.y <= cy).
/// So a centre exactly on a lower or left edge is inside and one on an upper or
/// right edge is outside, which makes an axis-aligned rectangle of width w
/// metres hold exactly w / cell_size columns whatever its offset on the
/// lattice, and lets two polygons that share an edge partition the cells.
///
/// Rejected, with the reason in `error`: fewer than 3 distinct vertices, a
/// non-finite coordinate, a non-positive or non-finite cell size, zero area
/// (collinear vertices), and a bounding box of more than `max_cells` lattice
/// cells (guards a swapped latitude/longitude, which would otherwise ask for a
/// raster of the whole earth). A polygon smaller than a cell is valid and
/// yields an empty mask (count 0).
  PolygonRasterResult rasterisePolygon(
    const std::vector < Point2 > &polygon, double cell_size, int64_t max_cells);

}  // namespace coverage_assessment

#endif  // COVERAGE_ASSESSMENT__POLYGON_RASTER_H_
