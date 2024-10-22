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

  if(order == "exclusive")
  {
    iho_fixed = 0.15;
    iho_percent = 0.0075;
  }
  else if(order == "special")
  {
    iho_fixed = 0.25;
    iho_percent = 0.0075;
  }
  else if(order == "order1a")
  {
    iho_fixed = 0.5;
    iho_percent = 0.013;
  }
  else if(order == "order1b")
  {
    iho_fixed = 0.5;
    iho_percent = 0.013;
  }
  else if(order ==  "order2")
  {
    iho_fixed = 1.0;
    iho_percent = 0.023;
  }
  else
    throw std::invalid_argument("Unknown IHO order: "+order);

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



} // namespace cube
