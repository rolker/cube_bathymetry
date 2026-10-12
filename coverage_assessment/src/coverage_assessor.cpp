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

#include "coverage_assessment/coverage_assessor.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace coverage_assessment
{

AreaAssessment assessArea(
  const RegisteredArea & area, const std::vector<const SourceAccumulator *> & sources,
  double h_surface)
{
  if (!std::isfinite(h_surface)) {
    throw std::invalid_argument("assessArea: water surface height must be finite");
  }
  for (const SourceAccumulator * s : sources) {
    if (s == nullptr) {
      throw std::invalid_argument("assessArea: null source");
    }
  }

  const PolygonMask & mask = area.mask;
  const SurveyStandard & standard = area.definition.standard;
  const bool standard_set = standard.isSet();
  const std::size_t n = sources.size();

  AreaAssessment out;
  out.area_id = area.definition.id;
  out.mask = mask;
  out.verdict.assign(mask.inside.size(), kOutside);
  out.sources.assign(n, SourceShare());
  const double cell_size = area.cell_size;
  for (const SourceAccumulator * s : sources) {
    if (s->cellSize() != cell_size) {
      throw std::invalid_argument("assessArea: a source uses a different cell size than the area");
    }
  }
  out.cell_size = cell_size;

  std::size_t total = 0, covered = 0, meeting = 0;
  std::vector<std::size_t> src_covered(n, 0), src_meeting(n, 0), src_unique(n, 0);

  for (int64_t j = mask.j0; j < mask.j0 + mask.nj; ++j) {
    for (int64_t i = mask.i0; i < mask.i0 + mask.ni; ++i) {
      const std::size_t k = static_cast<std::size_t>((j - mask.j0) * mask.ni + (i - mask.i0));
      if (!mask.inside[k]) {
        continue;
      }
      ++total;

      std::size_t covering = 0;
      std::size_t only = 0;
      bool above_surface = false;
      bool have_winner = false;
      double win_u = std::numeric_limits<double>::infinity();
      double win_depth = std::numeric_limits<double>::infinity();
      for (std::size_t s = 0; s < n; ++s) {
        const HeldCell c = sources[s]->get(i, j);
        const double depth = h_surface - static_cast<double>(c.elevation);
        if (std::isfinite(depth) && depth < -kNegativeDepthMargin) {
          above_surface = true;
        }
        if (!(std::isfinite(depth) && depth >= 0.0)) {
          continue;  // does not cover
        }
        ++covering;
        only = s;
        ++src_covered[s];
        const double u = static_cast<double>(c.uncertainty);
        if (!(std::isfinite(u) && u >= 0.0)) {
          continue;  // covers, does not compete
        }
        if (standard_set && u <= standard.allowed(depth)) {
          ++src_meeting[s];
        }
        if (!have_winner || u < win_u || (u == win_u && depth < win_depth)) {
          have_winner = true;
          win_u = u;
          win_depth = depth;
        }
      }
      if (above_surface) {
        ++out.cells_above_surface;
      }
      if (covering == 0) {
        out.verdict[k] = kNotCovered;
        continue;
      }
      ++covered;
      if (covering == 1) {
        ++src_unique[only];
      }
      if (standard_set && have_winner && win_u <= standard.allowed(win_depth)) {
        ++meeting;
        out.verdict[k] = kMeetsStandard;
      } else {
        out.verdict[k] = kBelowStandard;
      }
    }
  }

  const double cell_area = cell_size * cell_size;
  out.area_total = static_cast<double>(total) * cell_area;
  out.area_covered = static_cast<double>(covered) * cell_area;
  out.area_meeting_standard = static_cast<double>(meeting) * cell_area;
  for (std::size_t s = 0; s < n; ++s) {
    out.sources[s].area_covered = static_cast<double>(src_covered[s]) * cell_area;
    out.sources[s].area_meeting_standard = static_cast<double>(src_meeting[s]) * cell_area;
    out.sources[s].area_unique = static_cast<double>(src_unique[s]) * cell_area;
  }
  return out;
}

std::vector<AreaAssessment> assessAll(
  const AreaRegistry & registry, const std::vector<const SourceAccumulator *> & sources,
  double h_surface)
{
  std::vector<AreaAssessment> out;
  out.reserve(registry.areas().size());
  for (const RegisteredArea & a : registry.areas()) {
    out.push_back(assessArea(a, sources, h_surface));
  }
  return out;
}

}  // namespace coverage_assessment
