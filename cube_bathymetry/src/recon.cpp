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

#include "cube_bathymetry/recon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "marine_autonomy/gz4d_geo.h"

namespace cube
{

SpilledSounding SpilledSounding::from(const GeoSounding & s)
{
  SpilledSounding r;
  r.latitude = s.latitude;
  r.longitude = s.longitude;
  r.depth = s.sounding.depth;
  r.vertical_error = s.sounding.vertical_error;
  r.horizontal_error = s.sounding.horizontal_error;
  r.intensity = s.sounding.intensity;
  r.beam_angle = s.sounding.beam_angle;
  r.slant_range = s.sounding.slant_range;
  r.sonar_relative_x = s.sounding.sonar_relative_position.x;
  r.sonar_relative_y = s.sounding.sonar_relative_position.y;
  r.sonar_relative_z = s.sounding.sonar_relative_position.z;
  return r;
}

GeoSounding SpilledSounding::toGeoSounding() const
{
  GeoSounding s(gz4d::GeoPointLatLongDegrees(latitude, longitude, depth));
  s.sounding.depth = depth;
  s.sounding.vertical_error = vertical_error;
  s.sounding.horizontal_error = horizontal_error;
  s.sounding.intensity = intensity;
  s.sounding.beam_angle = beam_angle;
  s.sounding.slant_range = slant_range;
  s.sounding.sonar_relative_position.x = sonar_relative_x;
  s.sounding.sonar_relative_position.y = sonar_relative_y;
  s.sounding.sonar_relative_position.z = sonar_relative_z;
  return s;
}

namespace
{
/// Floor division, correct for negative numerators (`-1 / 2` truncates to 0 in
/// C++; the bin below zero must be -1).
int64_t floorDiv(int64_t a, int64_t b)
{
  const int64_t q = a / b;
  return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
}  // namespace

void DepthHistogram::add(float depth)
{
  if (!std::isfinite(depth)) {
    return;
  }
  ++count;
  // Clamp to a range no bathymetry leaves, so a corrupt value cannot push the
  // bin index out of int32_t (or force an unbounded number of width doublings).
  const double d = std::clamp(static_cast<double>(depth), -12000.0, 12000.0);
  bins[static_cast<int32_t>(std::floor(d / bin_width))] += 1;
  while (bins.size() > kMaxBins) {
    // Halve the resolution: merge adjacent bin pairs. Only reachable when a
    // single grid's depths span > kMaxBins * bin_width, i.e. a flier decades
    // away from the bathymetry.
    std::map<int32_t, uint64_t> merged;
    for (const auto & [bin, n] : bins) {
      merged[static_cast<int32_t>(floorDiv(bin, 2))] += n;
    }
    bins.swap(merged);
    bin_width *= 2.0;
  }
}

float DepthHistogram::decisionDepth(double percentile) const
{
  if (bins.empty()) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  uint64_t rank = static_cast<uint64_t>(std::ceil(percentile * static_cast<double>(count)));
  rank = std::max<uint64_t>(1, rank);
  rank = std::min<uint64_t>(rank, count);
  // Walk from the shallowest bin (largest key, negative-down) until the rank is
  // reached, and answer with that bin's shallow edge -- see the header: the
  // error is < bin_width and it is on the safe (shallow, finer-tile) side.
  uint64_t seen = 0;
  for (auto it = bins.rbegin(); it != bins.rend(); ++it) {
    seen += it->second;
    if (seen >= rank) {
      return static_cast<float>((static_cast<double>(it->first) + 1.0) * bin_width);
    }
  }
  // Unreachable: the counts in the bins sum to `count`, and rank <= count.
  return static_cast<float>((static_cast<double>(bins.begin()->first) + 1.0) * bin_width);
}

ReconCollector::ReconCollector(
  const LevelPlanPolicy & policy, std::string scratch_dir, std::size_t count_resident_tiles)
: policy_(policy), scratch_dir_(std::move(scratch_dir)), counts_(policy.count_level)
{
  policy_.validate();
  if (!scratch_dir_.empty()) {
    std::error_code ec;
    // Refuse an orphaned scratch dir left by a killed run: the spill is opened
    // for append, so reusing one would replay another run's soundings.
    if (std::filesystem::exists(scratch_dir_, ec) &&
      !std::filesystem::is_empty(scratch_dir_, ec) && !ec)
    {
      throw std::runtime_error(
              "recon: scratch dir " + scratch_dir_ + " already exists and is not empty "
              "(an orphaned spill from a killed run?); remove it or pass --scratch-dir");
    }
    std::filesystem::create_directories(scratch_dir_, ec);
    if (ec) {
      throw std::runtime_error(
              "recon: cannot create scratch dir " + scratch_dir_ + ": " + ec.message());
    }
    // Bound the count grid too: a level-14 count tile is 1.8 MB and a survey
    // makes ~340 of them per km^2, so an unbounded recon grows without limit.
    counts_.setSpillDir(
      (std::filesystem::path(scratch_dir_) / kCountSpillSubdir).string(),
      count_resident_tiles);
  }
}

ReconCollector::~ReconCollector()
{
  cleanup();
}

std::string ReconCollector::spillPath() const
{
  if (scratch_dir_.empty()) {
    return {};
  }
  return (std::filesystem::path(scratch_dir_) / kSpillFilename).string();
}

void ReconCollector::add(const std::vector<GeoSounding> & soundings, const Parameters & parameters)
{
  const gggs::Level l14(14);
  for (const auto & s : soundings) {
    // The SAME door gate GeoGrid::insert applies (cube#143 triage). The recon
    // has to admit exactly what the estimator will: a sounding the estimator
    // refuses inflates the count grid -- so the achieved level reads finer than
    // the data supports, and ground that will carry no estimate counts as
    // surveyed -- and is then spilled and replayed for nothing. A missing-attitude
    // ping yields exactly this (vertical_error/horizontal_error NaN via
    // Parameters::influenceRadius' sentinel: see
    // DetectionsProjectorTest.MissingAttitudeYieldsNaNUncertainty), so it is a
    // normal MRU-dropout condition, not a pathological input. Keep in step with
    // geo_grid.cpp's gate.
    if (!std::isfinite(s.latitude) || !std::isfinite(s.longitude) ||
      !std::isfinite(s.sounding.depth) ||
      !std::isfinite(s.sounding.vertical_error) ||
      !std::isfinite(s.sounding.horizontal_error) ||
      s.sounding.vertical_error <= 0.0f ||
      s.sounding.horizontal_error < 0.0f)
    {
      ++refused_;
      continue;
    }
    const double reach = parameters.maxSpreadRadius(s.sounding);
    counts_.add(s.latitude, s.longitude, std::isfinite(reach) ? reach : 0.0);
    // The ladder is fed WATER DEPTH UNDER THE TRANSDUCER, not the stored
    // depth. Stored depths are WGS84 ellipsoidal heights by design (uma
    // ADR-0002 D4), and the geoid runs tens of metres from the ellipsoid --
    // ~28 m below it at the UNH pier, where 8-13 m of water arrives here as
    // `s.sounding.depth` = -36..-41 m. `depthAdaptiveLevel`'s argument is a
    // footprint argument (cell = scale x |depth|) and a beam's footprint
    // scales with its range below the transducer, so a geoid-sized offset
    // would coarsen every tile by one to two levels.
    //
    // Sign: `sonar_relative_position.z` is `range * cos(tx) * cos(rx)`
    // (`sounding.h`), so it is POSITIVE DOWN -- a nadir beam at 15 m range
    // gives z = +15 (see DetectionsProjectorTest). The histogram's convention
    // is negative-down (shallowest is the largest value), so the depth it is
    // fed is `-|z|`; the absolute value is belt-and-braces against a driver or
    // frame that reports the other sign.
    //
    // Transducer draft is deliberately ignored: it is sub-metre on these
    // vessels, against a ladder whose levels are a factor of two apart.
    const double z = s.sounding.sonar_relative_position.z;
    if (std::isfinite(z) && z != 0.0) {
      histograms_[l14.gridIndex(s.latitude, s.longitude)].add(
        static_cast<float>(-std::abs(z)));
    } else {
      // No usable range below the transducer: counted (it is still real
      // ground) but it cannot decide a level, and a zero fed to the histogram
      // would answer "0 m of water" and ask for the finest level everywhere.
      ++without_range_;
    }

    if (scratch_dir_.empty()) {
      continue;
    }
    // One chronological file: the replay order is the arrival order, which is
    // what makes phase two byte-identical to a fixed-level import.
    if (!spill_out_) {
      spill_out_ = std::make_unique<std::ofstream>(
        spillPath(), std::ios::binary | std::ios::app);
      if (!*spill_out_) {
        throw std::runtime_error("recon: cannot open spill file " + spillPath());
      }
    }
    const SpilledSounding record = SpilledSounding::from(s);
    spill_out_->write(reinterpret_cast<const char *>(&record), sizeof(record));
    if (!*spill_out_) {
      throw std::runtime_error("recon: failed writing spill file " + spillPath());
    }
    ++spilled_;
  }
}

std::size_t ReconCollector::histogramBytes() const
{
  // Measured, not assumed: the bins are sparse, so a grid that saw one depth
  // costs one node, not kMaxBins. std::map's per-node overhead (three pointers
  // + colour, padded) is counted alongside the key/value pair.
  constexpr std::size_t kMapNodeOverhead = 4 * sizeof(void *);
  std::size_t bytes = 0;
  for (const auto & [grid, histogram] : histograms_) {
    bytes += sizeof(gggs::GridIndex) + sizeof(DepthHistogram) + kMapNodeOverhead;
    bytes += histogram.bins.size() *
      (sizeof(int32_t) + sizeof(uint64_t) + kMapNodeOverhead);
  }
  return bytes;
}

std::map<gggs::GridIndex, float> ReconCollector::decisionDepths() const
{
  std::map<gggs::GridIndex, float> depths;
  for (const auto & [grid, histogram] : histograms_) {
    const float d = histogram.decisionDepth(policy_.decision_depth_percentile);
    if (!std::isnan(d)) {
      depths.emplace(grid, d);
    }
  }
  return depths;
}

LevelPlan ReconCollector::plan() const
{
  return levelPlanFor(counts_, decisionDepths(), policy_);
}

uint64_t ReconCollector::forEachSpilled(const std::function<void(const GeoSounding &)> & fn)
{
  if (spilled_ == 0) {
    return 0;  // nothing was spilled (no spill dir, or no finite soundings)
  }
  if (spill_out_) {
    // The writes above went into the stream's buffer; this is where a disk-full
    // surfaces for every record still held there. Ignoring the state here is
    // how the tail of a survey can vanish between phase one and phase two.
    spill_out_->flush();
    if (!*spill_out_) {
      throw std::runtime_error(
              "recon: failed flushing spill file " + spillPath() +
              " (out of space on the scratch device?); the tail of the survey was lost");
    }
    spill_out_->close();
    if (spill_out_->fail()) {
      throw std::runtime_error(
              "recon: failed closing spill file " + spillPath() +
              "; the tail of the survey may not have reached the disk");
    }
    spill_out_.reset();
  }
  std::ifstream in(spillPath(), std::ios::binary);
  if (!in) {
    throw std::runtime_error("recon: cannot read spill file " + spillPath());
  }
  SpilledSounding record;
  uint64_t replayed = 0;
  while (in.read(reinterpret_cast<char *>(&record), sizeof(record))) {
    fn(record.toGeoSounding());
    ++replayed;
  }
  if (in.bad()) {
    throw std::runtime_error("recon: read error on spill file " + spillPath());
  }
  // A short final read is a truncated file, not the end of the data: the loop
  // above ends the same way for both, so without this the replay stops
  // silently mid-record and the store is written from a partial survey.
  if (in.gcount() != 0) {
    throw std::runtime_error(
            "recon: spill file " + spillPath() + " ends in a partial record (" +
            std::to_string(static_cast<int64_t>(in.gcount())) + " of " +
            std::to_string(sizeof(record)) + " bytes) after " +
            std::to_string(replayed) + " soundings; the spill is truncated");
  }
  return replayed;
}

void ReconCollector::requireFreeSpace(const std::string & dir, uint64_t needed_bytes)
{
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto info = std::filesystem::space(dir, ec);
  if (ec) {
    throw std::runtime_error("recon: cannot query free space in " + dir + ": " + ec.message());
  }
  if (info.available < needed_bytes) {
    throw std::runtime_error(
            "recon: " + dir + " has " + std::to_string(info.available / (1024 * 1024)) +
            " MB free, the recon spill (soundings plus the count-tile spill) needs ~" +
            std::to_string(needed_bytes / (1024 * 1024)) + " MB (use --scratch-dir)");
  }
}

void ReconCollector::cleanup()
{
  if (spill_out_) {
    spill_out_->close();
    spill_out_.reset();
  }
  if (scratch_dir_.empty()) {
    return;
  }
  // Report what could not be deleted: a spill that survives is gigabytes of
  // scratch this run leaves behind, and silently it is only ever seen by the
  // NEXT run's orphan sweep -- by which time the operator has no idea which
  // run left it. cleanup() also runs from the destructor, so this warns rather
  // than throws.
  std::error_code ec;
  std::filesystem::remove(spillPath(), ec);
  if (ec) {
    std::cerr << "warning: recon could not remove the spill file " << spillPath()
              << ": " << ec.message() << " (remove it by hand; it is the size of the survey)"
              << std::endl;
  }
  counts_.discardSpill();
  ec.clear();
  if (std::filesystem::exists(scratch_dir_, ec) && std::filesystem::is_empty(scratch_dir_, ec)) {
    std::filesystem::remove(scratch_dir_, ec);
    if (ec) {
      std::cerr << "warning: recon could not remove the scratch dir " << scratch_dir_
                << ": " << ec.message() << std::endl;
    }
  } else if (ec) {
    std::cerr << "warning: recon could not inspect the scratch dir " << scratch_dir_
              << ": " << ec.message() << std::endl;
  }
}

}  // namespace cube
