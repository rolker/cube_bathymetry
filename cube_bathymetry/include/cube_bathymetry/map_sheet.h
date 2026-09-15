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


#ifndef CUBE_BATHYMETRY__MAP_SHEET_H_
#define CUBE_BATHYMETRY__MAP_SHEET_H_

#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "cube_bathymetry/grid.h"

namespace cube
{

/// A grid of Grids used to grow surfaces without knowing the bounds
/// ahead of time.
  class MapSheet
  {
public:
  /// Constructor where counts is number of cells in individual grids, sizes contains the size of
  /// individual cells and order is the IHO order.
    MapSheet(CellCounts counts, CellSizes sizes, std::string iho_order = "order1a");

    void addSoundings(
      const std::vector < MapSounding > &soundings,
      std::chrono::steady_clock::time_point time = std::chrono::steady_clock::now());

  /// Return the grids within the bounds, creating new ones if necessary
    std::vector < std::shared_ptr < Grid >> getOrCreateGridsIn(const MapBounds & bounds);

  /// Return all existing grids
    std::vector < std::shared_ptr < Grid >> grids() const;

  /// Return total cell count of rectangle containing all the grids
    CellCounts totalCellCounts() const;

  /// Return bounds in map coordinates of rectangle containing all the grids
    MapBounds gridBounds() const;

    const CellSizes & cellSizes() const;
    const CellCounts & cellCountsPerGrid() const;

    GridIndex gridIndex(const MapPosition & position) const;

    std::chrono::steady_clock::time_point lastUpdateTime() const;

  /// @brief Set the spacing term of the node capture distance
  ///        (`Parameters::capture_spacing_scale`, cube_bathymetry#143); reaches
  ///        every owned grid, which hold the parameters by const reference.
  /// @throws std::invalid_argument if @p scale is not finite and positive.
    void setCaptureSpacingScale(float scale)
    {
      if (!std::isfinite(scale) || scale <= 0.0f) {
        throw std::invalid_argument(
          "MapSheet::setCaptureSpacingScale: scale must be finite and positive");
      }
      parameters_.capture_spacing_scale = scale;
    }

  /// The node spacing the sheet's Parameters use (`distance_scale`, metres).
    double distanceScale() const {return parameters_.distance_scale;}

private:
  /// Grid cell counts
    CellCounts counts_;
  /// Cell sizes (meters)
    CellSizes sizes_;

    Parameters parameters_;

    std::map < GridIndex, std::shared_ptr < Grid >> grids_;

    std::chrono::steady_clock::time_point last_update_time_;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__MAP_SHEET_H_
