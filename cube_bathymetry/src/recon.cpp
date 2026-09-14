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

ReconCollector::ReconCollector(const LevelPlanPolicy & policy, std::string scratch_dir)
: policy_(policy), scratch_dir_(std::move(scratch_dir)), counts_(policy.count_level)
{
  policy_.validate();
  if (!scratch_dir_.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(scratch_dir_, ec);
    if (ec) {
      throw std::runtime_error(
              "recon: cannot create scratch dir " + scratch_dir_ + ": " + ec.message());
    }
  }
}

ReconCollector::~ReconCollector()
{
  cleanup();
}

std::string ReconCollector::spillPath(const gggs::GridIndex & spill_grid) const
{
  return (std::filesystem::path(scratch_dir_) /
         (std::to_string(spill_grid.level()) + "_" + std::to_string(spill_grid.row()) + "_" +
         std::to_string(spill_grid.column()) + ".spill")).string();
}

void ReconCollector::add(const std::vector<GeoSounding> & soundings, const Parameters & parameters)
{
  const gggs::Level l14(14);
  const gggs::Level spill_level(kSpillLevel);
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
    const gggs::GridIndex g = spill_level.gridIndex(s.latitude, s.longitude);
    auto & out = spill_out_[g];
    if (!out) {
      out = std::make_unique<std::ofstream>(spillPath(g), std::ios::binary | std::ios::app);
      if (!*out) {
        throw std::runtime_error("recon: cannot open spill file " + spillPath(g));
      }
    }
    const SpilledSounding record = SpilledSounding::from(s);
    out->write(reinterpret_cast<const char *>(&record), sizeof(record));
    if (!*out) {
      throw std::runtime_error("recon: failed writing spill file " + spillPath(g));
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

std::vector<gggs::GridIndex> ReconCollector::spilledGrids() const
{
  std::vector<gggs::GridIndex> grids;
  for (const auto & [grid, out] : spill_out_) {
    grids.push_back(grid);
  }
  return grids;
}

void ReconCollector::forEachSpilled(
  const gggs::GridIndex & spill_grid,
  const std::function<void(const GeoSounding &)> & fn)
{
  auto it = spill_out_.find(spill_grid);
  if (it == spill_out_.end()) {
    return;  // nothing was spilled there
  }
  if (it->second) {
    it->second->flush();
    it->second->close();
    it->second.reset();  // keep the map entry: the grid was spilled
  }
  std::ifstream in(spillPath(spill_grid), std::ios::binary);
  if (!in) {
    throw std::runtime_error("recon: cannot read spill file " + spillPath(spill_grid));
  }
  SpilledSounding record;
  while (in.read(reinterpret_cast<char *>(&record), sizeof(record))) {
    fn(record.toGeoSounding());
  }
  if (in.bad()) {
    throw std::runtime_error("recon: read error on spill file " + spillPath(spill_grid));
  }
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
  for (auto & [grid, out] : spill_out_) {
    if (out) {
      out->close();
    }
    std::error_code ec;
    std::filesystem::remove(spillPath(grid), ec);
  }
  spill_out_.clear();
  if (!scratch_dir_.empty()) {
    std::error_code ec;
    if (std::filesystem::exists(scratch_dir_, ec) && std::filesystem::is_empty(scratch_dir_, ec)) {
      std::filesystem::remove(scratch_dir_, ec);
    }
  }
}

}  // namespace cube
