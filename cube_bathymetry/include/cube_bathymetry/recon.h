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

#ifndef CUBE_BATHYMETRY__RECON_H_
#define CUBE_BATHYMETRY__RECON_H_

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cube_bathymetry/count_grid.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/level_plan.h"
#include "cube_bathymetry/parameters.h"

/// @file
/// @brief The recon pass of a depth-adaptive import (cube_bathymetry#143):
///        counts, decision depths, and the sounding spill that phase two replays.
///
/// A depth-adaptive import runs the bags twice. The first pass projects and
/// georeferences exactly as a fixed-level import does but, instead of
/// estimating, hands each ping's soundings to a `ReconCollector`, which
///
/// - counts every sounding into the `CountGrid` (with its maximum spread
///   radius, so the level plan can expand occupied cells by the reach the
///   soundings actually had),
/// - keeps, per level-14 grid, a depth histogram so the grid's **decision
///   depth** -- a low percentile of its shallowest soundings, the flier
///   guard -- can be read off at the end at any survey density. What is
///   histogrammed is the **water depth under the transducer**
///   (`-|sonar_relative_position.z|`, negative-down), NOT the sounding's
///   stored depth: stored depths are WGS84 ellipsoidal heights (uma ADR-0002
///   D4) and the geoid offset (~28 m at the UNH pier) would coarsen every
///   tile by one to two levels, since the ladder's argument is a footprint
///   argument. Transducer draft is deliberately ignored (sub-metre against a
///   factor-of-two ladder), and
/// - spills the projected `GeoSounding` in full to **one chronological scratch
///   file**, so the expensive projection/TF work runs once and phase two
///   replays the spill front to back into the per-level accumulators.
///
/// The spill is a single file replayed in write order on purpose. CUBE's
/// sliding-median pre-filter is order-dependent, so any partition of the
/// soundings (an earlier revision spilled one file per level-10 grid) would
/// present a tile coarser than the partition with the pings interleaved
/// differently than the fixed-level path saw them, and the depth-adaptive
/// store would no longer be byte-identical to a fixed import of the same bags.
/// Replaying one file front to back hands phase two exactly the order phase one
/// saw, so the equivalence holds by construction rather than by argument.
///
/// This is Calder's CHRT two-pass structure (Calder & Rice, Computers &
/// Geosciences 2017) with the level of aggregation from his 2019 paper as the
/// pass-one product; the spill is the production version's "re-add every
/// observation" done from scratch files instead of from the bags.

namespace cube
{

/// @brief One spilled sounding, as written to and read from the scratch files.
///        Plain data; native layout (scratch is single-machine and deleted
///        after the run).
  struct SpilledSounding
  {
    double latitude = 0.0;
    double longitude = 0.0;
    float depth = 0.0f;
    float vertical_error = 0.0f;
    float horizontal_error = 0.0f;
    float intensity = 0.0f;
    float beam_angle = 0.0f;
    float slant_range = 0.0f;
    double sonar_relative_x = 0.0;
    double sonar_relative_y = 0.0;
    double sonar_relative_z = 0.0;

    static SpilledSounding from(const GeoSounding & s);
    GeoSounding toGeoSounding() const;
  };

/// @brief Per-level-14-grid depth histogram: what the decision depth's
///        percentile is read from.
///
/// A survey puts ~200 k soundings into one level-14 grid, so the decision
/// depth's rank (p = 0.02 -> ~4,000) lies far beyond any bounded set of
/// "the shallowest few" -- an earlier revision kept the 64 shallowest and
/// returned the 64th, which on a real M3 bag read 9 m shallower than anything
/// CUBE accepted, because M3 water-column fliers run to many hundreds per grid.
/// A histogram honours the percentile at any density in bounded memory: the
/// decision only has to resolve a level boundary of the depth ladder, and those
/// are metres apart, so a 0.25 m bin is far finer than the decision needs.
///
/// Bins are sparse (`floor(depth / width)` -> count), so only the depths a grid
/// actually saw cost anything. Should a grid's depth spread exceed `kMaxBins`
/// bins -- a flier at the surface over deep water -- the width doubles and the
/// bins merge, which bounds the memory per grid unconditionally and only
/// coarsens the answer where the spread is already large.
  struct DepthHistogram
  {
  /// Starting bin width, metres. The finest depth-ladder level boundary is
  /// ~1 m of depth apart (cell = scale x depth), so this resolves every
  /// boundary with room to spare.
    static constexpr double kInitialBinWidth = 0.25;
  /// Bins retained per grid before the width doubles (<= ~50 kB per grid).
    static constexpr std::size_t kMaxBins = 1024;

  /// Total soundings seen.
    uint64_t count = 0;
  /// Current bin width in metres; doubles as needed (see kMaxBins).
    double bin_width = kInitialBinWidth;
  /// Occupied bins: `floor(depth / bin_width)` -> soundings in that bin.
  /// Ordered, so the walk from the shallowest (largest key) is a plain
  /// reverse iteration.
    std::map < int32_t, uint64_t > bins;

