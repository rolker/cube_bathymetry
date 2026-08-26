// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>
#include "cube_bathymetry/geo_grid.h"

namespace cube
{

class GeoGridTest : public ::testing::Test
{
protected:
  Parameters params{CellSizes(1.0f), "order1a"};
  gggs::Level level{gggs::Level::fromCellSize(1.0f)};

  gggs::GridIndex makeGridIndex(double lat, double lon)
  {
    return level.gridIndex(lat, lon);
  }

  GeoSounding makeGeoSounding(
    double lat, double lon, double depth,
    float vert_err = 0.5f, float horiz_err = 0.1f)
  {
    gz4d::GeoPointLatLongDegrees point(lat, lon, depth);
    GeoSounding s(point);
    s.sounding.vertical_error = vert_err;
    s.sounding.horizontal_error = horiz_err;
    return s;
  }
};

TEST_F(GeoGridTest, ConstructorStoresIndex)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  EXPECT_EQ(g.index(), grid_index);
}

TEST_F(GeoGridTest, InsertSingleSoundingReturnsTrue)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  auto s = makeGeoSounding(43.07, -70.76, -10.0);
  EXPECT_TRUE(g.insert(s));
}

TEST_F(GeoGridTest, InsertEmptyVectorReturnsFalse)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  std::vector<GeoSounding> empty;
  EXPECT_FALSE(g.insert(empty));
}

TEST_F(GeoGridTest, InsertNonEmptyVectorReturnsTrue)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  std::vector<GeoSounding> soundings;
  soundings.push_back(makeGeoSounding(43.07, -70.76, -10.0));
  soundings.push_back(makeGeoSounding(43.07, -70.76, -10.5));
  EXPECT_TRUE(g.insert(soundings));
}

TEST_F(GeoGridTest, ValuesAfterInsertionsContainsValidDepths)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // Insert enough soundings to trigger hypothesis creation
  for (int i = 0; i < 20; ++i) {
    g.insert(makeGeoSounding(43.07, -70.76, -10.0));
  }

  auto vals = g.values();
  bool found_valid = false;
  for (const auto & v  : vals) {
    if (!std::isnan(v.depth)) {
      found_valid = true;
      break;
    }
  }
  EXPECT_TRUE(found_valid);
}

TEST_F(GeoGridTest, SetPredictedDepthAtLazyCreatesAndStores)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // The cell at the sounding location has no node yet.
  gggs::CellIndex cell = level.cellIndex(gggs::geoPoint(43.07, -70.76));
  EXPECT_EQ(g.predictedDepthAt(cell), INVALID_DATA)
    << "no node should exist before priming";

  // Prime the predicted depth -- this must lazy-create the node.
  const float depth = -12.5f;
  const float variance = 0.25f;
  g.setPredictedDepthAt(cell, depth, variance);

  EXPECT_FLOAT_EQ(g.predictedDepthAt(cell), depth);
}

TEST_F(GeoGridTest, PredictedDepthAtAbsentCellReturnsInvalid)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  gggs::CellIndex cell(grid_index, 0, 0);
  EXPECT_EQ(g.predictedDepthAt(cell), INVALID_DATA);
}

TEST_F(GeoGridTest, MultipleSoundingsConverge)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // Insert many consistent soundings at the same location
  for (int i = 0; i < 30; ++i) {
    g.insert(makeGeoSounding(43.07, -70.76, -10.0));
  }

  auto vals = g.values();
  bool found_close = false;
  for (const auto & v  : vals) {
    if (!std::isnan(v.depth)) {
      // Depth should converge toward the inserted value
      EXPECT_NEAR(v.depth, -10.0f, 2.0f);
      found_close = true;
    }
  }
  EXPECT_TRUE(found_close);
}

