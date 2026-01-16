// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint Hydrographic Center, University of New Hampshire
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


#ifndef CUBE_BATHYMETRY_GEO_MAP_SHEET_H
#define CUBE_BATHYMETRY_GEO_MAP_SHEET_H

#include "geo_grid.h"
#include <map>
#include <chrono>

#include "marine_autonomy/gggs.h"

namespace cube
{

/// Uses GlobalGGS grid heirarchy to organize Grids.
class GeoMapSheet
{
public:
  /// Constructor where cell_size is approximate resolution requested.
  GeoMapSheet(float cell_size, std::string iho_order = "order1a");

  void addSoundings(const std::vector<GeoSounding> & soundings, std::chrono::steady_clock::time_point time = std::chrono::steady_clock::now());

  /// Return the grids within the bounds, creating new ones if necessary
  std::vector<std::shared_ptr<GeoGrid> > getOrCreateGridsIn(const gz4d::BoundsDegrees& bounds);

  /// Return all existing grids
  std::vector<std::shared_ptr<GeoGrid> > grids() const;

  /// Return gggs::GridIndex bounds of rectangle containing all the grids
  gggs::GridBounds gridBounds() const;

  /// Cell size in degrees
  double cellSizeDegrees() const;

  double nominalCellSizeMeters() const;

  std::chrono::steady_clock::time_point lastUpdateTime() const;

private:
  /// Grid cell counts
  //CellCounts counts_;
  /// Cell sizes (meters)
  //CellSizes sizes_;

  Parameters parameters_;

  gggs::Level grid_level_;

  std::map<gggs::GridIndex, std::shared_ptr<GeoGrid> > grids_;

  std::chrono::steady_clock::time_point last_update_time_;
};

} // namespace cube

#endif
