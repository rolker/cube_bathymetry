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


#include "cube_bathymetry/backscatter_mode.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace cube
{

namespace
{
/// Bound on the ping stamps kept for the stale count, beside the window
/// (a ping rate high enough to fill it inside 60 s is not a sonar).
constexpr std::size_t kMaxRecentResolved = 1u << 20;

bool sameState(const SonarInfoState & a, const SonarInfoState & b)
{
  return a.pulse_lengths_s == b.pulse_lengths_s && a.bandwidths_hz == b.bandwidths_hz &&
         a.tx_signal_types == b.tx_signal_types && a.declared == b.declared;
}
}  // namespace

const char * toString(ModeResolution resolution)
{
  switch (resolution) {
    case ModeResolution::Resolved: return "resolved";
    case ModeResolution::NoSonarInfo: return "no SonarInfo at or before the ping";
    case ModeResolution::NoEntry: return "no table entry for the mode";
    case ModeResolution::Unanchored: return "mode has no measured offset (unanchored)";
    case ModeResolution::UnderPingFloor: return "mode entry under the ping floor";
    default: return "?";
  }
}

uint64_t BackscatterModeResolver::Counts::resolvedTotal() const
{
  uint64_t n = 0;
  for (const auto & kv : resolved_by_mode) {
    n += kv.second;
  }
  return n;
}

BackscatterModeResolver::BackscatterModeResolver(
  const BackscatterTable * table, BackscatterModeFallback fallback, uint64_t min_pings)
: table_(table), fallback_(fallback), min_pings_(min_pings)
{
}

void BackscatterModeResolver::setMismatchWarning(std::function<void(const std::string &)> warn)
{
  warn_ = std::move(warn);
}

bool BackscatterModeResolver::addSonarInfo(const SonarInfoState & state)
{
  ++counts_.sonar_info_seen;
  const int64_t stamp = state.stamp_ns;
  // A message stamped before the window cut is either far behind the pings
  // or the first of a timeline that went BACKWARDS (a sim restart, a looped
  // bag) before any ping of it arrived. Either way it is stored and this
  // call does not prune: pruning against the old anchor would cut the
  // new timeline down to its newest entry before the anchor, message by
  // message, until a ping of it re-anchors the window (resolve). It is older
  // than the stale count looks back (the window), so it counts nothing stale.
  const bool behind_window = have_resolved_ && stamp < windowCut();
  if (!behind_window) {
    behind_stamps_.clear();  // the current timeline goes on: those were late
  }
  const auto existing = history_.find(stamp);
  if (existing != history_.end() && sameState(existing->second, state)) {
    if (behind_window) {
      rememberBehind(stamp);
    }
    return true;  // a heartbeat: the same message again
  }
  // The same state again under a NEW stamp (a bridge republishing on every
  // ping) is stored, not folded into the entry in force: folding it breaks
  // out-of-order association (A@100 kept, A@300 folded, a late B@200 would
  // then hand a ping at 350 B instead of A), and the derive tool keeps
  // distinct stamps too. The window and the cap bound the history without it.
  // The pings this message would have described: the resolved ones from its
  // stamp up to the next stored stamp (those beyond use that later entry).
  // One is stale while the state now in force at its stamp gives it another
  // correction than the one it got -- a re-delivery that changes only the
  // declared TVG does not. Each ping counts once however many late messages
  // land before it, and a later one that restores its correction takes the
  // count back.
  const auto next = history_.upper_bound(stamp);
  uint64_t changed = 0;
  for (RecentPing & ping : recent_resolved_) {
    if (behind_window) {
      break;
    }
    if (ping.stamp_ns < stamp || (next != history_.end() && ping.stamp_ns >= next->first)) {
      continue;
    }
    const KeyDecision & d = decide(
      canonicalModeKey(
        state.pulse_lengths_s, state.bandwidths_hz, state.tx_signal_types,
        ping.frequency_hz));
    const bool stale = idxOf(d) != ping.idx;
    if (stale) {
      ++changed;
    }
    if (stale && !ping.stale) {
      ++counts_.stale;
    } else if (!stale && ping.stale) {
      --counts_.stale;
    }
    ping.stale = stale;
  }
  if (changed > 0) {
    ++counts_.late_sonar_info;
  }
  history_[stamp] = state;
  if (behind_window) {
    // Not capped with the window's entries: at the cap the cap would erase
    // this state the moment it is stored (it is older than every entry in the
    // window), and the re-anchoring ping would then keep nothing (review
    // round 3). rememberBehind bounds these on their own.
    rememberBehind(stamp);
  } else {
    prune();
  }
  return changed == 0;
}

bool BackscatterModeResolver::held(std::map<int64_t, SonarInfoState>::const_iterator it) const
{
  if (!hold_ns_) {
    return false;
  }
  const auto next = std::next(it);
  return next == history_.end() || next->first > *hold_ns_;
}

void BackscatterModeResolver::rememberBehind(int64_t stamp_ns)
{
  behind_stamps_.insert(stamp_ns);
  // The behind-window states have their own cap, so a new timeline that
  // sends SonarInfo and no ping cannot grow the history without bound. The
  // oldest goes from the set AND the history, so every stamp in the set is
  // a stored state. A waiting ping (holdFrom) is newer than the anchor, since
  // pings drain in stamp order, so it never holds one of these; the check is
  // there for a caller that breaks that order.
  while (behind_stamps_.size() > kMaxHistory) {
    const auto oldest = history_.find(*behind_stamps_.begin());
    if (oldest != history_.end()) {
      if (held(oldest)) {
        break;
      }
      history_.erase(oldest);
    }
    behind_stamps_.erase(behind_stamps_.begin());
  }
}

int64_t BackscatterModeResolver::windowCut() const
{
  return newest_resolved_ns_ < std::numeric_limits<int64_t>::min() + kHistoryWindowNs ?
         std::numeric_limits<int64_t>::min() : newest_resolved_ns_ - kHistoryWindowNs;
}

void BackscatterModeResolver::holdFrom(std::optional<int64_t> stamp_ns)
{
  hold_ns_ = stamp_ns;
}

void BackscatterModeResolver::capHistory()
{
  // The cap counts and drops only the current window's states, oldest first
  // (review round 3):
  //  - never a behind-window state (rememberBehind bounds those). prune,
  //    the only caller, runs after the current timeline went on, which
  //    clears the set, so none is held here today; skipping them keeps the
  //    cap from undoing a new timeline if that changes.
  //  - never the state in force at the anchor (the newest resolved ping):
  //    the next ping in order resolves against it or a newer one.
  //  - never the state in force at the hold (the oldest ping still waiting
  //    to resolve) or a later one. Past that the history may exceed the cap,
  //    but only by the states read ahead of pings that are themselves still
  //    held in RAM.
  auto anchor = history_.end();
  if (have_resolved_) {
    anchor = history_.upper_bound(newest_resolved_ns_);
    anchor = anchor == history_.begin() ? history_.end() : std::prev(anchor);
  }
  auto it = history_.begin();
  while (history_.size() - behind_stamps_.size() > kMaxHistory && it != history_.end()) {
    if (it == anchor || behind_stamps_.count(it->first) > 0) {
      ++it;
    } else if (held(it)) {
      break;  // this state and every later one are held
    } else {
      it = history_.erase(it);
    }
  }
}

void BackscatterModeResolver::prune()
{
  if (have_resolved_) {
    const int64_t cut = hold_ns_ ? std::min(windowCut(), *hold_ns_) : windowCut();
    // Keep the newest entry at or before the cut: a ping at or after the cut
    // may still resolve against it; nothing older can be selected again.
    auto keep = history_.upper_bound(cut);
    if (keep != history_.begin()) {
      --keep;
      history_.erase(history_.begin(), keep);
    }
    while (!recent_resolved_.empty() && recent_resolved_.front().stamp_ns < windowCut()) {
      recent_resolved_.pop_front();
    }
  }
  capHistory();
}

const SonarInfoState * BackscatterModeResolver::stateAt(int64_t stamp_ns) const
{
  auto it = history_.upper_bound(stamp_ns);
  if (it == history_.begin()) {
    return nullptr;
  }
  --it;
  return &it->second;
}

// Identity -> kCorrectionUnresolved (0), deliberately NOT kCorrectionIdentity
// (plan decision 9): 0 is the estimator's legacy single-curve path, and a
// table run carries no legacy curve (an explicit file is a curve OR a table,
// never both), so 0 records the beam uncorrected -- the identity fallback --
// while keeping the one index every pre-#172 path (the Sounding(depth)
// constructor, batch_regen's packed records, the live `auto` curve) uses.
// The reserved kCorrectionIdentity is for an explicit identity stamp.
uint16_t BackscatterModeResolver::unresolvedIdx() const
{
  return fallback_ == BackscatterModeFallback::Exclude ? kCorrectionExclude :
         kCorrectionUnresolved;
}

uint16_t BackscatterModeResolver::idxOf(const KeyDecision & d) const
{
  return d.resolution == ModeResolution::Resolved ? d.idx : unresolvedIdx();
}

const BackscatterModeResolver::KeyDecision & BackscatterModeResolver::decide(
  const std::string & key)
{
  auto decision = decisions_.find(key);
  if (decision == decisions_.end()) {
    KeyDecision d{kCorrectionUnresolved, ModeResolution::NoEntry};
    const auto idx = table_ ? table_->find(key) : std::nullopt;
    if (idx) {
      const ModeEntry * entry = table_->entry(*idx);
      if (!entry->offset_anchored) {
        d.resolution = ModeResolution::Unanchored;
      } else if (min_pings_ > 0 && entry->pings < min_pings_) {
        d.resolution = ModeResolution::UnderPingFloor;
      } else {
        d.resolution = ModeResolution::Resolved;
        d.idx = *idx;
      }
    }
    decision = decisions_.emplace(key, d).first;
  }
  return decision->second;
}

void BackscatterModeResolver::remember(int64_t ping_stamp_ns, float frequency_hz, uint16_t idx)
{
  if (recent_resolved_.size() >= kMaxRecentResolved) {
    recent_resolved_.pop_front();
  }
  recent_resolved_.push_back(RecentPing{ping_stamp_ns, frequency_hz, idx, false});
  const int64_t cut = windowCut();
  while (!recent_resolved_.empty() && recent_resolved_.front().stamp_ns < cut) {
    recent_resolved_.pop_front();
  }
}

uint16_t BackscatterModeResolver::resolve(
  int64_t ping_stamp_ns, float frequency_hz, ModeResolution & resolution)
{
  ++counts_.pings;
  if (!have_resolved_ || ping_stamp_ns > newest_resolved_ns_) {
    newest_resolved_ns_ = ping_stamp_ns;
    have_resolved_ = true;
    behind_stamps_.clear();  // the current timeline goes on
  } else if (ping_stamp_ns < windowCut()) {
    // More than the window behind the anchor: time went backwards (a sim
    // restart, `ros2 bag play --loop`; ~/reset keeps the resolver). The
    // anchor is a running maximum, so without this it would stay on the old
    // timeline and every addSonarInfo would prune the new one to a single
    // entry until time passed the old maximum. Re-anchor on this ping, and
    // drop the old timeline's pings from the stale look-back.
    newest_resolved_ns_ = ping_stamp_ns;
    recent_resolved_.clear();
    ++counts_.window_reanchored;
    // The old timeline's entries go, or one stamped later than the new
    // timeline has reached would be selected once it gets there:
    //  - SonarInfo arrived behind the window too, and nothing of the old
    //    timeline since: that is the new timeline's history, and every other
    //    entry is the old one's.
    //  - either way, an entry more than the window AHEAD of this ping is the
    //    old timeline's. That covers the new timeline's first ping arriving
    //    before its first SonarInfo (review round 3): the ping is unresolved
    //    ("no SonarInfo") rather than given the old run's mode, and so is
    //    every ping until the new run's SonarInfo arrives.
    // Arrival order cannot tell a restart from a ping (or a SonarInfo and a
    // ping) delivered more than the window late: a lone late ping also drops
    // the current timeline's entries more than the window ahead of it. That
    // is accepted -- a sonar ping 60 s late is not a live link, offline input
    // is merged in stamp order, and the cost is unresolved pings until the
    // next SonarInfo, never a wrong mode applied. See the header for why a
    // clock-jump callback is not the answer.
    const int64_t ahead_cut =
      ping_stamp_ns > std::numeric_limits<int64_t>::max() - kHistoryWindowNs ?
      std::numeric_limits<int64_t>::max() : ping_stamp_ns + kHistoryWindowNs;
    const bool new_history = !behind_stamps_.empty();
    for (auto it = history_.begin(); it != history_.end(); ) {
      const bool keep = behind_stamps_.count(it->first) > 0 ||
        (!new_history && it->first <= ahead_cut);
      it = keep ? std::next(it) : history_.erase(it);
    }
    behind_stamps_.clear();
  } else {
    behind_stamps_.clear();  // an in-window ping: the current timeline goes on
  }

  const SonarInfoState * state = stateAt(ping_stamp_ns);
  if (!state) {
    resolution = ModeResolution::NoSonarInfo;
    ++counts_.no_sonar_info;
    remember(ping_stamp_ns, frequency_hz, unresolvedIdx());
    return unresolvedIdx();
  }
  const std::string key = canonicalModeKey(
    state->pulse_lengths_s, state->bandwidths_hz, state->tx_signal_types, frequency_hz);
  const KeyDecision & decision = decide(key);
  remember(ping_stamp_ns, frequency_hz, idxOf(decision));
  resolution = decision.resolution;

  // Declared-state check (decision 8): a check, not an input. Compared on
  // every ping so the count is per ping; warned once per (mode, declared TVG
  // model). Keyed on the model, not the state's text: a declared absorption
  // that drifts would otherwise mint a new warning and a new key per value,
  // without bound (review round 2). The warning names the first value seen.
  if (const auto idx = table_ ? table_->find(key) : std::nullopt) {
    const ModeEntry * entry = table_->entry(*idx);
    const bool mixed = entry->declared.model == DeclaredTvg::Mixed;
    if (mixed || entry->declared != state->declared) {
      const auto pair = std::make_pair(key, std::string(cube::toString(state->declared.model)));
      ++counts_.declared_mismatch[pair];
      if (!warned_[pair]) {
        warned_[pair] = true;
        if (warn_) {
          std::ostringstream os;
          os << "SonarInfo declares TVG " << state->declared.toString() << " for mode " << key
             << " but the table recorded " << entry->declared.toString() << " for it; ";
          if (resolution == ModeResolution::Resolved) {
            os << "the table is applied as is (its range term is empirical)";
          } else {
            // The entry is not applied at all (unanchored, under the floor):
            // saying "applied as is" would misstate what the beams got.
            os << "the entry is not applied (" << toString(resolution) << "), the fallback "
               << toString(fallback_) << " is";
          }
          warn_(os.str());
        }
      }
    }
  }

  switch (resolution) {
    case ModeResolution::Resolved:
      ++counts_.resolved_by_mode[key];
      return decision.idx;
    case ModeResolution::NoEntry:
      ++counts_.no_entry_by_mode[key];
      break;
    case ModeResolution::Unanchored:
      ++counts_.unanchored_by_mode[key];
      break;
    case ModeResolution::UnderPingFloor:
      ++counts_.under_floor_by_mode[key];
      break;
    default:
      break;
  }
  return unresolvedIdx();
}

void BackscatterModeResolver::countBeam(
  uint16_t correction_idx, float raw_intensity, float beam_angle_rad, float range_m)
{
  if (!table_ || std::isnan(raw_intensity)) {
    return;
  }
  const ModeEntry * entry = table_->entry(correction_idx);
  if (!entry) {
    return;  // a reserved index: the ping's own reason already counts it
  }
  const TableBeamStatus status = tableBeamStatus(
    beamAbsAngleDeg(beam_angle_rad), static_cast<double>(range_m), *table_, correction_idx);
  if (status == TableBeamStatus::Corrected) {
    ++counts_.beams_correctable;
  } else {
    ++counts_.beam_fallback[{entry->key, toString(status)}];
  }
}

std::string BackscatterModeResolver::unresolvedSummary() const
{
  auto sum = [](const std::map<std::string, uint64_t> & m) {
      uint64_t n = 0;
      for (const auto & kv : m) {
        n += kv.second;
      }
      return n;
    };
  uint64_t no_frequency = 0;
  const std::string unknown_frequency = "_?kHz";
  for (const auto & kv : counts_.no_entry_by_mode) {
    const std::string & key = kv.first;
    if (key.size() >= unknown_frequency.size() &&
      key.compare(key.size() - unknown_frequency.size(), unknown_frequency.size(),
      unknown_frequency) == 0)
    {
      no_frequency += kv.second;
    }
  }
  std::ostringstream os;
  os << counts_.unresolved() << " of " << counts_.pings
     << " ping(s) not corrected by the table so far (fallback " << toString(fallback_)
     << "): " << counts_.no_sonar_info << " before any SonarInfo ("
     << counts_.sonar_info_seen << " SonarInfo message(s) seen), "
     << sum(counts_.no_entry_by_mode) << " with no table entry";
  if (no_frequency > 0) {
    os << " (" << no_frequency << " of them without a usable frequency, key '?kHz')";
  }
  os << ", " << sum(counts_.unanchored_by_mode) << " unanchored, "
     << sum(counts_.under_floor_by_mode) << " under the ping floor";
  return os.str();
}

std::string BackscatterModeResolver::report() const
{
  std::ostringstream os;
  os << "Backscatter mode resolution (" << (table_ ? table_->path : std::string("no table"))
     << ", fallback " << toString(fallback_);
  if (min_pings_ > 0) {
    os << ", ping floor " << min_pings_;
  }
  os << "): " << counts_.resolvedTotal() << " of " << counts_.pings << " pings resolved, "
     << counts_.sonar_info_seen << " SonarInfo message(s) seen\n";
  for (const auto & kv : counts_.resolved_by_mode) {
    os << "  " << kv.first << ": " << kv.second << " pings\n";
  }
  if (counts_.no_sonar_info > 0) {
    os << "  " << counts_.no_sonar_info << " ping(s) before the first SonarInfo: "
       << toString(fallback_) << "\n";
  }
  for (const auto & kv : counts_.no_entry_by_mode) {
    os << "  " << kv.first << ": " << kv.second << " ping(s), no table entry: "
       << toString(fallback_) << "\n";
  }
  for (const auto & kv : counts_.unanchored_by_mode) {
    os << "  " << kv.first << ": " << kv.second << " ping(s), entry unanchored (no offset): "
       << toString(fallback_) << "\n";
  }
  for (const auto & kv : counts_.under_floor_by_mode) {
    os << "  " << kv.first << ": " << kv.second << " ping(s), entry under the ping floor: "
       << toString(fallback_) << "\n";
  }
  for (const auto & kv : counts_.declared_mismatch) {
    os << "  " << kv.first.first << ": " << kv.second << " ping(s) declared TVG "
       << kv.first.second << " (any absorption), differs from the table\n";
  }
  uint64_t beam_fallbacks = 0;
  for (const auto & kv : counts_.beam_fallback) {
    beam_fallbacks += kv.second;
  }
  if (counts_.beams_correctable + beam_fallbacks > 0) {
    os << "  beams of resolved pings: " << counts_.beams_correctable << " correctable, "
       << beam_fallbacks << " not (" << toString(fallback_) << ")\n";
  }
  for (const auto & kv : counts_.beam_fallback) {
    os << "  " << kv.first.first << ": " << kv.second << " beam(s), " << kv.first.second
       << ": " << toString(fallback_) << "\n";
  }
  if (counts_.window_reanchored > 0) {
    os << "  " << counts_.window_reanchored << " ping(s) arrived more than "
       << kHistoryWindowNs / 1000000000LL << " s behind the newest one (time went backwards: "
       << "a restart or a looped bag); the history window was re-anchored on each\n";
  }
  if (counts_.stale > 0) {
    os << "  " << counts_.stale << " ping(s) resolved before a later-arriving SonarInfo ("
       << counts_.late_sonar_info << " late message(s)): corrected under the previous mode\n";
  }
  return os.str();
}

}  // namespace cube
