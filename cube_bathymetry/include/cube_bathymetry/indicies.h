#ifndef CUBE_BATHYMETRY_INDICIES_H
#define CUBE_BATHYMETRY_INDICIES_H

#include "xy.h"
#include "sizes.h"

namespace cube
{

/// @brief  XY structure with integers for use as indicies
/// @tparam T integer type
template <typename T, typename DT, typename ST>
struct XYIndex: public XY<T, DT >
{
  /// Default to max for invalid index
  XYIndex():XY<T, DT >(std::numeric_limits<T>::max(), std::numeric_limits<T>::max()){}

  /// Value parameters to allow casting
  XYIndex(T x, T y):XY<T, DT >(x, y){}

  friend bool valid(const DT &i)
  {
    return i.x != std::numeric_limits<T>::max() && i.y != std::numeric_limits<T>::max();
  }

  friend ST operator-(const DT &lhs, const DT &rhs)
  {
    return ST(lhs.x - rhs.x, lhs.y - rhs.y);
  }

};

/// Index of a cell within a Grid
struct CellIndex: public XYIndex<int32_t, CellIndex, CellCounts>
{
  CellIndex(){}
  CellIndex(int32_t x, int32_t y):XYIndex<int32_t, CellIndex, CellCounts>(x,y){}
};

/// Index of a Grid within a MapSheet
struct GridIndex: public XYIndex<int32_t, GridIndex, GridCounts>
{
  GridIndex(){}
  GridIndex(int32_t x, int32_t y):XYIndex<int32_t, GridIndex, GridCounts>(x,y){}
};

} // namespace cube

#endif