// ---- cube#107 bit-exact regression support -----------------------------------
namespace
{
// A GeoSounding carrying a backscatter {intensity, beam_angle} pair so the
// co-estimated intensity Welford is exercised alongside depth/uncertainty.
GeoSounding makeSoundingIB(
  double lat, double lon, double depth, float vert_err, float horiz_err,
  float intensity, float beam_angle)
{
  gz4d::GeoPointLatLongDegrees point(lat, lon, depth);
  GeoSounding s(point);
  s.sounding.vertical_error = vert_err;
  s.sounding.horizontal_error = horiz_err;
  s.sounding.intensity = intensity;
  s.sounding.beam_angle = beam_angle;
  return s;
}

// Deterministic synthetic batch for the cube#107 bit-exact regression: four
// touchdown clusters a few metres apart (distinct cells, overlapping influence
// spreads), each a burst of soundings at a fixed depth with a linearly varying
// backscatter intensity so the intensity Welford is non-trivial. Fixed inputs
// -> byte-reproducible values()/nodeRecords()/nodeIntensityWelford() across the
// unordered_map<uint32_t> + equirectangular-bounds refactor.
std::vector<GeoSounding> makeRegressionBatch()
{
  const double base_lat = 43.07;
  const double base_lon = -70.76;
  const double d = 3.0e-5;  // ~3.3 m; lands in distinct cells
  struct Cluster { double dlat; double dlon; double depth; float intensity0; };
  const Cluster clusters[] = {
    {0.0, 0.0, -10.0, -22.0f},
    {d, 0.0, -10.4, -20.0f},
    {0.0, d, -9.6, -24.0f},
    {d, d, -10.2, -21.0f},
  };
  std::vector<GeoSounding> batch;
  for (const auto & c : clusters) {
    for (int k = 0; k < 25; ++k) {
      const float intensity = c.intensity0 + 0.1f * static_cast<float>(k);
      const float beam_angle = 0.02f * static_cast<float>(k % 5);
      batch.push_back(makeSoundingIB(
        base_lat + c.dlat, base_lon + c.dlon, c.depth, 0.5f, 0.1f,
        intensity, beam_angle));
    }
  }
  return batch;
}
}  // namespace

