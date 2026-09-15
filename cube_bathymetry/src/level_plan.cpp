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

#include "cube_bathymetry/level_plan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cube
{

namespace
{
constexpr uint8_t kSurveyIndexFootprintLevel = 14;  // ADR-0002: the dirty-set footprint level
constexpr uint8_t kMaxGggsLevel = 20;
constexpr double kDenseBytesPerTile = 960.0 * 960.0 * 2.0 * 8.0;  // 2-band Float64 (uma#376)

gggs::GridIndex ancestorAt(gggs::GridIndex grid, uint8_t level)
{
  while (grid.valid() && grid.level() > level) {
    grid = gggs::parent(grid);
  }
  return grid.valid() && grid.level() == level ? grid : gggs::GridIndex();
}

std::string formatDepth(float depth)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(depth));
  return buffer;
}

std::string formatScale(double value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  return buffer;
}
}  // namespace

uint64_t LevelPlanPolicy::requiredObservations() const
{
  // Computed in double and clamped before the conversion (cube#143 triage): a
  // float-to-integer conversion whose value does not fit the target is
  // UNDEFINED, and this is reached by the tiling report before any bag is
  // opened. validate() rejects an allowance big enough to get here, so the
  // clamp is the defence for a policy that never went through it -- a plan file
  // is an operator-facing artifact passed between runs.
  const double required = std::ceil(
    static_cast<double>(min_obs_per_node) * (1.0 + blunder_allowance));
  if (!(required >= 1.0)) {
    return 1;
  }
  constexpr double kMaxRepresentable = 9.0e18;  // < 2^63, safely inside uint64_t
  return static_cast<uint64_t>(std::min(required, kMaxRepresentable));
}

void LevelPlanPolicy::validate() const
{
  if (!std::isfinite(depth.capture_distance_scale) || depth.capture_distance_scale <= 0.0) {
    throw std::invalid_argument(
        "level plan policy: capture_distance_scale must be finite and positive");
  }
  if (depth.finest_level > kSurveyIndexFootprintLevel) {
    throw std::invalid_argument(
            "level plan policy: finest_level " + std::to_string(depth.finest_level) +
            " exceeds 14, the survey index's footprint level -- a tile finer than the index "
            "breaks ADR-0002's dirty-set guarantee");
  }
  if (depth.coarsest_level > depth.finest_level) {
    throw std::invalid_argument("level plan policy: coarsest_level must not exceed finest_level");
  }
  if (count_level < depth.finest_level || count_level > kMaxGggsLevel) {
    throw std::invalid_argument(
            "level plan policy: count_level must satisfy finest_level <= count_level <= 20 "
            "(the achieved path can only reach the count level)");
  }
  if (min_obs_per_node < 1) {
    throw std::invalid_argument("level plan policy: min_obs_per_node must be >= 1");
  }
  // Upper-bounded, not just finite: the allowance multiplies an observation
  // count into a uint64_t (requiredObservations), and the bound keeps that
  // product representable by construction rather than by clamping after the
  // fact. kMaxBlunderAllowance is nine orders of magnitude above the 0.2
  // default -- no real survey is near it (cube#143 triage).
  constexpr double kMaxBlunderAllowance = 1e9;
  if (!std::isfinite(blunder_allowance) || blunder_allowance < 0.0 ||
    blunder_allowance > kMaxBlunderAllowance)
  {
    throw std::invalid_argument(
            "level plan policy: blunder_allowance must be in [0, 1e9]");
  }
  for (double p : {decision_depth_percentile, achieved_percentile}) {
    if (!(p >= 0.0 && p <= 1.0)) {
      throw std::invalid_argument("level plan policy: percentiles must be in [0, 1]");
    }
  }
  // A decision depth is a percentile of a grid's depths (recon.h's
  // DepthHistogram serves any rank at any density); 0 would make the single
  // shallowest sounding -- a flier -- the decision depth, with no guard at all.
  if (!(decision_depth_percentile > 0.0)) {
    throw std::invalid_argument(
            "level plan policy: decision_depth_percentile must be in (0, 1] -- at 0 the "
            "shallowest single sounding would set the level, with no flier guard");
  }
}

