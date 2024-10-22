#ifndef CUBE_BATHYMETRY_SOUNDING_H
#define CUBE_BATHYMETRY_SOUNDING_H

#include "common.h"

namespace cube
{

struct Sounding: public MapPosition
{
  float depth;
  //float range = 0.0;
  float vertical_error;
  float horizontal_error;
};

} // namespace cube

#endif
