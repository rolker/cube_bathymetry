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
    sounding.sounding.vertical_error <= 0.0)
  {
    return false;
  }

  double max_variance_allowed = parameters_.iho_fixed + parameters_.iho_percent *
    sounding.sounding.depth * sounding.sounding.depth / (CONF_95PC * CONF_95PC);
  double ratio = max_variance_allowed / sounding.sounding.vertical_error;

  /* Ensure some spreading on point */
  if(ratio <= 2.0) {
    ratio = 2.0;
  }

  double max_radius = CONF_99PC * std::sqrt(sounding.sounding.horizontal_error);

  double radius = parameters_.distance_scale * pow(ratio - 1.0,
      parameters_.inverse_distance_exponent) - max_radius;
  if (radius < 0.0) {
    radius = parameters_.distance_scale;
  }
  if (radius > max_radius) {
    radius = max_radius;
  }
  if (radius < parameters_.distance_scale) {
    radius = parameters_.distance_scale;
  }


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
        nodes_[index]->insert(sqrt(distance_squared), sounding.sounding, parameters_);
      }
    }
  }
  return true;
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