uint8_t levelNoFinerThan(double spacing_m, uint8_t coarsest, uint8_t finest)
{
  if (coarsest > finest) {
    throw std::invalid_argument("levelNoFinerThan: coarsest must not exceed finest");
  }
  if (!(spacing_m > 0.0) || std::isinf(spacing_m) || std::isnan(spacing_m)) {
    return coarsest;
  }
  // fromCellSize: the coarsest level whose cells are at-or-FINER than the
  // request. We want the finest level whose cell is at-or-COARSER.
  const gggs::Level at_or_finer = gggs::Level::fromCellSize(static_cast<float>(spacing_m));
  int level = at_or_finer.level();
  if (at_or_finer.cellSize() < spacing_m * (1.0 - 1e-9)) {
    level -= 1;
  }
  return static_cast<uint8_t>(std::clamp(level, static_cast<int>(coarsest),
      static_cast<int>(finest)));
}

bool LevelPlan::isEmitted(const gggs::GridIndex & grid) const
{
  return tiles_.count(grid) > 0;
}

void LevelPlan::setCaptureSpacingScale(float scale)
{
  if (!(scale > 0.0f) || !std::isfinite(scale)) {
    throw std::invalid_argument(
            "LevelPlan: capture_spacing_scale must be finite and positive");
  }
  capture_spacing_scale_ = scale;
}

bool LevelPlan::covers(const gggs::GridIndex & grid) const
{
  for (gggs::GridIndex g = grid; g.valid(); g = gggs::parent(g)) {
    if (tiles_.count(g) > 0) {
      return true;
    }
    if (g.level() == 0) {
      break;
    }
  }
  return false;
}

bool LevelPlan::isTouched(const gggs::GridIndex & grid) const
{
  auto it = touched_.find(grid.level());
  return it != touched_.end() && it->second.count(grid) > 0;
}

std::vector<gggs::GridIndex> LevelPlan::tilesAtLevel(uint8_t level) const
{
  std::vector<gggs::GridIndex> result;
  for (const auto & [grid, tile] : tiles_) {
    if (grid.level() == level) {
      result.push_back(grid);
    }
  }
  return result;
}

std::set<uint8_t> LevelPlan::levels() const
{
  std::set<uint8_t> result;
  for (const auto & [grid, tile] : tiles_) {
    result.insert(grid.level());
  }
  return result;
}

std::set<gggs::GridIndex> LevelPlan::tilesContaining(double latitude, double longitude) const
{
  std::set<gggs::GridIndex> result;
  for (uint8_t level : levels()) {
    const gggs::GridIndex grid = gggs::Level(level).gridIndex(latitude, longitude);
    if (isEmitted(grid)) {
      result.insert(grid);
    }
  }
  return result;
}

std::set<uint8_t> LevelPlan::levelsIntersecting(const gz4d::BoundsDegrees & bounds) const
{
  std::set<uint8_t> result;
  if (!valid(bounds)) {
    return result;
  }
  for (uint8_t level : levels()) {
    const gggs::Level l(level);
    gggs::GridAreaIterator it(
      l.gridIndex(bounds.minimum().latitude, bounds.minimum().longitude),
      l.gridIndex(bounds.maximum().latitude, bounds.maximum().longitude));
    for (; it.valid(); it.next()) {
      if (isEmitted(*it)) {
        result.insert(level);
        break;
      }
    }
  }
  return result;
}

std::vector<gggs::GridIndex> LevelPlan::coverageDeficit() const
{
  std::vector<gggs::GridIndex> result;
  for (const auto & [grid, tile] : tiles_) {
    if (tile.coverageDeficit()) {
      result.push_back(grid);
    }
  }
  return result;
}

double LevelPlan::nativeGroundM2(const gggs::GridIndex & grid) const
{
  const auto it = tiles_.find(grid);
  if (it == tiles_.end()) {
    return 0.0;
  }
  double ground = it->second.ground_m2;
  if (it->second.refined) {
    for (const auto & child : gggs::children(grid)) {
      const auto c = tiles_.find(child);
      if (c != tiles_.end()) {
        ground -= c->second.ground_m2;
      }
    }
  }
  // Floating-point subtraction of the same summed cell areas, so a residual of
  // a few ulp either way is possible; ground is never negative.
  return ground > 0.0 ? ground : 0.0;
}

