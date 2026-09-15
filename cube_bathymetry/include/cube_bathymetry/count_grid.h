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

#ifndef CUBE_BATHYMETRY__COUNT_GRID_H_
#define CUBE_BATHYMETRY__COUNT_GRID_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_tiled_raster_store/tiled_raster_tile.hpp"

/// @file
/// @brief Sparse per-cell sounding counts at one GGGS level, with Calder's
///        level-of-aggregation query (cube_bathymetry#143).
///
/// Prior art: B. R. Calder, "Resolution Determination through Level of
/// Aggregation Analysis", U.S. Hydrographic Conference, Biloxi MS, 2019. A
/// fine count grid `C(i,j)` is filled with one increment per sounding; the
/// **level of aggregation** at a cell is the smallest half-width `lambda` such
/// that the `(2*lambda+1)^2` box centred on the cell holds at least `n_req`
/// soundings (Calder eq. 2). The spacing `(2*lambda+1) * R` is the finest
/// estimate spacing the data around that cell can support at `n_req`
/// observations per node -- the *achieved* resolution, as opposed to the
/// *required* resolution a depth ladder asks for. Boxes are evaluated with a
/// summed-area table (Calder sec. III.A), so each query is O(1) after an O(N)
/// table build per tile, and the tables are built lazily in a small LRU so a
/// survey-sized count grid never holds all its tables at once.
///
/// **RAM is bounded by a directory-backed LRU.** A level-14 count tile is
/// 960^2 x 2 B = 1.8 MB over ~54 m of ground (~340 tiles/km^2, so ~630 MB/km^2
/// if every tile stayed resident); a survey-sized recon would otherwise grow
/// without limit. `setSpillDir` caps the resident tiles at a budget and writes
/// colder ones to a scratch directory as single-band UInt16 GeoTIFFs, reloading
/// them on demand -- during the recon, and again during plan computation, where
/// a level-of-aggregation query reads only the 3x3 neighbourhood of one tile at
/// a time. `residentPeak()` reports the high-water mark so the operator sees
/// what the recon actually cost.
///
/// This is deliberately count-only: it makes no assumption about the data's
/// structure, is immune to a single flier (which moves one count by one), and
/// can be updated incrementally as long as the counts are preserved -- Calder's
/// stated property, and what lets a live estimator re-derive its level plan
/// from the same structure (a follow-up to #143).

namespace cube
{

  class CountGrid
  {
public:
    using Count = uint16_t;
    using Tile = marine_tiled_raster_store::TiledRasterTile < Count >;

  /// Cells along a tile edge (GGGS constant, 960).
    static constexpr uint16_t kEdge = Tile::edge;

  /// Largest level of aggregation the query will search before reporting
  /// saturation: one full tile edge, so a box never reaches past the 3x3
  /// neighbourhood the stitched summed-area lookup covers.
    static constexpr uint32_t kMaxLambda = kEdge;

  /// @brief Construct an empty count grid whose cells are at GGGS @p level.
  ///        Unbounded (every tile resident) until `setSpillDir`.
  /// @throws std::out_of_range if @p level is not a valid GGGS level.
    explicit CountGrid(uint8_t level);

  /// Default resident count-tile budget: ~460 MB of level-14 tiles.
    static constexpr std::size_t kDefaultResidentTiles = 256;

  /// Smallest budget `setSpillDir` accepts. A level-of-aggregation query reads
  /// the 3x3 neighbourhood around a cell's tile, so a budget below that would
  /// thrash one tile per box evaluation; 16 leaves headroom above the nine.
    static constexpr std::size_t kMinResidentTiles = 16;

  /// @brief Bound RAM: keep at most @p resident_tiles count tiles in memory,
  ///        writing colder ones to @p dir as `<level>_<row>_<col>.tif` and
  ///        reloading them on demand.
  ///
  /// Call before the first `add`. Tiles already resident are trimmed to the
  /// budget immediately.
  /// @throws std::invalid_argument if @p resident_tiles < kMinResidentTiles or
  ///         @p dir is empty; std::runtime_error if @p dir cannot be created.
    void setSpillDir(
      const std::string & dir,
      std::size_t resident_tiles = kDefaultResidentTiles);