// Bit-exact regression across the cube#107 refactor (packed-uint32
// unordered_map node container + equirectangular insert bounds). The golden
// constants below were captured from the pre-refactor implementation
// (std::map<gggs::CellIndex> + gz4d radiusFromCenter ellipsoidal bounds) on the
// fixed synthetic batch. The refactor must reproduce them byte-for-byte: the
// container change re-keys the same nodes and the equirectangular bounds box is
// a strict superset of the in-loop distance gate (same metric), so the settled
// cells and their depth/uncertainty/backscatter are unchanged (#63 discipline:
// identical output or an explained diff). The four settled cells are the cluster
// centres -- deep inside both the old and new bounds boxes -- so no
// boundary-cell reshuffle can perturb them.
TEST_F(GeoGridTest, BulkInsertBitExactRegression)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);
  g.insert(makeRegressionBatch());

  // --- values(): exactly four settled cells, at fixed indices/values ---------
  struct ValueGolden { size_t index; float depth; float uncertainty; };
  const ValueGolden values_golden[] = {
    {885811, -10.0f, 0.0f},
    {885815, -9.60000038f, 0.0f},
    {888691, -10.3999996f, 0.0f},
    {888695, -10.1999998f, 0.0f},
  };
  auto vals = g.values();
  EXPECT_EQ(vals.size(), static_cast<size_t>(960 * 960));
  size_t non_nan = 0;
  for (const auto & v : vals) {
    if (!std::isnan(v.depth)) {++non_nan;}
  }
  EXPECT_EQ(non_nan, std::size(values_golden))
    << "the set of settled cells must not change across the refactor";
  for (const auto & gv : values_golden) {
    ASSERT_LT(gv.index, vals.size());
    EXPECT_FALSE(std::isnan(vals[gv.index].depth)) << "cell " << gv.index;
    EXPECT_FLOAT_EQ(vals[gv.index].depth, gv.depth) << "cell " << gv.index;
    EXPECT_FLOAT_EQ(vals[gv.index].uncertainty, gv.uncertainty) << "cell " << gv.index;
  }

  // --- nodeRecords(): co-estimated backscatter on the winning hypothesis ------
  struct RecordGolden { size_t index; float intensity; float intensity_var; uint32_t n; };
  const RecordGolden records_golden[] = {
    {885811, -20.7999992f, 0.0216666628f, 25},
    {885815, -22.7999992f, 0.0216666628f, 25},
    {888691, -18.7999992f, 0.0216666628f, 25},
    {888695, -19.7999992f, 0.0216666628f, 25},
  };
  auto recs = g.nodeRecords();
  ASSERT_EQ(recs.size(), vals.size());
  size_t intensity_cells = 0;
  for (const auto & r : recs) {
    if (r.n_samples > 0) {++intensity_cells;}
  }
  EXPECT_EQ(intensity_cells, std::size(records_golden));
  for (const auto & gr : records_golden) {
    ASSERT_LT(gr.index, recs.size());
    EXPECT_EQ(recs[gr.index].n_samples, gr.n) << "cell " << gr.index;
    EXPECT_FLOAT_EQ(recs[gr.index].intensity, gr.intensity) << "cell " << gr.index;
    EXPECT_FLOAT_EQ(recs[gr.index].intensity_var, gr.intensity_var) << "cell " << gr.index;
  }

  // --- nodeIntensityWelford(): the eviction-spill accumulator, keyed by cell --
  // Also exercises the packed-uint32 -> gggs::CellIndex reconstruction: the
  // returned map is still keyed by CellIndex (row/column reconstructed from the
  // packed key), so row()/column() must round-trip.
  struct WelfordGolden { uint16_t row; uint16_t column; uint32_t n; double mean; double m2; };
  const WelfordGolden welford_golden[] = {
    {922, 691, 25, -20.800000000000001, 12.999998092658647},
    {922, 695, 25, -22.800000000000001, 12.999998092658647},
    {925, 691, 25, -18.800000000000001, 12.999998092658647},
    {925, 695, 25, -19.800000000000001, 12.999998092658647},
  };
  // Assert by key lookup, not iteration order, so a legitimate future
  // gggs::CellIndex comparator change (uma#270) cannot break the golden; a
  // successful find() still proves the packed-key -> CellIndex round-trip.
  auto welford = g.nodeIntensityWelford();
  ASSERT_EQ(welford.size(), std::size(welford_golden));
  for (const auto & gw : welford_golden) {
    const gggs::CellIndex cell(grid_index, gw.row, gw.column);
    auto it = welford.find(cell);
    ASSERT_NE(it, welford.end())
      << "missing welford cell (" << gw.row << "," << gw.column << ")";
    EXPECT_EQ(it->second.n, gw.n) << "cell (" << gw.row << "," << gw.column << ")";
    EXPECT_DOUBLE_EQ(it->second.mean, gw.mean)
      << "cell (" << gw.row << "," << gw.column << ")";
    EXPECT_DOUBLE_EQ(it->second.m2, gw.m2)
      << "cell (" << gw.row << "," << gw.column << ")";
  }
}

// ---- Predicted-surface touchdown interpolation (#59, ADR-0008) ----
//
// GGGS nodes sit on the cells' SW-corner (row, column) lattice, so a 2x2 block
// of adjacent cells forms the interpolation stencil. Seeded pattern:
//   node (row,   col) = -10   node (row,   col+1) = -12
//   node (row+1, col) = -11   node (row+1, col+1) = -13

class GeoGridPredictedSurfaceTest : public GeoGridTest
{
protected:
  static constexpr uint16_t kRow = 480;
  static constexpr uint16_t kCol = 480;