std::string LevelPlan::toJson() const
{
  // Hand-written so the byte layout is fixed: key order, tile order (the map is
  // ordered by GridIndex: level, then row, then column), number formats.
  std::ostringstream out;
  out << "{\"schema\":2,\"policy\":{"
      << "\"capture_distance_scale\":" << formatScale(policy_.depth.capture_distance_scale)
      << ",\"coarsest_level\":" << static_cast<int>(policy_.depth.coarsest_level)
      << ",\"finest_level\":" << static_cast<int>(policy_.depth.finest_level)
      << ",\"count_level\":" << static_cast<int>(policy_.count_level)
      << ",\"min_obs_per_node\":" << policy_.min_obs_per_node
      << ",\"blunder_allowance\":" << formatScale(policy_.blunder_allowance)
      << ",\"decision_depth_percentile\":" << formatScale(policy_.decision_depth_percentile)
      << ",\"achieved_percentile\":" << formatScale(policy_.achieved_percentile)
      << "},\"capture_spacing_scale\":" << formatScale(capture_spacing_scale_)
      << ",\"ground_m2\":" << formatScale(surveyed_ground_m2_)
      << ",\"touched\":[";
  bool first = true;
  for (const auto & [level, grids] : touched_) {
    for (const auto & grid : grids) {
      if (!first) {out << ",";}
      first = false;
      out << "[" << static_cast<int>(grid.level()) << "," << grid.row() << "," << grid.column() <<
        "]";
    }
  }
  out << "],\"tiles\":[";
  first = true;
  for (const auto & [grid, tile] : tiles_) {
    if (!first) {out << ",";}
    first = false;
    out << "{\"l\":" << static_cast<int>(grid.level())
        << ",\"r\":" << grid.row()
        << ",\"c\":" << grid.column()
        << ",\"req\":" << static_cast<int>(tile.required_level)
        << ",\"ach\":" << static_cast<int>(tile.achieved_level)
        // "d": decision depth = water depth under the transducer, negative-down.
        << ",\"d\":" << formatDepth(tile.decision_depth)
        << ",\"ref\":" << (tile.refined ? "true" : "false")
        << ",\"g\":" << formatScale(tile.ground_m2) << "}";
  }
  out << "]}";
  return out.str();
}

