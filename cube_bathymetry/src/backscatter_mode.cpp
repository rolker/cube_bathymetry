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
  const auto existing = history_.find(stamp);
  if (existing != history_.end() && sameState(existing->second, state)) {
    return true;  // a heartbeat: the same message again
  }
  if (existing == history_.end()) {
    // The state in force at this stamp, if any: the same state again under
    // a new stamp (a bridge republishing on every ping) changes nothing.
    const SonarInfoState * in_force = stateAt(stamp);
    if (in_force && sameState(*in_force, state)) {
      prune();
      return true;
    }
  }
  // The pings this message would have described: the resolved ones from its
  // stamp up to the next stored stamp (those beyond use that later entry).
  // One is stale when this state gives it another correction than the one
  // it got -- a re-delivery that changes only the declared TVG is not.
  const auto next = history_.upper_bound(stamp);
  uint64_t stale_now = 0;
  for (const RecentPing & ping : recent_resolved_) {
    if (ping.stamp_ns < stamp || (next != history_.end() && ping.stamp_ns >= next->first)) {
      continue;
    }
    const KeyDecision & d = decide(
      canonicalModeKey(
        state.pulse_lengths_s, state.bandwidths_hz, state.tx_signal_types,
        ping.frequency_hz));
    if (idxOf(d) != ping.idx) {
      ++stale_now;
    }
  }
  if (stale_now > 0) {
    ++counts_.late_sonar_info;
    counts_.stale += stale_now;
  }
  history_[stamp] = state;
  prune();
  return stale_now == 0;
}

int64_t BackscatterModeResolver::windowCut() const
{
  return newest_resolved_ns_ < std::numeric_limits<int64_t>::min() + kHistoryWindowNs ?
         std::numeric_limits<int64_t>::min() : newest_resolved_ns_ - kHistoryWindowNs;
}

void BackscatterModeResolver::prune()
{
  if (have_resolved_) {
    const int64_t cut = windowCut();
    // Keep the newest entry at or before the cut: a ping at or after the cut
    // may still resolve against it; nothing older can be selected again.
    auto keep = history_.upper_bound(cut);
    if (keep != history_.begin()) {
      --keep;
      history_.erase(history_.begin(), keep);
    }
    while (!recent_resolved_.empty() && recent_resolved_.front().stamp_ns < cut) {
      recent_resolved_.pop_front();
    }
  }
  while (history_.size() > kMaxHistory) {
    history_.erase(history_.begin());
  }
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

uint16_t BackscatterModeResolver::fallbackIdx() const
{
  return fallback_ == BackscatterModeFallback::Exclude ? kCorrectionExclude :
         kCorrectionUnresolved;
}

uint16_t BackscatterModeResolver::idxOf(const KeyDecision & d) const
{
  return d.resolution == ModeResolution::Resolved ? d.idx : fallbackIdx();
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
  recent_resolved_.push_back(RecentPing{ping_stamp_ns, frequency_hz, idx});
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
  }

  const SonarInfoState * state = stateAt(ping_stamp_ns);
  if (!state) {
    resolution = ModeResolution::NoSonarInfo;
    ++counts_.no_sonar_info;
    remember(ping_stamp_ns, frequency_hz, fallbackIdx());
    return fallbackIdx();
  }
  const std::string key = canonicalModeKey(
    state->pulse_lengths_s, state->bandwidths_hz, state->tx_signal_types, frequency_hz);
  const KeyDecision & decision = decide(key);
  remember(ping_stamp_ns, frequency_hz, idxOf(decision));
  resolution = decision.resolution;

  // Declared-state check (decision 8): a check, not an input. Compared on
  // every ping so the count is per ping; warned once per (mode, state).
  if (const auto idx = table_ ? table_->find(key) : std::nullopt) {
    const ModeEntry * entry = table_->entry(*idx);
    const bool mixed = entry->declared.model == DeclaredTvg::Mixed;
    if (mixed || entry->declared != state->declared) {
      const auto pair = std::make_pair(key, state->declared.toString());
      ++counts_.declared_mismatch[pair];
      if (!warned_[pair]) {
        warned_[pair] = true;
        if (warn_) {
          std::ostringstream os;
          os << "SonarInfo declares TVG " << state->declared.toString() << " for mode " << key
             << " but the table recorded " << entry->declared.toString()
             << " for it; the table is applied as is (its range term is empirical)";
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
  return fallbackIdx();
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
       << kv.first.second << ", differs from the table\n";
  }
  if (counts_.stale > 0) {
    os << "  " << counts_.stale << " ping(s) resolved before a later-arriving SonarInfo ("
       << counts_.late_sonar_info << " late message(s)): corrected under the previous mode\n";
  }
  return os.str();
}

}  // namespace cube
