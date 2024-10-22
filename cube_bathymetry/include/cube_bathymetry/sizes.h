#ifndef CUBE_BATHYMETRY_SIZES_H
#define CUBE_BATHYMETRY_SIZES_H

#include "xy.h"

namespace cube
{

/// Represent sizes or counts
template <typename T, typename DT>
struct XYSizes: public XY<T, DT>
{
  XYSizes(const T & x, const T & y):XY<T, DT>(x, y){}

  /// Constructor for when x and y are the same.
  XYSizes(const T & n):XY<T, DT>(n,n){}

  /// Default to size zero
  XYSizes():XY<T, DT>(0, 0){}

  friend bool valid(const DT & s)
  {
    return s.x > 0 && s.y > 0;
  }

  friend DT operator+(const DT& lhs, const DT& rhs)
  {
    return DT(lhs.x+rhs.x, lhs.y+rhs.y);
  }
};


/// Number of cells in a grid
struct CellCounts: public XYSizes<uint32_t, CellCounts>
{
  CellCounts(uint32_t n):XYSizes<uint32_t, CellCounts>(n){}
  CellCounts(uint32_t x, uint32_t y):XYSizes<uint32_t, CellCounts>(x, y){}
};

/// Size of a cell in meters
struct CellSizes: public XYSizes<float, CellSizes>
{
  CellSizes(float n):XYSizes<float, CellSizes>(n){}
};

struct GridCounts: public XYSizes<uint32_t, GridCounts>
{
  GridCounts(uint32_t x, uint32_t y):XYSizes<uint32_t, GridCounts>(x, y){}
};

inline CellCounts operator*(const GridCounts &grid_counts, const CellCounts &cell_counts)
{
  return CellCounts(grid_counts.x * cell_counts.x, grid_counts.y * cell_counts.y);
}

} // namespace cube

#endif
