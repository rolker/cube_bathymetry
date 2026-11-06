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
#include <optional>
#include <set>
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

/// The live node's throttle for its unresolved-ping warning (cube#172): due
/// when it has not fired yet (@p last_ns empty), once @p interval_ns has
/// passed since @p last_ns, and when the clock went BACKWARDS past
/// @p last_ns (a sim restart under use_sim_time): a negative elapsed time is
/// "warn now", never a mute until the clock catches up with the old stamp.
  inline bool warnIntervalDue(
    std::optional < int64_t > last_ns, int64_t now_ns, int64_t interval_ns)
  {
    return !last_ns || now_ns < *last_ns || now_ns - *last_ns >= interval_ns;
  }

/// Resolves pings to `correction_idx` values. Time-ordered association:
/// a ping uses the latest SonarInfo whose stamp is at or before the ping
/// stamp. The history is keyed by stamp and deduplicated by stamp: a message
/// at a stamp already held with the same state is a heartbeat (re-delivery)
/// and is not stored again. The same state under a NEW stamp IS stored
/// (review round 2): folding it into the entry in force would hand a late
/// message stamped between the two the pings after the second (the derive
/// tool keeps distinct stamps too); the window and the cap bound it. A SonarInfo that arrives AFTER
/// pings it would have described is never applied to them retroactively;
/// those pings -- the resolved ones between its stamp and the next stored
/// stamp whose correction it would have changed -- are counted as stale
/// (`Counts::stale`).
///
/// **The history is bounded** (a live node runs for days, and a
/// mode-switching sonar republishes SonarInfo on every switch, which under
/// alternating multi-mode pinging is every ping):
///   - on every `addSonarInfo`, entries stamped before
///     `newest resolved ping stamp - kHistoryWindowNs` are dropped, EXCEPT
///     the newest entry at or before that cut. Pings drain in stamp order
///     offline and arrive in order live, so a later ping can only resolve
///     against that kept entry or a newer one: the dropped entries can never
///     be selected again. A late ping inside the window still finds the
///     state that was in force at its stamp.
///   - a hard cap of `kMaxHistory` entries, the oldest dropped first, for a
///     stream that changes state while no ping is resolved at all -- except
///     that the state in force at the hold (`holdFrom`) and every later one
///     stay: import_bag reads SonarInfo ahead of the pings waiting for the TF
///     frontier, and a TF stall through more than the cap's worth of state
///     changes must not drop the states those pings need.
/// The stale count looks back over the same window only.
///
/// **Time going backwards** (a sim restart, `ros2 bag play --loop`; ~/reset
/// keeps the resolver): a ping more than `kHistoryWindowNs` behind the anchor
/// re-anchors the window on itself and clears the stale look-back
/// (`Counts::window_reanchored`), and a SonarInfo stamped before the window
/// cut is stored as is (no pruning, no folding into an older entry), so the
/// new timeline's states survive until its first ping re-anchors. Without
/// both, the anchor (a running maximum) would stay on the old timeline and
/// prune the new one to one entry. When such SonarInfo arrived and nothing
/// in the window since, the re-anchoring ping also drops every entry that
/// did not (the old timeline's); a lone late ping only moves the anchor.
  class BackscatterModeResolver
  {
public:
  /// How far behind the newest resolved ping the history is kept (60 s).
    static constexpr int64_t kHistoryWindowNs = 60LL * 1000000000LL;
  /// Hard cap on stored SonarInfo states.
    static constexpr std::size_t kMaxHistory = 4096;

  /// Counters for the report. Keys are mode keys; the mismatch map is keyed
  /// by (mode key, declared TVG model as text) -- the model only, so a
  /// drifting declared absorption cannot grow it.
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
    /// delivery lag) that it would have corrected differently: they were
    /// corrected under the previous mode. Looked for inside the window; each
    /// ping counts at most once, and a later message that restores the
    /// correction it got takes the count back.
      uint64_t stale = 0;
      uint64_t late_sonar_info = 0;
    /// Pings that arrived more than the window behind the newest one so far
    /// (time went backwards: a sim restart, a looped bag); each re-anchored
    /// the window on itself.
      uint64_t window_reanchored = 0;
      uint64_t sonar_info_seen = 0;
    /// Beams of resolved pings handed to the estimator (`countBeam`), and
    /// those the table still cannot correct per (mode key, reason): no finite
    /// angle, >= 90 deg, outside the written angle bins, no finite range,
    /// unweighable bin. They take the
    /// fallback at record time. Counted as submitted, before CUBE's own gates.
      uint64_t beams_correctable = 0;
      std::map < std::pair < std::string, std::string >, uint64_t > beam_fallback;

      uint64_t unresolved() const
      {
        return pings - resolvedTotal();
      }
      uint64_t resolvedTotal() const;
    };

  /// @param table      the loaded table (must outlive the resolver).
  /// @param fallback   what an unresolved ping gets: Identity ->
  ///        `kCorrectionUnresolved` (0: the legacy single-curve path, which
  ///        records the beam uncorrected because a table run carries no
  ///        legacy curve -- the identity fallback by plan decision 9),
  ///        Exclude -> `kCorrectionExclude`.
  /// @param min_pings  entries resting on fewer pings than this (per the
  ///        table's `# mode_summary:` line) are treated as absent; 0 keeps
  ///        every entry.
    BackscatterModeResolver(
    const BackscatterTable * table,
    BackscatterModeFallback fallback = BackscatterModeFallback::Identity,
    uint64_t min_pings = 0);

  /// Called once per (mode key, declared TVG model) mismatch with a message
  /// naming the first declared state seen.
    void setMismatchWarning(std::function < void(const std::string &) > warn);

  /// Record a SonarInfo. Same stamp and state = the same message (a
  /// heartbeat or a re-delivery), not stored again. A different state at a
  /// held stamp replaces it. Then prunes the history (the bound above). Returns
  /// false when the message changes the state of pings already resolved
  /// (the stale case above).
    bool addSonarInfo(const SonarInfoState & state);

  /// The oldest stamp a ping may still be resolved at (import_bag: the
  /// oldest ping waiting for the TF frontier; empty = none waiting). Neither
  /// the window nor the cap drops the state in force there or any later one,
  /// so the history can exceed `kMaxHistory` by the states read ahead of the
  /// pings the caller is itself holding.
    void holdFrom(std::optional < int64_t > stamp_ns);

  /// Resolve one ping. @p frequency_hz is the ping's own
  /// `PingInfo.frequency`. Returns the `correction_idx` to stamp on every
  /// sounding of the ping and sets @p resolution to why.
    uint16_t resolve(int64_t ping_stamp_ns, float frequency_hz, ModeResolution & resolution);
    uint16_t resolve(int64_t ping_stamp_ns, float frequency_hz)
    {
      ModeResolution r;
      return resolve(ping_stamp_ns, frequency_hz, r);
    }

  /// Count one beam of a ping that was given @p correction_idx (call it for
  /// every sounding after stamping the ping): a beam of a RESOLVED ping that
  /// the table still cannot correct is counted per (mode, reason), with the
  /// same test the estimator applies (`tableBeamStatus`). A NaN intensity
  /// is not a backscatter sample, and a reserved index is a ping already
  /// counted by its reason, so neither is counted here.
    void countBeam(
      uint16_t correction_idx, float raw_intensity, float beam_angle_rad, float range_m);

  /// The SonarInfo a ping at @p stamp_ns would use (null when none yet).
    const SonarInfoState * stateAt(int64_t stamp_ns) const;

    const Counts & counts() const {return counts_;}
  /// Stored SonarInfo states (the bound above).
    std::size_t historySize() const {return history_.size();}
    BackscatterModeFallback fallback() const {return fallback_;}
    uint64_t minPings() const {return min_pings_;}
    const BackscatterTable & table() const {return *table_;}

  /// True when pings were resolved against a table and NOT ONE got a table
  /// entry: the run's backscatter carries no table correction at all. The
  /// usual causes are a wrong SonarInfo topic, no SonarInfo in the input, or
  /// pings without a frequency (cube_bathymetry#172 review).
    bool resolvedNothing() const {return counts_.pings > 0 && counts_.resolvedTotal() == 0;}

  /// One line for a warning: how many pings are unresolved so far and why
  /// (no SonarInfo / no entry / unanchored / under the floor), naming the
  /// pings that carried no usable frequency (their key reads `?kHz`).
    std::string unresolvedSummary() const;

  /// Multi-line summary for the import report / the node log: resolved
  /// pings per mode, every unresolved reason with its count, declared-state
  /// mismatches, stale pings.
    std::string report() const;

