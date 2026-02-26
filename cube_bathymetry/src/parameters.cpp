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


#include "cube_bathymetry/parameters.h"
#include <stdexcept>
#include <cmath>

namespace cube
{

Parameters::Parameters(CellSizes sizes, std::string order)
:iho_order(order)
{
  setIHOLimits(order);
  setGridResolution(sizes);
}

void Parameters::setIHOLimits(std::string order)
{
  iho_fixed = 0.0;
  iho_percent = 0.0;

  if(order == "exclusive") {
    iho_fixed = 0.15;
    iho_percent = 0.0075;
  } else if(order == "special") {
    iho_fixed = 0.25;
    iho_percent = 0.0075;
  } else if(order == "order1a") {
    iho_fixed = 0.5;
    iho_percent = 0.013;
  } else if(order == "order1b") {
    iho_fixed = 0.5;
    iho_percent = 0.013;
  } else if(order == "order2") {
    iho_fixed = 1.0;
    iho_percent = 0.023;
  } else {
    throw std::invalid_argument("Unknown IHO order: " + order);
  }

  iho_fixed *= iho_fixed;
  iho_percent *= iho_percent;
}

void Parameters::setGridResolution(CellSizes sizes)
{
  /* Compute distance scale based on node spacing */
  distance_scale = (sizes.x < sizes.y) ? sizes.x : sizes.y;
  minimum_context_search_range = DEFAULT_MIN_CONTEXT / distance_scale;
  maximum_context_search_range = DEFAULT_MAX_CONTEXT / distance_scale;

  /* Compute variance scaling factor for dilution function */
  variance_scale = std::pow(distance_scale, -distance_exponent);
}


}  // namespace cube
