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

#ifndef COVERAGE_ASSESSMENT__SOURCE_ACCUMULATOR_H_
#define COVERAGE_ASSESSMENT__SOURCE_ACCUMULATOR_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "coverage_assessment/polygon_raster.h"
#include "grid_map_core/grid_map_core.hpp"

namespace coverage_assessment
{

/// A 2-D affine map  p' = [a b; c d] p + (tx, ty).  The accumulator takes the
/// horizontal placement of the assessment raster in a source grid's frame as
/// one of these, so it never sees TF: the wrapper composes the transform once
/// per message and passes the result.
  struct Affine2
  {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double tx = 0.0;
    double ty = 0.0;

    Point2 apply(const Point2 & p) const
    {
      return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty};
    }

  /// The inverse map, or false when the matrix is singular or not finite.
    bool inverse(Affine2 * out) const;
  };

/// What the accumulator holds for one lattice cell. NaN means "never seen a
/// finite value for that field".
  struct HeldCell
  {
    float elevation = std::numeric_limits < float > ::quiet_NaN();
    float uncertainty = std::numeric_limits < float > ::quiet_NaN();
  };

  enum class AddStatus
  {
  /// The message was resampled into the accumulator.
    kAdded,
  /// The message has no elevation layer; nothing was stored.
    kMissingElevationLayer,
  /// Unusable geometry (non-positive or non-finite resolution or extent, a
  /// non-invertible or non-finite affine, or an implausibly large footprint);
  /// nothing was stored.
    kBadGeometry
  };

  struct AddResult
  {
    AddStatus status = AddStatus::kBadGeometry;
  /// False when the message has no uncertainty layer: its cells can cover but
  /// never meet a standard. The wrapper warns once per source.
    bool has_uncertainty_layer = false;
  /// Lattice cells whose centre fell inside the message's extent.
    std::size_t cells_in_message = 0;
  };

/// The latched union over time of one source's grids, on the assessment
/// lattice (the raster frame's, so the same cell indices as polygon_raster).
///
/// Each add() resamples PULL-style: for every lattice cell whose centre, mapped
/// into the message's frame, falls inside the message, take the message cell
/// containing that point. A push (message cell -> lattice) would alias into
/// pinholes, because each CUBE message sizes its geometry from its own extent,
/// so successive messages are not on one lattice.
///
/// Never erases, per field: a finite elevation overwrites the held elevation, a
/// finite uncertainty overwrites the held uncertainty, and a NaN in either
/// never clears that field. A finite elevation with NaN uncertainty (the sim
/// produces it before TF settles) therefore cannot wipe a good uncertainty.
///
/// Storage is paged in 64 x 64 blocks of two floats (about 8 B per cell plus the
/// block table); memory grows with the surveyed footprint only.
  class SourceAccumulator
  {
public:
  /// `cell_size` is the lattice spacing in metres (positive and finite).
    explicit SourceAccumulator(double cell_size);

    double cellSize() const {return cell_size_;}

  /// Fold one message in. `raster_to_source` maps raster-frame metres into the
  /// message's own frame (horizontal only; elevation is an ellipsoid height and
  /// frame independent, so it is never transformed).
    AddResult add(
      const grid_map::GridMap & message, const Affine2 & raster_to_source,
      const std::string & elevation_layer = "elevation",
      const std::string & uncertainty_layer = "uncertainty");

  /// The held values of lattice cell (i, j); both NaN when never seen.
    HeldCell get(int64_t i, int64_t j) const;

  /// Lattice cells holding a finite elevation.
    std::size_t elevationCellCount() const {return elevation_cells_;}

  /// Allocated 64 x 64 blocks, for the memory log line.
    std::size_t blockCount() const {return blocks_.size();}

  /// Approximate bytes held by the blocks.
    std::size_t memoryBytes() const;

  /// Forget everything (the reset path).
    void clear();

  /// Largest lattice footprint one message may span; beyond it add() returns
  /// kBadGeometry rather than looping over a bogus affine.
    static constexpr double kMaxFootprintCells = 1.0e8;

private:
    static constexpr int64_t kBlock = 64;

    struct Key
    {
      int64_t bi;
      int64_t bj;
      bool operator == (const Key & o) const {return bi == o.bi && bj == o.bj;}
    };

    struct KeyHash
    {
      std::size_t operator()(const Key & k) const
    {
        return std::hash < uint64_t > ()(
        static_cast < uint64_t > (k.bi) * 0x9E3779B97F4A7C15ULL ^ static_cast < uint64_t > (k.bj));
      }
    };

    struct Block
    {
      std::vector < float > elevation = std::vector < float > (
        kBlock * kBlock, std::numeric_limits < float > ::quiet_NaN());
      std::vector < float > uncertainty = std::vector < float > (
        kBlock * kBlock, std::numeric_limits < float > ::quiet_NaN());
    };

    Block & blockFor(int64_t i, int64_t j);

    double cell_size_;
    std::size_t elevation_cells_ = 0;
    std::unordered_map < Key, Block, KeyHash > blocks_;
  };

}  // namespace coverage_assessment

#endif  // COVERAGE_ASSESSMENT__SOURCE_ACCUMULATOR_H_
