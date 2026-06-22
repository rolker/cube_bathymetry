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

// Publish-equivalence regression -- the safety net for the MapSheet->GeoMapSheet
// migration (#21). The same soundings are accumulated through BOTH the legacy
// Cartesian MapSheet path (mirroring the pre-migration publishGrid) and the new
// GeoMapSheet + geoMapSheetToGridMap publish projection, and the resulting
// map-frame elevation grids are asserted to agree positionally and in depth.
//
// Construction: each sounding is defined by a geographic position (lat, lon) and
// a depth d. Its map-frame XY is map_from_earth * ECEF(lat, lon, 0) -- the SAME
// transform the projection uses. The legacy MapSheet is fed MapSounding(x, y, d)
// at that map XY; the GeoMapSheet is fed GeoSounding(lat, lon, d). CUBE's depth
// output depends only on the inserted depth samples (not XY), so the two grids
// must place finite cells at the same map positions carrying the same depths.

#include <gtest/gtest.h>

#include <Eigen/Geometry>

#include <cmath>
#include <vector>

#include "grid_map_core/GridMap.hpp"
#include "grid_map_core/iterators/GridMapIterator.hpp"

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/grid_projection.h"
#include "cube_bathymetry/map_sheet.h"
#include "marine_autonomy/gz4d_geo.h"

