// Copyright 2025 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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

#include <cstdint>
#include <cmath>
#include <cstring>

#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gz4d_geo.h"
#include "cube_bathymetry/geo_grid.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/parameters.h"
#include "cube_bathymetry/quantize_tile.h"

namespace
{

int16_t depthRaw(const marine_interfaces::msg::VisualizationBand & band, std::size_t k)
{
  int16_t v = 0;
  std::memcpy(&v, band.data.data() + k * sizeof(int16_t), sizeof(int16_t));
  return v;
}

// Row-major band index of GGGS cell (row, col).
std::size_t idx(uint16_t row, uint16_t col)
{
  return static_cast<std::size_t>(row) * gggs::GridIndex::cellColumnCount() + col;
}

}  // namespace

TEST(QuantizeTile, DepthAndUncertaintyBandsQuantizeCorrectly)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);

  cube::GeoGrid g(grid_index, params);
  g.setSettledDepthAt(gggs::CellIndex(grid_index, 0, 0), -35.0f, 0.5f);
  g.setSettledDepthAt(gggs::CellIndex(grid_index, 1, 2), -36.25f, 1.0f);

  builtin_interfaces::msg::Time stamp;
  stamp.sec = 123;
  const auto maybe = cube::quantizeTile(g, stamp);
  ASSERT_TRUE(maybe.has_value());
  const auto & tile = *maybe;

  // Identity + geometry.
  EXPECT_EQ(tile.index.level, grid_index.level());
  EXPECT_EQ(tile.index.row, grid_index.row());
  EXPECT_EQ(tile.index.col, grid_index.column());
  EXPECT_EQ(tile.width, gggs::GridIndex::cellColumnCount());
  EXPECT_EQ(tile.height, gggs::GridIndex::cellRowCount());
  EXPECT_EQ(tile.window_width, tile.width);
  EXPECT_EQ(tile.window_height, tile.height);
  EXPECT_EQ(tile.header.stamp.sec, 123);

  ASSERT_EQ(tile.bands.size(), 3u);
  const auto & depth = tile.bands[0];
  const auto & unc = tile.bands[1];
  const auto & bs = tile.bands[2];
  EXPECT_EQ(depth.name, "depth");
  EXPECT_EQ(unc.name, "uncertainty");
  EXPECT_EQ(bs.name, "backscatter");
  EXPECT_DOUBLE_EQ(depth.scale, 0.01);

  // Depth int16 cm: -35.0 m -> -3500, -36.25 m -> -3625; round-trips within a cm.
  EXPECT_EQ(depthRaw(depth, idx(0, 0)), -3500);
  EXPECT_EQ(depthRaw(depth, idx(1, 2)), -3625);
  EXPECT_NEAR(depthRaw(depth, idx(0, 0)) * depth.scale + depth.offset, -35.0, 0.01);
  // Empty cell -> nodata.
  EXPECT_EQ(depthRaw(depth, idx(5, 5)), -32768);
  EXPECT_DOUBLE_EQ(depth.nodata, -32768.0);

  // Uncertainty uint8 @ 0.05 m: 0.5 -> 10, 1.0 -> 20; empty -> 255.
  EXPECT_EQ(unc.data[idx(0, 0)], 10u);
  EXPECT_EQ(unc.data[idx(1, 2)], 20u);
  EXPECT_EQ(unc.data[idx(5, 5)], 255u);

  // No intensity was set, so backscatter is entirely nodata.
  EXPECT_EQ(bs.data[idx(0, 0)], 255u);
  EXPECT_EQ(bs.data[idx(1, 2)], 255u);
}

// Intensity-bearing soundings -> the backscatter band is populated and the
// per-tile uint8 auto-range (scale/offset) brackets the inserted intensities.
TEST(QuantizeTile, BackscatterBandAutoRangesOverInsertedIntensities)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  cube::GeoGrid g(grid_index, params);

  auto insert = [&](double lat, double lon, float depth, float intensity) {
      gz4d::GeoPointLatLongDegrees p(lat, lon, depth);
      cube::GeoSounding s(p);
      s.sounding.vertical_error = 0.05f;
      s.sounding.horizontal_error = 0.05f;
      s.sounding.intensity = intensity;
      s.sounding.beam_angle = 0.0f;
      g.insert(s);
    };
  // Two well-separated spots with distinct backscatter (dB) so the auto-range
  // spans a real interval, not the single-value degenerate case.
  insert(43.0700, -70.7600, -30.0f, -40.0f);  // low
  insert(43.0710, -70.7610, -30.0f, -10.0f);  // high

  builtin_interfaces::msg::Time stamp;
  const auto maybe = cube::quantizeTile(g, stamp);
  ASSERT_TRUE(maybe.has_value());
  const auto & bs = maybe->bands[2];
  ASSERT_EQ(bs.name, "backscatter");

  // Auto-range: offset is the min intensity (-40 dB); scale is a real span.
  EXPECT_NEAR(bs.offset, -40.0, 1.0);
  EXPECT_GT(bs.scale, 0.0);

  std::size_t finite = 0;
  const int nodata = static_cast<int>(std::lround(bs.nodata));
  for (auto v : bs.data) {
    if (v != nodata) {++finite;}
  }
  EXPECT_GE(finite, 2u);  // at least the two inserted touchdowns carry backscatter
}

TEST(QuantizeTile, EmptyGridYieldsNullopt)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  cube::GeoGrid g(grid_index, params);  // no cells set

  builtin_interfaces::msg::Time stamp;
  EXPECT_FALSE(cube::quantizeTile(g, stamp).has_value());
}
