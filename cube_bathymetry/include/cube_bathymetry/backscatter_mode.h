// Copyright 2026 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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


#ifndef CUBE_BATHYMETRY__BACKSCATTER_MODE_H_
#define CUBE_BATHYMETRY__BACKSCATTER_MODE_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cube_bathymetry/backscatter_table.h"

/// @file
/// @brief Per-ping pulse-mode resolution against a backscatter table
///        (cube_bathymetry#172): SonarInfo history -> mode key ->
///        `correction_idx`, with the declared-state check, the fallback policy
///        and the counters the import report and the live node print.
///
/// Message-free on purpose: the core library must not depend on
/// `marine_interfaces`. `sonar_info_curve.h` (its own target) converts a
/// `SonarInfo` message into the `SonarInfoState` below.

namespace cube
{

/// The fields of one SonarInfo the mode key and the declared-state check
/// read, stamped with its `header.stamp`.
  struct SonarInfoState
  {
    int64_t stamp_ns = 0;
    std::vector < float > pulse_lengths_s;
    std::vector < float > bandwidths_hz;
    std::vector < uint8_t > tx_signal_types;
    DeclaredTvgState declared;
  };

/// Why a ping did not get a table entry.
  enum class ModeResolution
  {
  /// A table entry corrects it.
    Resolved,
  /// No SonarInfo at or before the ping stamp.
    NoSonarInfo,
  /// The table has no entry for the ping's mode key.
    NoEntry,
  /// The entry has no measured offset (`offset_anchored=false`).
    Unanchored,
  /// The entry rests on fewer pings than the configured floor.
    UnderPingFloor,
  };

  const char * toString(ModeResolution resolution);

/// Resolves pings to `correction_idx` values. Time-ordered association:
/// a ping uses the latest SonarInfo whose stamp is at or before the ping
/// stamp; the history is keyed by stamp and deduplicated (heartbeats repeat
/// the change stamp). A SonarInfo that arrives AFTER pings it would have
/// described is never applied to them retroactively; the live node counts
/// those pings as stale (`lateSonarInfo`).
  class BackscatterModeResolver
  {
public:
  /// Counters for the report. Keys are mode keys; the mismatch map is keyed
  /// by (mode key, declared state as text).
    struct Counts
    {
      uint64_t pings = 0;
      std::map < std::string, uint64_t > resolved_by_mode;
      uint64_t no_sonar_info = 0;
      std::map < std::string, uint64_t > no_entry_by_mode;
      std::map < std::string, uint64_t > unanchored_by_mode;
      std::map < std::string, uint64_t > under_floor_by_mode;
      std::map < std::pair < std::string, std::string >, uint64_t > declared_mismatch;
    /// Pings resolved before a SonarInfo with an earlier stamp arrived (live
    /// delivery lag): they were corrected under the previous mode.
      uint64_t stale = 0;
      uint64_t late_sonar_info = 0;
      uint64_t sonar_info_seen = 0;

      uint64_t unresolved() const
      {
        return pings - resolvedTotal();
      }
      uint64_t resolvedTotal() const;
    };

  /// @param table      the loaded table (must outlive the resolver).
  /// @param fallback   what an unresolved ping gets: Identity ->
  ///        `kCorrectionUnresolved` (the legacy path), Exclude ->
  ///        `kCorrectionExclude`.
  /// @param min_pings  entries resting on fewer pings than this (per the
  ///        table's `# mode_summary:` line) are treated as absent; 0 keeps
  ///        every entry.
    BackscatterModeResolver(
    const BackscatterTable * table,
    BackscatterModeFallback fallback = BackscatterModeFallback::Identity,
    uint64_t min_pings = 0);

  /// Called once per (mode key, declared state) mismatch with a message.
    void setMismatchWarning(std::function < void(const std::string &) > warn);

  /// Record a SonarInfo. Same stamp = the same message (a heartbeat or a
  /// re-delivery): replaced, not duplicated. Returns false when the stamp
  /// lies before a ping already resolved (the stale case above).
    bool addSonarInfo(const SonarInfoState & state);

  /// Resolve one ping. @p frequency_hz is the ping's own
  /// `PingInfo.frequency`. Returns the `correction_idx` to stamp on every
  /// sounding of the ping and sets @p resolution to why.
    uint16_t resolve(int64_t ping_stamp_ns, float frequency_hz, ModeResolution & resolution);
    uint16_t resolve(int64_t ping_stamp_ns, float frequency_hz)
    {
      ModeResolution r;
      return resolve(ping_stamp_ns, frequency_hz, r);
    }

  /// The SonarInfo a ping at @p stamp_ns would use (null when none yet).
    const SonarInfoState * stateAt(int64_t stamp_ns) const;

    const Counts & counts() const {return counts_;}
    BackscatterModeFallback fallback() const {return fallback_;}
    uint64_t minPings() const {return min_pings_;}
    const BackscatterTable & table() const {return *table_;}

  /// Multi-line summary for the import report / the node log: resolved
  /// pings per mode, every unresolved reason with its count, declared-state
  /// mismatches, stale pings.
    std::string report() const;

private:
    uint16_t fallbackIdx() const;

    const BackscatterTable * table_;
    BackscatterModeFallback fallback_;
    uint64_t min_pings_;
    std::function < void(const std::string &) > warn_;
    Counts counts_;
    std::map < int64_t, SonarInfoState > history_;
  /// mode key -> resolved idx and reason, cached per key (the key string is
  /// rebuilt per ping; the table lookup and checks are not).
    struct KeyDecision
    {
      uint16_t idx;
      ModeResolution resolution;
    };
    std::unordered_map < std::string, KeyDecision > decisions_;
    std::map < std::pair < std::string, std::string >, bool > warned_;
  /// Stamps of pings resolved since the newest SonarInfo, for the stale count.
    std::deque < int64_t > resolved_since_newest_;
    int64_t newest_stamp_ns_ = 0;
    bool have_newest_ = false;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__BACKSCATTER_MODE_H_
