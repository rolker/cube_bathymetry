#ifndef CUBE_BATHYMETRY_GEO_SOUNDING_H
#define CUBE_BATHYMETRY_GEO_SOUNDING_H

#include "project11/utils.h"
#include "sounding.h"

namespace cube
{

struct GeoSounding: public gz4d::PositionDegrees
{
  GeoSounding(const gz4d::GeoPointLatLongDegrees& point):
    gz4d::PositionDegrees(point), sounding(point[2])
  {}

  Sounding sounding;
};

} // namespace cube

#endif
