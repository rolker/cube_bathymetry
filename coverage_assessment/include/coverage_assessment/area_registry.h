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

#ifndef COVERAGE_ASSESSMENT__AREA_REGISTRY_H_
#define COVERAGE_ASSESSMENT__AREA_REGISTRY_H_

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "coverage_assessment/polygon_raster.h"

namespace coverage_assessment
{

/// What "surveyed to standard" means: allowed(depth) = sqrt(a^2 + (b * depth)^2),
/// the form of marine_interfaces/SurveyStandard (a in metres at 95 %, b
/// dimensionless, depth positive down).
  struct SurveyStandard
  {
    std::string name;
    double a = 0.0;
    double b = 0.0;

  /// A standard is "set" when a and b are both finite and >= 0 and not both 0.
  /// A default-constructed one (a = b = 0), or one with a NaN, negative or
  /// infinite a or b, is NOT set: it fails closed and nothing meets it, whatever
  /// the cell's uncertainty (a cell with uncertainty exactly 0 would otherwise
  /// pass 0 <= 0).
    bool isSet() const
    {
      return std::isfinite(a) && std::isfinite(b) && a >= 0.0 && b >= 0.0 && (a > 0.0 || b > 0.0);
    }

  /// The largest uncertainty that meets the standard at `depth` (metres, positive
  /// down). Only meaningful when isSet().
    double allowed(double depth) const
    {
      return std::sqrt(a * a + (b * depth) * (b * depth));
    }

  /// True when a cell of uncertainty `u` at `depth` meets the standard. Only
  /// meaningful when isSet(). The grids carry float uncertainty, and a float
  /// such as 0.1f widens to a double above the double 0.1, so a cell exactly at
  /// the standard would fail a double compare; the allowed value is therefore
  /// rounded to float, the type `u` came in, before the comparison.
    bool meets(double u, double depth) const
    {
      return static_cast < float > (u) <= static_cast < float > (allowed(depth));
    }
  };

/// One target polygon, with the polygon already in assessment-raster metres
/// (the wrapper projects the geographic polygon and fills this).
  struct AreaDefinition
  {
    std::string id;
  /// When the definition was made, nanoseconds; orders repeats of one id.
    int64_t stamp_ns = 0;
    std::vector < Point2 > polygon;
    SurveyStandard standard;
  };

/// A held area with its polygon rasterised on the lattice.
  struct RegisteredArea
  {
    AreaDefinition definition;
    PolygonMask mask;
  /// The lattice spacing the mask was rasterised at, metres.
    double cell_size = 0.0;
  };

  enum class RegistryOutcome
  {
    // a new id
    kAdded,
    // the same id, stamp >= the held one
    kReplaced,
    // the same id, stamp older than the held one; nothing changed
    kIgnoredStale,
    // invalid; nothing changed (an earlier definition is kept)
    kRejected
  };

  struct RegistryResult
  {
    RegistryOutcome outcome = RegistryOutcome::kRejected;
  /// For kRejected and kIgnoredStale: the reason, ready for a log line.
    std::string reason;
  };

/// The areas currently being assessed, in the order their ids first arrived.
  class AreaRegistry
  {
public:
  /// `cell_size` is the lattice spacing; `max_cells` caps one area's bounding
  /// box in lattice cells (guards a swapped latitude/longitude);
  /// `max_total_cells` caps the bounding boxes of all held areas together
  /// (every assessment pass scans each held area's whole box once per source).
    AreaRegistry(
      double cell_size, int64_t max_cells,
      int64_t max_total_cells = std::numeric_limits < int64_t > ::max());

  /// Add, replace or refuse a definition. An id is replaced only when the new
  /// stamp is >= the held one (on an equal stamp the last received wins). A
  /// definition is refused, keeping any earlier one of the same id, when its
  /// polygon cannot be rasterised (fewer than 3 vertices, non-finite
  /// coordinates, zero area, over `max_cells`), or when holding it would take
  /// the bounding boxes of all areas over `max_total_cells` (an update of a held
  /// id is judged with that id's old box removed). The standard is never a reason
  /// to refuse: SurveyStandard.msg defines a = b = 0, or a NaN, negative or
  /// infinite a or b, as "not set", and nothing will meet it -- an honest state
  /// to publish (area_meeting_standard 0).
    RegistryResult update(const AreaDefinition & definition);

    const std::vector < RegisteredArea > & areas() const {
      return areas_;
    }
    const RegisteredArea * find(const std::string & id) const;
    double cellSize() const {return cell_size_;}
    void clear() {areas_.clear();}
  /// Bounding-box cells of all held areas: what one pass scans per source.
    int64_t totalCells() const;

private:
    bool exceedsTotal(
      int64_t others, int64_t added, const std::string & id, std::string * reason) const;

    double cell_size_;
    int64_t max_cells_;
    int64_t max_total_cells_;
    std::vector < RegisteredArea > areas_;
  };

}  // namespace coverage_assessment

#endif  // COVERAGE_ASSESSMENT__AREA_REGISTRY_H_
