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

#ifndef CUBE_BATHYMETRY__LEVEL_PLAN_H_
#define CUBE_BATHYMETRY__LEVEL_PLAN_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cube_bathymetry/count_grid.h"
#include "marine_autonomy/gggs.h"
#include "marine_autonomy/gz4d_geo.h"
#include "marine_bathymetry_store/depth_adaptive_level.hpp"

/// @file
/// @brief The depth-adaptive level plan (cube_bathymetry#143): which GGGS
///        tiles, at which levels, a multi-level import estimates and stores.
///
/// **Two resolutions decide a tile's level.** The resolution the survey
/// *requires* comes from the depth ladder
/// (`marine_bathymetry_store::depthAdaptiveLevel`, uma#369: a cell of
/// `capture_distance_scale * |depth|`, clamped to `[coarsest, finest]`),
/// evaluated at the tile's *decision depth* -- a flier-guarded shallow
/// percentile of the **water depth under the transducer** (not the stored
/// ellipsoidal height; see `recon.h`), rolled up as the minimum over children. The resolution the
/// data *achieves* comes from Calder's level of aggregation over a count grid
/// (`CountGrid`, US Hydro 2019): the finest spacing at which every occupied
/// cell in the tile still gathers `n_req` observations, taken at a high
/// percentile over the tile. The tile's target is the **coarser** of the two.
/// Where required is finer than achieved, the survey has not yet collected the
/// data its depth calls for: a *coverage deficit* the plan reports.
///
/// **Parents stay alive under their children.** The plan is a quadtree
/// *prefix*, not a cut: descending from each touched grid at `coarsest_level`,
/// every grid on the way down is emitted, and a grid is refined into its
/// touched children only while its target is finer than its own level. So a
/// coarse tile keeps a complete estimate under the finer tiles that refine part
/// of it -- it is the LOD level above them and the native tile for the
/// unrefined remainder (the store already holds overlapping native levels, and
/// uma ADR-0011's pyramid skips a parent slot that holds a native tile).
///
/// **Touched sets are per level.** A tile at level L is touched when some
/// sounding's influence disc, floored at that level's node spacing (the same
/// floor `Parameters::influenceRadius` applies and `GeoMapSheet`'s selection
/// window adds), intersects it. Built from the count grid's occupied cells and
/// the per-count-tile maximum spread term, so at a single level it is the set
/// of grids today's fixed-level import selects, near-seam neighbours included.
/// The emitted set at a level is the touched set restricted to the descent, and
/// it is what the multi-level accumulator admits at that level.

namespace cube
{

/// @brief Everything the plan's decisions depend on. Recorded in the plan's
///        JSON and in the build fingerprint (ADR-0003 `tiling.policy`).
  struct LevelPlanPolicy
  {
  /// The uma#369 depth ladder: scale, coarsest and finest level.
    marine_bathymetry_store::DepthAdaptiveLevelPolicy depth;

  /// GGGS level of the count grid; must satisfy `finest <= count_level <= 20`.
  /// The achieved path can only reach this level (achieved spacing is
  /// `(2*lambda+1)*R`), so it defaults to the ladder's fine end.
    uint8_t count_level = 14;

  /// Minimum observations per estimate node (Calder's n_req before the
  /// blunder allowance).
    uint32_t min_obs_per_node = 5;

  /// Fraction of raw soundings assumed to be blunders; inflates n_req.
    double blunder_allowance = 0.2;

  /// Percentile (0..1) of a level-14 grid's shallowest water depths under the
  /// transducer taken as its decision depth -- the flier guard. Applied by recon when it builds the
  /// decision depths; recorded here so the plan states what it rested on.
  /// Must be in (0, 1]. `recon.h`'s `DepthHistogram` honours any percentile at
  /// any density (the bounded "shallowest 64" reservoir it replaced could not,
  /// which is why this once carried a `kMaxDecisionDepthPercentile` cap).
    double decision_depth_percentile = 0.02;

  /// Percentile (0..1) of achieved spacing over a tile's occupied cells that
  /// decides the tile's achieved level (Calder uses 0.95-0.99).
    double achieved_percentile = 0.95;

  /// n_req after the blunder allowance: `ceil(min_obs_per_node * (1 + allowance))`.
    uint64_t requiredObservations() const;

  /// @throws std::invalid_argument naming the first violated constraint:
  ///   finest_level <= 14 (a tile finer than the level-14 survey-index footprint
  ///   breaks ADR-0002's dirty-set guarantee), coarsest_level <= finest_level,
  ///   finest_level <= count_level <= 20, a finite positive scale,
  ///   min_obs_per_node >= 1, allowance >= 0, percentiles in [0, 1].
    void validate() const;
  };

/// @brief One emitted tile and the two answers that decided its refinement.
  struct PlannedTile
  {
    gggs::GridIndex index;
  /// Level the depth ladder asks for at this tile's decision depth.
    uint8_t required_level = 0;
  /// Level the count grid supports over this tile (coarsest when saturated).
    uint8_t achieved_level = 0;
  /// The tile's decision depth: **water depth under the transducer**, metres,
  /// negative-down -- the shallow percentile of the recon's per-level-14-grid
  /// depth histogram, rolled up as the minimum over touched children. NOT the
  /// stored ellipsoidal height the tile's values carry (uma ADR-0002 D4): the
  /// ladder's argument is a footprint argument, and a footprint scales with
  /// range below the transducer, not with height above the ellipsoid.
  /// Transducer draft is deliberately ignored (sub-metre against a
  /// factor-of-two ladder).
    float decision_depth = 0.0f;
  /// Whether the descent continued into this tile's children.
    bool refined = false;
  /// Surveyed ground under this tile, square metres: the occupied cells of the
  /// recon count grid that fall inside it, times a count cell's area. This is
  /// ground the survey actually ensonified -- NOT the tile's footprint, which
  /// for a level-8 parent over one survey line is three orders of magnitude
  /// larger (cube#143 dry-run review).
    double ground_m2 = 0.0;

