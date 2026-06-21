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


#ifndef CUBE_BATHYMETRY__COMMON_H_
#define CUBE_BATHYMETRY__COMMON_H_

#include <limits>
#include "cube_bathymetry/xy.h"
#include "cube_bathymetry/sizes.h"
#include "cube_bathymetry/indicies.h"
#include "cube_bathymetry/positions.h"

namespace cube
{

  static constexpr double CONF_95PC = 1.96; /* Scale for 95% CI on Unit Normal */
  static constexpr double CONF_99PC = 2.576; /* Scale for 99% CI on Unit Normal */

  static constexpr float INVALID_DATA = std::numeric_limits < float > ::max();


  inline GridIndex floorDivide(const MapPosition & position, const MapOffset & grid_sizes)
  {
    return GridIndex(std::floor(position.x / grid_sizes.x), std::floor(position.y / grid_sizes.y));
  }

  inline GridIndex ceilDivide(const MapPosition & position, const MapOffset & grid_sizes)
  {
    return GridIndex(std::ceil(position.x / grid_sizes.x), std::ceil(position.y / grid_sizes.y));
  }

  inline MapOffset operator *(const CellSizes & sizes, const CellCounts & counts)
  {
    MapOffset ret;
    ret.x = sizes.x * counts.x;
    ret.y = sizes.y * counts.y;
    return ret;
  }

  inline MapOffset operator *(const CellCounts & counts, const CellSizes & sizes)
  {
    return sizes * counts;
  }

  inline MapPosition operator *(const MapOffset & grid_size, const GridIndex & index)
  {
    return MapPosition(grid_size.x * index.x, grid_size.y * index.y);
  }

  inline CellIndex operator / (const MapOffset & lhs, const CellSizes & rhs)
  {
    return CellIndex(std::floor(lhs.x / rhs.x), std::floor(lhs.y / rhs.y));
  }


#pragma pack(push, 1)
  struct DepthAndUncertainty
  {
    float depth;
    float uncertainty;

  /// Per-beam acoustic intensity (backscatter), carried through the median
  /// pre-queue bound to its depth so the two never mismatch when the queue is
  /// sorted (ADR-0007 D2/D3). NaN when the beam has no reported intensity.
    float intensity;

  /// Per-beam receive/steering angle (radians) accompanying the intensity, the
  /// {raw intensity, angle} sufficient-statistics pair (ADR-0007 D3). NaN when
  /// not reported. NOTE: extending this #pragma pack(push,1) struct from 8 to
  /// 16 bytes is layout-safe -- no caller depends on sizeof(DepthAndUncertainty)
  /// (the only sizeof uses are raster-band strides, 2*sizeof(float)).
    float beam_angle;

    DepthAndUncertainty(float depth = std::numeric_limits < float > ::quiet_NaN(),
      float uncertainty = std::numeric_limits < float > ::quiet_NaN(),
      float intensity = std::numeric_limits < float > ::quiet_NaN(),
      float beam_angle = std::numeric_limits < float > ::quiet_NaN())
      : depth(depth), uncertainty(uncertainty), intensity(intensity),
      beam_angle(beam_angle) {
    }
  };
#pragma pack(pop)

}  // namespace cube

#endif  // CUBE_BATHYMETRY__COMMON_H_
