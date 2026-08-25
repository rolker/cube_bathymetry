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

#include "cube_bathymetry/quantize_tile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace cube
{

namespace
{
// Fixed depth quantization: int16 centimetres (±327.67 m range covers any
// lake/coastal survey). Backscatter dtype values mirror VisualizationBand /
// sensor_msgs::PointField (UINT8=2, INT16=3).
constexpr double kDepthScale = 0.01;        // metres per int16 count
constexpr int16_t kDepthNodata = -32768;
constexpr double kUncScale = 0.05;          // metres per uint8 count (0..12.7 m)
constexpr uint8_t kU8Nodata = 255;
constexpr uint8_t kDtypeUint8 = 2;
constexpr uint8_t kDtypeInt16 = 3;
constexpr double kU8Max = 254.0;            // 255 reserved for nodata

uint8_t quantizeU8(double value, double scale, double offset)
{
  const double c = std::round((value - offset) / scale);
  return static_cast<uint8_t>(std::clamp(c, 0.0, kU8Max));
}
}  // namespace

std::optional<marine_interfaces::msg::SonarVisualizationTile>
quantizeTile(const GeoGrid & grid, const builtin_interfaces::msg::Time & stamp)
{
  // The full-tile window IS the whole-tile case of the sub-window packer, so
  // the two can never diverge: one code path, one set of quantization
  // constants, and the heal path is exercised by every sub-window test.
  return quantizeTileWindow(grid, stamp, CellBox::wholeTile());
}

std::optional<marine_interfaces::msg::SonarVisualizationTile>
quantizeTileWindow(
  const GeoGrid & grid, const builtin_interfaces::msg::Time & stamp,
  const CellBox & window)
{
  const std::vector<NodeRecord> records = grid.nodeRecords();
  const uint16_t rows = gggs::GridIndex::cellRowCount();
  const uint16_t cols = gggs::GridIndex::cellColumnCount();
  const std::size_t n = static_cast<std::size_t>(rows) * cols;
  if (records.size() != n) {
    return std::nullopt;  // unexpected grid size; nothing safe to emit
  }
  if (window.empty()) {
    return std::nullopt;  // nothing changed; nothing to patch
  }
  // Bound the window by the tile. The wire contract requires
  // window_col + window_width <= width (and the row equivalent), and an
  // out-of-range box must never run the packing loop off the end of `records`
  // -- the box comes from a caller, and field configs change under pressure.
  if (window.min_row >= rows || window.min_col >= cols) {
    return std::nullopt;  // entirely outside the tile
  }
  const uint16_t win_row = window.min_row;
  const uint16_t win_col = window.min_col;
  const uint16_t win_height = static_cast<uint16_t>(
    std::min<uint16_t>(window.max_row, static_cast<uint16_t>(rows - 1)) - win_row + 1);
  const uint16_t win_width = static_cast<uint16_t>(
    std::min<uint16_t>(window.max_col, static_cast<uint16_t>(cols - 1)) - win_col + 1);
  const std::size_t m = static_cast<std::size_t>(win_width) * win_height;

  // Row-major offset of the window's first cell in each of its rows. `records`
  // is full-tile row-major in CellAreaIterator order (GGGS cell order), so a
  // window row is a contiguous run of win_width records.
  const auto sourceRow = [cols, win_row, win_col](uint16_t r) {
      return static_cast<std::size_t>(win_row + r) * cols + win_col;
    };

  // First pass, over the WINDOW only: is there anything to show, and what is
  // the backscatter range for the auto-range? Scoping the range to the window
  // keeps the whole-tile case identical to the pre-sub-window output and gives
  // a narrow patch better dynamic range; the consumer dequantizes per message
  // (ADR-0008 D1), so a per-patch scale is well-defined.
  bool any_depth = false;
  double bmin = std::numeric_limits<double>::infinity();
  double bmax = -std::numeric_limits<double>::infinity();
  for (uint16_t r = 0; r < win_height; ++r) {
    const std::size_t base = sourceRow(r);
    for (uint16_t c = 0; c < win_width; ++c) {
      const NodeRecord & rec = records[base + c];
      if (std::isfinite(rec.depth)) {
        any_depth = true;
      }
      if (std::isfinite(rec.intensity)) {
        bmin = std::min(bmin, static_cast<double>(rec.intensity));
        bmax = std::max(bmax, static_cast<double>(rec.intensity));
      }
    }
  }
  if (!any_depth) {
    // No finite-depth cell in the window: nothing to display, and an all-nodata
    // patch would blank whatever the consumer already holds there.
    return std::nullopt;
  }

  // Backscatter auto-range. A flat or absent backscatter range degrades to a
  // unit scale so dequantization stays well-defined (every value maps to offset).
  const bool has_bs = std::isfinite(bmin) && std::isfinite(bmax);
  const double bspan = (has_bs && bmax > bmin) ? (bmax - bmin) : 1.0;
  const double bscale = bspan / kU8Max;
  const double boffset = has_bs ? bmin : 0.0;

  marine_interfaces::msg::SonarVisualizationTile tile;
  tile.header.stamp = stamp;
  tile.header.frame_id = "gggs";
  tile.index.level = grid.index().level();
  tile.index.row = grid.index().row();
  tile.index.col = grid.index().column();
  // width/height stay the FULL tile size even for a patch: they are the
  // consumer's tile geometry (and its consistency check), not the payload extent.
  tile.width = cols;
  tile.height = rows;
  tile.window_col = win_col;
  tile.window_row = win_row;
  tile.window_width = win_width;
  tile.window_height = win_height;

  marine_interfaces::msg::VisualizationBand depth;
  depth.name = "depth";
  depth.dtype = kDtypeInt16;
  depth.scale = kDepthScale;
  depth.offset = 0.0;
  depth.nodata = static_cast<double>(kDepthNodata);
  depth.data.resize(m * sizeof(int16_t));

  marine_interfaces::msg::VisualizationBand unc;
  unc.name = "uncertainty";
  unc.dtype = kDtypeUint8;
  unc.scale = kUncScale;
  unc.offset = 0.0;
  unc.nodata = static_cast<double>(kU8Nodata);
  unc.data.resize(m);

  marine_interfaces::msg::VisualizationBand bs;
  bs.name = "backscatter";
  bs.dtype = kDtypeUint8;
  bs.scale = bscale;
  bs.offset = boffset;
  bs.nodata = static_cast<double>(kU8Nodata);
  bs.data.resize(m);

  for (uint16_t row = 0; row < win_height; ++row) {
    const std::size_t base = sourceRow(row);
    const std::size_t out_base = static_cast<std::size_t>(row) * win_width;
    for (uint16_t col = 0; col < win_width; ++col) {
      const NodeRecord & r = records[base + col];
      const std::size_t k = out_base + col;

      int16_t depth_raw = kDepthNodata;
      if (std::isfinite(r.depth)) {
        const double cm = std::clamp(
          std::round(r.depth / kDepthScale), -32767.0, 32767.0);
        depth_raw = static_cast<int16_t>(cm);
      }
      // Pack little-endian without aliasing the byte buffer as int16*.
      std::memcpy(depth.data.data() + k * sizeof(int16_t), &depth_raw, sizeof(int16_t));

      unc.data[k] = std::isfinite(r.depth_var) ?
        quantizeU8(r.depth_var, kUncScale, 0.0) : kU8Nodata;

      bs.data[k] = (has_bs && std::isfinite(r.intensity)) ?
        quantizeU8(r.intensity, bscale, boffset) : kU8Nodata;
    }
  }

  tile.bands.push_back(std::move(depth));
  tile.bands.push_back(std::move(unc));
  tile.bands.push_back(std::move(bs));
  return tile;
}

}  // namespace cube
