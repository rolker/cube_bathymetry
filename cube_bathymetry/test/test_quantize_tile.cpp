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

#include <algorithm>
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

// Geographic position of the fractional lattice coordinate (row_f, col_f).
//
// Soundings must be placed through these rather than by typing a latitude and
// longitude: a 1 m GGGS tile spans only ~0.0078 deg, so two hand-picked
// positions a thousandth of a degree apart are very likely to straddle a tile
// boundary and leave one of them out of the grid entirely. Nodes sit on the
// cells' SW-CORNER lattice, and insert()'s influence radius is under half a
// cell for these test errors, so touchdowns are offset by a QUARTER cell to
// land inside node (row, col).
double latAt(const gggs::GridIndex & grid_index, double row_f)
{
  return grid_index.southLatitude() +
         row_f / gggs::cell_rows_per_grid * grid_index.latitudinalSpan();
}
double lonAt(const gggs::GridIndex & grid_index, double col_f)
{
  return grid_index.westLongitude() +
         col_f / gggs::cell_columns_per_grid * grid_index.longitudinalSpan();
}

// Row-major band index of GGGS cell (row, col).
std::size_t idx(uint16_t row, uint16_t col)
{
  return static_cast<std::size_t>(row) * gggs::GridIndex::cellColumnCount() + col;
}

// Row-major band index of GGGS cell (row, col) WITHIN a published window.
std::size_t windowIdx(
  const marine_interfaces::msg::SonarVisualizationTile & tile, uint16_t row, uint16_t col)
{
  return static_cast<std::size_t>(row - tile.window_row) * tile.window_width +
         (col - tile.window_col);
}

// Bytes per cell for a VisualizationBand dtype. Every dtype the wire contract
// DECLARES is handled by name; anything else fails the test where the unknown
// dtype is, rather than being silently sized as one byte.
//
// The silent fallback was not merely future-proofing: VisualizationBand.msg
// already declares UINT16 = 4 ("reserved for sidescan source rasters"), so a
// band using a dtype the contract names would have been sized at half its
// width -- and the resulting failure accuses the BAND of not covering the
// window when the helper is the thing that is wrong. A test that lies about
// which side of the contract broke is worse than one that fails loudly.
std::size_t dtypeSize(uint8_t dtype)
{
  switch (dtype) {
    case marine_interfaces::msg::VisualizationBand::UINT8:
      return sizeof(uint8_t);
    case marine_interfaces::msg::VisualizationBand::INT16:
      return sizeof(int16_t);
    case marine_interfaces::msg::VisualizationBand::UINT16:
      return sizeof(uint16_t);
    default:
      ADD_FAILURE() << "unknown VisualizationBand dtype " <<
        static_cast<unsigned>(dtype) <<
        "; add it here and to VisualizationBand.msg's documented set";
      return 0;
  }
}

