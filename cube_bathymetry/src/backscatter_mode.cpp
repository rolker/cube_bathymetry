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
#include <sstream>
#include <string>
#include <utility>

namespace cube
{

namespace
{
/// Bound on the stamps kept for the stale count between two SonarInfo
/// arrivals (a bag with no SonarInfo at all would otherwise keep every ping).
constexpr std::size_t kMaxResolvedSinceNewest = 1u << 20;

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
  const auto existing = history_.find(state.stamp_ns);
  if (existing != history_.end() && sameState(existing->second, state)) {
    return true;  // a heartbeat: the same message again
  }
  // Pings already resolved at or after this stamp would have used it.
  uint64_t stale_now = 0;
  for (const int64_t stamp : resolved_since_newest_) {
    if (stamp >= state.stamp_ns) {
      ++stale_now;
    }
  }
  if (stale_now > 0) {
    ++counts_.late_sonar_info;
    counts_.stale += stale_now;
  }
  history_[state.stamp_ns] = state;
  if (!have_newest_ || state.stamp_ns >= newest_stamp_ns_) {
    newest_stamp_ns_ = state.stamp_ns;
    have_newest_ = true;
  }
  resolved_since_newest_.clear();
  return stale_now == 0;
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

uint16_t BackscatterModeResolver::resolve(
  int64_t ping_stamp_ns, float frequency_hz, ModeResolution & resolution)
{
  ++counts_.pings;
  if (resolved_since_newest_.size() >= kMaxResolvedSinceNewest) {
    resolved_since_newest_.pop_front();
  }
  resolved_since_newest_.push_back(ping_stamp_ns);

  const SonarInfoState * state = stateAt(ping_stamp_ns);
  if (!state) {
    resolution = ModeResolution::NoSonarInfo;
    ++counts_.no_sonar_info;
    return fallbackIdx();
  }
  const std::string key = canonicalModeKey(
    state->pulse_lengths_s, state->bandwidths_hz, state->tx_signal_types, frequency_hz);

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
  resolution = decision->second.resolution;

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
      return decision->second.idx;
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
