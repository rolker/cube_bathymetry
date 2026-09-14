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

#include "cube_bathymetry/build_fingerprint.h"

#include <fcntl.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cube
{

namespace
{
bool sameDouble(double a, double b)
{
  return std::isnan(a) ? std::isnan(b) : a == b;
}
}  // namespace

const char * toString(BuildFingerprint::Mode mode)
{
  return mode == BuildFingerprint::Mode::Fixed ? "fixed" : "depth_adaptive";
}

bool BuildFingerprint::isStale(const BuildFingerprint & previous) const
{
  if (previous.schema_version != schema_version || previous.mode != mode) {
    return true;
  }
  if (mode == Mode::Fixed) {
    if (cell_size_m.has_value() != previous.cell_size_m.has_value()) {
      return true;
    }
    if (cell_size_m && !sameDouble(*cell_size_m, *previous.cell_size_m)) {
      return true;
    }
  }
  if (iho_order != previous.iho_order) {
    return true;
  }
  if (!sameDouble(policy.capture_distance_scale, previous.policy.capture_distance_scale) ||
    !sameDouble(policy.capture_spacing_scale, previous.policy.capture_spacing_scale))
  {
    return true;
  }
  if (mode == Mode::DepthAdaptive) {
    if (policy.coarsest_level != previous.policy.coarsest_level ||
      policy.finest_level != previous.policy.finest_level ||
      policy.count_level != previous.policy.count_level ||
      policy.min_obs_per_node != previous.policy.min_obs_per_node ||
      !sameDouble(policy.depth_adaptive_scale, previous.policy.depth_adaptive_scale) ||
      !sameDouble(policy.blunder_allowance, previous.policy.blunder_allowance) ||
      !sameDouble(
        policy.decision_depth_percentile, previous.policy.decision_depth_percentile) ||
      !sameDouble(policy.achieved_percentile, previous.policy.achieved_percentile))
    {
      return true;
    }
  }
  return false;
}

std::string BuildFingerprint::toJson() const
{
  nlohmann::json j;
  j["schema_version"] = schema_version;
  nlohmann::json tiling;
  tiling["mode"] = toString(mode);
  if (mode == Mode::Fixed && cell_size_m) {
    tiling["cell_size_m"] = *cell_size_m;
  } else {
    tiling["cell_size_m"] = nullptr;
  }
  tiling["iho_order"] = iho_order;
  nlohmann::json p;
  p["capture_distance_scale"] = policy.capture_distance_scale;
  p["capture_spacing_scale"] = policy.capture_spacing_scale;
  if (mode == Mode::DepthAdaptive) {
    p["depth_adaptive_scale"] = policy.depth_adaptive_scale;
    p["coarsest_level"] = policy.coarsest_level;
    p["finest_level"] = policy.finest_level;
    p["count_level"] = policy.count_level;
    p["min_obs_per_node"] = policy.min_obs_per_node;
    p["blunder_allowance"] = policy.blunder_allowance;
    p["decision_depth_percentile"] = policy.decision_depth_percentile;
    p["achieved_percentile"] = policy.achieved_percentile;
  } else {
    p["depth_adaptive_scale"] = nullptr;
    p["coarsest_level"] = nullptr;
    p["finest_level"] = nullptr;
    p["count_level"] = nullptr;
    p["min_obs_per_node"] = nullptr;
    p["blunder_allowance"] = nullptr;
    p["decision_depth_percentile"] = nullptr;
    p["achieved_percentile"] = nullptr;
  }
  tiling["policy"] = p;
  tiling["levels_used"] = std::vector<int>(levels_used.begin(), levels_used.end());
  j["tiling"] = tiling;
  return j.dump(2) + "\n";
}

BuildFingerprint BuildFingerprint::fromJson(const std::string & json)
{
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json);
  } catch (const nlohmann::json::exception & e) {
    throw std::runtime_error(std::string("build_fingerprint: malformed JSON: ") + e.what());
  }
  BuildFingerprint f;
  try {
    f.schema_version = j.at("schema_version").get<int>();
    if (f.schema_version != kSchemaVersion) {
      // A schema-1 (or future) file is readable enough to be reported stale;
      // its other fields are not interpreted.
      return f;
    }
    const auto & tiling = j.at("tiling");
    const std::string mode = tiling.at("mode").get<std::string>();
    if (mode == "fixed") {
      f.mode = Mode::Fixed;
    } else if (mode == "depth_adaptive") {
      f.mode = Mode::DepthAdaptive;
    } else {
      throw std::runtime_error("build_fingerprint: unknown tiling mode '" + mode + "'");
    }
    if (tiling.contains("cell_size_m") && !tiling.at("cell_size_m").is_null()) {
      f.cell_size_m = tiling.at("cell_size_m").get<double>();
    }
    f.iho_order = tiling.at("iho_order").get<std::string>();
    const auto & p = tiling.at("policy");
    f.policy.capture_distance_scale = p.at("capture_distance_scale").get<double>();
    f.policy.capture_spacing_scale = p.at("capture_spacing_scale").get<double>();
    if (f.mode == Mode::DepthAdaptive) {
      f.policy.depth_adaptive_scale = p.at("depth_adaptive_scale").get<double>();
      f.policy.coarsest_level = p.at("coarsest_level").get<uint8_t>();
      f.policy.finest_level = p.at("finest_level").get<uint8_t>();
      f.policy.count_level = p.at("count_level").get<uint8_t>();
      f.policy.min_obs_per_node = p.at("min_obs_per_node").get<uint32_t>();
      f.policy.blunder_allowance = p.at("blunder_allowance").get<double>();
      f.policy.decision_depth_percentile = p.at("decision_depth_percentile").get<double>();
      f.policy.achieved_percentile = p.at("achieved_percentile").get<double>();
    }
    for (const auto & level : tiling.at("levels_used")) {
      f.levels_used.insert(level.get<uint8_t>());
    }
  } catch (const nlohmann::json::exception & e) {
    throw std::runtime_error(std::string("build_fingerprint: ") + e.what());
  }
  return f;
}

