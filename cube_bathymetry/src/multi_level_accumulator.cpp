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

#include "cube_bathymetry/multi_level_accumulator.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <tuple>

#include "marine_autonomy/gz4d_geo.h"

namespace cube
{

float requestedCellSizeFor(uint8_t level)
{
  // fromCellSize takes ceil(log2(level0 / (cell * 960))): at exactly the
  // nominal cell the log2 is an integer in exact arithmetic, and float
  // rounding of `cell` can push it a hair above, which ceil() turns into the
  // NEXT level. Nudging the request coarser by 1e-5 keeps the log2 a hair
  // below the integer, so ceil() lands on `level` -- and the sheet's
  // distance_scale differs from the nominal cell by the same 1e-5.
  const double nominal = gggs::Level(level).cellSize();
  return static_cast<float>(nominal * (1.0 + 1e-5));
}

MultiLevelAccumulator::MultiLevelAccumulator(
  std::shared_ptr<const LevelPlan> plan, MultiLevelAccumulatorConfig config)
: plan_(std::move(plan)), cfg_(std::move(config)),
  clock_(std::make_shared<std::atomic<uint64_t>>(0))
{
  if (!plan_) {
    throw std::invalid_argument("MultiLevelAccumulator: null plan");
  }
  levels_ = plan_->levels();
  if (levels_.empty()) {
    throw std::invalid_argument("MultiLevelAccumulator: the plan emits no tiles");
  }
  coarsest_cell_m_ = gggs::Level(*levels_.begin()).cellSize();

  for (uint8_t level : levels_) {
    Level entry;
    if (cfg_.sheet_factory) {
      entry.sheet = cfg_.sheet_factory(level);
    } else {
      entry.sheet = std::make_unique<GeoMapSheet>(requestedCellSizeFor(level), cfg_.iho_order);
      entry.sheet->setCaptureSpacingScale(cfg_.capture_spacing_scale);
      entry.sheet->setBackscatterCorrection(
        cfg_.backscatter_mode, cfg_.backscatter_curve, cfg_.backscatter_tl_removed,
        cfg_.backscatter_absorption_db_per_m);
    }
    if (!entry.sheet) {
      // Same invariant batch_regen applies to its factory (#143): a factory that
      // returns null must be refused here, not dereferenced on the next line.
      throw std::invalid_argument(
              "MultiLevelAccumulator: the sheet factory returned no sheet for level " +
              std::to_string(level));
    }
    if (entry.sheet->gridLevel().level() != level) {
      throw std::invalid_argument(
              "MultiLevelAccumulator: the sheet for level " + std::to_string(level) +
              " snapped to level " + std::to_string(entry.sheet->gridLevel().level()));
    }
    entry.sheet->setTouchClock(clock_);
    // Admission: only the plan's emitted tiles at this level.
    std::shared_ptr<const LevelPlan> plan_ref = plan_;
    entry.sheet->setAdmission(
      [plan_ref](const gggs::GridIndex & grid) {return plan_ref->isEmitted(grid);});

    ImportAccumulatorConfig acc;
    acc.store_dir = cfg_.store_dir;
    acc.reference_store_dir = cfg_.reference_store_dir;
    acc.bs_store_dir = cfg_.bs_store_dir;
    acc.skip_survey_seed = cfg_.skip_survey_seed;
    // The store level matches this sheet's actual (GGGS-snapped) cell size so
    // the reload/seed/merge scratch stores tile identically -- per level.
    acc.cell_size_m = static_cast<float>(entry.sheet->nominalCellSizeMeters());
    // Eviction is owned here, across levels: the per-accumulator trigger is off.
    acc.max_resident_tiles = 0;
    entry.accumulator = std::make_unique<ImportAccumulator>(*entry.sheet, acc);
    per_level_.emplace(level, std::move(entry));
  }
}

gz4d::BoundsDegrees MultiLevelAccumulator::batchBounds(
  const std::vector<GeoSounding> & soundings) const
{
  // The widest reach any sheet will select for this batch: the soundings'
  // maximum spread radius floored at the coarsest level's cell, plus one such
  // cell (GeoMapSheet's own selection floor). A superset of every level's
  // window, so routing never misses a level a sheet would have touched.
  gz4d::BoundsDegrees bounds;
  const Parameters & p = per_level_.begin()->second.sheet->parameters();
  double reach = coarsest_cell_m_;
  for (const auto & s : soundings) {
    bounds.expand(s);
    const double r = p.maxSpreadRadius(s.sounding);
    if (std::isfinite(r)) {
      reach = std::max(reach, r);
    }
  }
  if (!valid(bounds)) {
    return bounds;
  }
  const auto lo = gz4d::BoundsDegrees::radiusFromCenter(
    bounds.minimum(), reach + coarsest_cell_m_);
  const auto hi = gz4d::BoundsDegrees::radiusFromCenter(
    bounds.maximum(), reach + coarsest_cell_m_);
  bounds.expand(lo.minimum());
  bounds.expand(hi.maximum());
  return bounds;
}

void MultiLevelAccumulator::addBatch(
  const std::vector<GeoSounding> & soundings,
  std::chrono::steady_clock::time_point time)
{
  if (soundings.empty()) {
    return;
  }
  const auto bounds = batchBounds(soundings);
  std::size_t levels_taken = 0;
  for (uint8_t level : plan_->levelsIntersecting(bounds)) {
    auto it = per_level_.find(level);
    if (it == per_level_.end()) {
      continue;
    }
    it->second.accumulator->addBatch(soundings, time);
    ++batches_per_level_[level];
    ++levels_taken;
  }
  if (levels_taken == 0) {
    // No emitted tile at any level intersects this batch: it went nowhere. The
    // caller's spill-replay accounting counts records READ, not soundings
    // routed, so without this the batch would vanish silently and the store
    // would still be fingerprinted as a complete build (cube#143 triage). The
    // second site of the same class as the plan-coverage check the import runs
    // before the replay -- this one catches what survives it.
    unrouted_soundings_ += soundings.size();
  }
  evictToBudget();
}

void MultiLevelAccumulator::evictToBudget()
{
  if (cfg_.max_resident_tiles == 0 || cfg_.store_dir.empty()) {
    return;
  }
  std::size_t resident = residentTileCount();
  if (resident <= cfg_.max_resident_tiles) {
    return;
  }
  // Globally coldest first: last-touch stamps are comparable across levels
  // because every sheet stamps from the shared clock.
  std::vector<std::tuple<uint64_t, uint8_t, gggs::GridIndex>> by_age;
  for (const auto & [level, entry] : per_level_) {
    for (const auto & [grid, touch] : entry.accumulator->residentTiles()) {
      by_age.emplace_back(touch, level, grid);
    }
  }
  std::sort(by_age.begin(), by_age.end(),
    [](const auto & a, const auto & b) {return std::get<0>(a) < std::get<0>(b);});
  for (const auto & [touch, level, grid] : by_age) {
    if (resident <= cfg_.max_resident_tiles) {
      break;
    }
    if (per_level_.at(level).accumulator->persistAndDrop(grid)) {
      --resident;
    }
    // A failed persist leaves the tile resident (the lossless guarantee);
    // the next batch retries.
  }
}

void MultiLevelAccumulator::finalize(
  const marine_bathymetry_store::StoreMetadata * bathy_metadata,
  const marine_mbes_backscatter_store::StoreMetadata * bs_metadata)
{
  // Persist every level's resident tiles; the sidecars are written once, below,
  // rather than by whichever level finalised last.
  for (auto & [level, entry] : per_level_) {
    entry.accumulator->finalize(nullptr, nullptr);
  }
  if (!cfg_.store_dir.empty() && bathy_metadata != nullptr && !bathy_metadata->empty()) {
    std::filesystem::create_directories(cfg_.store_dir);
    bathy_metadata->save(cfg_.store_dir);
  }
  if (!cfg_.bs_store_dir.empty() && bs_metadata != nullptr && !bs_metadata->empty()) {
    std::filesystem::create_directories(cfg_.bs_store_dir);
    bs_metadata->save(cfg_.bs_store_dir);
  }
}

ImportAccumulator & MultiLevelAccumulator::accumulatorAt(uint8_t level)
{
  return *per_level_.at(level).accumulator;
}

GeoMapSheet & MultiLevelAccumulator::sheetAt(uint8_t level)
{
  return *per_level_.at(level).sheet;
}

std::size_t MultiLevelAccumulator::residentTileCount() const
{
  std::size_t n = 0;
  for (const auto & [level, entry] : per_level_) {
    n += entry.accumulator->residentTileCount();
  }
  return n;
}

std::size_t MultiLevelAccumulator::bathyTilesPersisted() const
{
  std::size_t n = 0;
  for (const auto & [level, entry] : per_level_) {
    n += entry.accumulator->bathyTilesPersisted();
  }
  return n;
}

std::size_t MultiLevelAccumulator::backscatterTilesPersisted() const
{
  std::size_t n = 0;
  for (const auto & [level, entry] : per_level_) {
    n += entry.accumulator->backscatterTilesPersisted();
  }
  return n;
}

std::size_t MultiLevelAccumulator::evictedTileCount() const
{
  std::size_t n = 0;
  for (const auto & [level, entry] : per_level_) {
    n += entry.accumulator->evictedIndices().size();
  }
  return n;
}

}  // namespace cube
