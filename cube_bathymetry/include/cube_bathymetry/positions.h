#ifndef CUBE_BATHYMETRY_POSITIONS_H
#define CUBE_BATHYMETRY_POSITIONS_H

#include "xy.h"

namespace cube
{


/// Base position type for relative or absolute positions
template <typename T, typename DT>
struct XYPositionBase: public XY<T, DT>
{
  XYPositionBase(T x, T y): XY<T, DT>(x, y){}
  XYPositionBase():XY<T, DT>(std::numeric_limits<T>::quiet_NaN(), std::numeric_limits<T>::quiet_NaN()){}

  friend bool valid(const DT &p)
  {
    return !(std::isnan(p.x) || std::isnan(p.y));
  }
};

/// forward declare to use with XYPosition
template <typename T, typename PT, typename DT>
struct XYOffset;

/// Absolute position within a map frame
template <typename T, typename DT>
struct XYPosition: public XYPositionBase<T, DT>
{
  XYPosition(){}
  XYPosition(T x, T y): XYPositionBase<T, DT>(x, y){}

  template <typename OT>
  inline XYPosition<T, DT> &operator+=(const XYOffset<T, DT, OT> &o)
  {
    this->x += o.x;
    this->y += o.y;
    return *this;
  }

  template <typename OT>
  inline XYPosition<T, DT> &operator-=(const XYOffset<T, DT, OT> &o)
  {
    return operator+=(-o);
  }

};

struct MapPosition: public XYPosition<double, MapPosition>
{
  MapPosition(){}
  MapPosition(double x, double y): XYPosition<double, MapPosition>(x, y){}

};

/// Relative position within a map frame
/// Template parameters are base type, position type and derived type
template <typename T, typename PT, typename DT>
struct XYOffset: public XYPositionBase<T, DT>
{
  XYOffset(){}
  XYOffset(const PT& from, const PT& to):XYPositionBase<T, DT>(to.x-from.x, to.y-from.y){}

  inline DT operator-() const {return DT(-this->x, -this->y);}

  friend PT operator+(const PT &lhs, const DT &rhs)
  {
    PT ret(lhs);
    ret+=rhs;
    return ret;
  }

  friend PT operator+(const DT &lhs, const PT &rhs)
  {
    return rhs+lhs;
  }

  friend DT operator-(const PT &lhs, const PT &rhs)
  {
    return DT(lhs.x-rhs.x, lhs.y-rhs.y);
  }

};

struct MapOffset: public XYOffset<double, MapPosition, MapOffset>
{ 
  MapOffset(){}
  MapOffset(const MapPosition& from, const MapPosition& to): XYOffset<double, MapPosition, MapOffset>(from, to){}
};


} // namespace cube

#endif
