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

// The polygon and the grids must share one projection chain (#164). The node
// projects a SurveyArea vertex with projectGeoPoint(); cube_bathymetry projects
// a CUBE cell centre with geoGridsToGridMap(). A vertex and a sounding at the same
// latitude and longitude therefore have to land on the same raster-frame point,
// and the cell CUBE built from that sounding has to be there.

#include <gtest/gtest.h>

#include <Eigen/Geometry>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "coverage_assessment/source_accumulator.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/grid_projection.h"
#include "frame_chain.hpp"
#include "grid_map_core/GridMap.hpp"
#include "grid_map_core/iterators/GridMapIterator.hpp"
#include "marine_autonomy/gz4d_geo.h"

namespace
{

// An arbitrary rigid map <- earth with a rotation, so a transpose or a swapped
// axis cannot hide (the same shape of fixture test_publish_equivalence uses).
Eigen::Isometry3d makeMapFromEarth()
{
  Eigen::Isometry3d t = Eigen::Isometry3d::Identity();
  t.linear() =
    (Eigen::AngleAxisd(0.3, Eigen::Vector3d::UnitZ()) *
    Eigen::AngleAxisd(0.1, Eigen::Vector3d::UnitY())).toRotationMatrix();
  t.translation() = Eigen::Vector3d(1234.5, -678.9, 42.0);
  return t;
}

struct Spot
{
  double lat;
  double lon;
};

// A ~3 m x ~3 m patch of soundings near Portsmouth, NH.
std::vector<Spot> patch()
{
  std::vector<Spot> spots;
  for (int iy = 0; iy < 3; ++iy) {
    for (int ix = 0; ix < 3; ++ix) {
      spots.push_back(Spot{43.07 + iy * 1.2e-5, -70.76 + ix * 1.6e-5});
    }
  }
  return spots;
}

// Distance from (x, y) to the nearest cell centre with a finite elevation.
double distanceToNearestFiniteCell(const grid_map::GridMap & m, double x, double y)
{
  double best = std::numeric_limits<double>::infinity();
  const grid_map::Matrix & elev = m["elevation"];
  for (grid_map::GridMapIterator it(m); !it.isPastEnd(); ++it) {
    if (!std::isfinite(elev((*it)(0), (*it)(1)))) {
      continue;
    }
    grid_map::Position p;
    m.getPosition(*it, p);
    best = std::min(best, std::hypot(p.x() - x, p.y() - y));
  }
  return best;
}

}  // namespace

TEST(FrameChain, PolygonVertexLandsWhereCubeBuiltTheCell)
{
  const Eigen::Isometry3d map_from_earth = makeMapFromEarth();
  cube::GeoMapSheet sheet(1.0f);
  for (int rep = 0; rep < 25; ++rep) {
    for (const auto & s : patch()) {
      gz4d::GeoPointLatLongDegrees ll(s.lat, s.lon, -10.0);
      cube::GeoSounding gs(ll);
      gs.sounding.vertical_error = 0.5f;
      gs.sounding.horizontal_error = 0.1f;
      sheet.addSoundings({gs});
    }
  }

  std::vector<std::shared_ptr<const cube::GeoGrid>> grids;
  for (const auto & g : sheet.grids()) {
    grids.push_back(g);
  }
  // A 10 cm output grid keeps the cell-centre quantisation (5 cm) small.
  const grid_map::GridMap projected =
    cube::geoGridsToGridMap(grids, "map", 0.1, map_from_earth);
  ASSERT_TRUE(projected.exists("elevation")) << "CUBE produced no estimate";

  for (const auto & s : patch()) {
    const coverage_assessment::Point2 p =
      coverage_assessment::projectGeoPoint(s.lat, s.lon, map_from_earth);
    // The sounding falls inside a ~1 m GGGS cell that CUBE filled, and the
    // projection puts a finite output cell at that cell's centre: within half a
    // cell diagonal (0.71 m) of the vertex, plus the 5 cm output quantisation.
    // (Exact cell-centre placement is pinned by test_publish_equivalence; this
    // pins that the polygon vertex uses the same chain.)
    EXPECT_LT(distanceToNearestFiniteCell(projected, p.x, p.y), 0.8)
      << "vertex (" << s.lat << ", " << s.lon << ") projected to (" << p.x << ", " << p.y
      << ") has no CUBE cell under it";
  }

  // Negative control: the same vertices through a map <- earth that is 20 m off
  // must miss the patch, so the check above can fail.
  Eigen::Isometry3d shifted = map_from_earth;
  shifted.translation().x() += 20.0;
  for (const auto & s : patch()) {
    const coverage_assessment::Point2 p =
      coverage_assessment::projectGeoPoint(s.lat, s.lon, shifted);
    EXPECT_GT(distanceToNearestFiniteCell(projected, p.x, p.y), 5.0);
  }
}