  /// Count tiles currently in RAM.
    std::size_t residentTileCount() const noexcept {return tiles_.size();}

  /// High-water mark of `residentTileCount()` over this grid's life.
    std::size_t residentPeak() const noexcept {return resident_peak_;}

  /// Resident-tile budget; 0 when unbounded (no spill dir).
    std::size_t residentBudget() const noexcept {return resident_budget_;}

  /// Occupied tiles currently held on disk rather than in RAM.
    std::size_t spilledTileCount() const noexcept {return grids_.size() - tiles_.size();}

  /// @brief Delete the tile spill directory and **discard every non-resident
  ///        tile** (they exist only there). End-of-run cleanup: the grid is not
  ///        a complete count grid afterwards.
    void discardSpill();

    uint8_t level() const noexcept {return level_.level();}

  /// Nominal cell size R in metres at this level.
    double cellSizeMeters() const noexcept {return level_.cellSize();}

  /// @brief Count one sounding at a geographic position (saturating at 65535).
  ///
  /// @param spread_term_m The sounding's un-floored spread term
  ///        (`Parameters::influenceRadius` before its node-spacing floor), in
  ///        metres; the per-tile maximum is recorded so a level plan can expand
  ///        occupied cells by the reach the soundings actually had. Pass 0 when
  ///        it is unknown or irrelevant.
    void add(double latitude, double longitude, double spread_term_m = 0.0);

  /// @brief Count one sounding at a cell of this grid's level.
  /// @throws std::invalid_argument if @p cell is invalid or at another level.
    void add(const gggs::CellIndex & cell, double spread_term_m = 0.0);

  /// Count at a cell; 0 when the tile is absent.
  /// @throws std::invalid_argument if @p cell is invalid or at another level.
    Count countAt(const gggs::CellIndex & cell) const;

  /// Sum of all counts.
    uint64_t total() const noexcept {return total_;}

  /// Number of count tiles that hold at least one sounding (resident or spilled).
    std::size_t tileCount() const noexcept {return grids_.size();}

  /// Every occupied count tile's grid, resident or spilled.
    const std::set < gggs::GridIndex > & grids() const noexcept {return grids_;}

  /// @brief The count tile at @p grid, loaded from the spill if it is not
  ///        resident; nullptr when @p grid holds no soundings.
  ///
  /// **The returned pointer (and any reference into its bands) is invalidated
  /// by the next access to a DIFFERENT tile**, which may evict this one. Copy
  /// what you need out of the tile -- as `achievedLevelHistogram` copies the
  /// occupied mask -- before calling anything that touches another tile.
    const Tile * tileAt(const gggs::GridIndex & grid) const;

  /// Largest spread term recorded for soundings in @p tile; 0 when absent.
    double maxSpreadTerm(const gggs::GridIndex & tile) const;

  /// @brief Sum of counts in the `(2*lambda+1)^2` box centred on @p cell.
  ///
  /// The box is evaluated across the 3x3 neighbourhood of count tiles around
  /// the cell's tile, so a box crossing a tile edge reads the neighbour's
  /// counts rather than seeing zeros. Absent tiles contribute zero.
  /// @throws std::invalid_argument if @p cell is invalid or at another level,
  ///         or if @p lambda exceeds kMaxLambda.
    uint64_t countInBox(const gggs::CellIndex & cell, uint32_t lambda) const;

    struct LevelOfAggregation
    {
    /// Smallest half-width whose box holds >= n_req soundings, or kMaxLambda
    /// when `saturated`.
      uint32_t lambda = 0;
    /// True when even the kMaxLambda box does not reach n_req: the data
    /// around this cell supports no spacing finer than a tile edge, which the
    /// caller must read as "the coarsest level" (the safe direction).
      bool saturated = false;
    };

