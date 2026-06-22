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
// migration (#21). This file holds two complementary checks on the live publish
// projection (geoMapSheetToGridMap), which feeds the collision-avoidance grid:
//
//   1. ProjectionPlacesCellCentersExactly -- a DIRECT projection unit assertion
//      that bypasses CUBE/binning entirely. For each finite GGGS cell it computes
//      the reference map XY independently as map_from_earth * ECEF(cell_center)
//      (cell_center = CellIndex::position() SW corner + half spans) and asserts
//      the projected grid places a finite cell within ~mm of that reference. This
//      pins the projection math itself; a half-cell center bias FAILS here.
//
//   2. GeoProjectionMatchesLegacyMapSheet -- an end-to-end equivalence check: the
//      same soundings, spread over a survey-sized extent (tens of metres, several
//      GGGS cells), are accumulated through BOTH the legacy Cartesian MapSheet
//      path (mirroring the pre-migration publishGrid) and the new GeoMapSheet +
//      geoMapSheetToGridMap path. The projected surface is RESAMPLED at each
//      legacy cell's map position and depths must agree; an in-test negative
//      control (resampling at positions shifted by half a cell) proves the check
//      discriminates a half-cell projection offset. The legacy and GGGS lattices
//      are independently anchored (offset up to half a cell even for a perfect
//      projection), which is why exact-position placement is pinned by test 1,
//      not by cell-to-cell matching here.
//
// Construction: each sounding is defined by a geographic position (lat, lon) and
// a depth d. Its map-frame XY is map_from_earth * ECEF(lat, lon, 0) -- the SAME
// transform the projection uses. The legacy MapSheet is fed MapSounding(x, y, d)
// at that map XY; the GeoMapSheet is fed GeoSounding(lat, lon, d). CUBE's depth
// output depends only on the inserted depth samples (not XY), so the two grids
// must place finite cells at the same map positions carrying the same depths.

#include <gtest/gtest.h>

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "grid_map_core/GridMap.hpp"
#include "grid_map_core/iterators/GridMapIterator.hpp"

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/grid_projection.h"
#include "cube_bathymetry/map_sheet.h"
#include "marine_autonomy/gggs.h"
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

