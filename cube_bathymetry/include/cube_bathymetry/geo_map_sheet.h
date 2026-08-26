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


#ifndef CUBE_BATHYMETRY__GEO_MAP_SHEET_H_
#define CUBE_BATHYMETRY__GEO_MAP_SHEET_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "cube_bathymetry/geo_grid.h"

#include "marine_autonomy/gggs.h"

namespace cube
{

/// Uses GlobalGGS grid heirarchy to organize Grids.
  class GeoMapSheet
  {
public:
  /// Constructor where cell_size is approximate resolution requested.
    explicit GeoMapSheet(float cell_size, std::string iho_order = "order1a");

    void addSoundings(
      const std::vector < GeoSounding > &soundings,
      std::chrono::steady_clock::time_point time = std::chrono::steady_clock::now());

  /// @brief The grid indices @ref addSoundings would touch for @p soundings,
  ///        WITHOUT creating them (cube_bathymetry#92 reload-before-add).
  ///
  /// Computes the same expanded bounds @ref addSoundings uses (the sounding extent
  /// grown by each sounding's influence radius, one-cell floor -- #104 -- so a
  /// near-seam sounding reaches every neighbour tile its spillover writes) and
  /// returns every `gggs::GridIndex` in that window. The bounded-RAM importer calls
  /// this BEFORE addSoundings to reload any evicted tile the batch is about to
  /// touch, so the new soundings accrete onto the reloaded hypotheses (lossless
  /// blend) instead of forming a fresh, partial tile. Empty for empty input.
    std::vector < gggs::GridIndex > gridIndicesForSoundings(
      const std::vector < GeoSounding > &soundings) const;

  /// @brief The grid index a single sounding's centre falls in (no creation).
  ///
  /// Maps the sounding position to its home tile via the sheet's grid level. The
  /// bounded-RAM importer uses this to count the soundings lost when a tile that
  /// failed to reload is dropped. A sounding's influence radius can spread it into
  /// neighbour tiles too (see GeoGrid::insert), so this home-tile count is a
  /// conservative floor on the affected soundings, not an exact cell tally.
    gggs::GridIndex gridIndexForSounding(const GeoSounding & sounding) const;

  /// @brief Configure the per-beam backscatter angular-response correction
  ///        applied at node-output (ADR-0007 D3, cube_bathymetry#81).
  ///
  /// Writes @p mode and @p curve into the sheet's private @c parameters_, which
  /// every owned GeoGrid holds by const reference -- so the setting reaches all
  /// existing and future grids. Call AFTER construction and BEFORE/at processing.
  /// An Empirical mode with an empty curve is a no-op (the caller should warn).
  ///
  /// @p tl_removed / @p absorption_db_per_m carry the tier-2 TL provenance
  /// (cube_bathymetry#87): when @p tl_removed is true the @p curve is a TL-
  /// removed residual and the estimator removes `40*log10(R) + 2*alpha*R` per
  /// beam (alpha == @p absorption_db_per_m) before subtracting the residual.
  /// Both default to the tier-1 values (false / 0), so existing callers are
  /// unchanged.
    void setBackscatterCorrection(
      BackscatterAngleCorrection mode,
      std::vector < std::pair < float, float >> curve,
      bool tl_removed = false,
      float absorption_db_per_m = 0.0f);

  /// Return the grids within the bounds, creating new ones if necessary
    std::vector < std::shared_ptr <
    GeoGrid >> getOrCreateGridsIn(const gz4d::BoundsDegrees & bounds);

  /// Return all existing grids
    std::vector < std::shared_ptr < GeoGrid >> grids() const;

  /// @brief Return the grid at @p index, or nullptr if none exists.
  ///
  /// Read-only handle (no lazy creation) used by the periodic save loop to
  /// convert a dirty grid without resurrecting empties.
    std::shared_ptr < const GeoGrid > gridAt(const gggs::GridIndex & index) const;

  /// @brief Find or create the grid at @p index (no dirty mark).
  ///
  /// Used by the warm-start prime path to reach a specific grid by index without
  /// going through the sounding-driven `addSoundings` path.
    std::shared_ptr < GeoGrid > getOrCreateGrid(const gggs::GridIndex & index);

  /// @brief Seed the predicted depth at @p cell (lazy-creates the grid + node).
  ///
  /// Warm-start prime for slope correction from a persisted draft tile (#21).
  /// Does NOT mark the grid dirty -- priming reproduces already-persisted data,
  /// so re-saving it would be redundant churn.
    void setPredictedDepthAt(const gggs::CellIndex & cell, float depth, float variance);

  /// @brief Reseed a previously-settled depth/uncertainty at @p cell as a CUBE
  ///        hypothesis (lossless reload; lazy-creates the grid + node).
  ///
  /// Unlike @ref setPredictedDepthAt (slope prior only), this restores the cell's
  /// best estimate so it survives the next whole-tile save and refines under new
  /// soundings (ADR-0001). Used by the tile-eviction revisit-reload and the
  /// startup prime. Does NOT mark the grid dirty (reproduces persisted data).
    void setSettledDepthAt(const gggs::CellIndex & cell, float depth, float uncertainty);