LevelPlan LevelPlan::fromJson(const std::string & json)
{
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json);
  } catch (const nlohmann::json::exception & e) {
    throw std::runtime_error(std::string("LevelPlan::fromJson: ") + e.what());
  }
  LevelPlan plan;
  try {
    // Schema 2 (cube#143 dry-run review) added the surveyed-ground fields and
    // defines `d` as the water depth under the transducer (negative-down), not
    // the stored ellipsoidal height an earlier revision wrote there; both
    // changes landed before any plan file was released, so schema 2 was
    // amended rather than a schema 3 minted. A schema-1 plan carries tile
    // footprints only and cannot answer the report's area columns, so it is
    // refused rather than reported against wrongly.
    if (j.at("schema").get<int>() != 2) {
      throw std::runtime_error("LevelPlan::fromJson: unsupported schema");
    }
    const auto & p = j.at("policy");
    plan.policy_.depth.capture_distance_scale = p.at("capture_distance_scale").get<double>();
    plan.policy_.depth.coarsest_level = p.at("coarsest_level").get<uint8_t>();
    plan.policy_.depth.finest_level = p.at("finest_level").get<uint8_t>();
    plan.policy_.count_level = p.at("count_level").get<uint8_t>();
    plan.policy_.min_obs_per_node = p.at("min_obs_per_node").get<uint32_t>();
    plan.policy_.blunder_allowance = p.at("blunder_allowance").get<double>();
    plan.policy_.decision_depth_percentile = p.at("decision_depth_percentile").get<double>();
    plan.policy_.achieved_percentile = p.at("achieved_percentile").get<double>();
    plan.policy_.validate();

    auto gridFrom = [](uint8_t level, uint32_t row, uint32_t column) {
        // No public row/column constructor: resolve by the grid's centre.
        const gggs::Level l(level);
        const double lat_span = gggs::levels[level].grid_angular_span;
        const double lat = -96.0 + (row + 0.5) * lat_span;
        // Column span depends on the row's latitude band; take a probe grid in
        // the row to read it.
        const gggs::GridIndex probe = l.gridIndex(std::clamp(lat, -90.0, 90.0), -180.0);
        const double lon = -180.0 + (column + 0.5) * probe.longitudinalSpan();
        const gggs::GridIndex grid = l.gridIndex(std::clamp(lat, -90.0, 90.0), lon);
        if (grid.row() != row || grid.column() != column) {
          throw std::runtime_error("LevelPlan::fromJson: grid index does not round-trip");
        }
        return grid;
      };

    // Schema 2 is unreleased (minted this morning, amended once already), so
    // the capture scale is added to it rather than a schema 3 being minted --
    // and it is REQUIRED, not defaulted: a plan that did not state it would
    // silently rebuild at the reader's own default, which is the defect
    // (cube#143 triage).
    plan.setCaptureSpacingScale(j.at("capture_spacing_scale").get<float>());
    plan.surveyed_ground_m2_ = j.at("ground_m2").get<double>();
    if (!(plan.surveyed_ground_m2_ >= 0.0) || !std::isfinite(plan.surveyed_ground_m2_)) {
      throw std::runtime_error("LevelPlan::fromJson: ground_m2 must be finite and >= 0");
    }
    // Every record is validated against the policy the file itself declares
    // (cube#143 triage). A plan is an operator-facing artifact passed between
    // runs, so it is an external input: a file with finest_level 14 and a tile
    // at level 15 was accepted, and MultiLevelAccumulator would then build a
    // sheet finer than the survey index's footprint level -- the ADR-0002
    // dirty-set invariant the policy check exists to protect.
    auto requireLevelInLadder = [&plan](uint8_t level, const char * what) {
        if (level < plan.policy_.depth.coarsest_level ||
          level > plan.policy_.depth.finest_level)
        {
          throw std::runtime_error(
                  std::string("LevelPlan::fromJson: ") + what + " " +
                  std::to_string(static_cast<int>(level)) + " is outside the plan's own "
                  "ladder [" + std::to_string(
                    static_cast<int>(plan.policy_.depth.coarsest_level)) + ", " +
                  std::to_string(static_cast<int>(plan.policy_.depth.finest_level)) + "]");
        }
      };
    for (const auto & t : j.at("touched")) {
      const gggs::GridIndex grid = gridFrom(t.at(0).get<uint8_t>(), t.at(1).get<uint32_t>(),
        t.at(2).get<uint32_t>());
      requireLevelInLadder(grid.level(), "touched grid at level");
      plan.touched_[grid.level()].insert(grid);
    }
    for (const auto & t : j.at("tiles")) {
      PlannedTile tile;
      tile.index = gridFrom(t.at("l").get<uint8_t>(), t.at("r").get<uint32_t>(),
        t.at("c").get<uint32_t>());
      requireLevelInLadder(tile.index.level(), "tile at level");
      tile.required_level = t.at("req").get<uint8_t>();
      tile.achieved_level = t.at("ach").get<uint8_t>();
      // req/ach are the two answers that DECIDED the tile, both clamped to the
      // ladder when the plan was built; a value outside it means the record and
      // the policy disagree about what plan this is.
      requireLevelInLadder(tile.required_level, "tile required level");
      requireLevelInLadder(tile.achieved_level, "tile achieved level");
      tile.decision_depth = t.at("d").get<float>();
      tile.refined = t.at("ref").get<bool>();
      tile.ground_m2 = t.at("g").get<double>();
      // Surveyed ground: finite and non-negative. It is the denominator of the
      // report's storage multiplier and the area columns sum to it, so a NaN
      // here makes every number in the report NaN.
      if (!(tile.ground_m2 >= 0.0) || !std::isfinite(tile.ground_m2)) {
        throw std::runtime_error(
                "LevelPlan::fromJson: tile ground_m2 must be finite and >= 0");
      }
      plan.tiles_.emplace(tile.index, tile);
    }
  } catch (const nlohmann::json::exception & e) {
    throw std::runtime_error(std::string("LevelPlan::fromJson: ") + e.what());
  }
  return plan;
}

