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


#include "cube_bathymetry/geo_map_sheet.h"
#include <cmath>
#include <iostream>

namespace cube
{

GeoMapSheet::GeoMapSheet(float cell_size, std::string iho_order)
:parameters_(CellSizes(cell_size), iho_order), grid_level_(gggs::Level::fromCellSize(cell_size))
{
}

void GeoMapSheet::addSoundings(
  const std::vector<GeoSounding> & soundings,
  std::chrono::steady_clock::time_point time)
{
  if(soundings.empty()) {
    return;
  }

  gz4d::BoundsDegrees bounds;
  for (const auto & s  :  soundings) {
    bounds.expand(s);
  }

  // quick hack to make sure to go a bit beyond the outer soundings
  auto angular_span = grid_level_.cellAngularSpan();
  auto min = bounds.minimum();
  min = gz4d::PositionDegrees(min.latitude - angular_span, min.longitude - angular_span);
  bounds.expand(min);

  auto max = bounds.maximum();
  max = gz4d::PositionDegrees(max.latitude + angular_span, max.longitude + angular_span);
  bounds.expand(max);

  auto grids = getOrCreateGridsIn(bounds);
  for (auto g  :  grids) {
    if(g->insert(soundings)) {
      last_update_time_ = time;
    }
  }
}

std::vector<std::shared_ptr<GeoGrid>> GeoMapSheet::getOrCreateGridsIn(
  const gz4d::BoundsDegrees & bounds)
{
  std::vector<std::shared_ptr<GeoGrid>> ret;

  gggs::GridAreaIterator i(grid_level_.gridIndex(bounds.minimum()),
    grid_level_.gridIndex(bounds.maximum()));

  while(i.valid()) {
    if(!grids_[*i]) {
      grids_[*i] = std::make_shared<GeoGrid>(*i, parameters_);
    }
    ret.push_back(grids_[*i]);
    i.next();
  }

  return ret;
}

std::vector<std::shared_ptr<GeoGrid>> GeoMapSheet::grids() const
{
  std::vector<std::shared_ptr<GeoGrid>> ret;
  for (const auto g  :  grids_) {
    if(g.second) {
      ret.push_back(g.second);
    }
  }
  return ret;
}

double GeoMapSheet::cellSizeDegrees() const
{
  return grid_level_.cellAngularSpan();
}

double GeoMapSheet::nominalCellSizeMeters() const
{
  return grid_level_.cellSize();
}

gggs::GridBounds GeoMapSheet::gridBounds() const
{
  gggs::GridBounds ret;
  for (const auto & g  :  grids_) {
    ret.expand(g.first);
  }
  return ret;
}

std::chrono::steady_clock::time_point GeoMapSheet::lastUpdateTime() const
{
  return last_update_time_;
}

}  // namespace cube