  /// @brief Calder's level of aggregation at @p cell for @p n_req observations.
  ///
  /// Bisection over `lambda` in `[0, kMaxLambda]`; `countInBox` is monotone in
  /// `lambda`, so the smallest satisfying value is found in ~10 evaluations.
  /// @throws std::invalid_argument as `countInBox`, or if @p n_req is 0.
    LevelOfAggregation levelOfAggregation(const gggs::CellIndex & cell, uint64_t n_req) const;

  /// Achieved spacing `(2*lambda+1) * R` in metres; +infinity when saturated.
    double achievedSpacing(const LevelOfAggregation & loa) const noexcept;

  /// @brief The @p p-th percentile (0..1) of achieved spacing over the
  ///        **occupied** count cells inside @p coarse, a grid at this level or
  ///        a coarser one.
  ///
  /// Only occupied cells (count > 0) vote: an unsurveyed part of a coarse grid
  /// says nothing about the resolution the surveyed part supports, whereas a
  /// gap *between* survey lines does (its neighbouring occupied cells see the
  /// gap in their boxes). Saturated cells vote +infinity, so a percentile that
  /// lands on one reads as "coarsest".
  /// @return nullopt when no occupied cell lies inside @p coarse.
  /// @throws std::invalid_argument if @p coarse is invalid, finer than this
  ///         level, if @p p is outside [0, 1], or if @p n_req is 0.
    std::optional < double > achievedSpacingPercentile(
    const gggs::GridIndex & coarse, double p, uint64_t n_req) const;

  /// One bin per GGGS level (0..20) plus a saturated bin at index 21: the
  /// number of occupied cells whose achieved spacing maps to that level
  /// (`levelNoFinerThan` over the full 0..20 range, no policy clamp).
    static constexpr std::size_t kLevelBins = 22;
    static constexpr std::size_t kSaturatedBin = 21;
    using LevelHistogram = std::array < uint32_t, kLevelBins >;

  /// @brief Achieved-level histogram of the occupied cells in one count tile,
  ///        computed once per tile for a given @p n_req and cached until the
  ///        tile changes. The plan's per-tile percentiles sum these, so a
  ///        survey is evaluated once rather than once per emitted tile.
  /// @throws std::invalid_argument if @p tile is absent or @p n_req is 0.
    const LevelHistogram & achievedLevelHistogram(
      const gggs::GridIndex & tile,
      uint64_t n_req) const;

    struct AchievedLevel
    {
    /// Level at the percentile, unclamped (0..20); meaningless when saturated.
      uint8_t level = 0;
    /// The percentile landed on saturated cells: read as "coarsest".
      bool saturated = false;
    };

  /// @brief The @p p-th percentile (0..1) of achieved level over the occupied
  ///        count cells inside @p coarse -- the histogram form of
  ///        `achievedSpacingPercentile`, exact for the level-quantised answer
  ///        and O(tiles inside) per query after the per-tile histograms exist.
  ///        Percentiles are of *spacing* (ascending), so p = 0.95 is the level
  ///        that 95 % of occupied cells achieve at or finer.
  /// @return nullopt when no occupied cell lies inside @p coarse.
  /// @throws as `achievedSpacingPercentile`.
    std::optional < AchievedLevel > achievedLevelPercentile(
    const gggs::GridIndex & coarse, double p, uint64_t n_req) const;

  /// @brief Whether count tile @p tile lies inside @p coarse (same or coarser level).
    static bool tileInside(const gggs::GridIndex & tile, const gggs::GridIndex & coarse);

  /// @brief The grid @p d_row rows north and @p d_column columns east of @p grid
  ///        at the same level (resolved geographically -- GridIndex has no
  ///        public row/column constructor). Invalid past the poles; at the
  ///        antimeridian the column wraps.
    static gggs::GridIndex neighbourGrid(const gggs::GridIndex & grid, int d_row, int d_column);