std::string LevelPlan::report(double observed_bytes_per_tile) const
{
  std::ostringstream out;
  out << std::fixed;
  out << "Depth-adaptive level plan (cube_bathymetry#143)\n";
  out << "  policy: cell = " << std::setprecision(3) << policy_.depth.capture_distance_scale
      << " x depth, levels " << static_cast<int>(policy_.depth.coarsest_level) << ".."
      << static_cast<int>(policy_.depth.finest_level)
      << "; count level " << static_cast<int>(policy_.count_level)
      << ", n_req " << policy_.requiredObservations() << " (" << policy_.min_obs_per_node
      << " obs/node + " << std::setprecision(0) << policy_.blunder_allowance * 100.0
      << "% blunders), achieved p" << policy_.achieved_percentile * 100.0
      << ", decision depth p" << policy_.decision_depth_percentile * 100.0
      << " (water depth under the transducer)\n";

  const auto lvls = levels();
  if (lvls.empty()) {
    out << "  no tiles: the survey touched no ground with data\n";
    return out.str();
  }
  // Areas are SURVEYED GROUND -- the count grid's occupied cells, per tile --
  // not tile footprints: one level-8 parent over a single survey line covers
  // 12 km2 of footprint and ~0.008 km2 of ensonified bottom (cube#143 dry-run
  // review). "covered" is the ground a level's tiles sit over; "native" is the
  // ground for which they are the finest emitted tile, i.e. what they store.
  double total_covered = 0.0, total_dense = 0.0, total_observed = 0.0;
  double coarse_native = 0.0, deficit_ground = 0.0;
  std::size_t total_tiles = 0, deficit_tiles = 0;
  out << "  level  cell(m)  tiles  covered(km2)  native(km2)  dense(MB)  observed(MB)\n";
  for (uint8_t level : lvls) {
    const auto at = tilesAtLevel(level);
    double covered = 0.0, native = 0.0;
    for (const auto & grid : at) {
      const auto & tile = tiles_.at(grid);
      covered += tile.ground_m2;
      const double own = nativeGroundM2(grid);
      native += own;
      if (tile.coverageDeficit()) {
        ++deficit_tiles;
        deficit_ground += own;
      }
    }
    const double dense = at.size() * kDenseBytesPerTile;
    const double observed = at.size() * observed_bytes_per_tile;
    total_covered += covered;
    total_dense += dense;
    total_observed += observed;
    total_tiles += at.size();
    if (level < 10) {
      coarse_native += native;
    }
    out << "  " << std::setw(5) << static_cast<int>(level)
        << "  " << std::setw(7) << std::setprecision(3) << gggs::Level(level).cellSize()
        << "  " << std::setw(5) << at.size()
        << "  " << std::setw(12) << std::setprecision(4) << covered / 1e6
        << "  " << std::setw(11) << std::setprecision(4) << native / 1e6
        << "  " << std::setw(9) << std::setprecision(1) << dense / 1e6
        << "  " << std::setw(12) << std::setprecision(1) << observed / 1e6 << "\n";
  }
  out << "  surveyed ground (occupied count cells): " << std::setprecision(4)
      << surveyed_ground_m2_ / 1e6 << " km2\n";
  out << "  total: " << total_tiles << " tiles, " << std::setprecision(1) << total_dense / 1e6
      << " MB dense, " << total_observed / 1e6 << " MB at " << observed_bytes_per_tile / 1e6
      << " MB/tile observed fill\n";
  out << "  estimate-count multiplier (parents estimated under children): "
      << std::setprecision(2)
      << (surveyed_ground_m2_ > 0.0 ? total_covered / surveyed_ground_m2_ : 0.0)
      << "x the surveyed ground\n";
  out << "  coverage deficit (depth requires finer than the data achieves): "
      << deficit_tiles << " tiles, " << std::setprecision(4) << deficit_ground / 1e6 << " km2\n";
  out << "  ground stored coarser than level 10 (today's fixed level): "
      << std::setprecision(4) << coarse_native / 1e6 << " km2 -- resolution lost against "
    "today's stores in >36 m water; inherent to the pinned uma#369 ladder. Ground with a "
    "native tile at level 10 or finer is not counted here, however many parents-alive "
    "tiles also cover it\n";
  return out.str();
}

