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


#include "cube_bathymetry/grid_projection.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gz4d_geo.h"

namespace cube
{

namespace
{

// One finite cell's projected map-frame position and value, computed once in the
// first pass and reused in the fill pass (so the lat/lon->ECEF->affine work runs
// exactly once per cell).
struct ProjectedCell
{
  double map_x;
  double map_y;
  float depth;
  float uncertainty;
};

// Convert a cell center lat/lon (degrees) to ECEF, apply the single map<-earth
// affine, and return the map-frame XY. Mirrors (in reverse) the earth->lat/lon
// ingestion chain in bag_to_geotiff.cpp; the affine is computed ONCE by the
// caller (no per-cell TF lookup).
inline void projectCellCenter(
  double lat, double lon, const Eigen::Isometry3d & map_from_earth,
  double & out_x, double & out_y)
{
  gz4d::GeoPointLatLongDegrees ll(lat, lon, 0.0);
  gz4d::GeoPointECEF ecef(ll);
  const Eigen::Vector3d map_pt =
    map_from_earth * Eigen::Vector3d(ecef.x(), ecef.y(), ecef.z());
  out_x = map_pt.x();
  out_y = map_pt.y();
}

}  // namespace

grid_map::GridMap geoGridsToGridMap(
  const std::vector<std::shared_ptr<const GeoGrid>> & grids,
  const std::string & map_frame,
  double cell_size_m,
  const Eigen::Isometry3d & map_from_earth)
{
  grid_map::GridMap map;

  // First pass: project every finite-depth cell center into map frame, caching
  // the result and tracking the map-frame bounds. CellIndex::position() is the
  // SW corner; the center is + half the cell's lat/lon spans. The per-grid
  // longitudinal span already encodes the GGGS polar column-stretch, so cell
  // centers are correct at any latitude with no separate interpolation pass.
  std::vector<ProjectedCell> cells;
  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();

  for (const auto & grid : grids) {
    if (!grid) {
      continue;
    }
    const gggs::GridIndex & index = grid->index();

    // Half-cell offsets in degrees: latitudinal span is uniform across the grid;
    // longitudinal span is per-grid (polar-scaled) but constant within one grid.
    const double half_lat_cell =
      0.5 * index.latitudinalSpan() / gggs::GridIndex::cellRowCount();
    const double half_lon_cell =
      0.5 * index.longitudinalSpan() / gggs::GridIndex::cellColumnCount();

    const std::vector<DepthAndUncertainty> values = grid->values();

    gggs::CellAreaIterator it(index);
    std::size_t k = 0;
    for (; it.valid() && k < values.size(); it.next(), ++k) {
      const DepthAndUncertainty & v = values[k];
      if (std::isnan(v.depth)) {
        continue;
      }
      const auto sw = (*it).position();  // GeoPoint, SW corner of the cell
      const double lat = sw.latitude + half_lat_cell;
      const double lon = sw.longitude + half_lon_cell;

      double x = 0.0;
      double y = 0.0;
      projectCellCenter(lat, lon, map_from_earth, x, y);

      cells.push_back(ProjectedCell{x, y, v.depth, v.uncertainty});
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }

  if (cells.empty()) {
    // No finite data: return an empty (geometry-less) grid; caller skips publish.
    return map;
  }

  // Size the grid to contain every projected cell, padding by half a cell on
  // each side so border cells round into a valid index (mirrors the legacy
  // Cartesian extent, which spanned cell-center min..max).
  const double width = (max_x - min_x) + cell_size_m;
  const double height = (max_y - min_y) + cell_size_m;

  map.setGeometry(grid_map::Length(width, height), cell_size_m);
  map.setPosition(grid_map::Position(
      min_x + (max_x - min_x) / 2.0, min_y + (max_y - min_y) / 2.0));
  map.setFrameId(map_frame);
  map.add("elevation");
  map.add("uncertainty");

  // Fill pass: place each cached cell value at its map-frame index.
  for (const auto & c : cells) {
    grid_map::Index index;
    if (map.getIndex(grid_map::Position(c.map_x, c.map_y), index)) {
      map.at("elevation", index) = c.depth;
      map.at("uncertainty", index) = c.uncertainty;
    }
  }

  return map;
}

grid_map::GridMap geoMapSheetToGridMap(
  const GeoMapSheet & map_sheet,
  const std::string & map_frame,
  double cell_size_m,
  const Eigen::Isometry3d & map_from_earth)
{
  // Whole-sheet projection delegates to the subset core over every resident
  // grid. shared_ptr<GeoGrid> -> shared_ptr<const GeoGrid> is an implicit upcast.
  std::vector<std::shared_ptr<const GeoGrid>> grids;
  for (const auto & grid : map_sheet.grids()) {
    grids.push_back(grid);
  }
  return geoGridsToGridMap(grids, map_frame, cell_size_m, map_from_earth);
}

}  // namespace cube
