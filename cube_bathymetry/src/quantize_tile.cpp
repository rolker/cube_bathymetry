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
  const std::vector<NodeRecord> records = grid.nodeRecords();
  const uint16_t rows = gggs::GridIndex::cellRowCount();
  const uint16_t cols = gggs::GridIndex::cellColumnCount();
  const std::size_t n = static_cast<std::size_t>(rows) * cols;
  if (records.size() != n) {
    return std::nullopt;  // unexpected grid size; nothing safe to emit
  }

  // First pass: is there anything to show, and what is the backscatter range for
  // the per-tile auto-range?
  bool any_depth = false;
  double bmin = std::numeric_limits<double>::infinity();
  double bmax = -std::numeric_limits<double>::infinity();
  for (const auto & r : records) {
    if (std::isfinite(r.depth)) {
      any_depth = true;
    }
    if (std::isfinite(r.intensity)) {
      bmin = std::min(bmin, static_cast<double>(r.intensity));
      bmax = std::max(bmax, static_cast<double>(r.intensity));
    }
  }
  if (!any_depth) {
    return std::nullopt;  // no finite-depth cell: nothing to display
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
  tile.width = cols;
  tile.height = rows;
  tile.window_col = 0;
  tile.window_row = 0;
  tile.window_width = cols;
  tile.window_height = rows;

  marine_interfaces::msg::VisualizationBand depth;
  depth.name = "depth";
  depth.dtype = kDtypeInt16;
  depth.scale = kDepthScale;
  depth.offset = 0.0;
  depth.nodata = static_cast<double>(kDepthNodata);
  depth.data.resize(n * sizeof(int16_t));

  marine_interfaces::msg::VisualizationBand unc;
  unc.name = "uncertainty";
  unc.dtype = kDtypeUint8;
  unc.scale = kUncScale;
  unc.offset = 0.0;
  unc.nodata = static_cast<double>(kU8Nodata);
  unc.data.resize(n);

  marine_interfaces::msg::VisualizationBand bs;
  bs.name = "backscatter";
  bs.dtype = kDtypeUint8;
  bs.scale = bscale;
  bs.offset = boffset;
  bs.nodata = static_cast<double>(kU8Nodata);
  bs.data.resize(n);

  for (std::size_t k = 0; k < n; ++k) {
    const NodeRecord & r = records[k];

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

  tile.bands.push_back(std::move(depth));
  tile.bands.push_back(std::move(unc));
  tile.bands.push_back(std::move(bs));
  return tile;
}

}  // namespace cube