  void seedStencil(
    GeoGrid & g, const gggs::GridIndex & grid_index,
    uint16_t row = kRow, uint16_t col = kCol)
  {
    g.setPredictedDepthAt(gggs::CellIndex(grid_index, row, col), -10.0f, 0.25f);
    g.setPredictedDepthAt(gggs::CellIndex(grid_index, row, col + 1), -12.0f, 0.25f);
    g.setPredictedDepthAt(gggs::CellIndex(grid_index, row + 1, col), -11.0f, 0.25f);
    g.setPredictedDepthAt(gggs::CellIndex(grid_index, row + 1, col + 1), -13.0f, 0.25f);
  }

  // Geographic position of the fractional lattice coordinate (row_f, col_f).
  static double latAt(const gggs::GridIndex & grid_index, double row_f)
  {
    return grid_index.southLatitude() +
           row_f / gggs::cell_rows_per_grid * grid_index.latitudinalSpan();
  }
  static double lonAt(const gggs::GridIndex & grid_index, double col_f)
  {
    return grid_index.westLongitude() +
           col_f / gggs::cell_columns_per_grid * grid_index.longitudinalSpan();
  }
};

TEST_F(GeoGridPredictedSurfaceTest, InterpolateBilinearInteriorOffCenter)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);
  seedStencil(g, grid_index);

  // Off-center on BOTH axes (dr=0.75, dc=0.25) to catch axis mix-ups:
  // -10*0.75*0.25 + -12*0.25*0.25 + -11*0.75*0.75 + -13*0.25*0.75 = -11.25
  const double lat = latAt(grid_index, kRow + 0.75);
  const double lon = lonAt(grid_index, kCol + 0.25);
  EXPECT_NEAR(g.interpolatePredictedDepth(lat, lon), -11.25f, 1e-4);

  // At exactly the SW node the weights collapse to that node's value.
  EXPECT_NEAR(
    g.interpolatePredictedDepth(latAt(grid_index, kRow), lonAt(grid_index, kCol)),
    -10.0f, 1e-4);
}

TEST_F(GeoGridPredictedSurfaceTest, InterpolateNoDataCornerSentinel)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  const double lat = latAt(grid_index, kRow + 0.75);
  const double lon = lonAt(grid_index, kCol + 0.25);
  // Fully unseeded grid: sentinel.
  EXPECT_EQ(g.interpolatePredictedDepth(lat, lon), INVALID_DATA);
  // Three of four corners seeded (NE missing): still the sentinel.
  g.setPredictedDepthAt(gggs::CellIndex(grid_index, kRow, kCol), -10.0f, 0.25f);
  g.setPredictedDepthAt(gggs::CellIndex(grid_index, kRow, kCol + 1), -12.0f, 0.25f);
  g.setPredictedDepthAt(gggs::CellIndex(grid_index, kRow + 1, kCol), -11.0f, 0.25f);
  EXPECT_EQ(g.interpolatePredictedDepth(lat, lon), INVALID_DATA);
}

TEST_F(GeoGridPredictedSurfaceTest, InterpolateTileEdgeSentinel)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);
  // Seed the top TWO rows so only the range check (not missing data) can fire.
  const uint16_t top = gggs::cell_rows_per_grid - 1;  // 959
  seedStencil(g, grid_index, top - 1, kCol);

  // A touchdown inside the top row of cells needs node row 960 -- off the tile.
  const double lat = latAt(grid_index, top + 0.25);
  const double lon = lonAt(grid_index, kCol + 0.25);
  EXPECT_EQ(g.interpolatePredictedDepth(lat, lon), INVALID_DATA);
  // One row further south the full stencil exists and interpolation runs.
  EXPECT_NE(
    g.interpolatePredictedDepth(latAt(grid_index, top - 0.75), lon), INVALID_DATA);
}

TEST_F(GeoGridPredictedSurfaceTest, InterpolateExtremeCoordinateSentinel)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);
  seedStencil(g, grid_index);
  // insert() has no out-of-tile gate ahead of this public method, so an
  // absurd-but-finite coordinate floors to a lattice index beyond int32_t.
  // The range-check must reject it in double before the cast (else UB).
  const double lon = lonAt(grid_index, kCol + 0.25);
  EXPECT_EQ(g.interpolatePredictedDepth(1e300, lon), INVALID_DATA);
}

