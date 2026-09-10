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

#ifndef CUBE_BATHYMETRY__PROJECTION_SUMMARY_H_
#define CUBE_BATHYMETRY__PROJECTION_SUMMARY_H_

#include <cstddef>
#include <iostream>
#include <ostream>

// Shared run summary for the three offline tools (import_bag, batch_regen,
// bag_to_geotiff). It was three verbatim copies of the same paragraph, which
// is how the wording drifts; there is one copy now (#144). Header-only and
// rclcpp-free so the no-node library target can carry it.

namespace cube
{

/// Whole-run accumulation of ProjectionDiagnostics, plus the ping counters the
/// tools keep themselves. Add fields here, not in three switch-off copies.
  struct ProjectionRunTotals
  {
  /// Pings handed to DetectionsProjector::project().
    size_t pings = 0;

  /// Pings that made it into the grid, and pings dropped for want of an earth
  /// transform. Only reported when `reports_georeferencing` is set --
  /// bag_to_geotiff does not georeference in this pass and leaves them zero.
    size_t georeferenced_pings = 0;
    size_t dropped_georef = 0;

  /// Soundings kept (after the range gate) and beams seen (before it).
    size_t soundings = 0;
    size_t beams = 0;

    size_t filtered_range = 0;
    size_t missing_attitude = 0;
    size_t missing_heave = 0;

  /// Beams whose angular uncertainty came from the generic
  /// `Device::across_track_beamwidth` rather than from the ping. See
  /// `ProjectionDiagnostics::default_beamwidth_beams`.
    size_t default_beamwidth_beams = 0;

  /// False for tools that do not georeference (bag_to_geotiff): the
  /// georeferenced/dropped clause and its warning are then omitted.
    bool reports_georeferencing = true;
  };

/// Print the offline-projection run summary, plus the warnings that only make
/// sense once the whole pass is finished. `out` takes the summary, `err` the
/// warnings, matching the tools' existing stdout/stderr split.
  inline void report_projection_summary(
    const ProjectionRunTotals & totals,
    std::ostream & out = std::cout,
    std::ostream & err = std::cerr)
  {
    out << "Offline projection: " << totals.pings << " pings";
    if (totals.reports_georeferencing) {
      out << " projected, " << totals.georeferenced_pings
          << " georeferenced into the grid, " << totals.dropped_georef
          << " dropped (no earth TF);";
    } else {
      out << ",";
    }
    out << " " << totals.soundings << " soundings ("
        << totals.filtered_range << " range-filtered, "
        << totals.missing_attitude << " missing attitude, "
        << totals.missing_heave << " missing heave, "
        << totals.default_beamwidth_beams << " of " << totals.beams
        << " beams on the default beamwidth)" << std::endl;

    if (totals.default_beamwidth_beams > 0) {
      err << "WARNING: " << totals.default_beamwidth_beams << " of " << totals.beams
          << " beams took the generic device across-track beamwidth "
        "(rx_beamwidths absent, too short, or reporting a value that is "
        "non-finite, non-positive, or >= pi rad), so their angular "
        "uncertainty is a default rather than a measurement."
          << std::endl;
    }
    if (totals.pings > 0 && totals.soundings == 0) {
      err << "WARNING: projected 0 soundings from " << totals.pings
          << " pings -- check the --*-frame overrides match the bag's "
          << "namespaced frames (see README 'Configuring frames per platform')."
          << std::endl;
    }
    if (totals.reports_georeferencing &&
      totals.georeferenced_pings == 0 && totals.dropped_georef > 0)
    {
      err << "WARNING: every ping was dropped for lack of an earth transform -- "
          << "check that the bag has a localization chain to the 'earth' frame."
          << std::endl;
    }
  }

}  // namespace cube

#endif  // CUBE_BATHYMETRY__PROJECTION_SUMMARY_H_
