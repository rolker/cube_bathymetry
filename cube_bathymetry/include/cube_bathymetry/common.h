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


#ifndef CUBE_BATHYMETRY_COMMON_H
#define CUBE_BATHYMETRY_COMMON_H

#include "xy.h"
#include "sizes.h"
#include "indicies.h"
#include "positions.h"

namespace cube
{

static constexpr double CONF_95PC = 1.96; /* Scale for 95% CI on Unit Normal */
static constexpr double CONF_99PC = 2.576; /* Scale for 99% CI on Unit Normal */

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


#pragma pack(push, 1)
struct DepthAndUncertainty
{
  float depth;
  float uncertainty;

  DepthAndUncertainty(float depth = std::numeric_limits<float>::quiet_NaN(), float uncertainty = std::numeric_limits<float>::quiet_NaN()): depth(depth), uncertainty(uncertainty){}
};
#pragma pack(pop)

} // namespace cube

#endif
