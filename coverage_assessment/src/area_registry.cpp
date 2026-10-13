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

#include <stdexcept>
#include <string>

namespace coverage_assessment
{

namespace
{
int64_t boxCells(const PolygonMask & mask)
{
  return mask.ni * mask.nj;
}
}  // namespace

AreaRegistry::AreaRegistry(
  double cell_size, int64_t max_cells, int64_t max_cell_checks, int64_t source_count)
: cell_size_(cell_size), max_cells_(max_cells), max_cell_checks_(max_cell_checks),
  source_count_(source_count), max_total_cells_(0)
{
  if (source_count < 1) {
    throw std::invalid_argument("AreaRegistry: source_count must be at least 1");
  }
  max_total_cells_ = max_cell_checks / source_count;
}

int64_t AreaRegistry::totalCells() const
{
  int64_t total = 0;
  for (const RegisteredArea & a : areas_) {
    total += boxCells(a.mask);
  }
  return total;
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

bool AreaRegistry::exceedsTotal(
  int64_t others, int64_t added, const std::string & id, std::string * reason) const
{
  // Written to avoid overflow: both operands are bounded by max_cells_ times areas.
  if (others <= max_total_cells_ && added <= max_total_cells_ - others) {
    return false;
  }
  *reason = "area '" + id + "': its " + std::to_string(added) +
    "-cell raster with the " + std::to_string(others) +
    " cell(s) already held would take an assessment pass over its budget: a pass checks "
    "every cell once per source, and " + std::to_string(source_count_) +
    " source(s) allow at most " + std::to_string(max_total_cells_) +
    " cell(s) under max_pass_cell_checks=" + std::to_string(max_cell_checks_);
  return true;
}

RegistryResult AreaRegistry::update(const AreaDefinition & definition)
{
  RegistryResult result;
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
    if (exceedsTotal(totalCells() - boxCells(held.mask), boxCells(raster.mask), definition.id,
        &result.reason))
    {
      result.outcome = RegistryOutcome::kRejected;
      return result;
    }
    held.definition = definition;
    held.mask = std::move(raster.mask);
    result.outcome = RegistryOutcome::kReplaced;
    return result;
  }
  if (exceedsTotal(totalCells(), boxCells(raster.mask), definition.id, &result.reason)) {
    result.outcome = RegistryOutcome::kRejected;
    return result;
  }
  areas_.push_back(RegisteredArea{definition, std::move(raster.mask), cell_size_});
  result.outcome = RegistryOutcome::kAdded;
  return result;
}

}  // namespace coverage_assessment
