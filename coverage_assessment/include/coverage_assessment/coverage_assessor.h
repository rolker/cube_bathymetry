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

#ifndef COVERAGE_ASSESSMENT__COVERAGE_ASSESSOR_H_
#define COVERAGE_ASSESSMENT__COVERAGE_ASSESSOR_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coverage_assessment/area_registry.h"
#include "coverage_assessment/source_accumulator.h"

namespace coverage_assessment
{

/// The verdict on one cell of an area's mask.
  enum CellVerdict : uint8_t
  {
    // not in the polygon
    kOutside = 0,
    // in the polygon, no source has a finite depth >= 0 there
    kNotCovered = 1,
    // covered, does not meet the standard
    kBelowStandard = 2,
    // covered and meets the standard
    kMeetsStandard = 3
  };

/// One source's share of an area, in square metres of the assessment raster.
  struct SourceShare
  {
    double area_covered = 0.0;
    double area_meeting_standard = 0.0;
    double area_unique = 0.0;
  };

/// The result for one area; mirrors marine_interfaces/CoverageAssessment.
  struct AreaAssessment
  {
    std::string area_id;
    double cell_size = 0.0;
    double area_total = 0.0;
    double area_covered = 0.0;
    double area_meeting_standard = 0.0;
  /// One per source, in the order the sources were given.
    std::vector < SourceShare > sources;

  /// Cells of the area where some source has a depth below -kNegativeDepthMargin
  /// (the seabed "above" the water surface by more than the margin). They are
  /// not covered (a cell above the surface has no survey depth), but a count
  /// well above zero means a wrong sign, datum or stale tide, so the wrapper
  /// logs it rather than let it pose as a plausible verdict.
    std::size_t cells_above_surface = 0;

  /// The area's mask and one CellVerdict per mask cell (row-major, as the mask).
    PolygonMask mask;
    std::vector < uint8_t > verdict;
  };

/// Depths more negative than this (metres) are counted in cells_above_surface.
  constexpr double kNegativeDepthMargin = 1.0;

/// Judge one area against the union of the given sources (the order of
/// `sources` is the order of AreaAssessment::sources). Every accumulator must
/// use the area's lattice (equal cell size).
///
/// Rules, exactly those of marine_interfaces/CoverageAssessment and
/// CoverageContribution:
///  - depth = h_surface - elevation, metres, positive down;
///  - a source COVERS a cell when its depth there is finite and >= 0
///    (uncertainty is not needed); the cell is covered when any source covers it;
///  - only sources with a finite depth >= 0 AND a finite uncertainty >= 0
///    COMPETE for the standard; the one with the smallest uncertainty supplies
///    the depth, ties going to the shallower depth (fail closed, since
///    allowed(depth) grows with depth); the cell meets when that uncertainty is
///    <= standard.allowed(depth); a covered cell where no source competes does
///    not meet;
///  - a standard that is not set is checked first: nothing meets it;
///  - per source: covered and meeting are judged on its own grid with its own
///    depth, and area_unique is the cells only that source covers.
///
/// Throws std::invalid_argument for a non-finite `h_surface` or a mismatched
/// lattice: both are caller bugs, and answering anyway would read as "nothing
/// covered".
  AreaAssessment assessArea(
    const RegisteredArea & area, const std::vector < const SourceAccumulator * > &sources,
    double h_surface);

/// assessArea for every registered area, in registry order.
  std::vector < AreaAssessment > assessAll(
  const AreaRegistry & registry, const std::vector < const SourceAccumulator * > &sources,
  double h_surface);

}  // namespace coverage_assessment

#endif  // COVERAGE_ASSESSMENT__COVERAGE_ASSESSOR_H_
