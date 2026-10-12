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

#include "coverage_assessment/assessment_raster.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace coverage_assessment
{

namespace
{

CoverageGridResult fail(const std::string & why)
{
  CoverageGridResult r;
  r.ok = false;
  r.error = why;
  return r;
}

}  // namespace

CoverageGridResult buildCoverageGrid(
  const std::vector<AreaAssessment> & assessments, double cell_size, const std::string & frame_id,
  int64_t max_cells)
{
  if (!std::isfinite(cell_size) || cell_size <= 0.0) {
    return fail("cell size must be finite and positive");
  }
  bool any = false;
  int64_t i_lo = 0, j_lo = 0, i_hi = 0, j_hi = 0;  // [lo, hi) in lattice cells
  for (const AreaAssessment & a : assessments) {
    if (a.mask.ni <= 0 || a.mask.nj <= 0) {
      continue;
    }
    if (!any) {
      i_lo = a.mask.i0;
      j_lo = a.mask.j0;
      i_hi = a.mask.i0 + a.mask.ni;
      j_hi = a.mask.j0 + a.mask.nj;
      any = true;
    } else {
      i_lo = std::min(i_lo, a.mask.i0);
      j_lo = std::min(j_lo, a.mask.j0);
      i_hi = std::max(i_hi, a.mask.i0 + a.mask.ni);
      j_hi = std::max(j_hi, a.mask.j0 + a.mask.nj);
    }
  }
  if (!any) {
    return fail("no area holds a cell to draw");
  }
  const double cells = static_cast<double>(i_hi - i_lo) * static_cast<double>(j_hi - j_lo);
  if (cells > static_cast<double>(max_cells)) {
    std::ostringstream os;
    os << "the areas together span " << cells << " lattice cells, over the limit of " << max_cells;
    return fail(os.str());
  }

  CoverageGridResult out;
  grid_map::GridMap & g = out.grid;
  g.setFrameId(frame_id);
  // Geometry first: setGeometry resizes (and so clears) any layer already added.
  g.setGeometry(
    grid_map::Length(
      static_cast<double>(i_hi - i_lo) * cell_size, static_cast<double>(j_hi - j_lo) * cell_size),
    cell_size,
    grid_map::Position(
      0.5 * static_cast<double>(i_lo + i_hi) * cell_size,
      0.5 * static_cast<double>(j_lo + j_hi) * cell_size));
  g.add(kMeetsStandardLayer, std::numeric_limits<float>::quiet_NaN());

  grid_map::Matrix & layer = g.get(kMeetsStandardLayer);
  for (const AreaAssessment & a : assessments) {
    for (int64_t j = a.mask.j0; j < a.mask.j0 + a.mask.nj; ++j) {
      for (int64_t i = a.mask.i0; i < a.mask.i0 + a.mask.ni; ++i) {
        const uint8_t v = a.verdict[static_cast<std::size_t>(
              (j - a.mask.j0) * a.mask.ni + (i - a.mask.i0))];
        if (v != kBelowStandard && v != kMeetsStandard) {
          continue;
        }
        grid_map::Index idx;
        if (!g.getIndex(
            grid_map::Position(
              (static_cast<double>(i) + 0.5) * cell_size,
              (static_cast<double>(j) + 0.5) * cell_size), idx))
        {
          return fail("internal error: a cell of an area fell outside the coverage grid");
        }
        const float value = (v == kMeetsStandard) ? 1.0f : 0.0f;
        float & cell = layer(idx(0), idx(1));
        cell = std::isnan(cell) ? value : std::min(cell, value);
      }
    }
  }
  out.ok = true;
  return out;
}

}  // namespace coverage_assessment