TEST_F(GeoGridPredictedSurfaceTest, InsertAppliesSlopeOffset)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);
  seedStencil(g, grid_index);
  // A primed cell far outside the sounding's influence: must stay NaN
  // (gates-not-fills, ADR-0008).
  const gggs::CellIndex far_cell(grid_index, kRow + 10, kCol + 10);
  g.setPredictedDepthAt(far_cell, -15.0f, 0.25f);

  // Touchdown at (kRow+0.25, kCol+0.25): interpolated predicted depth
  // = -10*0.5625 + -12*0.1875 + -11*0.1875 + -13*0.0625 = -10.75.
  // Which stencil nodes capture the sounding depends on the metric cell
  // aspect at this latitude, so assert the per-node invariant instead of a
  // fixed capture set: every populated node's estimate carries ITS offset,
  // estimate = depth + (predicted@node - predicted@touchdown). The SW node
  // (kRow, kCol) is ~0.3 m away and must always capture: -11 + 0.75 = -10.25.
  const double lat = latAt(grid_index, kRow + 0.25);
  const double lon = lonAt(grid_index, kCol + 0.25);
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(g.insert(makeGeoSounding(lat, lon, -11.0)));
  }

  const float interp = g.interpolatePredictedDepth(lat, lon);
  ASSERT_NEAR(interp, -10.75f, 1e-4);

  auto vals = g.values();
  gggs::CellAreaIterator it(grid_index);
  bool checked_sw = false;
  for (std::size_t k = 0; it.valid(); it.next(), ++k) {
    ASSERT_LT(k, vals.size());
    if (it->row() == kRow && it->column() == kCol) {
      EXPECT_NEAR(vals[k].depth, -10.25, 1e-3);
      checked_sw = true;
    } else if (!std::isnan(vals[k].depth)) {
      // Any other populated node must be a stencil node carrying its own
      // offset relative to the shared touchdown prediction.
      const float pred = g.predictedDepthAt(*it);
      ASSERT_NE(pred, INVALID_DATA)
        << "unexpected populated unprimed cell (" << it->row() << "," <<
        it->column() << ")";
      EXPECT_NEAR(vals[k].depth, -11.0 + (pred - interp), 1e-3)
        << "cell (" << it->row() << "," << it->column() << ")";
    }
    if (it->row() == far_cell.row() && it->column() == far_cell.column()) {
      EXPECT_TRUE(std::isnan(vals[k].depth)) << "far primed cell must not fill";
    }
  }
  EXPECT_TRUE(checked_sw);
}

TEST_F(GeoGridPredictedSurfaceTest, InsertWithoutPriorUnchanged)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // No prior anywhere: the offset-0 path must reproduce the raw depth.
  const double lat = latAt(grid_index, kRow + 0.25);
  const double lon = lonAt(grid_index, kCol + 0.25);
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(g.insert(makeGeoSounding(lat, lon, -11.0)));
  }

  auto vals = g.values();
  gggs::CellAreaIterator it(grid_index);
  bool checked = false;
  for (std::size_t k = 0; it.valid(); it.next(), ++k) {
    ASSERT_LT(k, vals.size());
    if (it->row() == kRow && it->column() == kCol) {
      EXPECT_NEAR(vals[k].depth, -11.0, 1e-3);
      checked = true;
    }
  }
  EXPECT_TRUE(checked);
}

// --- Publish-dirty cell bounds (ADR-0001 section 4 sub-window addendum) ------
//
// The box is what lets the incremental display-tile publish send a sub-window
// instead of the whole 960x960 tile, so its correctness is a data-integrity
// concern: too small and changed cells never reach the operator; not reset and
// the saving evaporates.

