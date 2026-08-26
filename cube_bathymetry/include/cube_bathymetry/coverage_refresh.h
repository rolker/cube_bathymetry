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

#ifndef CUBE_BATHYMETRY__COVERAGE_REFRESH_H_
#define CUBE_BATHYMETRY__COVERAGE_REFRESH_H_

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "marine_autonomy/gggs.h"

namespace cube
{

/// @brief Whole-tile refresh policy for the incremental coverage push (#112).
///
/// The live coverage stream is best-effort, and once it carries sub-window
/// PATCHES a lost message is undiscoverable by the consumer: the producer bumps
/// the tile's catalog version on a patch exactly as on a whole tile, so a
/// consumer that records the message stamp as its held version matches the
/// catalog and never re-requests. CAMP's SonarLiveTile does precisely that
/// (camp#121), so a dropped patch would leave a permanent, invisible gap in the
/// operator's coverage display while anti-entropy reported convergence.
///
/// This tracker is the heal, and its whole point is that it does NOT depend on
/// the consumer noticing anything: a tile that has received a patch is re-sent
/// WHOLE within @ref interval seconds, BUDGET PERMITTING, so a lost patch is a
/// gap of bounded duration instead of a permanent one.
///
/// "Budget permitting" is not a hedge. The drain clears at most
/// `tiles_per_cycle` tiles per tick, so at the defaults it heals 24 quiet tiles
/// per 60 s interval; a line-end turn that quiets more than that at once
/// degrades the actual latency, oldest-debt-first, until the backlog clears.
/// No tile is starved -- the ordering guarantees that -- but the interval is a
/// target under load, not a bound. @ref backlogExceedsBudget lets the caller
/// say so out loud rather than let the operator infer a guarantee that is not
/// being met.
///
/// Node-free by design (plain seconds, no rclcpp) so the policy is unit-tested
/// directly rather than through the node, whose publish path has no test
/// harness (see test_anti_entropy_disk_serve.cpp's known limitation).
  class CoverageRefreshTracker
  {
public:
  /// @param interval_s Seconds a patched tile may go without a whole re-send.
  ///        0 disables the refresh: a lost patch then never heals, which is
  ///        only safe against a consumer that tracks patch possession itself.
  /// @param tiles_per_cycle Maximum quiet tiles healed per publish cycle, so
  ///        the heal can never become the burst it exists to prevent.
    void configure(double interval_s, std::size_t tiles_per_cycle)
    {
      interval_s_ = interval_s;
      tiles_per_cycle_ = tiles_per_cycle;
    }

  /// Does this tile owe a whole-tile send? Only a tile holding unconfirmed
  /// patches can: one that has only ever gone out whole is already healed.
    bool refreshDue(const gggs::GridIndex & index, double now_s) const
    {
      const auto it = last_full_.find(index);
      if (it == last_full_.end()) {
        // NEVER SENT WHOLE. Always due, and deliberately gated on neither the
        // interval nor an outstanding patch: a consumer cannot apply a patch
        // to a tile it has never received. A tile's FIRST message must be the
        // whole tile, or one first touched on the last ping of a line reaches
        // the operator as a patch over nothing -- and, since the catalog is
        // bumped only on a whole send, carries no catalog version for the
        // consumer to notice it is missing.
        return true;
      }
      if (interval_s_ <= 0.0 || patched_.count(index) == 0) {
        // Periodic heal disabled, or nothing outstanding to heal.
        return false;
      }
      return now_s - it->second >= interval_s_;
    }

  /// Record what actually reached the wire. A whole tile discharges the debt;
  /// a patch incurs it. Called with what was PUBLISHED, never with what was
  /// merely intended -- a tile that produced no message heals nothing.
    void notePublished(const gggs::GridIndex & index, double now_s, bool whole)
    {
      if (whole) {
        last_full_[index] = now_s;
        patched_.erase(index);
      } else {
        patched_.insert(index);
      }
    }

