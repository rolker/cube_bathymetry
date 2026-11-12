// Copyright 2026 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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

#ifndef CUBE_BATHYMETRY__PING_GEOREFERENCE_H_
#define CUBE_BATHYMETRY__PING_GEOREFERENCE_H_

#include <vector>

#include "cube_bathymetry/detections_projector.h"
#include "cube_bathymetry/geo_sounding.h"
#include "geometry_msgs/msg/transform_stamped.hpp"

/// @file
/// @brief Georeferencing of one projected ping (cube_bathymetry#63, #107).
///        Moved out of `import_bag_main.cpp` (cube_bathymetry#173) so the
///        tests of the beam geometry and of the slope run can reach it.

namespace cube
{

/// Georeference one projected ping into GeoSoundings (the cube#63/#107 hot
/// path). @p transform is the earth <- sonar-frame transform at the ping
/// stamp; every sounding of @p projection is mapped through it.
  std::vector < GeoSounding > georeferencePing(
    const ProjectionResult & projection,
    const geometry_msgs::msg::TransformStamped & transform);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__PING_GEOREFERENCE_H_