  /// The coarser of required and achieved, in level numbers.
    uint8_t targetLevel() const
    {
      return required_level < achieved_level ? required_level : achieved_level;
    }
  /// Required finer than achieved: the data does not yet support what the
  /// depth calls for.
    bool coverageDeficit() const {return required_level > achieved_level;}
  };

/// @brief The finest GGGS level whose cell is no finer than @p spacing_m,
///        clamped to `[coarsest, finest]`.
///
/// `gggs::Level::fromCellSize` returns the coarsest level at-or-**finer** than
/// its argument (the unsafe direction for an *achieved* resolution), so this
/// takes the coarser neighbour unless that level's cell equals @p spacing_m
/// exactly. +infinity (a saturated level of aggregation) maps to @p coarsest.
  uint8_t levelNoFinerThan(double spacing_m, uint8_t coarsest, uint8_t finest);

  class LevelPlan
  {
public:
    LevelPlan() = default;

    const LevelPlanPolicy & policy() const noexcept {return policy_;}

  /// Emitted tiles keyed by grid, every level.
    const std::map < gggs::GridIndex, PlannedTile > & tiles() const noexcept {return tiles_;}

  /// Whether @p grid is an emitted tile.
    bool isEmitted(const gggs::GridIndex & grid) const;

  /// @brief Whether @p grid itself, or any ancestor of it, is an emitted tile:
  ///        whether a sounding landing in @p grid reaches this plan AT ALL.
  ///
  /// Parents stay alive, so a covered grid always has an emitted ancestor at
  /// the coarsest level; an UNCOVERED one is ground the plan never emitted, and
  /// every sounding over it would be admitted by no accumulator. That is what
  /// makes a plan from another survey (or a re-run over changed bags) a
  /// silently-incomplete store rather than a refusal (cube#143 triage).
    bool covers(const gggs::GridIndex & grid) const;

  /// Whether @p grid is in the touched set at its level (a superset of the
  /// emitted set: touched but not emitted means the descent stopped above it).
    bool isTouched(const gggs::GridIndex & grid) const;

  /// The emitted tiles at @p level, in grid order.
    std::vector < gggs::GridIndex > tilesAtLevel(uint8_t level) const;

  /// Levels holding at least one emitted tile.
    std::set < uint8_t > levels() const;

  /// Every emitted tile containing the geographic position, one per level at
  /// most (the ancestor chain of the position's grid at each emitted level).
    std::set < gggs::GridIndex > tilesContaining(double latitude, double longitude) const;

  /// Levels at which some emitted tile intersects @p bounds.
    std::set < uint8_t > levelsIntersecting(const gz4d::BoundsDegrees & bounds) const;

  /// Emitted tiles whose required level is finer than their achieved level.
    std::vector < gggs::GridIndex > coverageDeficit() const;

  /// @brief Surveyed ground, square metres: every occupied cell of the recon
  ///        count grid, times a count cell's area. The denominator of the
  ///        report's storage multiplier, and what its area columns sum to.
    double surveyedGroundM2() const noexcept {return surveyed_ground_m2_;}

  /// @brief Ground, square metres, for which @p grid is the FINEST emitted
  ///        tile -- its own ground minus its emitted children's. This is the
  ///        ground the tile actually stores; a refined parent keeps whatever
  ///        its children did not take (a child is emitted only where the
  ///        descent reached it). Zero for a grid that is not emitted.
    double nativeGroundM2(const gggs::GridIndex & grid) const;

  /// @brief Canonical JSON: fixed key order, tiles sorted by (level, row,
  ///        column), no whitespace, numbers in a fixed format. Two plans with
  ///        the same policy and tiles serialise to the same bytes regardless of
  ///        construction order.
    std::string toJson() const;

  /// @throws std::runtime_error on malformed input; std::invalid_argument if
  ///         the recorded policy fails validation.
    static LevelPlan fromJson(const std::string & json);

  /// @brief Operator report: per-level tile counts, SURVEYED GROUND (covered
  ///        and natively stored, from the count grid's occupied cells) and
  ///        storage (dense and at @p observed_bytes_per_tile), the coverage
  ///        deficit, the ground whose finest native level is coarser than 10,
  ///        and the estimate-count multiplier that parents-alive costs at
  ///        import time.
    std::string report(double observed_bytes_per_tile = 2.42e6) const;

private:
    friend LevelPlan levelPlanFor(
      const CountGrid &, const std::map < gggs::GridIndex, float > &, const LevelPlanPolicy &);

    LevelPlanPolicy policy_;
    double surveyed_ground_m2_ = 0.0;
    std::map < gggs::GridIndex, PlannedTile > tiles_;
    std::map < uint8_t, std::set < gggs::GridIndex >> touched_;
  };

/// @brief Build the plan.
///
/// @param counts The recon count grid (level `policy.count_level`), with its
///        per-tile maximum spread terms.
/// @param decision_depth_by_l14_grid The decision depth of every level-14 grid
///        the survey touched: water depth under the transducer, negative-down
///        (`ReconCollector::decisionDepths()`). Grids absent here contribute no
///        depth; a grid with no depth anywhere beneath it is never emitted.
/// @throws std::invalid_argument if the policy fails validation or the count
///         grid's level differs from `policy.count_level`.
  LevelPlan levelPlanFor(
    const CountGrid & counts,
    const std::map < gggs::GridIndex, float > &decision_depth_by_l14_grid,
    const LevelPlanPolicy & policy);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__LEVEL_PLAN_H_