  /// Tiles to re-send whole this cycle: those owing a refresh that were not
  /// already published, oldest debt first (so a busy neighbour cannot starve
  /// one), capped at tiles_per_cycle.
  ///
  /// @tparam ResidentFn Any callable (const gggs::GridIndex &) -> bool. A
  ///         template rather than a std::function: no allocation on a path the
  ///         publish cycle takes every time, and no indirect call.
  /// @param resident Whether a tile can still be quantized from memory. A tile
  ///        that has left RAM cannot pay its debt here, so it is dropped from
  ///        the set and counted in @p dropped rather than retained forever --
  ///        the caller surfaces that, because the consumer keeps whatever gap
  ///        it has.
    template < typename ResidentFn >
    std::vector < gggs::GridIndex > dueForRefresh(
    const std::set < gggs::GridIndex > &published_this_cycle,
    double now_s,
    ResidentFn resident,
    std::size_t * dropped = nullptr)
  {
    if (dropped) {
        *dropped = 0;
    }
    if (tiles_per_cycle_ == 0 || interval_s_ <= 0.0 || patched_.empty()) {
        return {};
    }

    std::vector < std::pair < double, gggs::GridIndex >> due;
    std::vector < gggs::GridIndex > gone;
    for (const auto & index : patched_) {
        if (published_this_cycle.count(index) != 0 || !refreshDue(index, now_s)) {
          continue;
        }
        if (!resident(index)) {
          gone.push_back(index);
          continue;
        }
        const auto it = last_full_.find(index);
      // A tile never sent whole sorts first: its debt is the oldest there is.
        due.emplace_back(
        it == last_full_.end() ? -std::numeric_limits < double > ::infinity() : it->second,
        index);
    }
    for (const auto & index : gone) {
        patched_.erase(index);
        last_full_.erase(index);
    }
    if (dropped) {
        *dropped = gone.size();
    }

    std::sort(due.begin(), due.end(),
      [] (const auto & a, const auto & b) {return a.first < b.first;});
    if (due.size() > tiles_per_cycle_) {
        due.resize(tiles_per_cycle_);
    }
    std::vector < gggs::GridIndex > out;
    out.reserve(due.size());
    for (const auto & entry : due) {
        out.push_back(entry.second);
    }
      return out;
    }

  /// Is the outstanding debt larger than the drain can clear in one interval?
  ///
  /// @param cycles_per_interval How many drain ticks fit in one interval.
  /// The caller knows the tick period; the tracker deliberately does not.
  /// True means the stated heal latency is NOT currently being met, which is
  /// worth an operator-visible warning: the coverage they are looking at may
  /// carry gaps older than the interval implies.
    bool backlogExceedsBudget(double cycles_per_interval) const
    {
      if (tiles_per_cycle_ == 0 || interval_s_ <= 0.0 || cycles_per_interval <= 0.0) {
        return false;  // the heal is off; there is no latency to fail to meet
      }
      const double clearable =
        static_cast < double > (tiles_per_cycle_) * cycles_per_interval;
      return static_cast < double > (patched_.size()) > clearable;
    }

  /// Tiles currently holding unconfirmed patches.
    std::size_t owedCount() const {return patched_.size();}

  /// Forget a tile entirely (it is gone, and its debt with it).
    void forget(const gggs::GridIndex & index)
    {
      patched_.erase(index);
      last_full_.erase(index);
    }

  /// Drop all state. The tracker describes ONE sheet: carried across a
  /// reconfigure it would claim tiles had been sent whole that the new sheet
  /// has never sent at all, suppressing the heal for exactly the tiles a
  /// restart is most likely to have left a consumer stale on.
    void clear()
    {
      last_full_.clear();
      patched_.clear();
    }

private:
    std::map < gggs::GridIndex, double > last_full_;
    std::set < gggs::GridIndex > patched_;
    double interval_s_ = 60.0;
    std::size_t tiles_per_cycle_ = 2;
  };

/// Is a published tile whole? Decided from the window itself, which is what the
/// consumer sees, rather than from a flag the producer could forget to set.
  inline bool isWholeTileWindow(
    std::uint16_t window_col, std::uint16_t window_row,
    std::uint16_t window_width, std::uint16_t window_height,
    std::uint16_t width, std::uint16_t height)
  {
    return window_col == 0 && window_row == 0 &&
           window_width == width && window_height == height;
  }

}  // namespace cube

#endif  // CUBE_BATHYMETRY__COVERAGE_REFRESH_H_
