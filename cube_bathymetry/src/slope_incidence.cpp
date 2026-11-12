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

#include "cube_bathymetry/slope_incidence.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace cube
{

namespace seabed = marine_backscatter::seabed;

void IncidenceCounters::addBaseline(double full_baseline_m)
{
  if (!std::isfinite(full_baseline_m)) {
    return;
  }
  ++baseline_count_;
  const double bin = std::floor(full_baseline_m / kBaselineBinM);
  const std::size_t index = bin < 0.0 ? 0 :
    static_cast<std::size_t>(std::min<double>(bin, kBaselineBins - 1));
  ++baseline_hist_[index];
}

void IncidenceCounters::addSlope(double slope_deg)
{
  if (!std::isfinite(slope_deg)) {
    return;
  }
  // Nearest degree: floor() would put a 12.0 deg plane computed as 11.9999999
  // in the 11 bin.
  const double bin = std::round(slope_deg);
  const std::size_t index = bin < 0.0 ? 0 :
    static_cast<std::size_t>(std::min<double>(bin, kSlopeBins - 1));
  ++slope_hist_[index];
}

double IncidenceCounters::baselinePercentile(double p) const
{
  if (baseline_count_ == 0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double clamped = std::min(1.0, std::max(0.0, p));
  const uint64_t target = std::max<uint64_t>(
    1, static_cast<uint64_t>(std::ceil(clamped * static_cast<double>(baseline_count_))));
  uint64_t seen = 0;
  for (std::size_t i = 0; i < baseline_hist_.size(); ++i) {
    seen += baseline_hist_[i];
    if (seen >= target) {
      // The upper edge of the bin: the histogram can only say "at most this".
      return static_cast<double>(i + 1) * kBaselineBinM;
    }
  }
  return static_cast<double>(baseline_hist_.size()) * kBaselineBinM;
}

uint64_t IncidenceCounters::fallbackTotal() const
{
  uint64_t total = 0;
  for (const uint64_t n : fallback) {
    total += n;
  }
  return total;
}

std::string IncidenceCounters::report() const
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(2);
  const auto pct = [this](uint64_t n) {
      return beams == 0 ? 0.0 : 100.0 * static_cast<double>(n) / static_cast<double>(beams);
    };
  os << "Incidence angle: " << beams << " beam(s); " << from_surface
     << " (" << pct(from_surface) << "%) against the surface normal, "
     << fallbackTotal() << " (" << pct(fallbackTotal())
     << "%) against the vertical (not slope-corrected), " << no_beam_vector
     << " with no beam vector, " << invalid_angle << " with an invalid angle.\n";
  if (fallbackTotal() > 0) {
    os << "  vertical-normal beams by reason:";
    for (std::size_t i = 1; i < kReasonCount; ++i) {
      if (fallback[i] > 0) {
        os << " " << seabed::toString(static_cast<seabed::FallbackReason>(i)) << "="
           << fallback[i];
      }
    }
    os << "\n";
  }
  os << "  back-facing (incidence >= 90 deg): " << back_facing << " beam(s)\n";
  if (baseline_count_ > 0) {
    os << "  differencing baseline (full width, metres): median "
       << baselinePercentile(0.5) << ", 90th percentile " << baselinePercentile(0.9)
       << ", 99th percentile " << baselinePercentile(0.99) << " (resolution "
       << kBaselineBinM << " m); largest half-width " << max_half_baseline_m << "\n";
    os << "  baseline at the one-cell floor: " << at_floor << " of " << baseline_count_
       << " (" << 100.0 * static_cast<double>(at_floor) / static_cast<double>(baseline_count_)
       << "%)\n";
  }
  if (from_surface > 0) {
    os << "  surface slope, beams per degree:";
    for (std::size_t i = 0; i < kSlopeBins; ++i) {
      if (slope_hist_[i] > 0) {
        os << " " << i << "=" << slope_hist_[i];
      }
    }
    os << "\n";
  }
  return os.str();
}

void stampIncidence(
  std::vector<GeoSounding> & soundings, seabed::SeabedHeightSampler & sampler,
  const SlopeOptions & options, IncidenceCounters & counters)
{
  constexpr double kDegToRad = M_PI / 180.0;
  const double baseline_rad = options.baseline_deg * kDegToRad;
  for (auto & gs : soundings) {
    Sounding & s = gs.sounding;
    ++counters.beams;
    s.incidence_angle = std::nanf("");
    const auto & v = s.beam_dir_enu;
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) {
      ++counters.no_beam_vector;
      continue;
    }
    // Half of slant_range x angle. A beam with no usable range asks for the
    // smallest positive step so that the module's one-cell floor decides.
    const double range = std::isfinite(s.slant_range) && s.slant_range > 0.0f ?
      static_cast<double>(s.slant_range) : 0.0;
    seabed::NormalOptions normal_options;
    normal_options.step_m = std::max(0.5 * range * baseline_rad, 1e-9);
    const seabed::Normal normal = seabed::seabedNormal(
      sampler, gs.latitude, gs.longitude, normal_options);
    if (normal.step_used_m > 0.0) {
      counters.addBaseline(2.0 * normal.step_used_m);
      counters.max_half_baseline_m = std::max(counters.max_half_baseline_m, normal.step_used_m);
      if (normal.step_clamped) {
        ++counters.at_floor;
      }
    }
    const seabed::AngleResult angle = seabed::incidenceAngle(
      seabed::Enu{v[0], v[1], v[2]}, normal.normal);
    if (!angle.valid) {
      ++counters.invalid_angle;
      continue;
    }
    // Counted only once the beam really got an angle, so the parts add up to
    // the beams (cube#173 review).
    if (normal.from_surface) {
      ++counters.from_surface;
      counters.addSlope(std::acos(std::min(1.0, std::max(-1.0, normal.normal.u))) / kDegToRad);
    } else {
      const std::size_t reason = static_cast<std::size_t>(normal.reason);
      ++counters.fallback[reason < IncidenceCounters::kReasonCount ? reason : 1];
    }
    if (angle.rad >= M_PI / 2.0) {
      ++counters.back_facing;
    }
    s.incidence_angle = static_cast<float>(angle.rad);
  }
}

}  // namespace cube
