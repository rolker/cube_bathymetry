// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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

#include "coverage_assessment/area_registry.h"

#include <cmath>
#include <sstream>

namespace coverage_assessment
{

AreaRegistry::AreaRegistry(double cell_size, int64_t max_cells)
: cell_size_(cell_size), max_cells_(max_cells)
{
}

const RegisteredArea * AreaRegistry::find(const std::string & id) const
{
  for (const RegisteredArea & a : areas_) {
    if (a.definition.id == id) {
      return &a;
    }
  }
  return nullptr;
}

RegistryResult AreaRegistry::update(const AreaDefinition & definition)
{
  RegistryResult result;
  const SurveyStandard & s = definition.standard;
  if (!std::isfinite(s.a) || !std::isfinite(s.b) || s.a < 0.0 || s.b < 0.0) {
    std::ostringstream os;
    os << "area '" << definition.id << "': standard a=" << s.a << " b=" << s.b
       << " must be finite and >= 0";
    result.outcome = RegistryOutcome::kRejected;
    result.reason = os.str();
    return result;
  }
  PolygonRasterResult raster = rasterisePolygon(definition.polygon, cell_size_, max_cells_);
  if (!raster.ok) {
    result.outcome = RegistryOutcome::kRejected;
    result.reason = "area '" + definition.id + "': " + raster.error;
    return result;
  }

  for (RegisteredArea & held : areas_) {
    if (held.definition.id != definition.id) {
      continue;
    }
    if (definition.stamp_ns < held.definition.stamp_ns) {
      result.outcome = RegistryOutcome::kIgnoredStale;
      result.reason = "area '" + definition.id + "': older than the definition already held";
      return result;
    }
    held.definition = definition;
    held.mask = std::move(raster.mask);
    result.outcome = RegistryOutcome::kReplaced;
    return result;
  }
  areas_.push_back(RegisteredArea{definition, std::move(raster.mask), cell_size_});
  result.outcome = RegistryOutcome::kAdded;
  return result;
}

}  // namespace coverage_assessment