TEST(FrameChain, RasterToSourceAffineIsTheHorizontalPartOfTheTransform)
{
  Eigen::Isometry3d source_from_map = Eigen::Isometry3d::Identity();
  source_from_map.linear() =
    (Eigen::AngleAxisd(0.7, Eigen::Vector3d::UnitZ())).toRotationMatrix();
  source_from_map.translation() = Eigen::Vector3d(-12.5, 40.25, 3.0);
  const coverage_assessment::Affine2 m =
    coverage_assessment::rasterToSourceAffine(source_from_map);
  for (const Eigen::Vector2d & p : {Eigen::Vector2d(0, 0), Eigen::Vector2d(10, -4),
      Eigen::Vector2d(-300.5, 125.0)})
  {
    const Eigen::Vector3d expected = source_from_map * Eigen::Vector3d(p.x(), p.y(), 0.0);
    const coverage_assessment::Point2 got = m.apply({p.x(), p.y()});
    EXPECT_NEAR(got.x, expected.x(), 1e-12);
    EXPECT_NEAR(got.y, expected.y(), 1e-12);
  }
}

TEST(FrameChain, GeodeticAltitudeRecoversTheHeightOfAnEcefPoint)
{
  for (const double h : {-28.3, 0.0, 12.0, 48.88}) {
    gz4d::GeoPointLatLongDegrees ll(43.07, -70.76, h);
    gz4d::GeoPointECEF ecef(ll);
    EXPECT_NEAR(
      coverage_assessment::geodeticAltitude(Eigen::Vector3d(ecef.x(), ecef.y(), ecef.z())), h,
      1e-6);
  }
}

namespace
{

// A map <- earth at the Gulf of Maine: the translation is -R * origin, about the
// Earth's radius long.
Eigen::Isometry3d makeEarthScaleMapFromEarth()
{
  gz4d::GeoPointECEF origin(gz4d::GeoPointLatLongDegrees(43.07, -70.76, 0.0));
  Eigen::Isometry3d earth_from_map = Eigen::Isometry3d::Identity();
  earth_from_map.linear() =
    (Eigen::AngleAxisd(0.4, Eigen::Vector3d::UnitZ()) *
    Eigen::AngleAxisd(-1.2, Eigen::Vector3d::UnitY())).toRotationMatrix();
  earth_from_map.translation() = Eigen::Vector3d(origin.x(), origin.y(), origin.z());
  return earth_from_map.inverse();
}

}  // namespace

TEST(FrameChain, MapMovementIsZeroForTheSameTransform)
{
  const Eigen::Isometry3d t = makeEarthScaleMapFromEarth();
  double shift = -1.0;
  double angle = -1.0;
  coverage_assessment::mapFrameMovement(t, t, &shift, &angle);
  EXPECT_NEAR(shift, 0.0, 1e-9);
  EXPECT_NEAR(angle, 0.0, 1e-12);
}

TEST(FrameChain, ARotationBelowTheToleranceAboutTheMapOriginIsNotAMove)
{
  // 1.6e-9 rad about the map's own origin turns the 6.4e6 m translation of
  // map <- earth by 1 cm, but the frame is where it was.
  const Eigen::Isometry3d captured = makeEarthScaleMapFromEarth();
  const Eigen::Isometry3d earth_from_map = captured.inverse();
  Eigen::Isometry3d live_earth_from_map = earth_from_map;
  const Eigen::Matrix3d tiny =
    Eigen::AngleAxisd(1.6e-9, Eigen::Vector3d::UnitX()).toRotationMatrix();
  live_earth_from_map.linear() = earth_from_map.linear() * tiny;
  const Eigen::Isometry3d live = live_earth_from_map.inverse();
  // The naive translation difference reads as a move of about a centimetre.
  EXPECT_GT((live.translation() - captured.translation()).norm(), 0.005);

  double shift = -1.0;
  double angle = -1.0;
  coverage_assessment::mapFrameMovement(captured, live, &shift, &angle);
  EXPECT_LT(shift, 1e-6);
  EXPECT_NEAR(angle, 1.6e-9, 1e-12);
  EXPECT_LE(shift, 0.01);
  EXPECT_LE(angle, 1.0e-6);
}

TEST(FrameChain, AFiveMetreOriginMoveIsReportedInMetres)
{
  const Eigen::Isometry3d captured = makeEarthScaleMapFromEarth();
  const Eigen::Isometry3d earth_from_map = captured.inverse();
  // 5 m along the map's own x axis.
  const Eigen::Isometry3d live_earth_from_map =
    earth_from_map * Eigen::Isometry3d(Eigen::Translation3d(5.0, 0.0, 0.0));
  double shift = -1.0;
  double angle = -1.0;
  coverage_assessment::mapFrameMovement(captured, live_earth_from_map.inverse(), &shift, &angle);
  EXPECT_NEAR(shift, 5.0, 1e-6);
  EXPECT_NEAR(angle, 0.0, 1e-12);
}

