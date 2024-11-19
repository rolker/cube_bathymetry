#ifndef CUBE_BATHYMETRY_SOUNDING_H
#define CUBE_BATHYMETRY_SOUNDING_H

#include "common.h"

namespace cube
{

struct Sounding
{
  Sounding(float depth):
    depth(depth)
  {}
  /// Depth is positive up above sea surface and negative is down below sea surface
  float depth = std::nan("");
  float vertical_error = 0.0;
  float horizontal_error = 0.0;
};

struct MapSounding: public MapPosition
{
  MapSounding(double x, double y, float z):
    MapPosition(x, y), sounding(z)
  {}
  Sounding sounding;
};

} // namespace cube

#endif