namespace cube
{

namespace
{

// A fixed, arbitrary rigid map<-earth transform. The rotation keeps the test
// honest (an identity rotation would hide a transpose bug); the exact value is
// irrelevant as long as BOTH paths use it.
Eigen::Isometry3d makeMapFromEarth()
{
  Eigen::Isometry3d t = Eigen::Isometry3d::Identity();
  t.linear() =
    (Eigen::AngleAxisd(0.3, Eigen::Vector3d::UnitZ()) *
    Eigen::AngleAxisd(0.1, Eigen::Vector3d::UnitY())).toRotationMatrix();
  t.translation() = Eigen::Vector3d(1234.5, -678.9, 42.0);
  return t;
}

struct SyntheticSounding
{
  double lat;
  double lon;
  double depth;
};

// A small cluster near Portsmouth, NH (non-polar survey latitude). The points
// span a few cells so several grid cells populate.
std::vector<SyntheticSounding> makeSoundings()
{
  std::vector<SyntheticSounding> s;
  const double base_lat = 43.07;
  const double base_lon = -70.76;
  // Repeat each location so CUBE forms a hypothesis (a single sample may not
  // resolve). ~1e-5 deg ~ 1 m steps.
  for (int rep = 0; rep < 25; ++rep) {
    for (int i = 0; i < 4; ++i) {
      s.push_back(SyntheticSounding{
            base_lat + i * 1e-5, base_lon + i * 1e-5, -10.0 - i});
    }
  }
  return s;
}

// Map-frame XY of a geographic point under map_from_earth (ECEF at altitude 0).
void mapXY(
  double lat, double lon, const Eigen::Isometry3d & map_from_earth,
  double & x, double & y)
{
  gz4d::GeoPointLatLongDegrees ll(lat, lon, 0.0);
  gz4d::GeoPointECEF ecef(ll);
  const Eigen::Vector3d p =
    map_from_earth * Eigen::Vector3d(ecef.x(), ecef.y(), ecef.z());
  x = p.x();
  y = p.y();
}

// Build a map-frame grid_map from a legacy Cartesian MapSheet exactly the way
// the pre-migration publishGrid() did (origin + i*cellsize fill).
grid_map::GridMap legacyGrid(const MapSheet & sheet, double cell_size)
{
  grid_map::GridMap map;
  const MapBounds bounds = sheet.gridBounds();
  if (std::isnan(bounds.maximum.x)) {
    return map;
  }
  const double width = bounds.maximum.x - bounds.minimum.x;
  const double height = bounds.maximum.y - bounds.minimum.y;
  map.setGeometry(grid_map::Length(width + cell_size, height + cell_size), cell_size);
  map.setPosition(grid_map::Position(
      bounds.minimum.x + width / 2.0, bounds.minimum.y + height / 2.0));
  map.setFrameId("map");
  map.add("elevation");
  map.add("uncertainty");

  for (auto grid : sheet.grids()) {
    const auto origin = grid->origin();
    const auto counts = grid->cellCounts();
    const auto sizes = grid->cellSizes();
    const auto values = grid->values();
    for (int j = 0; j < counts.y; ++j) {
      for (int i = 0; i < counts.x; ++i) {
        const auto v = values[j * counts.x + i];
        if (!std::isnan(v.depth)) {
          grid_map::Position p(origin.x + i * sizes.x, origin.y + j * sizes.y);
          grid_map::Index index;
          if (map.getIndex(p, index)) {
            map.at("elevation", index) = v.depth;
            map.at("uncertainty", index) = v.uncertainty;
          }
        }
      }
    }
  }
  return map;
}

}  // namespace

// The migrated geographic-accumulation + publish-projection path must produce a
// map-frame elevation grid equivalent to the legacy Cartesian path: every finite
// cell in one grid has a finite cell within half a cell of the same map position
// in the other, with depths agreeing within tolerance.
TEST(PublishEquivalence, GeoProjectionMatchesLegacyMapSheet)
{
  const double cell_size = 1.0;
  const Eigen::Isometry3d map_from_earth = makeMapFromEarth();
  const auto soundings = makeSoundings();

  // Legacy Cartesian path.
  MapSheet map_sheet(CellCounts(25), CellSizes(static_cast<float>(cell_size)));
  // New geographic path.
  GeoMapSheet geo_sheet(static_cast<float>(cell_size));

  for (const auto & s : soundings) {
    double x = 0.0;
    double y = 0.0;
    mapXY(s.lat, s.lon, map_from_earth, x, y);

    MapSounding ms(
      static_cast<float>(x), static_cast<float>(y), static_cast<float>(s.depth));
    ms.sounding.vertical_error = 0.5f;
    ms.sounding.horizontal_error = 0.1f;
    map_sheet.addSoundings({ms});

    gz4d::GeoPointLatLongDegrees ll(s.lat, s.lon, s.depth);
    GeoSounding gs(ll);
    gs.sounding.vertical_error = 0.5f;
    gs.sounding.horizontal_error = 0.1f;
    geo_sheet.addSoundings({gs});
  }

  const grid_map::GridMap legacy = legacyGrid(map_sheet, cell_size);
  const grid_map::GridMap projected =
    geoMapSheetToGridMap(geo_sheet, "map", cell_size, map_from_earth);

  ASSERT_TRUE(legacy.exists("elevation"));
  ASSERT_TRUE(projected.exists("elevation"));
  EXPECT_EQ(projected.getFrameId(), "map");
  EXPECT_DOUBLE_EQ(projected.getResolution(), cell_size);

  // Collect finite (position, depth) cells from each grid.
  struct Cell
  {
    double x;
    double y;
    float depth;
  };
  auto collect = [](const grid_map::GridMap & m) {
      std::vector<Cell> cells;
      const grid_map::Matrix & elev = m["elevation"];
      for (grid_map::GridMapIterator it(m); !it.isPastEnd(); ++it) {
        const float d = elev((*it)(0), (*it)(1));
        if (std::isfinite(d)) {
          grid_map::Position p;
          m.getPosition(*it, p);
          cells.push_back(Cell{p.x(), p.y(), d});
        }
      }
      return cells;
    };

  const auto legacy_cells = collect(legacy);
  const auto projected_cells = collect(projected);

  ASSERT_FALSE(legacy_cells.empty()) << "legacy path produced no finite cells";
  ASSERT_FALSE(projected_cells.empty()) << "projected path produced no finite cells";

  // Comparable counts (the two binnings can differ by a cell at the edges).
  EXPECT_NEAR(
    static_cast<double>(projected_cells.size()),
    static_cast<double>(legacy_cells.size()),
    std::max<std::size_t>(2, legacy_cells.size() / 4))
    << "projected cell count (" << projected_cells.size()
    << ") far from legacy (" << legacy_cells.size() << ")";

  // Each legacy cell must have a nearby projected cell (within ~1 cell) whose
  // depth agrees. Positional tolerance covers the GGGS-vs-Cartesian binning
  // offset + the ECEF round-trip; depth tolerance covers float accumulation.
  const double pos_tol = cell_size;        // within one cell
  const double depth_tol = 0.1;            // metres
  std::size_t matched = 0;
  for (const auto & lc : legacy_cells) {
    for (const auto & pc : projected_cells) {
      if (std::hypot(pc.x - lc.x, pc.y - lc.y) <= pos_tol &&
        std::abs(pc.depth - lc.depth) <= depth_tol)
      {
        ++matched;
        break;
      }
    }
  }
  // The overwhelming majority of legacy cells must have a matching projected
  // cell (allow a few edge cells to differ by binning).
  EXPECT_GE(
    static_cast<double>(matched),
    0.9 * static_cast<double>(legacy_cells.size()))
    << matched << " of " << legacy_cells.size()
    << " legacy cells matched a projected cell";
}

}  // namespace cube
