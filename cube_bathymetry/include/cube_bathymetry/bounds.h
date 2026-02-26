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


#ifndef CUBE_BATHYMETRY__BOUNDS_H_
#define CUBE_BATHYMETRY__BOUNDS_H_

#include <algorithm>
#include "cube_bathymetry/common.h"

namespace cube
{

  template < typename T, typename RT >
  struct Bounds
  {
    T minimum;
    T maximum;

    Bounds() {
    }
    explicit Bounds(const T & p)
    : minimum(p), maximum(p) {
    }
    Bounds(const T & p1, const T & p2) : minimum(p1), maximum(p1)
  {
    expand(p2);
    }

    inline Bounds & expand(const T & p)
    {
      if(valid(*this)) {
        minimum = min(minimum, p);
        maximum = max(maximum, p);
      } else {
        minimum = p;
        maximum = p;
      }
      return *this;
    }

    inline Bounds & expand(const Bounds < T, RT > &other)
    {
      return expand(other.minimum).expand(other.maximum);
    }

    inline Bounds & buffer(const RT & b)
    {
      minimum -= b;
      maximum += b;
      return *this;
    }

    inline bool contains(const T & p)
    {
      return valid(*this) && p >= minimum && p <= maximum;
    }

    inline RT range()
    {
      return maximum - minimum;
    }


    friend bool valid(const Bounds & b)
    {
      return valid(b.minimum) && valid(b.maximum) && b.minimum <= b.maximum;
    }


    friend std::ostream & operator << (std::ostream & stream, const Bounds < T, RT > &b)
        {
        stream << "min: " << b.minimum << " max: " << b.maximum;
        return stream;
      }
  };

  using MapBounds = Bounds < MapPosition, MapOffset >;


  inline GridIndex minimumIndex(const MapBounds & bounds, const MapOffset & grid_sizes)
  {
    return floorDivide(bounds.minimum, grid_sizes);
  }

  inline GridIndex maximumIndex(const MapBounds & bounds, const MapOffset & grid_sizes)
  {
    return ceilDivide(bounds.maximum, grid_sizes);
  }

  using GridIndexRange = Bounds < GridIndex, GridCounts >;

}  // namespace cube

#endif  // CUBE_BATHYMETRY__BOUNDS_H_
