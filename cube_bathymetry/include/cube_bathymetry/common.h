#ifndef CUBE_BATHYMETRY_COMMON_H
#define CUBE_BATHYMETRY_COMMON_H

#include "xy.h"
#include "sizes.h"
#include "indicies.h"
#include "positions.h"

namespace cube
{

static constexpr double CONF_95PC = 1.96; /* Scale for 95% CI on Unit Normal */
static constexpr double CONF_99PC = 2.95; /* Scale for 99% CI on Unit Normal */

static constexpr float INVALID_DATA = std::numeric_limits<float>::max();


inline GridIndex floorDivide(const MapPosition &position, const MapOffset &grid_sizes)
{
  return GridIndex(std::floor(position.x/grid_sizes.x), std::floor(position.y/grid_sizes.y));
}

inline GridIndex ceilDivide(const MapPosition &position, const MapOffset &grid_sizes)
{
  return GridIndex(std::ceil(position.x/grid_sizes.x), std::ceil(position.y/grid_sizes.y));
}

inline MapOffset operator*(const CellSizes &sizes, const CellCounts &counts)
{
  MapOffset ret;
  ret.x = sizes.x*counts.x;
  ret.y = sizes.y*counts.y;
  return ret;
}

inline MapOffset operator*(const CellCounts &counts, const CellSizes &sizes)
{
  return sizes*counts;
}

inline MapPosition operator*(const MapOffset &grid_size, const GridIndex &index)
{
  return MapPosition(grid_size.x*index.x, grid_size.y*index.y);
}

inline CellIndex operator/(const MapOffset &lhs, const CellSizes &rhs)
{
  return CellIndex(std::floor(lhs.x/rhs.x), std::floor(lhs.y/rhs.y));
}

struct DepthAndUncertainty
{
  float depth;
  float uncertainty;

  DepthAndUncertainty(float depth = std::numeric_limits<float>::quiet_NaN(), float uncertainty = std::numeric_limits<float>::quiet_NaN()): depth(depth), uncertainty(uncertainty){}
};


} // namespace cube

#endif
