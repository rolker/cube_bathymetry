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


#include "cube_bathymetry/geo_map_sheet.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include "marine_autonomy/gz4d_geo.h"

namespace cube
{

namespace
{
// Expanded geographic bounds covering a sounding batch: the sounding extent grown
// by one cell on every side so a sounding near a tile seam still reaches its
// neighbour tile. Shared by addSoundings (which then creates the grids) and
// gridIndicesForSoundings (which only enumerates them) so the two never drift.
gz4d::BoundsDegrees boundsForSoundings(
  const std::vector<GeoSounding> & soundings, const gggs::Level & grid_level)
{
  gz4d::BoundsDegrees bounds;
  for (const auto & s  :  soundings) {
    bounds.expand(s);
  }
  // quick hack to make sure to go a bit beyond the outer soundings
  const auto angular_span = grid_level.cellAngularSpan();
  auto min = bounds.minimum();
  min = gz4d::PositionDegrees(min.latitude - angular_span, min.longitude - angular_span);
  bounds.expand(min);

  auto max = bounds.maximum();
  max = gz4d::PositionDegrees(max.latitude + angular_span, max.longitude + angular_span);
  bounds.expand(max);
  return bounds;
}
}  // namespace

GeoMapSheet::GeoMapSheet(float cell_size, std::string iho_order)
:parameters_(CellSizes(cell_size), iho_order), grid_level_(gggs::Level::fromCellSize(cell_size))
{
}

void GeoMapSheet::setBackscatterCorrection(
  BackscatterAngleCorrection mode,
  std::vector<std::pair<float, float>> curve,
  bool tl_removed,
  float absorption_db_per_m)
{
  // Grids hold a const reference to parameters_, so this reaches all of them.
  parameters_.backscatter_angle_correction = mode;
  parameters_.angular_response_curve = std::move(curve);
  parameters_.backscatter_tl_removed = tl_removed;
  parameters_.backscatter_absorption_db_per_m = absorption_db_per_m;
}

void GeoMapSheet::addSoundings(
  const std::vector<GeoSounding> & soundings,
  std::chrono::steady_clock::time_point time)
{
  if(soundings.empty()) {
    return;
  }

  auto grids = getOrCreateGridsIn(boundsForSoundings(soundings, grid_level_));
  for (auto g  :  grids) {
    if(g->insert(soundings)) {
      last_update_time_ = time;
      // Record the grid as dirty so the periodic save loop (#21) writes only
      // grids that actually changed since the last save, and in the separate
      // publish-dirty set so the incremental ~/tiles publish emits it (ADR-0001).
      dirty_grids_.insert(g->index());
      publish_dirty_grids_.insert(g->index());
    }
  }
}

std::vector<gggs::GridIndex> GeoMapSheet::gridIndicesForSoundings(
  const std::vector<GeoSounding> & soundings) const
{
  std::vector<gggs::GridIndex> ret;
  if(soundings.empty()) {
    return ret;
  }
  const gz4d::BoundsDegrees bounds = boundsForSoundings(soundings, grid_level_);
  gggs::GridAreaIterator i(
    grid_level_.gridIndex(bounds.minimum().latitude, bounds.minimum().longitude),
    grid_level_.gridIndex(bounds.maximum().latitude, bounds.maximum().longitude));
  while(i.valid()) {
    ret.push_back(*i);
    i.next();
  }
  return ret;
}

void GeoMapSheet::setSettledIntensitySamplesAt(
  const gggs::CellIndex & cell, std::vector<BeamIntensitySample> samples)
{
  // Lazy-create the grid (mirrors setSettledDepthAt) so the call is safe even if
  // the grid is absent; GeoGrid::setSettledIntensitySamplesAt is a no-op when the
  // node was not seeded. Does NOT mark dirty (reproduces persisted data).
  getOrCreateGrid(cell.grid())->setSettledIntensitySamplesAt(cell, std::move(samples));
}

std::vector<std::shared_ptr<GeoGrid>> GeoMapSheet::getOrCreateGridsIn(
  const gz4d::BoundsDegrees & bounds)
{
  std::vector<std::shared_ptr<GeoGrid>> ret;

  // gridIndex now takes lat/lon doubles (gz4d retired from the GGGS API,
  // unh_marine_autonomy#144); bounds remains a gz4d type internally.
  gggs::GridAreaIterator i(
    grid_level_.gridIndex(bounds.minimum().latitude, bounds.minimum().longitude),
    grid_level_.gridIndex(bounds.maximum().latitude, bounds.maximum().longitude));

  while(i.valid()) {
    if(!grids_[*i]) {
      grids_[*i] = std::make_shared<GeoGrid>(*i, parameters_);
    }
    // Every grid in the current sounding bounds is being actively surveyed, so
    // bump its last-touch recency -- this is the signal LRU eviction uses to keep
    // near-vessel tiles resident and evict cold far-away ones (ADR-0001).
    last_touch_[*i] = ++touch_counter_;
    ret.push_back(grids_[*i]);
    i.next();
  }

  return ret;
}

std::vector<std::shared_ptr<GeoGrid>> GeoMapSheet::grids() const
{
  std::vector<std::shared_ptr<GeoGrid>> ret;
  for (const auto g  :  grids_) {
    if(g.second) {
      ret.push_back(g.second);
    }
  }
  return ret;
}

std::shared_ptr<const GeoGrid> GeoMapSheet::gridAt(const gggs::GridIndex & index) const
{
  auto it = grids_.find(index);
  if(it == grids_.end()) {
    return nullptr;
  }
  return it->second;
}

std::shared_ptr<GeoGrid> GeoMapSheet::getOrCreateGrid(const gggs::GridIndex & index)
{
  if(!grids_[index]) {
    grids_[index] = std::make_shared<GeoGrid>(index, parameters_);
    // Seed a last-touch entry on first creation so every resident grid has one
    // (the prime/reload path lands here). Touched once at load -- relatively cold
    // versus actively-surveyed tiles, so primed-but-inactive tiles evict first.
    last_touch_[index] = ++touch_counter_;
  }
  return grids_[index];
}

void GeoMapSheet::setPredictedDepthAt(
  const gggs::CellIndex & cell, float depth, float variance)
{
  auto grid = getOrCreateGrid(cell.grid());
  grid->setPredictedDepthAt(cell, depth, variance);
}

void GeoMapSheet::setSettledDepthAt(
  const gggs::CellIndex & cell, float depth, float uncertainty)
{
  auto grid = getOrCreateGrid(cell.grid());
  grid->setSettledDepthAt(cell, depth, uncertainty);
}

std::set<gggs::GridIndex> GeoMapSheet::dirtyGrids() const
{
  return dirty_grids_;
}

void GeoMapSheet::clearDirtyGrids()
{
  dirty_grids_.clear();
}

std::set<gggs::GridIndex> GeoMapSheet::publishDirtyGrids() const
{
  return publish_dirty_grids_;
}

void GeoMapSheet::clearPublishDirtyGrids()
{
  publish_dirty_grids_.clear();
}

std::vector<gggs::GridIndex> GeoMapSheet::coldTiles(std::size_t max_resident) const
{
  if(grids_.size() <= max_resident) {
    return {};
  }

  // Order all resident grids by last-touch ascending (coldest first). A grid
  // always has a last_touch_ entry (set on creation), but default to 0 if
  // somehow missing so it sorts as coldest rather than being skipped.
  std::vector<std::pair<uint64_t, gggs::GridIndex>> by_age;
  by_age.reserve(grids_.size());
  for (const auto & g  :  grids_) {
    auto it = last_touch_.find(g.first);
    const uint64_t touch = (it == last_touch_.end()) ? 0 : it->second;
    by_age.emplace_back(touch, g.first);
  }
  std::sort(by_age.begin(), by_age.end(),
    [](const auto & a, const auto & b){return a.first < b.first;});

  const std::size_t evict_count = grids_.size() - max_resident;
  std::vector<gggs::GridIndex> ret;
  ret.reserve(evict_count);
  for (std::size_t k = 0; k < evict_count; ++k) {
    ret.push_back(by_age[k].second);
  }
  return ret;
}

void GeoMapSheet::dropTile(const gggs::GridIndex & index)
{
  grids_.erase(index);
  last_touch_.erase(index);
  dirty_grids_.erase(index);
  publish_dirty_grids_.erase(index);
}

std::size_t GeoMapSheet::residentTileCount() const
{
  return grids_.size();
}

uint64_t GeoMapSheet::lastTouchOf(const gggs::GridIndex & index) const
{
  auto it = last_touch_.find(index);
  return (it == last_touch_.end()) ? 0 : it->second;
}

double GeoMapSheet::cellSizeDegrees() const
{
  return grid_level_.cellAngularSpan();
}

double GeoMapSheet::nominalCellSizeMeters() const
{
  return grid_level_.cellSize();
}

gggs::GridBounds GeoMapSheet::gridBounds() const
{
  gggs::GridBounds ret;
  for (const auto & g  :  grids_) {
    ret.expand(g.first);
  }
  return ret;
}

std::chrono::steady_clock::time_point GeoMapSheet::lastUpdateTime() const
{
  return last_update_time_;
}

}  // namespace cube
