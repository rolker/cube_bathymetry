#ifndef CUBE_BATHYMETRY_SOUNDING_H
#define CUBE_BATHYMETRY_SOUNDING_H

#include "common.h"

namespace cube
{

struct Sounding: public MapPosition
{
  /// Depth is positive up above sea surface and negative is down below sea surface
  float depth;
  float vertical_error;
  float horizontal_error;
};

} // namespace cube

#endif
