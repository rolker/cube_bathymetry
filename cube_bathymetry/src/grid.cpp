#include "cube_bathymetry/grid.h"
#include <cmath>

namespace cube
{

Grid::Grid(CellCounts counts, CellSizes sizes, MapPosition origin, const Parameters& parameters)
  :counts_(counts), sizes_(sizes), origin_(origin), parameters_(parameters)
{
  nodes_.resize(counts.x*counts.y);
}

bool Grid::insert(const std::vector<Sounding> & soundings)
{
  bool ret = false;
  for(const auto &s: soundings)
    ret = insert(s) || ret;
  return ret;
}


bool Grid::insert(const Sounding &sounding)
{
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


  /* Determine coordinates of effect square.  This is designed to
    * compute the largest region that the sounding can affect, and hence
    * to make the insertion more efficient by only offering the sounding
    * where it is likely to be used.
    */
  int32_t min_x = ((sounding.x - radius) - origin_.x)/sizes_.x;
  int32_t max_x = ((sounding.x + radius) - origin_.x)/sizes_.x;
  int32_t min_y = ((sounding.y - radius) - origin_.y)/sizes_.y;
  int32_t max_y = ((sounding.y + radius) - origin_.y)/sizes_.y;
  
  /* Check that the sounding hits somewhere in the grid */
  if(max_x < 0 || min_x >= counts_.x || max_y < 0 || min_y >= counts_.y)
    return false;

  /* Clip to interior of current grid */
  min_x = std::max(0, min_x);
  max_x = std::min<int32_t>(counts_.x - 1, max_x);
  min_y = std::max(0, min_y);
  max_y = std::min<int32_t>(counts_.y - 1, max_y);

  auto radius_squared = radius * radius;

  for (auto y = min_y; y <= max_y; ++y)
  {
    for (auto x = min_x; x <= max_x; ++x)
    {
      auto node_x = origin_.x + x * sizes_.x;
      auto node_y = origin_.y + y * sizes_.y;
      auto distance_squared = (node_x - sounding.x)*(node_x - sounding.x)
                        +(node_y - sounding.y)*(node_y - sounding.y);
      if(distance_squared < radius_squared)
      {
        auto index = y*counts_.x+x;
        if(!nodes_[index])
          nodes_[index] = std::make_shared<Node>();
        nodes_[index]->insert(node_x, node_y, distance_squared, sounding, parameters_);
      }

    }
  }
  return true;

}

const MapPosition &Grid::origin() const
{
  return origin_;
}

const CellCounts &Grid::cellCounts() const
{
  return counts_;
}

const CellSizes &Grid::cellSizes() const
{
  return sizes_;
}
  
std::vector<DepthAndUncertainty > Grid::values() const
{
  std::vector<DepthAndUncertainty> ret;
  for(auto node: nodes_)
  {
    if(node)
    {
      node->queueFlush(parameters_);
      ret.push_back(node->extractDepthAndUncertainty(parameters_));
    }
    else
      ret.push_back(DepthAndUncertainty());
  }
  return ret;
}

MapBounds Grid::bounds() const
{
  return MapBounds(origin_, origin_+(sizes_*counts_));
}


} // namespace cube