std::optional<BuildFingerprint> BuildFingerprint::read(const std::string & store_dir)
{
  const auto path = std::filesystem::path(store_dir) / kFilename;
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("build_fingerprint: cannot read " + path.string());
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  return fromJson(buffer.str());
}

void BuildFingerprint::write(const std::string & store_dir) const
{
  namespace fs = std::filesystem;
  fs::create_directories(store_dir);
  const fs::path final_path = fs::path(store_dir) / kFilename;
  const fs::path tmp_path = final_path.string() + ".tmp";
  {
    // ADR-0003: write the temp file, fsync it to DISK, then rename over the
    // previous fingerprint so a partial write never corrupts it. fflush alone
    // only pushes the bytes out of the C library's buffer into the page cache;
    // a machine that loses power between the rename and the kernel's writeback
    // would then publish a fingerprint whose bytes never reached the platter,
    // and the next run would trust a truncated or empty file as the record of
    // what the store was built from.
    std::FILE * f = std::fopen(tmp_path.c_str(), "w");
    if (!f) {
      throw std::runtime_error("build_fingerprint: cannot create " + tmp_path.string());
    }
    const std::string text = toJson();
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size() &&
      std::fflush(f) == 0;
    if (ok) {
      const int fd = ::fileno(f);
      ok = fd >= 0 && ::fsync(fd) == 0;
    }
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) {
      fs::remove(tmp_path);
      throw std::runtime_error("build_fingerprint: write failed for " + tmp_path.string());
    }
  }
  std::error_code ec;
  fs::rename(tmp_path, final_path, ec);
  if (ec) {
    fs::remove(tmp_path);
    throw std::runtime_error("build_fingerprint: rename failed: " + ec.message());
  }
  // The rename itself is a directory operation: fsync the directory too, or the
  // new name can be lost while the (synced) file contents survive nameless.
  const int dir_fd = ::open(store_dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (dir_fd >= 0) {
    ::fsync(dir_fd);
    ::close(dir_fd);
  }
}

}  // namespace cube
