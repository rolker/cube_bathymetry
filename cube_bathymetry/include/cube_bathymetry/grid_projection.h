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


#ifndef CUBE_BATHYMETRY__GRID_PROJECTION_H_
#define CUBE_BATHYMETRY__GRID_PROJECTION_H_

#include <Eigen/Geometry>

#include <string>

#include "grid_map_core/GridMap.hpp"

#include "cube_bathymetry/geo_map_sheet.h"

namespace cube
{

/// @brief Project a geographic @ref GeoMapSheet into a Cartesian `map`-frame
///        `grid_map::GridMap` (live-node publish path, issue #21).
///
/// The migrated live node accumulates into a geographic GeoMapSheet but MUST
/// keep publishing a `grid_map::GridMap` in `map_frame` (collision avoidance /
/// costmap #164 / CAMP / rviz depend on the unchanged topic/frame/layers/
/// resolution contract). This helper performs that projection.
///
/// **Batched affine (review must-fix):** the caller looks up the `map <- earth`
/// transform ONCE per publish and passes it as @p map_from_earth (an
/// `Eigen::Isometry3d`, e.g. from `tf2::transformToEigen`). Each GGGS cell
/// center is converted lat/lon -> ECEF and multiplied by that single affine --
/// there is no per-cell TF buffer lookup and no per-cell `tf2::doTransform`.
///
/// **Cell center (review must-fix):** GGGS exposes no cell-center accessor;
/// `gggs::CellIndex::position()` is the south-west corner. The center is derived
/// as SW corner + half the cell's latitudinal span and + half its longitudinal
/// span. The per-grid longitudinal span already encodes the GGGS polar
/// column-stretch (1x/3x/9x), so projecting cell centers handles polar scaling
/// by construction -- no separate interpolation/stretch pass is needed (unlike
/// the uniform-raster GeoTIFF path in bag_to_geotiff). Round-trip fidelity is
/// validated for non-polar survey latitudes (|lat| < 72 deg), matching the
/// store's documented envelope (tile_io.hpp).
///
/// The returned grid's geometry (center, length) is sized to contain every
/// finite-depth cell's projected position; `elevation` and `uncertainty` layers
/// are filled exactly as the legacy Cartesian path did. An empty sheet (no
/// finite cells) yields a default-constructed GridMap (no geometry set); the
/// caller should detect this and skip publishing.
///
/// @param map_sheet       Geographic accumulator to project.
/// @param map_frame       Frame id stamped on the returned grid.
/// @param cell_size_m     Grid resolution in meters (the published contract).
/// @param map_from_earth  Single `map <- earth` affine applied to every cell.
/// @return A `grid_map::GridMap` in @p map_frame, or an empty (geometry-less)
///         GridMap if no finite cells exist.
  grid_map::GridMap geoMapSheetToGridMap(
    const GeoMapSheet & map_sheet,
    const std::string & map_frame,
    double cell_size_m,
    const Eigen::Isometry3d & map_from_earth);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__GRID_PROJECTION_H_
