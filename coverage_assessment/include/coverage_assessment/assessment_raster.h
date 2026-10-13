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

#ifndef COVERAGE_ASSESSMENT__ASSESSMENT_RASTER_H_
#define COVERAGE_ASSESSMENT__ASSESSMENT_RASTER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "coverage_assessment/coverage_assessor.h"
#include "grid_map_core/grid_map_core.hpp"

namespace coverage_assessment
{

/// Name of the one layer of the coverage grid.
  constexpr const char * kMeetsStandardLayer = "meets_standard";

  struct CoverageGridResult
  {
    bool ok = false;
    grid_map::GridMap grid;
  /// For !ok: the reason, ready for a log line.
    std::string error;
  };

/// The per-cell verdicts of all assessed areas as one raster over the bounding
/// box of the areas' masks, on the shared lattice (cell (i, j) of the lattice is
/// the grid cell at ((i + 0.5) * cell_size, (j + 0.5) * cell_size) in the
/// raster frame). Layer `meets_standard`: NaN where the cell is outside every
/// area or not covered, 0 where it is covered and does not meet the standard,
/// 1 where it meets it. Where areas overlap, a cell is 1 only if it meets the
/// standard of EVERY area containing it (it is 0 if any area's standard is not
/// met there).
///
/// Fails (ok = false, reason in `error`) when there is no cell to draw (no
/// assessments, or every area smaller than a cell) or the bounding box of all
/// areas exceeds `max_cells`, which two far-apart areas can do even when each is
/// within its own limit.
  CoverageGridResult buildCoverageGrid(
    const std::vector < AreaAssessment > &assessments, double cell_size,
    const std::string & frame_id,
    int64_t max_cells);

}  // namespace coverage_assessment

#endif  // COVERAGE_ASSESSMENT__ASSESSMENT_RASTER_H_