class GeoGridDirtyCellsTest : public GeoGridTest
{
protected:
  // Geographic position of the fractional lattice coordinate (row_f, col_f).
  //
  // Nodes sit on the cells' SW-CORNER lattice (see GeoGrid::insert), and
  // insert() gates each node on hypot(dlat, dlon) < influenceRadius. For these
  // test soundings that radius is under half a cell, so a touchdown placed at
  // the cell CENTRE (row + 0.5, col + 0.5) is ~0.71 cells from every
  // surrounding node and writes NOTHING -- insert() returns false and the
  // dirty box stays empty. The tests below therefore offset by a QUARTER cell,
  // which lands inside node (row, col)'s radius. Verified by probe: offsets
  // 0.0/0.1/0.25/0.4 accept 20 of 20 soundings, 0.5 accepts 0 of 20.
  static double latAt(const gggs::GridIndex & grid_index, double row_f)
  {
    return grid_index.southLatitude() +
           row_f / gggs::cell_rows_per_grid * grid_index.latitudinalSpan();
  }
  static double lonAt(const gggs::GridIndex & grid_index, double col_f)
  {
    return grid_index.westLongitude() +
           col_f / gggs::cell_columns_per_grid * grid_index.longitudinalSpan();
  }

  // Cells the grid actually holds a finite depth for, as a bounding box. The
  // independent oracle the tracked box is checked against: it is derived from
  // values(), not from the tracking code under test.
  static CellBox observedBox(const GeoGrid & g)
  {
    CellBox box;
    const auto values = g.values();
    gggs::CellAreaIterator it(g.index());
    for (std::size_t k = 0; it.valid(); it.next(), ++k) {
      if (k < values.size() && std::isfinite(values[k].depth)) {
        box.expand(it->row(), it->column());
      }
    }
    return box;
  }
};

TEST_F(GeoGridDirtyCellsTest, FreshGridHasAnEmptyBox)
{
  GeoGrid g(makeGridIndex(43.07, -70.76), params);
  EXPECT_TRUE(g.publishDirtyCells().empty());
  EXPECT_EQ(g.publishDirtyCells().rows(), 0);
  EXPECT_EQ(g.publishDirtyCells().columns(), 0);
}

TEST_F(GeoGridDirtyCellsTest, InsertBoundsTheCellsItWrote)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // Enough soundings at one touchdown to settle a hypothesis, so values() has
  // finite cells for the oracle below to bound (see
  // ValuesAfterInsertionsContainsValidDepths).
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(g.insert(makeGeoSounding(
        latAt(grid_index, 480.25), lonAt(grid_index, 480.25), -10.0)));
  }

  const CellBox & box = g.publishDirtyCells();
  ASSERT_FALSE(box.empty());
  // A sounding spreads over its influence radius, so the box is small
  // but not necessarily 1x1; it must be a tiny neighbourhood of cell 480,480
  // and -- the load-bearing property -- must CONTAIN every cell that ended up
  // with data.
  const CellBox observed = observedBox(g);
  ASSERT_FALSE(observed.empty());
  EXPECT_LE(box.min_row, observed.min_row);
  EXPECT_LE(box.min_col, observed.min_col);
  EXPECT_GE(box.max_row, observed.max_row);
  EXPECT_GE(box.max_col, observed.max_col);
  EXPECT_LT(box.rows(), gggs::cell_rows_per_grid);
  EXPECT_LT(box.columns(), gggs::cell_columns_per_grid);
}

