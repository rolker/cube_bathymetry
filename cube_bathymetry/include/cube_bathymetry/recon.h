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
/// - keeps, per level-14 grid, the 64 shallowest soundings so the grid's
///   **decision depth** -- a low percentile of its shallowest, the flier
///   guard -- can be read off at the end, and
/// - spills the projected `GeoSounding` in full to one scratch file per
///   level-10 grid, so the expensive projection/TF work runs once and phase
///   two replays the spill grid by grid into the per-level accumulators.
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

/// @brief Per-level-14-grid reservoir of the shallowest soundings.
  struct ShallowReservoir
  {
    static constexpr std::size_t kCapacity = 64;
  /// Total soundings seen (not only the retained ones).
    uint64_t count = 0;
  /// The shallowest `<= kCapacity` depths seen, negative-down, so the
  /// shallowest is the largest value; kept sorted descending.
    std::vector < float > shallowest;

    void add(float depth);

  /// The `max(1, ceil(p * count))`-th shallowest depth, capped at the
  /// kCapacity-th -- the 2nd percentile exactly for `count <= 3200` at
  /// p = 0.02, and "at least 64 fliers deep" beyond. NaN when empty.
    float decisionDepth(double percentile) const;
  };

  class ReconCollector
  {
public:
  /// GGGS level of the spill partition (~870 m grids).
    static constexpr uint8_t kSpillLevel = 10;

  /// @brief Construct a collector.
  /// @param policy Validated on construction.
  /// @param scratch_dir Directory for the spill files (created as needed);
  ///        empty disables the spill (recon-only runs that never replay).
  /// @throws std::invalid_argument on a bad policy; std::runtime_error if the
  ///         scratch dir cannot be created.
    ReconCollector(const LevelPlanPolicy & policy, std::string scratch_dir);
    ~ReconCollector();

  /// @brief Count, reservoir and spill one ping's soundings.
  /// @param parameters The estimator parameters the import will run with;
  ///        supplies `maxSpreadRadius`.
  /// @throws std::runtime_error if a spill write fails.
    void add(const std::vector < GeoSounding > &soundings, const Parameters & parameters);

    const CountGrid & counts() const noexcept {return counts_;}
    uint64_t soundingsSeen() const noexcept {return counts_.total();}
    uint64_t soundingsSpilled() const noexcept {return spilled_;}

  /// Decision depth of every level-14 grid that received a sounding.
    std::map < gggs::GridIndex, float > decisionDepths() const;

  /// The plan for what was collected: `levelPlanFor(counts, decisionDepths, policy)`.
    LevelPlan plan() const;

  /// Spill files written so far, keyed by level-10 grid.
    std::vector < gggs::GridIndex > spilledGrids() const;

  /// @brief Replay the spill of one level-10 grid, in the order it was written.
  ///        Closes the file for writing first (a replay after `add` is the
  ///        normal phase-one -> phase-two hand-off).
  /// @throws std::runtime_error if the file cannot be read.
    void forEachSpilled(
      const gggs::GridIndex & spill_grid,
      const std::function < void(const GeoSounding &) > &fn);

  /// Bytes one spilled sounding occupies on disk.
    static constexpr std::size_t kBytesPerSpilledSounding = sizeof(SpilledSounding);

  /// @brief Free space check for the spill: throws std::runtime_error naming
  ///        the shortfall when @p dir has fewer than @p needed_bytes free.
    static void requireFreeSpace(const std::string & dir, uint64_t needed_bytes);

  /// Delete the spill files and the scratch dir (also done by the destructor).
    void cleanup();

private:
    LevelPlanPolicy policy_;
    std::string scratch_dir_;
    CountGrid counts_;
    std::map < gggs::GridIndex, ShallowReservoir > reservoirs_;
    std::map < gggs::GridIndex, std::unique_ptr < std::ofstream >> spill_out_;
    uint64_t spilled_ = 0;

    std::string spillPath(const gggs::GridIndex & spill_grid) const;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__RECON_H_
