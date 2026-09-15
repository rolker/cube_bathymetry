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

#ifndef CUBE_BATHYMETRY__MULTI_LEVEL_ACCUMULATOR_H_
#define CUBE_BATHYMETRY__MULTI_LEVEL_ACCUMULATOR_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/level_plan.h"
#include "cube_bathymetry/store_import.h"

/// @file
/// @brief One CUBE accumulator per level of a level plan, driven as one
///        (cube_bathymetry#143).
///
/// The fixed-level import builds one `GeoMapSheet` and pins the store's cell
/// size to it; a depth-adaptive import owns one `{GeoMapSheet,
/// ImportAccumulator}` per level the plan emitted, each built at that level's
/// own cell size, so every accumulator's scratch/reload/seed stores still tile
/// identically to its own sheet -- the invariant the single-sheet design
/// protected. What is dropped is "one sheet per run", not the coupling.
///
/// - **Routing**: a batch goes to every level whose emitted tiles its
///   influence-expanded bounds intersect, parents included (they are estimated
///   in full under their children).
/// - **Admission**: each sheet admits only the plan's emitted tiles at its
///   level (`GeoMapSheet::setAdmission`), so a swath clipping one planned fine
///   tile cannot build fine tiles over the unplanned plain beside it.
/// - **One RAM budget**: `max_resident_tiles` is the store-wide total. The
///   per-accumulator eviction is disabled (their budget is 0); this class
///   evicts the globally coldest tiles across levels, comparable because every
///   sheet stamps last-touch from one shared clock.
/// - **One sidecar write**: `registry.json` and the backscatter metadata are
///   written once after every level has finalised, not once per level.

namespace cube
{

/// @brief The requested cell size that resolves to exactly @p level through
///        `gggs::Level::fromCellSize`: the level's nominal cell nudged a hair
///        coarser, so float rounding can never snap it one level finer. A
///        fixed-level run at this resolution is the exact equivalent of the
///        adaptive path's sheet at @p level.
  float requestedCellSizeFor(uint8_t level);

  struct MultiLevelAccumulatorConfig
  {
  /// Store roots and seeding, as for the fixed-level accumulator.
    std::string store_dir;
    std::string reference_store_dir;
    std::string bs_store_dir;
    bool skip_survey_seed = false;

  /// Store-wide resident-tile budget across all levels; 0 = unbounded.
    std::size_t max_resident_tiles = 0;

  /// Sheet construction per level; defaults to
  /// `GeoMapSheet(requestedCellSizeFor(level), iho_order)` with the capture
  /// and backscatter settings below applied. Tests override it to pin a
  /// specific requested resolution.
    std::function < std::unique_ptr < GeoMapSheet > (uint8_t level) > sheet_factory;
    std::string iho_order = "order1a";
    float capture_spacing_scale = 0.71f;
    BackscatterAngleCorrection backscatter_mode = BackscatterAngleCorrection::None;
    std::vector < std::pair < float, float >> backscatter_curve;
    bool backscatter_tl_removed = false;
    float backscatter_absorption_db_per_m = 0.0f;
  };

  class MultiLevelAccumulator
  {
public:
  /// @throws std::invalid_argument if the plan emits nothing.
    MultiLevelAccumulator(std::shared_ptr < const LevelPlan > plan,
      MultiLevelAccumulatorConfig config);

  /// @brief Route one ping's soundings to every level it touches, then evict
  ///        the globally coldest tiles back to the budget.
    void addBatch(
      const std::vector < GeoSounding > &soundings,
      std::chrono::steady_clock::time_point time = std::chrono::steady_clock::now());

  /// @brief Persist every still-resident tile at every level, then write the
  ///        store-level sidecars once.
    void finalize(
      const marine_bathymetry_store::StoreMetadata * bathy_metadata = nullptr,
      const marine_mbes_backscatter_store::StoreMetadata * bs_metadata = nullptr);

    const LevelPlan & plan() const {return *plan_;}
    const std::set < uint8_t > & levels() const {
      return levels_;
    }

    ImportAccumulator & accumulatorAt(uint8_t level);
    GeoMapSheet & sheetAt(uint8_t level);

  /// Resident tiles summed over every level.
    std::size_t residentTileCount() const;
    std::size_t bathyTilesPersisted() const;
    std::size_t backscatterTilesPersisted() const;
  /// Tiles evicted mid-run, summed over every level.
    std::size_t evictedTileCount() const;
  /// Batches routed to each level (diagnostics for the import log).
    const std::map < uint8_t, uint64_t > & batchesPerLevel() const {
      return batches_per_level_;
    }

  /// @brief Soundings in batches that NO level took: no emitted tile at any
  ///        level intersected them, so they were dropped.
  ///
  /// Non-zero means the plan does not span the data being replayed -- a plan
  /// from another survey, or a re-run over changed bags. The caller must treat
  /// it as a failed import: the store would be partial-coverage and would
  /// otherwise be fingerprinted as a complete build (cube#143).
    uint64_t unroutedSoundings() const noexcept {return unrouted_soundings_;}

private:
    struct Level
    {
      std::unique_ptr < GeoMapSheet > sheet;
      std::unique_ptr < ImportAccumulator > accumulator;
    };

    void evictToBudget();
    gz4d::BoundsDegrees batchBounds(const std::vector < GeoSounding > &soundings) const;

    std::shared_ptr < const LevelPlan > plan_;
    MultiLevelAccumulatorConfig cfg_;
    std::shared_ptr < std::atomic < uint64_t >> clock_;
    std::set < uint8_t > levels_;
    std::map < uint8_t, Level > per_level_;
    std::map < uint8_t, uint64_t > batches_per_level_;
    uint64_t unrouted_soundings_ = 0;
    double coarsest_cell_m_ = 0.0;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__MULTI_LEVEL_ACCUMULATOR_H_