private:
  /// mode key -> resolved idx and reason, cached per key (the key string is
  /// rebuilt per ping; the table lookup and checks are not).
    struct KeyDecision
    {
      uint16_t idx;
      ModeResolution resolution;
    };
  /// One resolved ping inside the window: what it got, so a late SonarInfo
  /// can tell whether it changes that.
    struct RecentPing
    {
      int64_t stamp_ns;
      float frequency_hz;
      uint16_t idx;
      /// Counted in `Counts::stale` (the state now in force disagrees).
      bool stale;
    };

  /// The index an unresolved ping is stamped with: Exclude ->
  /// `kCorrectionExclude`; Identity -> `kCorrectionUnresolved` (0, the
  /// identity path of a table run, see the definition), not
  /// `kCorrectionIdentity`.
    uint16_t unresolvedIdx() const;
    uint16_t idxOf(const KeyDecision & d) const;
    const KeyDecision & decide(const std::string & key);
    void remember(int64_t ping_stamp_ns, float frequency_hz, uint16_t idx);
    void rememberBehind(int64_t stamp_ns);
    void capHistory();

    const BackscatterTable * table_;
    BackscatterModeFallback fallback_;
    uint64_t min_pings_;
    std::function < void(const std::string &) > warn_;
    Counts counts_;
    std::map < int64_t, SonarInfoState > history_;
    std::unordered_map < std::string, KeyDecision > decisions_;
    std::map < std::pair < std::string, std::string >, bool > warned_;
  /// Stamps of the pings resolved inside the history window, oldest first,
  /// for the stale count.
    std::deque < RecentPing > recent_resolved_;
  /// The newest ping stamp resolved so far (the window's anchor).
    int64_t newest_resolved_ns_ = 0;
    bool have_resolved_ = false;
  /// Stamps of the SonarInfo stored behind the window since the last
  /// in-window message or ping: a new timeline's, if a ping behind the window
  /// follows (time went backwards), else late messages.
    std::set < int64_t > behind_stamps_;
  /// See holdFrom.
    std::optional < int64_t > hold_ns_;

  /// Oldest stamp the window keeps; meaningless before the first ping.
    int64_t windowCut() const;
    void prune();
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__BACKSCATTER_MODE_H_