TEST_F(GeoGridDirtyCellsTest, BoxSpansTwoSeparatedTouchdowns)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  ASSERT_TRUE(g.insert(makeGeoSounding(
      latAt(grid_index, 200.25), lonAt(grid_index, 300.25), -10.0)));
  ASSERT_TRUE(g.insert(makeGeoSounding(
      latAt(grid_index, 400.25), lonAt(grid_index, 700.25), -12.0)));

  const CellBox & box = g.publishDirtyCells();
  ASSERT_FALSE(box.empty());
  // The hull of both touchdowns -- the over-coverage the box representation
  // deliberately accepts (see CellBox's docs).
  EXPECT_LE(box.min_row, 200);
  EXPECT_GE(box.max_row, 400);
  EXPECT_LE(box.min_col, 300);
  EXPECT_GE(box.max_col, 700);
  // Still far short of the whole tile, which is the entire point.
  EXPECT_LT(box.rows(), gggs::cell_rows_per_grid);
  EXPECT_LT(box.columns(), gggs::cell_columns_per_grid);
}

TEST_F(GeoGridDirtyCellsTest, ClearResetsAndTheBoxReaccumulates)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  ASSERT_TRUE(g.insert(makeGeoSounding(
      latAt(grid_index, 100.25), lonAt(grid_index, 100.25), -10.0)));
  ASSERT_FALSE(g.publishDirtyCells().empty());

  g.clearPublishDirtyCells();
  EXPECT_TRUE(g.publishDirtyCells().empty())
    << "a published tile must start the next cycle with no dirty cells";

  // Re-accumulate somewhere else: the new box must describe ONLY the new work,
  // not the union with the already-published region.
  ASSERT_TRUE(g.insert(makeGeoSounding(
      latAt(grid_index, 800.25), lonAt(grid_index, 800.25), -11.0)));
  const CellBox & box = g.publishDirtyCells();
  ASSERT_FALSE(box.empty());
  EXPECT_GT(box.min_row, 100)
    << "the box re-accumulated from the cleared state, not from the old hull";
  EXPECT_GT(box.min_col, 100);
}

TEST_F(GeoGridDirtyCellsTest, RejectedSoundingLeavesTheBoxEmpty)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // Door-gated at insert (non-positive vertical error): nothing is written, so
  // nothing may be advertised as dirty.
  auto bad = makeGeoSounding(
    latAt(grid_index, 480.25), lonAt(grid_index, 480.25), -10.0, 0.0f);
  EXPECT_FALSE(g.insert(bad));
  EXPECT_TRUE(g.publishDirtyCells().empty());
}

TEST_F(GeoGridDirtyCellsTest, SeedAndReloadPathsDoNotDirtyCells)
{
  auto grid_index = makeGridIndex(43.07, -70.76);
  GeoGrid g(grid_index, params);

  // These reproduce already-persisted data and carry an explicit no-dirty-mark
  // contract (see GeoGrid's docs); the cell box must honour the same contract
  // or a warm start would push the whole primed region over the link.
  g.setPredictedDepthAt(gggs::CellIndex(grid_index, 10, 10), -10.0f, 0.25f);
  g.setSettledDepthAt(gggs::CellIndex(grid_index, 20, 20), -11.0f, 0.5f);
  EXPECT_TRUE(g.publishDirtyCells().empty());
}

TEST(CellBoxTest, WholeTileCoversEveryCell)
{
  const CellBox box = CellBox::wholeTile();
  EXPECT_FALSE(box.empty());
  EXPECT_EQ(box.min_row, 0);
  EXPECT_EQ(box.min_col, 0);
  EXPECT_EQ(box.rows(), gggs::cell_rows_per_grid);
  EXPECT_EQ(box.columns(), gggs::cell_columns_per_grid);
}

TEST(CellBoxTest, DefaultIsEmptyAndExpandMakesItOneCell)
{
  CellBox box;
  EXPECT_TRUE(box.empty());
  box.expand(7, 9);
  EXPECT_FALSE(box.empty());
  EXPECT_EQ(box.min_row, 7);
  EXPECT_EQ(box.max_row, 7);
  EXPECT_EQ(box.min_col, 9);
  EXPECT_EQ(box.max_col, 9);
  EXPECT_EQ(box.rows(), 1);
  EXPECT_EQ(box.columns(), 1);
  box.clear();
  EXPECT_TRUE(box.empty());
}

}  // namespace cube