// Every band's payload must cover exactly window_width * window_height cells
// for its dtype -- the invariant a consumer's decode_band() reshape depends on.
void expectBandsCoverWindow(const marine_interfaces::msg::SonarVisualizationTile & tile)
{
  const std::size_t cells =
    static_cast<std::size_t>(tile.window_width) * tile.window_height;
  ASSERT_GT(cells, 0u);
  for (const auto & band : tile.bands) {
    EXPECT_EQ(band.data.size(), cells * dtypeSize(band.dtype))
      << "band '" << band.name << "' does not cover the window";
  }
  // The wire contract's extent bound: the window must lie inside the tile.
  EXPECT_LE(
    static_cast<int>(tile.window_col) + tile.window_width, static_cast<int>(tile.width));
  EXPECT_LE(
    static_cast<int>(tile.window_row) + tile.window_height, static_cast<int>(tile.height));
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
  // spans a real interval, not the single-value degenerate case. Placed on the
  // lattice: hand-typed positions 0.0010 deg apart straddle the tile's north
  // edge here, which silently left the high touchdown out of the grid and made
  // this a single-value test that still passed every assertion below.
  insert(latAt(grid_index, 200.25), lonAt(grid_index, 200.25), -30.0f, -40.0f);  // low
  insert(latAt(grid_index, 600.25), lonAt(grid_index, 600.25), -30.0f, -10.0f);  // high

  builtin_interfaces::msg::Time stamp;
  const auto maybe = cube::quantizeTile(g, stamp);
  ASSERT_TRUE(maybe.has_value());
  const auto & bs = maybe->bands[2];
  ASSERT_EQ(bs.name, "backscatter");

  // Auto-range: offset is the min intensity (-40 dB) and the scale spans the
  // real 30 dB interval, not the 1.0 unit span the degenerate single-value
  // case falls back to.
  EXPECT_NEAR(bs.offset, -40.0, 1.0);
  EXPECT_GT(bs.scale, 0.0);
  EXPECT_NEAR(bs.scale, 30.0 / 254.0, 1.0 / 254.0);

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

// --- Dirty sub-window publishing (ADR-0001 section 4 sub-window addendum) ----

namespace
{
// Two cells with distinct depths inside a 3-row x 5-column region anchored at
// (100, 200). Asymmetric on purpose: a row/column transposition in the packer
// would land these at different offsets (or run off the end of the band).
void seedTwoCells(cube::GeoGrid & g, const gggs::GridIndex & grid_index)
{
  g.setSettledDepthAt(gggs::CellIndex(grid_index, 100, 200), -35.0f, 0.5f);
  g.setSettledDepthAt(gggs::CellIndex(grid_index, 102, 204), -36.25f, 1.0f);
}
}  // namespace

TEST(QuantizeTileWindow, PacksExactlyTheWindowCellsInRowMajorOrder)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  cube::GeoGrid g(grid_index, params);
  seedTwoCells(g, grid_index);

  cube::CellBox window;
  window.expand(100, 200);
  window.expand(102, 204);   // 3 rows x 5 columns

  builtin_interfaces::msg::Time stamp;
  stamp.sec = 7;
  const auto maybe = cube::quantizeTileWindow(g, stamp, window);
  ASSERT_TRUE(maybe.has_value());
  const auto & tile = *maybe;

  // Window fields describe the dirty box...
  EXPECT_EQ(tile.window_row, 100);
  EXPECT_EQ(tile.window_col, 200);
  EXPECT_EQ(tile.window_height, 3);
  EXPECT_EQ(tile.window_width, 5);
  // ...while width/height stay the FULL tile size, as the contract requires.
  EXPECT_EQ(tile.width, gggs::GridIndex::cellColumnCount());
  EXPECT_EQ(tile.height, gggs::GridIndex::cellRowCount());
  EXPECT_EQ(tile.header.stamp.sec, 7);
  ASSERT_EQ(tile.bands.size(), 3u);
  expectBandsCoverWindow(tile);

  const auto & depth = tile.bands[0];
  const auto & unc = tile.bands[1];

  // The two seeded cells land at their window-local offsets, in row-major order.
  EXPECT_EQ(depthRaw(depth, windowIdx(tile, 100, 200)), -3500);
  EXPECT_EQ(depthRaw(depth, windowIdx(tile, 102, 204)), -3625);
  EXPECT_EQ(unc.data[windowIdx(tile, 100, 200)], 10u);
  EXPECT_EQ(unc.data[windowIdx(tile, 102, 204)], 20u);
  // windowIdx is the message's own arithmetic; pin the absolute offsets too so
  // a transposed packer cannot satisfy both sides of the same mistake.
  EXPECT_EQ(depthRaw(depth, 0u), -3500);                 // (row 0, col 0)
  EXPECT_EQ(depthRaw(depth, 2u * 5u + 4u), -3625);       // (row 2, col 4)

  // Every other cell of the window is nodata -- and nothing outside it is sent.
  EXPECT_EQ(depthRaw(depth, windowIdx(tile, 101, 202)), -32768);
  EXPECT_EQ(depth.data.size(), 3u * 5u * sizeof(int16_t));
}

