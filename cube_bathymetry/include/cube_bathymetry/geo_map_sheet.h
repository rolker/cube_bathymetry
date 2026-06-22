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


#ifndef CUBE_BATHYMETRY__GEO_MAP_SHEET_H_
#define CUBE_BATHYMETRY__GEO_MAP_SHEET_H_

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "cube_bathymetry/geo_grid.h"

#include "marine_autonomy/gggs.h"

namespace cube
{

/// Uses GlobalGGS grid heirarchy to organize Grids.
  class GeoMapSheet
  {
public:
  /// Constructor where cell_size is approximate resolution requested.
    explicit GeoMapSheet(float cell_size, std::string iho_order = "order1a");

    void addSoundings(
      const std::vector < GeoSounding > &soundings,
      std::chrono::steady_clock::time_point time = std::chrono::steady_clock::now());

  /// Return the grids within the bounds, creating new ones if necessary
    std::vector < std::shared_ptr <
    GeoGrid >> getOrCreateGridsIn(const gz4d::BoundsDegrees & bounds);

  /// Return all existing grids
    std::vector < std::shared_ptr < GeoGrid >> grids() const;

  /// @brief Return the grid at @p index, or nullptr if none exists.
  ///
  /// Read-only handle (no lazy creation) used by the periodic save loop to
  /// convert a dirty grid without resurrecting empties.
    std::shared_ptr < const GeoGrid > gridAt(const gggs::GridIndex & index) const;

  /// @brief Find or create the grid at @p index (no dirty mark).
  ///
  /// Used by the warm-start prime path to reach a specific grid by index without
  /// going through the sounding-driven `addSoundings` path.
    std::shared_ptr < GeoGrid > getOrCreateGrid(const gggs::GridIndex & index);

  /// @brief Seed the predicted depth at @p cell (lazy-creates the grid + node).
  ///
  /// Warm-start prime for slope correction from a persisted draft tile (#21).
  /// Does NOT mark the grid dirty -- priming reproduces already-persisted data,
  /// so re-saving it would be redundant churn.
    void setPredictedDepthAt(const gggs::CellIndex & cell, float depth, float variance);

  /// @brief Grid indices touched (returning true from insert) since the last
  ///        clearDirtyGrids(). Returned by value -- safe to iterate while saving.
    std::set < gggs::GridIndex > dirtyGrids() const;

  /// @brief Clear the dirty-grid set (called after a successful save).
    void clearDirtyGrids();

  /// Return gggs::GridIndex bounds of rectangle containing all the grids
    gggs::GridBounds gridBounds() const;

  /// Cell size in degrees
    double cellSizeDegrees() const;

    double nominalCellSizeMeters() const;

    std::chrono::steady_clock::time_point lastUpdateTime() const;

private:
  /// Grid cell counts
  // CellCounts counts_;
  /// Cell sizes (meters)
  // CellSizes sizes_;

    Parameters parameters_;

    gggs::Level grid_level_;

    std::map < gggs::GridIndex, std::shared_ptr < GeoGrid >> grids_;

  /// Grids that received data (insert() returned true) since the last
  /// clearDirtyGrids(). Drives the periodic incremental tile save (#21).
    std::set < gggs::GridIndex > dirty_grids_;

    std::chrono::steady_clock::time_point last_update_time_;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__GEO_MAP_SHEET_H_
