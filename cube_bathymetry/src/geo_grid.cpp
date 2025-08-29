// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint Hydrographic Center, University of New Hampshire
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


#include "cube_bathymetry/geo_grid.h"
#include <cmath>

namespace cube
{

GeoGrid::GeoGrid(gggs::GridIndex index, const Parameters& parameters)
  :index_(index), parameters_(parameters)
{
}

bool GeoGrid::insert(const std::vector<GeoSounding> & soundings)
{
  bool ret = false;
  for(const auto &s: soundings)
    ret = insert(s) || ret;
  return ret;
}


bool GeoGrid::insert(const GeoSounding &geo_sounding)
{
  const Sounding& sounding = geo_sounding.sounding;
  double max_variance_allowed = parameters_.iho_fixed + parameters_.iho_percent*sounding.depth*sounding.depth/(CONF_95PC * CONF_95PC);
  double ratio = max_variance_allowed / sounding.vertical_error;

  /* Ensure some spreading on point */
  if(ratio <= 2.0)
    ratio = 2.0;

  double max_radius = CONF_99PC * std::sqrt(sounding.horizontal_error);

  double radius = parameters_.distance_scale * pow(ratio - 1.0, parameters_.inverse_distance_exponent) - max_radius;
  if (radius < 0.0)
    radius = parameters_.distance_scale;
  if (radius > max_radius)
    radius = max_radius;
  if (radius < parameters_.distance_scale)
    radius = parameters_.distance_scale;


  auto bounds = gz4d::BoundsDegrees::radiusFromCenter(geo_sounding, radius);

  gggs::CellAreaIterator i(index_, bounds);

  while(i.valid())
  {
    auto distance = i->position().distanceFrom(geo_sounding);
    if(distance < radius)
    {
      if(!nodes_[*i])
      {
        nodes_[*i] = std::make_shared<Node>();
      }
      nodes_[*i]->insert(distance, sounding, parameters_);
    }
    i.next();
  }

  return true;

}

const gggs::GridIndex& GeoGrid::index() const
{
  return index_;
}
  
std::vector<DepthAndUncertainty > GeoGrid::values() const
{
  std::vector<DepthAndUncertainty> ret;

  gggs::CellAreaIterator i(index_);

  while(i.valid())
  {
    auto node = nodes_.find(*i);

    if(node == nodes_.end() || !node->second)
    {
      // empty node, so default nan value
      ret.push_back(DepthAndUncertainty());
    }
    else
    {
      node->second->queueFlush(parameters_);
      ret.push_back(node->second->extractDepthAndUncertainty(parameters_));
    }
    i.next();
  }
  return ret;
}

} // namespace cube