TEST(QuantizeTileWindow, WholeTileWindowIsByteForByteTheFullTile)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  cube::GeoGrid g(grid_index, params);
  seedTwoCells(g, grid_index);

  builtin_interfaces::msg::Time stamp;
  stamp.sec = 11;
  const auto full = cube::quantizeTile(g, stamp);              // the default path
  const auto whole = cube::quantizeTileWindow(g, stamp, cube::CellBox::wholeTile());
  ASSERT_TRUE(full.has_value());
  ASSERT_TRUE(whole.has_value());

  // A change touching the whole tile degenerates to today's message exactly:
  // this is what makes publish_dirty_subwindow=false a true no-op default.
  EXPECT_EQ(full->width, whole->width);
  EXPECT_EQ(full->height, whole->height);
  EXPECT_EQ(full->window_col, whole->window_col);
  EXPECT_EQ(full->window_row, whole->window_row);
  EXPECT_EQ(full->window_width, whole->window_width);
  EXPECT_EQ(full->window_height, whole->window_height);
  EXPECT_EQ(full->window_col, 0);
  EXPECT_EQ(full->window_row, 0);
  EXPECT_EQ(full->window_width, full->width);
  EXPECT_EQ(full->window_height, full->height);
  ASSERT_EQ(full->bands.size(), whole->bands.size());
  for (std::size_t b = 0; b < full->bands.size(); ++b) {
    EXPECT_EQ(full->bands[b].name, whole->bands[b].name);
    EXPECT_EQ(full->bands[b].dtype, whole->bands[b].dtype);
    EXPECT_DOUBLE_EQ(full->bands[b].scale, whole->bands[b].scale);
    EXPECT_DOUBLE_EQ(full->bands[b].offset, whole->bands[b].offset);
    EXPECT_DOUBLE_EQ(full->bands[b].nodata, whole->bands[b].nodata);
    EXPECT_EQ(full->bands[b].data, whole->bands[b].data)
      << "band '" << full->bands[b].name << "' differs from the full-tile output";
  }
  expectBandsCoverWindow(*full);
}

// The sub-window carries the same cell values the full tile would have carried
// for those cells -- a patch must not perturb what the operator sees.
TEST(QuantizeTileWindow, WindowCellsMatchTheFullTileAtTheSameCells)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  cube::GeoGrid g(grid_index, params);
  seedTwoCells(g, grid_index);

  builtin_interfaces::msg::Time stamp;
  const auto full = cube::quantizeTile(g, stamp);
  ASSERT_TRUE(full.has_value());

  cube::CellBox window;
  window.expand(99, 199);
  window.expand(103, 205);
  const auto patch = cube::quantizeTileWindow(g, stamp, window);
  ASSERT_TRUE(patch.has_value());
  expectBandsCoverWindow(*patch);

  for (uint16_t row = patch->window_row;
    row < patch->window_row + patch->window_height; ++row)
  {
    for (uint16_t col = patch->window_col;
      col < patch->window_col + patch->window_width; ++col)
    {
      EXPECT_EQ(
        depthRaw(patch->bands[0], windowIdx(*patch, row, col)),
        depthRaw(full->bands[0], idx(row, col))) << "depth at (" << row << "," << col << ")";
      EXPECT_EQ(
        patch->bands[1].data[windowIdx(*patch, row, col)],
        full->bands[1].data[idx(row, col)]) << "uncertainty at (" << row << "," << col << ")";
    }
  }
}

TEST(QuantizeTileWindow, BandLengthsTrackTheWindowAcrossShapes)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  const uint16_t last_row = static_cast<uint16_t>(gggs::GridIndex::cellRowCount() - 1);
  const uint16_t last_col = static_cast<uint16_t>(gggs::GridIndex::cellColumnCount() - 1);

  // (min_row, min_col, max_row, max_col): a single cell, a row strip, a column
  // strip, a survey-line-shaped band, and the tile corners.
  const uint16_t shapes[][4] = {
    {100, 200, 100, 200},
    {100, 0, 100, last_col},
    {0, 200, last_row, 200},
    {400, 0, 440, last_col},
    {0, 0, 0, 0},
    {last_row, last_col, last_row, last_col},
  };

  builtin_interfaces::msg::Time stamp;
  for (const auto & shape : shapes) {
    cube::GeoGrid g(grid_index, params);
    // Seed the window's own first cell so the emptiness gate passes for each
    // shape independently.
    g.setSettledDepthAt(gggs::CellIndex(grid_index, shape[0], shape[1]), -20.0f, 0.5f);

    cube::CellBox window;
    window.expand(shape[0], shape[1]);
    window.expand(shape[2], shape[3]);
    const auto maybe = cube::quantizeTileWindow(g, stamp, window);
    ASSERT_TRUE(maybe.has_value())
      << "shape " << shape[0] << "," << shape[1] << " -> " << shape[2] << "," << shape[3];
    EXPECT_EQ(maybe->window_row, shape[0]);
    EXPECT_EQ(maybe->window_col, shape[1]);
    EXPECT_EQ(maybe->window_height, shape[2] - shape[0] + 1);
    EXPECT_EQ(maybe->window_width, shape[3] - shape[1] + 1);
    expectBandsCoverWindow(*maybe);
  }
}

