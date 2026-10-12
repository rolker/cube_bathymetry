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

/// A geographic polygon (degrees) in local equirectangular metres about its mean
/// latitude and first vertex, with no transform: east/north offsets from the first
/// vertex, the longitude difference wrapped to +-180. NOT the assessment raster's
/// frame (that needs map <- earth and is projectGeoPoint's job); it has the right
/// size and shape to a fraction of a percent at survey scales, which is what the
/// shape checks on arrival need (zero area, collinear, absurd extent).
inline std::vector<Point2> localGeoPolygon(
  const std::vector<std::pair<double, double>> & lat_lon_deg)
{
  constexpr double kEarthRadius = 6371008.8;
  constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;
  std::vector<Point2> out;
  if (lat_lon_deg.empty()) {
    return out;
  }
  double mean_lat = 0.0;
  for (const auto & p : lat_lon_deg) {
    mean_lat += p.first;
  }
  mean_lat /= static_cast<double>(lat_lon_deg.size());
  const double cos_lat = std::max(std::cos(mean_lat * kRadPerDeg), 1.0e-3);
  const double lat0 = lat_lon_deg.front().first;
  const double lon0 = lat_lon_deg.front().second;
  for (const auto & p : lat_lon_deg) {
    double dlon = std::fmod(p.second - lon0, 360.0);
    if (dlon > 180.0) {
      dlon -= 360.0;
    } else if (dlon < -180.0) {
      dlon += 360.0;
    }
    out.push_back({dlon * kRadPerDeg * kEarthRadius * cos_lat,
        (p.first - lat0) * kRadPerDeg * kEarthRadius});
  }
  return out;
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
