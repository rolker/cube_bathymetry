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
#include <filesystem>
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

void ShallowReservoir::add(float depth)
{
  ++count;
  // Sorted descending (shallowest first, negative-down). Insert only if it
  // beats the current deepest retained, or there is room.
  if (shallowest.size() < kCapacity) {
    shallowest.insert(
      std::upper_bound(shallowest.begin(), shallowest.end(), depth, std::greater<float>()),
      depth);
    return;
  }
  if (depth > shallowest.back()) {
    shallowest.pop_back();
    shallowest.insert(
      std::upper_bound(shallowest.begin(), shallowest.end(), depth, std::greater<float>()),
      depth);
  }
}

float ShallowReservoir::decisionDepth(double percentile) const
{
  if (shallowest.empty()) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  uint64_t rank = static_cast<uint64_t>(std::ceil(percentile * static_cast<double>(count)));
  rank = std::max<uint64_t>(1, rank);
  rank = std::min<uint64_t>(rank, shallowest.size());
  return shallowest[static_cast<std::size_t>(rank - 1)];
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
    if (!std::isfinite(s.latitude) || !std::isfinite(s.longitude) ||
      !std::isfinite(s.sounding.depth))
    {
      continue;
    }
    const double reach = parameters.maxSpreadRadius(s.sounding);
    counts_.add(s.latitude, s.longitude, std::isfinite(reach) ? reach : 0.0);
    reservoirs_[l14.gridIndex(s.latitude, s.longitude)].add(s.sounding.depth);

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

std::map<gggs::GridIndex, float> ReconCollector::decisionDepths() const
{
  std::map<gggs::GridIndex, float> depths;
  for (const auto & [grid, reservoir] : reservoirs_) {
    const float d = reservoir.decisionDepth(policy_.decision_depth_percentile);
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
            std::to_string(static_cast<long long>(in.gcount())) + " of " +
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
            " MB free, the sounding spill needs ~" +
            std::to_string(needed_bytes / (1024 * 1024)) + " MB (use --scratch-dir)");
  }
}

void ReconCollector::cleanup()
{
  if (spill_out_) {
    spill_out_->close();
    spill_out_.reset();
  }
  if (!scratch_dir_.empty()) {
    std::error_code ec;
    std::filesystem::remove(spillPath(), ec);
    counts_.discardSpill();
  }
  if (!scratch_dir_.empty()) {
    std::error_code ec;
    if (std::filesystem::exists(scratch_dir_, ec) && std::filesystem::is_empty(scratch_dir_, ec)) {
      std::filesystem::remove(scratch_dir_, ec);
    }
  }
}

}  // namespace cube
