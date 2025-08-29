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


#ifndef CUBE_BATHYMETRY_XY_H
#define CUBE_BATHYMETRY_XY_H

#include <limits>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <ostream>


namespace cube
{

template <typename T>
bool valid(const T& v);

template <typename T, typename DT>
struct XY
{
  T x;
  T y;

  XY(const T &x, const T &y): x(x), y(y){}

  friend bool operator<(const DT &lhs, const DT &rhs)
  {
    return valid(lhs) && valid(rhs) &&  (lhs.x < rhs.x || lhs.x == rhs.x && lhs.y < rhs.y);
  }

  friend bool operator==(const DT &lhs, const DT &rhs)
  {
    return valid(lhs) && valid(rhs) && lhs.x == rhs.x && lhs.y == rhs.y;
  }

  friend bool operator>(const DT &lhs, const DT &rhs)
  {
    return rhs < lhs;
  }

  friend bool operator<=(const DT &lhs, const DT &rhs)
  {
    return !(lhs > rhs);
  }

  friend bool operator>=(const DT &lhs, const DT &rhs)
  {
    return !(lhs < rhs);
  }

  friend bool operator!=(const DT &lhs, const DT &rhs)
  {
    return !(lhs == rhs);
  }

  friend DT min(const DT &lhs, const DT &rhs)
  {
    if(!valid(lhs))
      return rhs;
    if(!valid(rhs))
      return lhs;
    return DT(std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y));
  }

  friend DT max(const DT &lhs, const DT &rhs)
  {
    if(!valid(lhs))
      return rhs;
    if(!valid(rhs))
      return lhs;
    return DT(std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y));
  }

  friend std::ostream& operator<< (std::ostream& stream, const DT &p)
  {
    stream << "x: " << p.x << " y: " << p.y;
    return stream;
  }

};




} // namespace cube

#endif
