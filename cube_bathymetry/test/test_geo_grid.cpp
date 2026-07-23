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
  auto welford = g.nodeIntensityWelford();
  ASSERT_EQ(welford.size(), std::size(welford_golden));
  size_t wi = 0;
  for (const auto & kv : welford) {
    const auto & gw = welford_golden[wi];
    EXPECT_EQ(kv.first.row(), gw.row) << "welford entry " << wi;
    EXPECT_EQ(kv.first.column(), gw.column) << "welford entry " << wi;
    EXPECT_EQ(kv.second.n, gw.n) << "welford entry " << wi;
    EXPECT_DOUBLE_EQ(kv.second.mean, gw.mean) << "welford entry " << wi;
    EXPECT_DOUBLE_EQ(kv.second.m2, gw.m2) << "welford entry " << wi;
    ++wi;
  }
}

}  // namespace cube