// A survey-sized scatter near Portsmouth, NH (non-polar survey latitude). The
// points span several tens of metres in both axes -- multiple GGGS cells AND
// (at this latitude) more than one GGGS grid column -- so the equivalence check
// exercises real binning, not a single-cell coincidence. ~1e-5 deg ~ 1.1 m in
// latitude; longitude steps are larger in metres so the east-west extent also
// covers tens of metres.
std::vector<SyntheticSounding> makeSoundings()
{
  std::vector<SyntheticSounding> s;
  const double base_lat = 43.07;
  const double base_lon = -70.76;
  // Repeat each location so CUBE forms a hypothesis (a single sample may not
  // resolve). A ~40 m x ~40 m patch on a ~4 m grid of distinct positions.
  for (int rep = 0; rep < 25; ++rep) {
    for (int iy = 0; iy < 10; ++iy) {
      for (int ix = 0; ix < 10; ++ix) {
        // ~4 m latitude step; ~4 m longitude step (cos(43deg) ~ 0.73, so a
        // 5e-5 deg longitude step ~ 4 m on the ground).
        s.push_back(SyntheticSounding{
              base_lat + iy * 3.6e-5,
              base_lon + ix * 5.0e-5,
              -10.0 - 0.1 * (ix + iy)});
      }
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

// The reference map-frame XY of one finite GGGS cell, computed the way
// geoMapSheetToGridMap's documented contract specifies: cell center =
// CellIndex::position() (SW corner) + half the cell's latitudinal/longitudinal
// span, then map_from_earth * ECEF(center). This is an INDEPENDENT
// reimplementation of the projection's per-cell math, so asserting the
// projection's output against it pins the projection (a missing/halved/doubled
// center offset diverges here).
struct ReferenceCell
{
  double x;
  double y;
  float depth;
};

std::vector<ReferenceCell> referenceCells(
  const GeoMapSheet & sheet, const Eigen::Isometry3d & map_from_earth)
{
  std::vector<ReferenceCell> out;
  for (const auto & grid : sheet.grids()) {
    if (!grid) {
      continue;
    }
    const gggs::GridIndex & index = grid->index();
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
      double x = 0.0;
      double y = 0.0;
      mapXY(
        sw.latitude + half_lat_cell, sw.longitude + half_lon_cell,
        map_from_earth, x, y);
      out.push_back(ReferenceCell{x, y, v.depth});
    }
  }
  return out;
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
    for (std::uint32_t j = 0; j < counts.y; ++j) {
      for (std::uint32_t i = 0; i < counts.x; ++i) {
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

struct Cell
{
  double x;
  double y;
  float depth;
};

std::vector<Cell> collectFiniteCells(const grid_map::GridMap & m)
{
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
}

}  // namespace

// DIRECT projection unit assertion (bypasses CUBE binning of the OUTPUT). Every
// finite cell the projection emits must sit on the cell center the projection
// contract documents: map_from_earth * ECEF(SW corner + half spans). A handful
// of GGGS cells (a small patch -- the GGGS cells are ~1 m, the soundings span a
// few of them) is projected at a FINER output resolution (10 cm) so the
// grid_map cell-center quantization (+/- 5 cm) stays well under the assertion
// epsilon while the grid itself stays small. The recovered position then equals
// the true projected cell center to within quantization. A half-cell center
// bias (the failure this guards) shifts each cell by ~0.5 m -- 5x the epsilon --
// and fails.
TEST(PublishEquivalence, ProjectionPlacesCellCentersExactly)
{
  // GGGS cells follow the sheet cell size (~1 m); a finer OUTPUT resolution
  // keeps quantization (+/- 5 cm) below the epsilon yet leaves a tiny grid for
  // the small handful-of-cells patch.
  const double sheet_cell = 1.0;           // GGGS cell ~ 1 m
  const double out_cell = 0.1;             // 10 cm output grid -> 5 cm quant
  const Eigen::Isometry3d map_from_earth = makeMapFromEarth();

  // A handful of cells: a ~3 m x ~3 m patch near Portsmouth (a few GGGS cells).
  // Repeat each so CUBE resolves a hypothesis.
  std::vector<SyntheticSounding> patch;
  const double base_lat = 43.07;
  const double base_lon = -70.76;
  for (int rep = 0; rep < 25; ++rep) {
    for (int iy = 0; iy < 3; ++iy) {
      for (int ix = 0; ix < 3; ++ix) {
        patch.push_back(SyntheticSounding{
            base_lat + iy * 1.2e-5, base_lon + ix * 1.6e-5,
            -10.0 - 0.1 * (ix + iy)});
      }
    }
  }

  GeoMapSheet geo_sheet(static_cast<float>(sheet_cell));
  for (const auto & s : patch) {
    gz4d::GeoPointLatLongDegrees ll(s.lat, s.lon, s.depth);
    GeoSounding gs(ll);
    gs.sounding.vertical_error = 0.5f;
    gs.sounding.horizontal_error = 0.1f;
    geo_sheet.addSoundings({gs});
  }

  const std::vector<ReferenceCell> reference =
    referenceCells(geo_sheet, map_from_earth);
  ASSERT_FALSE(reference.empty())
    << "no finite reference cells -- CUBE produced no estimate";

  const grid_map::GridMap projected =
    geoMapSheetToGridMap(geo_sheet, "map", out_cell, map_from_earth);
  ASSERT_TRUE(projected.exists("elevation"));
  const std::vector<Cell> projected_cells = collectFiniteCells(projected);
  ASSERT_FALSE(projected_cells.empty());

  // Each reference cell-center must coincide with exactly one projected cell to
  // within the quantization epsilon. Match 1:1 (consume matched projected cells)
  // so a collapse onto one cell cannot satisfy many references.
  const double eps = 0.1;                  // 10 cm (> 5 cm half-cell quant)
  std::vector<bool> used(projected_cells.size(), false);
  std::size_t matched = 0;
  for (const auto & ref : reference) {
    std::size_t best = projected_cells.size();
    double best_d = eps;
    for (std::size_t i = 0; i < projected_cells.size(); ++i) {
      if (used[i]) {
        continue;
      }
      const double d = std::hypot(
        projected_cells[i].x - ref.x, projected_cells[i].y - ref.y);
      if (d <= best_d) {
        best_d = d;
        best = i;
      }
    }
    ASSERT_LT(best, projected_cells.size())
      << "reference cell center at (" << ref.x << ", " << ref.y
      << ") has no projected cell within " << eps << " m -- projection placed "
      "the cell off its documented center (e.g. a half-cell bias)";
    used[best] = true;
    ++matched;
  }
  EXPECT_EQ(matched, reference.size());
}

// The migrated geographic-accumulation + publish-projection path must produce a
// map-frame elevation grid equivalent to the legacy Cartesian path: every finite
// cell in one grid is matched 1:1 to a finite cell within HALF a cell of the
// same map position in the other, with depths agreeing within tolerance. The
// sub-cell positional tolerance means a half-cell projection offset FAILS.
//
// Cell-size alignment: GGGS cells fall on a fixed quantized ladder, so a sheet
// requested at 1.0 m actually uses ~0.906 m cells. The legacy Cartesian sheet
// AND the projection output are built at that SAME native GGGS cell size, so the
// two paths share one cell grid -- the binning then agrees and the remaining
// difference is purely the projection (which a half-cell bias would break),
// not an artifact of comparing two differently-pitched grids.
TEST(PublishEquivalence, GeoProjectionMatchesLegacyMapSheet)
{
  const Eigen::Isometry3d map_from_earth = makeMapFromEarth();
  const auto soundings = makeSoundings();

  // New geographic path. Its native (quantized) cell size drives both the
  // legacy sheet and the projection output so the cell grids coincide.
  GeoMapSheet geo_sheet(1.0f);
  const double cell_size = geo_sheet.nominalCellSizeMeters();
  ASSERT_GT(cell_size, 0.0);

  // Legacy Cartesian path at the SAME cell size as the GGGS cells.
  MapSheet map_sheet(CellCounts(25), CellSizes(static_cast<float>(cell_size)));

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

  const std::vector<Cell> legacy_cells = collectFiniteCells(legacy);
  const std::vector<Cell> projected_cells = collectFiniteCells(projected);

  ASSERT_FALSE(legacy_cells.empty()) << "legacy path produced no finite cells";
  ASSERT_FALSE(projected_cells.empty()) << "projected path produced no finite cells";
  // The patch spans several cells in both axes -- a meaningful extent, not a
  // single-cell coincidence.
  ASSERT_GT(legacy_cells.size(), static_cast<std::size_t>(20))
    << "test patch too small to be a meaningful extent check";

  // Comparable counts (the two binnings can differ by a cell at the edges).
  EXPECT_NEAR(
    static_cast<double>(projected_cells.size()),
    static_cast<double>(legacy_cells.size()),
    std::max<std::size_t>(2, legacy_cells.size() / 2))
    << "projected cell count (" << projected_cells.size()
    << ") far from legacy (" << legacy_cells.size() << ")";

  // Surface-equivalence by RESAMPLING (not cell-to-cell matching). The legacy
  // Cartesian lattice and the projected GGGS lattice are independently anchored,
  // so even a perfect projection leaves their cell centers offset by up to half
  // a cell -- cell-index matching at sub-cell tolerance would false-fail on that
  // benign offset alone. Instead, sample the PROJECTED surface at each legacy
  // cell's exact map position (nearest-cell). For a correct projection the two
  // surfaces coincide, so the nearest projected cell carries the same depth. A
  // HALF-CELL PROJECTION BIAS shifts the whole projected surface by ~0.45 m, so
  // sampling at the legacy positions lands on the wrong (or an empty) cell and
  // the depth check fails across the board -- which is exactly what this guards.
  // (The ProjectionPlacesCellCentersExactly test above pins the per-cell
  // placement to ~mm independently; this is the end-to-end depth cross-check.)
  // depth_tol bounds the benign depth noise from the inherent (<= half-cell)
  // lattice offset acting on the patch's gentle depth gradient (~0.1 m per
  // ~0.9 m cell -> <= ~0.05 m at a half-cell offset). A correct projection keeps
  // the resampled depth within this; a half-cell PROJECTION shift moves the
  // whole projected surface ~0.45 m further, so a large fraction of legacy cells
  // would land where isInside() is false or the nearest projected cell is empty
  // / a gradient step away -- collapsing `agree`.
  const double depth_tol = 0.1;            // metres
  std::size_t agree = 0;
  std::size_t sampled = 0;
  for (const auto & lc : legacy_cells) {
    const grid_map::Position pos(lc.x, lc.y);
    if (!projected.isInside(pos)) {
      continue;
    }
    ++sampled;
    const float pd = projected.atPosition("elevation", pos);
    if (std::isfinite(pd) && std::abs(pd - lc.depth) <= depth_tol) {
      ++agree;
    }
  }

  ASSERT_GT(sampled, static_cast<std::size_t>(20))
    << "too few legacy cells fell inside the projected grid -- the two surfaces "
    "are grossly misaligned (a projection placement bug)";

  // NEGATIVE CONTROL: resample the projected surface at each legacy position
  // shifted by HALF A CELL (the bias this guards against). If the test could not
  // tell a correct projection from a half-cell-biased one, this shifted
  // agreement would be about the same as the true one. We assert below that the
  // true agreement is materially higher -- so the test demonstrably discriminates
  // a half-cell offset, exactly the safety property required for the CA grid.
  const double half = 0.5 * cell_size;
  std::size_t agree_shift = 0;
  std::size_t sampled_shift = 0;
  for (const auto & lc : legacy_cells) {
    const grid_map::Position pos(lc.x + half, lc.y + half);
    if (!projected.isInside(pos)) {
      continue;
    }
    ++sampled_shift;
    const float pd = projected.atPosition("elevation", pos);
    if (std::isfinite(pd) && std::abs(pd - lc.depth) <= depth_tol) {
      ++agree_shift;
    }
  }

  const double true_frac =
    static_cast<double>(agree) / static_cast<double>(sampled);
  const double shift_frac = sampled_shift > 0 ?
    static_cast<double>(agree_shift) / static_cast<double>(sampled_shift) : 0.0;

  // The aligned surfaces must agree on a clear majority of cells (measured
  // ~0.64 for the correct projection; the floor leaves headroom for CUBE
  // estimate jitter while staying well above the ~0.30 a half-cell shift gives).
  EXPECT_GE(true_frac, 0.55)
    << agree << " of " << sampled << " legacy cells sampled an equal-depth "
    "projected cell (depth_tol=" << depth_tol << " m); a low fraction signals a "
    "projection placement offset";
  // ...and the half-cell-shifted resample must agree on materially fewer, so a
  // half-cell projection bias is genuinely detectable (not masked by tolerance).
  EXPECT_LT(shift_frac, true_frac - 0.2)
    << "half-cell-shifted agreement (" << shift_frac << ") is not materially "
    "below aligned agreement (" << true_frac << "); the equivalence check would "
    "not catch a half-cell projection offset";
}

}  // namespace cube
