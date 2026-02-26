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


#ifndef CUBE_BATHYMETRY__INDICIES_H_
#define CUBE_BATHYMETRY__INDICIES_H_

#include <limits>
#include "cube_bathymetry/xy.h"
#include "cube_bathymetry/sizes.h"

namespace cube
{

/// @brief  XY structure with integers for use as indicies
/// @tparam T integer type
  template < typename T, typename DT, typename ST >
  struct XYIndex : public XY < T, DT >
  {
  /// Default to max for invalid index
    XYIndex() : XY < T,
      DT > (std::numeric_limits < T > ::max(), std::numeric_limits < T > ::max()) {}

  /// Value parameters to allow casting
    XYIndex(T x, T y) : XY < T, DT > (x, y) {}

    friend bool valid(const DT & i)
    {
      return i.x != std::numeric_limits < T > ::max() && i.y != std::numeric_limits < T > ::max();
    }

    friend ST operator - (const DT & lhs, const DT & rhs)
    {
      return ST(lhs.x - rhs.x, lhs.y - rhs.y);
    }
  };

/// Index of a cell within a Grid
  struct CellIndex : public XYIndex < int32_t, CellIndex, CellCounts >
  {
    CellIndex() {
    }
    CellIndex(int32_t x, int32_t y) : XYIndex < int32_t, CellIndex, CellCounts > (x, y) {}
  };

/// Index of a Grid within a MapSheet
  struct GridIndex : public XYIndex < int32_t, GridIndex, GridCounts >
  {
    GridIndex() {
    }
    GridIndex(int32_t x, int32_t y) : XYIndex < int32_t, GridIndex, GridCounts > (x, y) {}
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__INDICIES_H_