LevelPlan levelPlanFor(
  const CountGrid & counts,
  const std::map<gggs::GridIndex, float> & decision_depth_by_l14_grid,
  const LevelPlanPolicy & policy)
{
  policy.validate();
  if (counts.level() != policy.count_level) {
    throw std::invalid_argument(
            "levelPlanFor: count grid is at level " + std::to_string(counts.level()) +
            ", policy.count_level is " + std::to_string(policy.count_level));
  }
  LevelPlan plan;
  plan.policy_ = policy;
  const uint8_t coarsest = policy.depth.coarsest_level;
  const uint8_t finest = policy.depth.finest_level;

  // ---- Touched sets, one per level from coarsest to finest ----------------
  //
  // A level-L tile is touched when some occupied count cell, expanded by
  // max(recorded spread term, cell_L), intersects it. The home ancestor chain
  // of an occupied tile is always touched; only cells within the expansion
  // radius of their count tile's edge can reach a neighbour, so those are the
  // only cells examined individually.
  const double count_cell_m = counts.cellSizeMeters();
  // Surveyed ground per count tile: its occupied cells times a cell's area.
  // Gathered in this pass because the count grid is spill-backed -- a second
  // sweep would re-read every spilled tile from disk.
  const double count_cell_area_m2 = count_cell_m * count_cell_m;
  std::map<gggs::GridIndex, double> ground_by_grid;
  double surveyed_ground_m2 = 0.0;
  for (const auto & count_tile : counts.grids()) {
    const double spread = counts.maxSpreadTerm(count_tile);
    const CountGrid::Tile * count_tile_data = counts.tileAt(count_tile);
    if (!count_tile_data) {
      continue;
    }
    // Copied, not referenced: the count grid is spill-backed, so any later tile
    // access may evict this one (see CountGrid::tileAt).
    const std::vector<CountGrid::Count> band = count_tile_data->band(0);
    uint64_t occupied_cells = 0;
    for (std::size_t i = 0; i < band.size(); ++i) {
      if (band[i] != 0) {
        ++occupied_cells;
      }
    }
    if (occupied_cells == 0) {
      continue;
    }
    // Ground under this count tile, charged to every ancestor that could carry
    // it. A count tile is at or below `finest`, so it lies wholly inside one
    // tile at each planned level -- the ancestor chain is exact, no clipping.
    const double tile_ground_m2 = static_cast<double>(occupied_cells) * count_cell_area_m2;
    surveyed_ground_m2 += tile_ground_m2;
    for (int level = coarsest; level <= finest; ++level) {
      const gggs::GridIndex a = ancestorAt(count_tile, static_cast<uint8_t>(level));
      if (a.valid()) {
        ground_by_grid[a] += tile_ground_m2;
      }
    }
    for (int level = coarsest; level <= finest; ++level) {
      auto & touched = plan.touched_[static_cast<uint8_t>(level)];
      const double radius = std::max(spread, gggs::Level(level).cellSize());
      // Home chain.
      const gggs::GridIndex home = ancestorAt(count_tile, static_cast<uint8_t>(level));
      if (home.valid()) {
        touched.insert(home);
      }
      // Edge band: cells closer than `radius` to the count tile's edge.
      const int band_cells = static_cast<int>(std::ceil(radius / count_cell_m)) + 1;
      const gggs::Level l(static_cast<uint8_t>(level));
      auto examine = [&](uint16_t r, uint16_t c) {
          if (band[static_cast<std::size_t>(r) * CountGrid::kEdge + c] == 0) {
            return;
          }
          const gggs::CellIndex cell(count_tile, r, c);
          const auto sw = cell.position();
          const double lat = sw.latitude + 0.5 * count_tile.latitudinalSpan() / CountGrid::kEdge;
          const double lon = sw.longitude + 0.5 * count_tile.longitudinalSpan() / CountGrid::kEdge;
          const auto disc = gz4d::BoundsDegrees::radiusFromCenter(
            gz4d::PositionDegrees(lat, lon), radius);
          gggs::GridAreaIterator it(
            l.gridIndex(disc.minimum().latitude, disc.minimum().longitude),
            l.gridIndex(disc.maximum().latitude, disc.maximum().longitude));
          for (; it.valid(); it.next()) {
            touched.insert(*it);
          }
        };
      const int edge = CountGrid::kEdge;
      const int b = std::min(band_cells, edge);
      for (int r = 0; r < edge; ++r) {
        const bool row_in_band = r < b || r >= edge - b;
        if (row_in_band) {
          for (int c = 0; c < edge; ++c) {
            examine(static_cast<uint16_t>(r), static_cast<uint16_t>(c));
          }
        } else {
          for (int c = 0; c < b; ++c) {
            examine(static_cast<uint16_t>(r), static_cast<uint16_t>(c));
          }
          for (int c = std::max(b, edge - b); c < edge; ++c) {
            examine(static_cast<uint16_t>(r), static_cast<uint16_t>(c));
          }
        }
      }
    }
  }

  // ---- Decision depth: min over touched children, base at level 14 ---------
  std::map<gggs::GridIndex, float> decision_cache;
  std::function<float(const gggs::GridIndex &)> decisionDepth =
    [&](const gggs::GridIndex & grid) -> float {
      if (grid.level() == kSurveyIndexFootprintLevel) {
        auto it = decision_depth_by_l14_grid.find(grid);
        return it == decision_depth_by_l14_grid.end() ?
               std::numeric_limits<float>::quiet_NaN() : it->second;
      }
      auto cached = decision_cache.find(grid);
      if (cached != decision_cache.end()) {
        return cached->second;
      }
      float shallowest = std::numeric_limits<float>::quiet_NaN();
      for (const auto & child : gggs::children(grid)) {
        // Below `finest` no touched set exists; the rollup still needs the
        // level-14 depths, so descend through every child there.
        const bool consider = child.level() > finest || plan.isTouched(child);
        if (!consider) {
          continue;
        }
        const float d = decisionDepth(child);
        if (std::isnan(d)) {
          continue;
        }
        // Negative-down: the shallowest is the LARGEST value.
        shallowest = std::isnan(shallowest) ? d : std::max(shallowest, d);
      }
      decision_cache.emplace(grid, shallowest);
      return shallowest;
    };

  // ---- Descent ---------------------------------------------------------------
  const uint64_t n_req = policy.requiredObservations();
  std::function<void(const gggs::GridIndex &)> descend =
    [&](const gggs::GridIndex & grid) {
      const float depth = decisionDepth(grid);
      if (std::isnan(depth)) {
        return;  // no soundings anywhere beneath: nothing to write
      }
      PlannedTile tile;
      tile.index = grid;
      tile.decision_depth = depth;
      tile.required_level = marine_bathymetry_store::depthAdaptiveLevel(depth,
        policy.depth).level();
      const auto achieved = counts.achievedLevelPercentile(grid, policy.achieved_percentile, n_req);
      tile.achieved_level = (achieved && !achieved->saturated) ?
        static_cast<uint8_t>(std::clamp<int>(achieved->level, coarsest, finest)) : coarsest;
      const uint8_t target = tile.targetLevel();
      // A tile whose own target is coarser than its level is not emitted: its
      // parent (already emitted) carries the estimate for that ground. This is
      // what keeps a refined parent's deep-plain children from becoming fine
      // tiles beside a shoal. The root is always emitted (its target is
      // clamped to >= coarsest_level).
      if (target < grid.level()) {
        return;
      }
      if (target > grid.level() && grid.level() < finest) {
        tile.refined = true;
        plan.tiles_.emplace(grid, tile);
        for (const auto & child : gggs::children(grid)) {
          if (plan.isTouched(child)) {
            descend(child);
          }
        }
      } else {
        plan.tiles_.emplace(grid, tile);
      }
    };
  auto roots = plan.touched_.find(coarsest);
  if (roots != plan.touched_.end()) {
    for (const auto & root : roots->second) {
      descend(root);
    }
  }

  // Surveyed ground, per emitted tile and in total. Reported instead of tile
  // footprints, which over a single survey line overstate the ground by three
  // orders of magnitude (cube#143 dry-run review).
  plan.surveyed_ground_m2_ = surveyed_ground_m2;
  for (auto & [grid, tile] : plan.tiles_) {
    const auto g = ground_by_grid.find(grid);
    tile.ground_m2 = g == ground_by_grid.end() ? 0.0 : g->second;
  }
  return plan;
}

}  // namespace cube