  /// Count one sounding's depth. Negative-down, and in the recon that is the
  /// **water depth under the transducer**, not the stored ellipsoidal height
  /// (see the file comment and `ReconCollector::add`). Non-finite is ignored.
    void add(float depth);

  /// @brief The `max(1, ceil(p * count))`-th shallowest depth, to within one
  ///        bin: the SHALLOW edge of the bin the rank falls in, so the answer
  ///        lies in `(truth, truth + bin_width]` -- shallow by at most one bin,
  ///        never deep. That is the safe direction: a shallower decision depth
  ///        asks for a FINER tile. NaN when empty.
    float decisionDepth(double percentile) const;
  };

  class ReconCollector
  {
public:
  /// Name of the single chronological spill file inside the scratch dir.
    static constexpr const char * kSpillFilename = "soundings.spill";

  /// @brief Construct a collector.
  /// @param policy Validated on construction.
  /// @param scratch_dir Directory for the spill file and the count-grid tile
  ///        spill (created as needed); empty disables both (recon-only runs
  ///        that never replay, and that hold every count tile in RAM).
  /// @param count_resident_tiles Resident count-tile budget when a scratch dir
  ///        is given (`CountGrid::setSpillDir`); ~1.8 MB per tile at level 14.
  /// @throws std::invalid_argument on a bad policy or a budget below
  ///         `CountGrid::kMinResidentTiles`; std::runtime_error if the scratch
  ///         dir cannot be created, or if it already exists and is not empty
  ///         (an orphaned scratch dir from a killed run: appending to its spill
  ///         would replay another run's soundings).
    ReconCollector(
      const LevelPlanPolicy & policy, std::string scratch_dir,
      std::size_t count_resident_tiles = CountGrid::kDefaultResidentTiles);
    ~ReconCollector();

  /// @brief Count, histogram and spill one ping's soundings.
  /// @param parameters The estimator parameters the import will run with;
  ///        supplies `maxSpreadRadius`.
  /// @throws std::runtime_error if a spill write fails.
    void add(const std::vector < GeoSounding > &soundings, const Parameters & parameters);

    const CountGrid & counts() const noexcept {return counts_;}
    uint64_t soundingsSeen() const noexcept {return counts_.total();}
    uint64_t soundingsSpilled() const noexcept {return spilled_;}

  /// @brief Soundings counted but NOT histogrammed, because their sonar-frame
  ///        `z` was non-finite or exactly zero and so carried no water depth.
  ///
  /// A producer that georeferences without carrying `sonar_relative_position`
  /// through leaves every `z` at 0, and a histogram of zeros answers 0 m of
  /// water -- a silently wrong plan that asks for the finest level everywhere
  /// (it did, before this was plumbed: cube#143 dry-run review round 2). The
  /// caller reports this, and refuses a plan where it accounts for every
  /// sounding.
    uint64_t soundingsWithoutRange() const noexcept {return without_range_;}

  /// Decision depth of every level-14 grid that received a sounding: water
  /// depth under the transducer, negative-down.
    std::map < gggs::GridIndex, float > decisionDepths() const;

  /// The plan for what was collected: `levelPlanFor(counts, decisionDepths, policy)`.
    LevelPlan plan() const;

  /// @brief Replay the whole spill front to back -- the chronological order
  ///        phase one saw the soundings in, which is what makes a
  ///        single-level plan byte-identical to a fixed-level import.
  ///        Closes the file for writing first (a replay after `add` is the
  ///        normal phase-one -> phase-two hand-off); re-runnable.
  ///        Does nothing when nothing was spilled.
  /// @return The number of soundings replayed. The caller MUST compare it with
  ///         `soundingsSpilled()`: a record lost at a record boundary (a
  ///         write that never reached the disk) is invisible to the stream
  ///         state, and replaying a short spill would silently import a
  ///         truncated survey.
  /// @throws std::runtime_error if the spill could not be flushed or closed
  ///         (a disk-full at the last buffered flush loses the tail of the
  ///         survey), if it cannot be read, or if it ends in a partial record.
    uint64_t forEachSpilled(const std::function < void(const GeoSounding &) > &fn);

  /// Bytes one spilled sounding occupies on disk.
    static constexpr std::size_t kBytesPerSpilledSounding = sizeof(SpilledSounding);

  /// @brief Free space check for the spill: throws std::runtime_error naming
  ///        the shortfall when @p dir has fewer than @p needed_bytes free.
    static void requireFreeSpace(const std::string & dir, uint64_t needed_bytes);

  /// Delete the spill file, the count-tile spill and the scratch dir (also
  /// done by the destructor).
    void cleanup();

  /// Path of the sounding spill file; empty when the spill is disabled.
    std::string spillPath() const;

  /// Subdirectory of the scratch dir that holds the spilled count tiles.
    static constexpr const char * kCountSpillSubdir = "counts";

private:
    LevelPlanPolicy policy_;
    std::string scratch_dir_;
    CountGrid counts_;
    std::map < gggs::GridIndex, DepthHistogram > histograms_;
    std::unique_ptr < std::ofstream > spill_out_;
    uint64_t spilled_ = 0;
    uint64_t without_range_ = 0;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__RECON_H_