  /// @brief Restore a corrected-intensity Welford onto @p cell's winning
  ///        hypothesis (cube_bathymetry#92/#93 lossless eviction reload).
  ///
  /// Forwards to `GeoGrid::setSettledIntensityWelfordAt` on the grid owning
  /// @p cell (lazy-creates the grid; a no-op on the node if @ref setSettledDepthAt
  /// did not seed it). Must run AFTER the depth reload and BEFORE the revisit's
  /// soundings are added, so those beams continue the Welford on the same reloaded
  /// hypothesis (bit-identical to never-evicting). Does NOT mark the grid dirty
  /// (it reproduces already-persisted data).
    void setSettledIntensityWelfordAt(
      const gggs::CellIndex & cell, const IntensityWelford & intensity);

  /// @brief Grid indices touched (returning true from insert) since the last
  ///        clearDirtyGrids(). Returned by value -- safe to iterate while saving.
    std::set < gggs::GridIndex > dirtyGrids() const;

  /// @brief Clear the dirty-grid set (called after a successful save).
    void clearDirtyGrids();

  /// @brief Grid indices changed since the last clearPublishDirtyGrids().
  ///
  /// A SECOND dirty set, tracked alongside the save-dirty set but cleared by the
  /// incremental publish path instead of the save path (ADR-0001). Decoupling the
  /// two lets the ~/tiles publish and the draft save run on independent cadences
  /// and clear independently -- in particular the publish set still clears when
  /// persistence is disabled (no save to clear it). Returned by value.
    std::set < gggs::GridIndex > publishDirtyGrids() const;

  /// @brief Clear the publish-dirty set (called after an incremental publish).
  ///
  /// Also resets each of those grids' `GeoGrid::publishDirtyCells()` box, so
  /// the tile-level set and the cell-level bounds the sub-window publish reads
  /// always clear together (ADR-0001 section 4 sub-window addendum).
    void clearPublishDirtyGrids();

  /// @brief The least-recently-touched grid indices beyond @p max_resident, in
  ///        eviction order (coldest first); empty when within budget.
  ///
  /// "Touched" = created or received soundings (last_touch_ sequence). The node
  /// persists each returned tile before calling @ref dropTile, so eviction never
  /// loses data (ADR-0001).
    std::vector < gggs::GridIndex > coldTiles(std::size_t max_resident) const;

  /// @brief Erase one grid from RAM (grids_, last-touch, and both dirty sets).
  ///
  /// Caller must have persisted the tile first when persistence is enabled --
  /// dropTile makes no disk write. Safe if @p index is absent (no-op).
    void dropTile(const gggs::GridIndex & index);

  /// @brief Number of grids currently resident in RAM.
    std::size_t residentTileCount() const;

  /// @brief Last-touch sequence number of @p index, or 0 if not resident.
  ///        Monotonic; higher = more recently touched. For tests / diagnostics.
    uint64_t lastTouchOf(const gggs::GridIndex & index) const;

  /// Return gggs::GridIndex bounds of rectangle containing all the grids
    gggs::GridBounds gridBounds() const;

  /// Cell size in degrees
    double cellSizeDegrees() const;

    double nominalCellSizeMeters() const;

  /// @brief The sheet's GGGS grid level (fixed at construction from the
  ///        requested cell size).
  ///
  /// Exposed so store-prime helpers can level-scope a multi-level prior store
  /// (#91): tiles at other levels must be skipped or resampled, never primed
  /// cell-for-cell.
    const gggs::Level & gridLevel() const {return grid_level_;}

    std::chrono::steady_clock::time_point lastUpdateTime() const;

private:
  /// Grid cell counts
  // CellCounts counts_;
  /// Cell sizes (meters)
  // CellSizes sizes_;

    Parameters parameters_;

    gggs::Level grid_level_;

    std::map < gggs::GridIndex, std::shared_ptr < GeoGrid >> grids_;

  /// Grids that received data (insert() returned true) since the last
  /// clearDirtyGrids(). Drives the periodic incremental tile save (#21).
    std::set < gggs::GridIndex > dirty_grids_;

  /// Grids changed since the last clearPublishDirtyGrids(). Drives the
  /// incremental ~/tiles publish, cleared independently of the save set so the
  /// publish and save cadences don't interfere (ADR-0001).
    std::set < gggs::GridIndex > publish_dirty_grids_;

  /// Per-grid last-touch sequence number (created or received soundings),
  /// assigned from touch_counter_. Drives LRU eviction (coldTiles); higher =
  /// more recently touched.
    std::map < gggs::GridIndex, uint64_t > last_touch_;

  /// Monotonic counter sourced for last_touch_ on every touch. Starts at 1 so 0
  /// reads as "never touched / not resident" (see lastTouchOf).
    uint64_t touch_counter_ = 0;

    std::chrono::steady_clock::time_point last_update_time_;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__GEO_MAP_SHEET_H_
