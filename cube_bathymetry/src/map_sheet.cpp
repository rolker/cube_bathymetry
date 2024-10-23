#include "cube_bathymetry/map_sheet.h"
#include <cmath>
#include <iostream>

namespace cube
{

MapSheet::MapSheet(CellCounts counts, CellSizes sizes, std::string iho_order)
  :counts_(counts), sizes_(sizes), parameters_(sizes, iho_order)
{

}

const CellSizes & MapSheet::cellSizes() const
{
  return sizes_;
}

const CellCounts& MapSheet::cellCountsPerGrid() const
{
  return counts_;
}


void MapSheet::addSoundings(const std::vector<Sounding> & soundings, std::chrono::steady_clock::time_point time)
{
  if(soundings.empty())
    return;

  MapBounds bounds;
  for(const auto &s: soundings)
    bounds.expand(s);

  auto grids = getOrCreateGridsIn(bounds);
  for(auto g: grids)
    if(g->insert(soundings));
      last_update_time_ = time;
}

std::vector<std::shared_ptr<Grid> > MapSheet::getOrCreateGridsIn(const MapBounds& bounds)
{
  auto grid_sizes = sizes_*counts_;
  auto min_index = minimumIndex(bounds, grid_sizes);
  auto max_index = maximumIndex(bounds, grid_sizes);

  std::vector<std::shared_ptr<Grid> > ret;

  for(int row = min_index.y; row <= max_index.y; row++)
    for(int col = min_index.x; col <= max_index.x; col++)
    {
      GridIndex index(col,row);
      if(!grids_[index])
      {
        auto origin = sizes_*counts_*index;
        grids_[index] = std::make_shared<Grid>(counts_, sizes_, origin, parameters_);
      }
      ret.push_back(grids_[index]);
    }
  return ret;

}

std::vector<std::shared_ptr<Grid> > MapSheet::grids() const
{
  std::vector<std::shared_ptr<Grid> > ret;
  for(const auto g: grids_)
    if(g.second)
      ret.push_back(g.second);
  return ret;
}


CellCounts MapSheet::totalCellCounts() const
{
  GridIndexRange range;
  for(const auto g: grids_)
    if(g.second)
      range.expand(g.first);

  return (range.range()+GridCounts(1,1))*counts_;
}

MapBounds MapSheet::gridBounds() const
{
  MapBounds ret;
  for(const auto g: grids_)
    if(g.second)
      ret.expand(g.second->bounds());
  return ret;
}

GridIndex MapSheet::gridIndex(const MapPosition &position) const
{
  return floorDivide(position, sizes_*counts_);
}

std::chrono::steady_clock::time_point MapSheet::lastUpdateTime() const
{
  return last_update_time_;
}

}