  /// @brief Add every count of @p other into this grid (saturating), and take
  ///        the per-tile maximum of the spread terms.
  /// @throws std::invalid_argument if the levels differ.
    void merge(const CountGrid & other);

  /// @brief Write the occupied tiles under @p dir as single-band UInt16
  ///        GeoTIFFs named `<level>_<row>_<col>.tif`, plus `spread_terms.txt`
  ///        (`<centre lat> <centre lon> <metres>` per line) and `level.txt`.
  ///
  /// The written state is the whole grid (not only tiles changed since the
  /// last save): a count grid is small and a partial write would be
  /// unmergeable. Creates @p dir as needed, and REFUSES a @p dir that already
  /// holds anything -- `mergeFrom` reads the whole directory, so a tile left by
  /// an earlier, larger survey would merge in as if this grid had counted it.
  /// @return The number of tiles written.
  /// @throws std::runtime_error if @p dir is not empty, or on any GDAL or
  ///         filesystem failure.
    std::size_t saveTo(const std::string & dir) const;

  /// @brief Merge the tiles found under @p dir into this grid (additive).
  /// @return The number of tiles read.
  /// @throws std::invalid_argument if the directory's `level.txt` names another
  ///         level; std::runtime_error on GDAL or filesystem failure.
    std::size_t mergeFrom(const std::string & dir);

  /// @brief The level recorded in @p dir's `level.txt`.
  /// @throws std::runtime_error if it is absent or unparsable.
    static uint8_t levelOf(const std::string & dir);

private:
    using Sat = std::vector < uint32_t >;  // (kEdge+1)^2 inclusive prefix sums, saturating

  /// Resident tile for @p grid, reloading from the spill or creating an empty
  /// tile (@p create) as needed; nullptr when absent and @p create is false.
  /// Touches the LRU and may evict another tile.
    Tile * fetch(const gggs::GridIndex & grid, bool create);
  /// The occupied-cell mask of @p grid (kEdge*kEdge bits), taken in one
  /// access so the caller can query other tiles without holding a reference.
    std::vector < bool > occupiedMask(const gggs::GridIndex & grid) const;
    const Sat & satFor(const gggs::GridIndex & grid) const;
    static uint32_t satSum(const Sat & sat, int r0, int c0, int r1, int c1);
    void invalidateSat(const gggs::GridIndex & grid) const;
    void checkCell(const gggs::CellIndex & cell) const;
    std::string tilePath(const gggs::GridIndex & grid) const;
    void touch(const gggs::GridIndex & grid);
  /// Evict coldest-first until `tiles_.size() + incoming <= resident_budget_`.
  /// No-op when unbounded.
    void trimResident(std::size_t incoming);

    gggs::Level level_;
    std::set < gggs::GridIndex > grids_;
  // Resident subset of `grids_`; mutable so a read can reload a spilled tile.
    mutable std::map < gggs::GridIndex, Tile > tiles_;
    mutable std::list < gggs::GridIndex > lru_;  // most recently used first
    mutable std::map < gggs::GridIndex, std::list < gggs::GridIndex > ::iterator > lru_pos_;
    std::string spill_dir_;
    std::size_t resident_budget_ = 0;  // 0 == unbounded
    mutable std::size_t resident_peak_ = 0;
    std::map < gggs::GridIndex, double > max_spread_term_;
    uint64_t total_ = 0;

  // Lazily-built summed-area tables, bounded LRU (a 3x3 stitch touches nine).
    static constexpr std::size_t kSatCacheSize = 16;
    mutable std::list < std::pair < gggs::GridIndex, Sat >> sat_cache_;

  // Per-tile achieved-level histograms, keyed by (tile, n_req); a tile's
  // entries are dropped when it or a neighbour changes (a neighbour's counts
  // reach into this tile's boxes).
    mutable std::map < std::pair < gggs::GridIndex, uint64_t >, LevelHistogram > histogram_cache_;
    void invalidateHistograms(const gggs::GridIndex & grid) const;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__COUNT_GRID_H_
