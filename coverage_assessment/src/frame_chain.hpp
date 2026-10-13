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

#ifndef FRAME_CHAIN_HPP_
#define FRAME_CHAIN_HPP_

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "coverage_assessment/polygon_raster.h"
#include "coverage_assessment/source_accumulator.h"
#include "marine_autonomy/gz4d_geo.h"

// The frame arithmetic the ROS wrapper shares with cube_bathymetry. Header-only
// and free of ROS types so a test can pin it without a node.

namespace coverage_assessment
{

/// A geographic point (degrees) in the assessment raster's frame, metres.
///
/// Exactly the chain cube::geoGridsToGridMap (grid_projection.cpp) uses for a
/// cell centre: GeoPointLatLongDegrees(lat, lon, 0) -> ECEF -> the one
/// map <- earth transform. The polygon and the grids share this chain, so a
/// polygon vertex and a cell centre at the same latitude and longitude land on
/// the same raster-frame point.
inline Point2 projectGeoPoint(
  double lat_deg, double lon_deg, const Eigen::Isometry3d & map_from_earth)
{
  gz4d::GeoPointLatLongDegrees ll(lat_deg, lon_deg, 0.0);
  gz4d::GeoPointECEF ecef(ll);
  const Eigen::Vector3d map_pt =
    map_from_earth * Eigen::Vector3d(ecef.x(), ecef.y(), ecef.z());
  return {map_pt.x(), map_pt.y()};
}

/// A geographic polygon (degrees) in metres on the plane tangent to the WGS84
/// ellipsoid at its first vertex, with no transform: each vertex at ellipsoid
/// height 0 to ECEF, then east/north of the first vertex. The same kind of plane
/// as the assessment raster's map frame (an east-north-up tangent plane), but
/// anchored at the polygon itself because the map frame needs map <- earth from
/// TF, which is not there yet when a definition arrives (projectGeoPoint does the
/// real projection later). Unlike a latitude/longitude grid it has no trouble at
/// a pole or across the antimeridian, so the shape checks on arrival (zero area,
/// collinear) hold everywhere.
inline std::vector<Point2> localGeoPolygon(
  const std::vector<std::pair<double, double>> & lat_lon_deg)
{
  constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;
  std::vector<Point2> out;
  if (lat_lon_deg.empty()) {
    return out;
  }
  auto ecef = [](const std::pair<double, double> & p) {
      const gz4d::GeoPointECEF e(gz4d::GeoPointLatLongDegrees(p.first, p.second, 0.0));
      return Eigen::Vector3d(e.x(), e.y(), e.z());
    };
  const double lat0 = lat_lon_deg.front().first * kRadPerDeg;
  const double lon0 = lat_lon_deg.front().second * kRadPerDeg;
  const Eigen::Vector3d east(-std::sin(lon0), std::cos(lon0), 0.0);
  const Eigen::Vector3d north(
    -std::sin(lat0) * std::cos(lon0), -std::sin(lat0) * std::sin(lon0), std::cos(lat0));
  const Eigen::Vector3d origin = ecef(lat_lon_deg.front());
  for (const auto & p : lat_lon_deg) {
    const Eigen::Vector3d d = ecef(p) - origin;
    out.push_back({east.dot(d), north.dot(d)});
  }
  return out;
}

/// Why a geographic polygon (degrees) cannot be a survey area, judged without TF,
/// or an empty string when its shape is usable. Only true shape errors: zero area or
/// collinear vertices, measured in localGeoPolygon's tangent-plane metres. The cell
/// cap (`max_raster_cells`) is deliberately NOT applied here: it bounds the raster in
/// the map frame, and an east/north bounding box can be far larger than the map-frame
/// one when map is rotated, so applying it on arrival would refuse valid areas. The
/// real projection applies it.
inline std::string geoPolygonShapeError(
  const std::vector<std::pair<double, double>> & lat_lon_deg)
{
  if (lat_lon_deg.size() < 3) {
    return "fewer than 3 vertices";
  }
  const std::vector<Point2> pts = localGeoPolygon(lat_lon_deg);
  double twice_area = 0.0;
  double min_x = pts.front().x, max_x = pts.front().x;
  double min_y = pts.front().y, max_y = pts.front().y;
  for (std::size_t k = 0; k < pts.size(); ++k) {
    const Point2 & a = pts[k];
    const Point2 & b = pts[(k + 1) % pts.size()];
    twice_area += a.x * b.y - b.x * a.y;
    min_x = std::min(min_x, a.x);
    max_x = std::max(max_x, a.x);
    min_y = std::min(min_y, a.y);
    max_y = std::max(max_y, a.y);
  }
  // Zero area relative to the polygon's size. The vertices come through ECEF
  // (coordinates ~6.4e6 m), so points on one great circle land a few 1e-9 m off
  // their common line; the tolerance absorbs that and nothing a survey would
  // draw. Three points on one PARALLEL are not on a great circle and pass (their
  // middle sags ~7 mm over 300 m at 43 N), as they do in the real projection,
  // where such a sliver rasterises to no cells.
  const double extent = std::max(max_x - min_x, max_y - min_y);
  if (!(extent > 0.0) || !(std::fabs(twice_area) > 1e-9 * extent * extent)) {
    return "polygon has zero area";
  }
  return "";
}

/// The horizontal map of raster-frame metres into a source grid's frame, from
/// the one transform source_from_map (lookupTransform(source_frame, map_frame)).
/// Lattice centre -> earth -> source frame is that composed transform at z = 0:
/// elevation is an ellipsoid height and never transformed, so only the
/// horizontal placement matters.
inline Affine2 rasterToSourceAffine(const Eigen::Isometry3d & source_from_map)
{
  Affine2 m;
  m.a = source_from_map.linear()(0, 0);
  m.b = source_from_map.linear()(0, 1);
  m.c = source_from_map.linear()(1, 0);
  m.d = source_from_map.linear()(1, 1);
  m.tx = source_from_map.translation().x();
  m.ty = source_from_map.translation().y();
  return m;
}

/// How far a live map <- earth transform has moved from a captured one.
///
/// `shift_m` is the distance between the two map ORIGINS in earth coordinates
/// (the translations of the inverse transforms), and `angle_rad` the angle of the
/// rotation between the two frames. The translation of map <- earth itself is
/// the wrong thing to compare: it is -R * (origin in earth), so a rotation of
/// 1.6e-9 rad turns the 6.4e6 m Earth-radius vector by 1 cm while the frame has
/// not moved at all.
inline void mapFrameMovement(
  const Eigen::Isometry3d & captured_map_from_earth,
  const Eigen::Isometry3d & live_map_from_earth, double * shift_m, double * angle_rad)
{
  *shift_m = (live_map_from_earth.inverse().translation() -
    captured_map_from_earth.inverse().translation()).norm();
  *angle_rad = std::fabs(Eigen::AngleAxisd(
      captured_map_from_earth.linear().transpose() * live_map_from_earth.linear()).angle());
}

/// Geodetic (WGS84 ellipsoid) altitude, metres, of a point given in ECEF.
inline double geodeticAltitude(const Eigen::Vector3d & ecef_point)
{
  gz4d::GeoPointECEF ecef(ecef_point.x(), ecef_point.y(), ecef_point.z());
  gz4d::GeoPointLatLongDegrees ll(ecef);
  return ll.altitude();
}

}  // namespace coverage_assessment

#endif  // FRAME_CHAIN_HPP_
