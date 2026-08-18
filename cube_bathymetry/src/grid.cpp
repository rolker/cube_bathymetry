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


#include "cube_bathymetry/grid.h"
#include <cmath>

namespace cube
{

Grid::Grid(CellCounts counts, CellSizes sizes, MapPosition origin, const Parameters & parameters)
:counts_(counts), sizes_(sizes), origin_(origin), parameters_(parameters)
{
  nodes_.resize(counts.x * counts.y);
}

bool Grid::insert(const std::vector<MapSounding> & soundings)
{
  bool ret = false;
  for (const auto & s  :  soundings) {
    ret = insert(s) || ret;
  }
  return ret;
}


bool Grid::insert(const MapSounding & sounding)
{
  // Reject non-finite soundings at the door. A NaN depth, position, or
  // uncertainty propagates through the CUBE variance math (gain, predicted
  // variance) into a NaN hypothesis estimate, which then blanks the cell and,
  // because medians/queues mix neighbours, can corrupt good data. One bad
  // sounding must never be able to empty the grid. (Upstream cause is usually
  // missing attitude/odom TF making the error model emit NaN uncertainty.)
  if(!std::isfinite(sounding.x) || !std::isfinite(sounding.y) ||
    !std::isfinite(sounding.sounding.depth) ||
    !std::isfinite(sounding.sounding.vertical_error) ||
    !std::isfinite(sounding.sounding.horizontal_error) ||
    sounding.sounding.vertical_error <= 0.0 ||
    sounding.sounding.horizontal_error < 0.0)
  {
    // Note: horizontal_error feeds std::sqrt() below, so a negative value
    // (not just NaN) would reintroduce NaN; reject it here.
    return false;
  }

  // Shared with GeoGrid::insert and GeoMapSheet's grid-selection margin (#104).
  const double radius = parameters_.influenceRadius(sounding.sounding);


  /* Determine coordinates of effect square.  This is designed to
    * compute the largest region that the sounding can affect, and hence
    * to make the insertion more efficient by only offering the sounding
    * where it is likely to be used.
    */
  int32_t min_x = std::floor(((sounding.x - radius) - origin_.x) / sizes_.x);
  int32_t max_x = std::ceil(((sounding.x + radius) - origin_.x) / sizes_.x);
  int32_t min_y = std::floor(((sounding.y - radius) - origin_.y) / sizes_.y);
  int32_t max_y = std::ceil(((sounding.y + radius) - origin_.y) / sizes_.y);

 /* Clip to interior of current grid */
  min_x = std::max(0, min_x);
  max_x = std::min<int32_t>(counts_.x, max_x);
  min_y = std::max(0, min_y);
  max_y = std::min<int32_t>(counts_.y, max_y);

  /* Check that the sounding hits somewhere in the grid */
  if(max_x < 0 || min_x >= counts_.x || max_y < 0 || min_y >= counts_.y) {
    return false;
  }

  auto radius_squared = radius * radius;

  // Slope correction (#59, ADR-0008): stamp the predicted-surface depth at the
  // touchdown once per sounding. INVALID_DATA (no prior, missing corner, or
  // stencil off the grid) leaves the no-correction sentinel, so Node::insert
  // applies offset 0 — the pre-#59 behaviour.
  Sounding corrected = sounding.sounding;
  corrected.predicted_depth_at_touchdown =
    interpolatePredictedDepth(sounding.x, sounding.y);

  for (auto y = min_y; y < max_y; ++y) {
    for (auto x = min_x; x < max_x; ++x) {
      auto node_x = origin_.x + x * sizes_.x;
      auto node_y = origin_.y + y * sizes_.y;
      auto distance_squared = (node_x - sounding.x) * (node_x - sounding.x) +
        (node_y - sounding.y) * (node_y - sounding.y);
      if(distance_squared < radius_squared) {
        auto index = y * counts_.x + x;
        if(!nodes_[index]) {
          nodes_[index] = std::make_shared<Node>();
        }
        nodes_[index]->insert(sqrt(distance_squared), corrected, parameters_);
      }
    }
  }
  return true;
}

void Grid::setPredictedDepthAt(uint32_t x, uint32_t y, float depth, float variance)
{
  if(x >= static_cast<uint32_t>(counts_.x) || y >= static_cast<uint32_t>(counts_.y)) {
    return;
  }
  auto & node = nodes_[y * counts_.x + x];
  if(!node) {
    node = std::make_shared<Node>();
  }
  node->setPredictedDepth(depth, variance);
}

float Grid::interpolatePredictedDepth(double x, double y) const
{
  // Continuous node-lattice coordinates: node (i, j) sits at origin + i*sizes,
  // so floor picks the lower-left node exactly as cube_grid_interpolate does
  // (ADR-0008). The stencil must fit inside the lattice; a touchdown outside it
  // (including within the last row/column of nodes) gets no correction.
  const double rx = (x - origin_.x) / sizes_.x;
  const double ry = (y - origin_.y) / sizes_.y;
  // Range-check the floored lattice coordinates in double before casting to
  // int32_t: an extreme finite input (or NaN) passed to this public method
  // would otherwise overflow the cast, which is UB rather than the documented
  // INVALID_DATA. The stencil needs col..col+1 and row..row+1 inside the
  // lattice; the negated comparison also rejects NaN (all comparisons false).
  const double col_f = std::floor(rx);
  const double row_f = std::floor(ry);
  if(!(col_f >= 0.0 && col_f + 1.0 < counts_.x &&
    row_f >= 0.0 && row_f + 1.0 < counts_.y))
  {
    return INVALID_DATA;
  }
  const auto col = static_cast<int32_t>(col_f);
  const auto row = static_cast<int32_t>(row_f);

  // Corner order matches the original: z[0]=LL, z[1]=LR, z[2]=UL, z[3]=UR.
  float z[4];
  for (int j = 0; j < 2; ++j) {
    for (int i = 0; i < 2; ++i) {
      const auto & node = nodes_[(row + j) * counts_.x + (col + i)];
      const float d = node ? node->predictedDepth() : INVALID_DATA;
      if(d == INVALID_DATA || std::isnan(d)) {
        return INVALID_DATA;
      }
      z[j * 2 + i] = d;
    }
  }

  const double dx = rx - col;
  const double dy = ry - row;
  return static_cast<float>(
    z[0] * (1.0 - dx) * (1.0 - dy) + z[1] * dx * (1.0 - dy) +
    z[2] * (1.0 - dx) * dy + z[3] * dx * dy);
}

const MapPosition & Grid::origin() const
{
  return origin_;
}

const CellCounts & Grid::cellCounts() const
{
  return counts_;
}

const CellSizes & Grid::cellSizes() const
{
  return sizes_;
}

std::vector<DepthAndUncertainty> Grid::values() const
{
  std::vector<DepthAndUncertainty> ret;
  for (auto node  :  nodes_) {
    if(node) {
      node->queueFlush(parameters_);
      ret.push_back(node->extractDepthAndUncertainty(parameters_));
    } else {
      ret.push_back(DepthAndUncertainty());
    }
  }
  return ret;
}

MapBounds Grid::bounds() const
{
  return MapBounds(origin_, origin_ + (sizes_ * counts_));
}


}  // namespace cube
