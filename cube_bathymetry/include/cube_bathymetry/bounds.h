#ifndef CUBE_BATHYMETRY_BOUNDS_H
#define CUBE_BATHYMETRY_BOUNDS_H

#include "common.h"

namespace cube
{

template <typename T, typename RT>
struct Bounds
{
  T minimum;
  T maximum;

  Bounds(){}
  Bounds(const T& p):minimum(p), maximum(p){}
  Bounds(const T& p1, const T& p2):minimum(p1), maximum(p1)
  {
    expand(p2);
  }

  inline Bounds &expand(const T &p)
  {
    if(valid(*this))
    {
      minimum = min(minimum, p);
      maximum = max(maximum, p);
    }
    else
    {
      minimum = p;
      maximum = p;
    }
    return *this;
  }

  inline Bounds &expand(const Bounds<T, RT> &other)
  {
    return expand(other.minimum).expand(other.maximum);
  }

  inline bool contains(const T &p)
  {
    return valid(*this) && p >= minimum && p <= maximum;
  }

  inline RT range()
  {
    return maximum - minimum;
  }


  friend bool valid(const Bounds &b)
  {
    return valid(b.minimum) && valid(b.maximum) && b.minimum <= b.maximum;
  }


  friend std::ostream& operator<< (std::ostream& stream, const Bounds<T, RT> &b)
  {
    stream << "min: " << b.minimum << " max: " << b.maximum;
    return stream;
  }
};

using MapBounds = Bounds<MapPosition, MapOffset>;


inline GridIndex minimumIndex(const MapBounds &bounds, const MapOffset &grid_sizes)
{
  return floorDivide(bounds.minimum, grid_sizes);
}

inline GridIndex maximumIndex(const MapBounds &bounds, const MapOffset &grid_sizes)
{
  return ceilDivide(bounds.maximum, grid_sizes);
}

using GridIndexRange = Bounds<GridIndex, GridCounts>;

} // namespace cube

#endif