TEST(FrameChain, ARotationAboveTheToleranceIsReportedInRadians)
{
  const Eigen::Isometry3d captured = makeEarthScaleMapFromEarth();
  const Eigen::Isometry3d earth_from_map = captured.inverse();
  Eigen::Isometry3d live_earth_from_map = earth_from_map;
  const Eigen::Matrix3d larger =
    Eigen::AngleAxisd(2.0e-6, Eigen::Vector3d::UnitZ()).toRotationMatrix();
  live_earth_from_map.linear() = earth_from_map.linear() * larger;
  double shift = -1.0;
  double angle = -1.0;
  coverage_assessment::mapFrameMovement(captured, live_earth_from_map.inverse(), &shift, &angle);
  EXPECT_NEAR(angle, 2.0e-6, 1e-12);
  EXPECT_GT(angle, 1.0e-6);
  EXPECT_LT(shift, 1e-6);
}

TEST(FrameChain, LocalGeoPolygonHasTheSizeAndShapeOfTheRing)
{
  // A 0.001 deg x 0.002 deg box at 43 N: about 111.2 m north-south and
  // 2 * 111.2 * cos(43 deg) = 162.7 m east-west.
  const std::vector<std::pair<double, double>> ring = {
    {43.0, -70.0}, {43.0, -69.998}, {43.001, -69.998}, {43.001, -70.0}};
  const auto local = coverage_assessment::localGeoPolygon(ring);
  ASSERT_EQ(local.size(), 4u);
  EXPECT_NEAR(local[0].x, 0.0, 1e-9);
  EXPECT_NEAR(local[0].y, 0.0, 1e-9);
  EXPECT_NEAR(local[1].x, 162.7, 0.5);
  EXPECT_NEAR(local[2].y, 111.2, 0.2);
  // The antimeridian: the difference of longitude is wrapped.
  const auto wrapped = coverage_assessment::localGeoPolygon({{0.0, 179.999}, {0.0, -179.999}});
  EXPECT_NEAR(wrapped[1].x, 222.4, 0.5);
  EXPECT_TRUE(coverage_assessment::localGeoPolygon({}).empty());
}

namespace
{
// lat/lon (degrees) of east/north offsets (metres) from 43 N, 70.7 W.
std::pair<double, double> geoAt(double east, double north)
{
  constexpr double kMetresPerDegree = 6371008.8 * 3.14159265358979323846 / 180.0;
  return {43.0 + north / kMetresPerDegree,
    -70.7 + east / (kMetresPerDegree * std::cos(43.0 * 3.14159265358979323846 / 180.0))};
}
}  // namespace

// The arrival check refuses only shape errors. A long thin area lying diagonally in
// east/north has an east/north bounding box of about 2 million 1 m cells, far over
// the default cap, though it covers about 20,000 cells in a map frame aligned with
// it; the cap belongs to the real projection, so arrival must accept it.
TEST(FrameChain, ArrivalShapeCheckDoesNotApplyTheCellCap)
{
  const double c = std::sqrt(0.5);
  const std::vector<std::pair<double, double>> diagonal = {
    geoAt(0.0, 0.0), geoAt(2000.0 * c, 2000.0 * c),
    geoAt(2000.0 * c - 10.0 * c, 2000.0 * c + 10.0 * c), geoAt(-10.0 * c, 10.0 * c)};
  EXPECT_EQ(coverage_assessment::geoPolygonShapeError(diagonal), "");
}

TEST(FrameChain, ArrivalShapeCheckRefusesZeroArea)
{
  // Exactly collinear in the local projection: one meridian. (Three points on a
  // diagonal through geoAt are not: the projection's mean-latitude scale differs
  // from geoAt's, which leaves a genuine sliver of non-zero area.)
  const std::vector<std::pair<double, double>> one_meridian = {
    geoAt(0.0, 0.0), geoAt(0.0, 100.0), geoAt(0.0, 300.0)};
  EXPECT_EQ(
    coverage_assessment::geoPolygonShapeError(one_meridian), "polygon has zero area");
  const std::vector<std::pair<double, double>> one_latitude = {
    geoAt(0.0, 0.0), geoAt(100.0, 0.0), geoAt(300.0, 0.0)};
  EXPECT_EQ(coverage_assessment::geoPolygonShapeError(one_latitude), "polygon has zero area");
  const std::vector<std::pair<double, double>> repeated = {
    geoAt(5.0, 5.0), geoAt(5.0, 5.0), geoAt(5.0, 5.0)};
  EXPECT_EQ(coverage_assessment::geoPolygonShapeError(repeated), "polygon has zero area");
}

// Near a pole the equirectangular approximation collapses a ring into a line; the
// check must not refuse a shape the real projection accepts.
TEST(FrameChain, ArrivalShapeCheckAcceptsAPolarRing)
{
  const std::vector<std::pair<double, double>> ring = {
    {89.5, 0.0}, {89.5, 90.0}, {89.5, 180.0}, {89.5, -90.0}};
  EXPECT_EQ(coverage_assessment::geoPolygonShapeError(ring), "");
}
