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

#include "coverage_assessment/polygon_raster.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace coverage_assessment
{

namespace
{

PolygonRasterResult fail(const std::string & why)
{
  PolygonRasterResult r;
  r.ok = false;
  r.error = why;
  return r;
}

// First lattice index i with (i + 0.5) * cs >= x. The division is only a
// starting guess; the loops make the answer agree with the comparison the
// rule is defined by, so rounding in x / cs can never move a boundary cell.
int64_t firstCentreAtOrAbove(double x, double cs)
{
  int64_t i = static_cast<int64_t>(std::ceil(x / cs - 0.5));
  while ((static_cast<double>(i) + 0.5) * cs < x) {
    ++i;
  }
  while ((static_cast<double>(i - 1) + 0.5) * cs >= x) {
    --i;
  }
  return i;
}

std::string oversize(double cells, double cell_size, int64_t max_cells)
{
  std::ostringstream os;
  os << "polygon spans " << cells << " lattice cells at cell size " << cell_size
     << " m, over the limit of " << max_cells;
  return os.str();
}

}  // namespace

PolygonRasterResult rasterisePolygon(
  const std::vector<Point2> & polygon, double cell_size, int64_t max_cells)
{
  if (!std::isfinite(cell_size) || cell_size <= 0.0) {
    return fail("cell size must be finite and positive");
  }
  for (const Point2 & p : polygon) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
      return fail("polygon has a non-finite coordinate");
    }
  }
  double largest = 0.0;
  for (const Point2 & p : polygon) {
    largest = std::max(largest, std::max(std::fabs(p.x), std::fabs(p.y)));
  }
  if (largest > kMaxRasterCoordinate) {
    std::ostringstream os;
    os << "polygon has a coordinate " << largest <<
      " m from the raster origin, beyond the limit of "
       << kMaxRasterCoordinate << " m (check the map frame and the vertices)";
    return fail(os.str());
  }
  if (largest / cell_size > kMaxLatticeIndex) {
    std::ostringstream os;
    os << "polygon reaches lattice index " << largest / cell_size << " at cell size " << cell_size
       << " m, beyond the limit of " << kMaxLatticeIndex;
    return fail(os.str());
  }

  std::vector<Point2> ring = polygon;
  if (ring.size() > 1 && ring.front().x == ring.back().x && ring.front().y == ring.back().y) {
    ring.pop_back();
  }
  if (ring.size() < 3) {
    return fail("polygon needs at least 3 distinct vertices");
  }

  double min_x = ring[0].x, max_x = ring[0].x, min_y = ring[0].y, max_y = ring[0].y;
  double twice_area = 0.0;
  for (std::size_t k = 0; k < ring.size(); ++k) {
    const Point2 & a = ring[k];
    const Point2 & b = ring[(k + 1) % ring.size()];
    min_x = std::min(min_x, a.x);
    max_x = std::max(max_x, a.x);
    min_y = std::min(min_y, a.y);
    max_y = std::max(max_y, a.y);
    twice_area += a.x * b.y - b.x * a.y;
  }
  const double box = (max_x - min_x) * (max_y - min_y);
  if (!(std::fabs(twice_area) > 1e-12 * box)) {
    return fail("polygon has zero area");
  }

  // Bounding box of the lattice cells whose CENTRE can be inside: the first
  // centre at or above the minimum up to the last centre below the maximum.
  // Counted in double first (a loose estimate, within one cell of the exact
  // count) so a huge extent is refused before any integer conversion could
  // overflow.
  const double est_ni = std::ceil(max_x / cell_size - 0.5) - std::ceil(min_x / cell_size - 0.5);
  const double est_nj = std::ceil(max_y / cell_size - 0.5) - std::ceil(min_y / cell_size - 0.5);
  const double estimate = std::max(est_ni, 0.0) * std::max(est_nj, 0.0);
  if (!(estimate <= static_cast<double>(max_cells))) {
    return fail(oversize(estimate, cell_size, max_cells));
  }

  PolygonRasterResult out;
  out.ok = true;
  PolygonMask & m = out.mask;
  const int64_t i_first = firstCentreAtOrAbove(min_x, cell_size);
  const int64_t i_end = firstCentreAtOrAbove(max_x, cell_size);
  const int64_t j_first = firstCentreAtOrAbove(min_y, cell_size);
  const int64_t j_end = firstCentreAtOrAbove(max_y, cell_size);
  if (i_end <= i_first || j_end <= j_first) {
    return out;  // no cell centre can be inside: valid, zero cells
  }
  if (static_cast<double>(i_end - i_first) * static_cast<double>(j_end - j_first) >
    static_cast<double>(max_cells))
  {
    return fail(oversize(
      static_cast<double>(i_end - i_first) * static_cast<double>(j_end - j_first),
      cell_size, max_cells));
  }
  m.i0 = i_first;
  m.j0 = j_first;
  m.ni = i_end - i_first;
  m.nj = j_end - j_first;
  m.inside.assign(static_cast<std::size_t>(m.ni * m.nj), 0);

  std::vector<double> xs;
  for (int64_t j = m.j0; j < m.j0 + m.nj; ++j) {
    const double cy = (static_cast<double>(j) + 0.5) * cell_size;
    xs.clear();
    for (std::size_t k = 0; k < ring.size(); ++k) {
      const Point2 & a = ring[k];
      const Point2 & b = ring[(k + 1) % ring.size()];
      if ((a.y <= cy) != (b.y <= cy)) {
        xs.push_back(a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y));
      }
    }
    std::sort(xs.begin(), xs.end());
    for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
      const int64_t first = std::max(firstCentreAtOrAbove(xs[k], cell_size), m.i0);
      const int64_t end = std::min(firstCentreAtOrAbove(xs[k + 1], cell_size), m.i0 + m.ni);
      for (int64_t i = first; i < end; ++i) {
        m.inside[static_cast<std::size_t>((j - m.j0) * m.ni + (i - m.i0))] = 1;
        ++m.count;
      }
    }
  }
  return out;
}

}  // namespace coverage_assessment