TEST(QuantizeTileWindow, EmptyWindowYieldsNullopt)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  cube::GeoGrid g(grid_index, params);
  seedTwoCells(g, grid_index);

  builtin_interfaces::msg::Time stamp;
  // Nothing changed since the last publish: send nothing, not an empty message.
  EXPECT_FALSE(cube::quantizeTileWindow(g, stamp, cube::CellBox()).has_value());
}

TEST(QuantizeTileWindow, WindowWithNoFiniteDepthYieldsNullopt)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  cube::GeoGrid g(grid_index, params);
  seedTwoCells(g, grid_index);

  cube::CellBox window;   // far from the two seeded cells
  window.expand(700, 700);
  window.expand(710, 710);

  builtin_interfaces::msg::Time stamp;
  // An all-nodata patch would blank cells the consumer already holds.
  EXPECT_FALSE(cube::quantizeTileWindow(g, stamp, window).has_value());
}

TEST(QuantizeTileWindow, WindowIsBoundedByTheTile)
{
  cube::Parameters params{cube::CellSizes(1.0f), "order1a"};
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  const gggs::GridIndex grid_index = level.gridIndex(43.07, -70.76);
  cube::GeoGrid g(grid_index, params);
  seedTwoCells(g, grid_index);
  builtin_interfaces::msg::Time stamp;

  // A box running past the tile edge is clamped, never emitted out of bounds
  // (and never read past the end of the record vector).
  cube::CellBox over;
  over.expand(100, 200);
  over.max_row = 5000;
  over.max_col = 5000;
  const auto clamped = cube::quantizeTileWindow(g, stamp, over);
  ASSERT_TRUE(clamped.has_value());
  EXPECT_EQ(clamped->window_row, 100);
  EXPECT_EQ(clamped->window_col, 200);
  EXPECT_EQ(clamped->window_height, gggs::GridIndex::cellRowCount() - 100);
  EXPECT_EQ(clamped->window_width, gggs::GridIndex::cellColumnCount() - 200);
  expectBandsCoverWindow(*clamped);

  // A box that starts outside the tile has nothing to say about it.
  cube::CellBox outside;
  outside.expand(2000, 2000);
  EXPECT_FALSE(cube::quantizeTileWindow(g, stamp, outside).has_value());
}

// The backscatter auto-range is scoped to the window, so a patch can carry a
// different scale/offset than the tile's last full message. That is why the
// contract requires the consumer to dequantize on receipt (ADR-0008 D1).
TEST(QuantizeTileWindow, BackscatterAutoRangeIsScopedToTheWindow)
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
  for (int i = 0; i < 20; ++i) {
    insert(latAt(grid_index, 200.25), lonAt(grid_index, 200.25), -30.0f, -40.0f);  // low
    insert(latAt(grid_index, 600.25), lonAt(grid_index, 600.25), -30.0f, -10.0f);  // high
  }

  builtin_interfaces::msg::Time stamp;
  const auto full = cube::quantizeTile(g, stamp);
  ASSERT_TRUE(full.has_value());
  const double full_offset = full->bands[2].offset;

  // A window around only the LOW touchdown cannot see the high one -- they are
  // 400 cells apart -- so its auto-range is narrower than the tile's.
  cube::CellBox low;
  low.expand(198, 198);
  low.expand(202, 202);

  const auto patch = cube::quantizeTileWindow(g, stamp, low);
  ASSERT_TRUE(patch.has_value()) << "the low touchdown's own cell must carry a depth";
  expectBandsCoverWindow(*patch);
  ASSERT_EQ(patch->bands[2].name, "backscatter");
  EXPECT_LT(patch->bands[2].scale, full->bands[2].scale)
    << "a window holding one backscatter value must not span the whole tile's range";
  EXPECT_GE(patch->bands[2].offset, full_offset);
}
