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

#include "coverage_assessment/source_accumulator.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace coverage_assessment
{

namespace
{

// Floor division and the matching non-negative remainder, for negative lattice
// indices.
int64_t floorDiv(int64_t a, int64_t b)
{
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) {
    --q;
  }
  return q;
}

}  // namespace

bool Affine2::inverse(Affine2 * out) const
{
  const double det = a * d - b * c;
  if (!std::isfinite(det) || det == 0.0 || !std::isfinite(tx) || !std::isfinite(ty)) {
    return false;
  }
  out->a = d / det;
  out->b = -b / det;
  out->c = -c / det;
  out->d = a / det;
  out->tx = -(out->a * tx + out->b * ty);
  out->ty = -(out->c * tx + out->d * ty);
  return std::isfinite(out->a) && std::isfinite(out->b) && std::isfinite(out->c) &&
         std::isfinite(out->d) && std::isfinite(out->tx) && std::isfinite(out->ty);
}

SourceAccumulator::SourceAccumulator(double cell_size)
: cell_size_(cell_size)
{
  if (!std::isfinite(cell_size) || cell_size <= 0.0) {
    throw std::invalid_argument("SourceAccumulator: cell size must be finite and positive");
  }
}

SourceAccumulator::Block & SourceAccumulator::blockFor(int64_t i, int64_t j)
{
  return blocks_[Key{floorDiv(i, kBlock), floorDiv(j, kBlock)}];
}

HeldCell SourceAccumulator::get(int64_t i, int64_t j) const
{
  const auto it = blocks_.find(Key{floorDiv(i, kBlock), floorDiv(j, kBlock)});
  if (it == blocks_.end()) {
    return HeldCell();
  }
  const std::size_t k =
    static_cast<std::size_t>((j - floorDiv(j, kBlock) * kBlock) * kBlock +
    (i - floorDiv(i, kBlock) * kBlock));
  HeldCell cell;
  cell.elevation = it->second.elevation[k];
  cell.uncertainty = it->second.uncertainty[k];
  return cell;
}

std::size_t SourceAccumulator::memoryBytes() const
{
  return blocks_.size() * (2 * kBlock * kBlock * sizeof(float));
}

void SourceAccumulator::clear()
{
  blocks_.clear();
  elevation_cells_ = 0;
}

AddResult SourceAccumulator::add(
  const grid_map::GridMap & message, const Affine2 & raster_to_source,
  const std::string & elevation_layer, const std::string & uncertainty_layer)
{
  AddResult result;
  if (!message.exists(elevation_layer)) {
    result.status = AddStatus::kMissingElevationLayer;
    return result;
  }
  result.has_uncertainty_layer = message.exists(uncertainty_layer);

  const double res = message.getResolution();
  const grid_map::Length length = message.getLength();
  const grid_map::Position centre = message.getPosition();
  Affine2 source_to_raster;
  if (!(std::isfinite(res) && res > 0.0) || !length.allFinite() || !centre.allFinite() ||
    message.getSize().minCoeff() <= 0 || !raster_to_source.inverse(&source_to_raster))
  {
    result.status = AddStatus::kBadGeometry;
    result.reason = "its resolution, extent or placement is not finite and positive";
    return result;
  }

  // Footprint of the message in the raster frame: the box around its four
  // corners. Cells of this box whose centre is outside the message are
  // rejected by getIndex() below.
  double lo_x = std::numeric_limits<double>::infinity(), hi_x = -lo_x;
  double lo_y = lo_x, hi_y = -lo_x;
  for (const double sx : {-0.5, 0.5}) {
    for (const double sy : {-0.5, 0.5}) {
      const Point2 q = source_to_raster.apply(
        {centre.x() + sx * length.x(), centre.y() + sy * length.y()});
      lo_x = std::min(lo_x, q.x);
      hi_x = std::max(hi_x, q.x);
      lo_y = std::min(lo_y, q.y);
      hi_y = std::max(hi_y, q.y);
    }
  }
  const double largest = std::max(
    std::max(std::fabs(lo_x), std::fabs(hi_x)), std::max(std::fabs(lo_y), std::fabs(hi_y)));
  if (!(largest <= kMaxRasterCoordinate) || !(largest / cell_size_ <= kMaxLatticeIndex)) {
    result.status = AddStatus::kOutOfRange;
    std::ostringstream os;
    os << "its footprint reaches " << largest << " m from the raster origin (limit "
       << kMaxRasterCoordinate << " m, or " << kMaxLatticeIndex << " lattice cells)";
    result.reason = os.str();
    return result;
  }
  const double fi0 = std::floor(lo_x / cell_size_);
  const double fi1 = std::floor(hi_x / cell_size_);
  const double fj0 = std::floor(lo_y / cell_size_);
  const double fj1 = std::floor(hi_y / cell_size_);
  const double footprint = (fi1 - fi0 + 1.0) * (fj1 - fj0 + 1.0);
  if (!std::isfinite(footprint) || footprint > kMaxFootprintCells) {
    result.status = AddStatus::kFootprintTooLarge;
    std::ostringstream os;
    os << "its footprint spans " << footprint << " lattice cells, over the limit of "
       << kMaxFootprintCells;
    result.reason = os.str();
    return result;
  }

  const grid_map::Matrix & elevation = message.get(elevation_layer);
  const grid_map::Matrix * uncertainty =
    result.has_uncertainty_layer ? &message.get(uncertainty_layer) : nullptr;

  result.status = AddStatus::kAdded;
  for (int64_t j = static_cast<int64_t>(fj0); j <= static_cast<int64_t>(fj1); ++j) {
    for (int64_t i = static_cast<int64_t>(fi0); i <= static_cast<int64_t>(fi1); ++i) {
      const Point2 s = raster_to_source.apply(
        {(static_cast<double>(i) + 0.5) * cell_size_, (static_cast<double>(j) + 0.5) * cell_size_});
      grid_map::Index idx;
      if (!message.getIndex(grid_map::Position(s.x, s.y), idx)) {
        continue;
      }
      ++result.cells_in_message;
      const float e = elevation(idx(0), idx(1));
      const float u = uncertainty != nullptr ? (*uncertainty)(idx(0), idx(1)) :
        std::numeric_limits<float>::quiet_NaN();
      const bool e_ok = std::isfinite(e);
      // A negative uncertainty is not a measurement: it must not overwrite a held
      // value (the union must never shrink or turn bad), exactly as the assessor
      // refuses to let one compete.
      const bool u_ok = std::isfinite(u) && u >= 0.0f;
      if (!e_ok && !u_ok) {
        continue;  // nothing to hold; do not allocate a block for it
      }
      Block & block = blockFor(i, j);
      const std::size_t k = static_cast<std::size_t>(
        (j - floorDiv(j, kBlock) * kBlock) * kBlock + (i - floorDiv(i, kBlock) * kBlock));
      if (e_ok) {
        if (!std::isfinite(block.elevation[k])) {
          ++elevation_cells_;
        }
        block.elevation[k] = e;
      }
      if (u_ok) {
        block.uncertainty[k] = u;
      }
    }
  }
  return result;
}

}  // namespace coverage_assessment
