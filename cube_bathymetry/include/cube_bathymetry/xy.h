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
    return valid(lhs) && valid(rhs) && lhs.x < rhs.x && lhs.y < rhs.y;
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
