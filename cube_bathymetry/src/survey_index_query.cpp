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

#include "cube_bathymetry/survey_index_query.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "marine_survey_index/footprint.hpp"

namespace cube
{

namespace
{

// Reconstruct a GridIndex from the (level, row, column) triple the survey-index
// `passes` table stores. GridIndex has no public row/column constructor (only
// gggs::Level is its friend), so we resolve the tile's centre from the public
// LevelSpecs geometry and look it up through Level::gridIndex — the same round
// trip gggs::parent()/children() use. The centre of grid (row, col) is
// unambiguously inside that grid, so the lookup returns exactly it.
gggs::GridIndex tileFromRowCol(std::uint8_t level, std::uint32_t row, std::uint32_t col)
{
  const gggs::LevelSpecs & spec = gggs::levels[level];
  const double center_lat = -96.0 + (static_cast<double>(row) + 0.5) * spec.grid_angular_span;
  const double center_lon =
    -180.0 + (static_cast<double>(col) + 0.5) * spec.gridLongitudinalSpan(row);
  return gggs::Level(level).gridIndex(center_lat, center_lon);
}

// Roll a tile up the GGGS quadtree to `target_level` by iterating gggs::parent()
// one level per step (ADR-0002: L14 -> L10 is four applications). A tile already
// at or coarser than the target is returned unchanged — the index footprint is
// never coarser than the store level in practice (L14 >= L10).
gggs::GridIndex ancestorAtLevel(gggs::GridIndex tile, std::uint8_t target_level)
{
  while (tile.valid() && tile.level() > target_level) {
    tile = gggs::parent(tile);
  }
  return tile;
}

// The index-level (L14) footprint tiles the given new bags touched, read
// straight from the passes/bags join. Returns distinct tiles. `sensor_filter`,
// when non-empty, restricts the footprint to that exact sensor_type.
std::vector<gggs::GridIndex> newBagFootprint(
  sqlite3 * db,
  const std::vector<std::string> & new_bag_paths,
  const std::string & sensor_filter)
{
  std::vector<gggs::GridIndex> footprint;
  if (new_bag_paths.empty()) {
    return footprint;
  }

  std::string sql =
    "SELECT DISTINCT p.level, p.tile_row, p.tile_col"
    " FROM passes p JOIN bags b ON p.bag_id = b.id"
    " WHERE b.path = ?";
  if (!sensor_filter.empty()) {
    sql += " AND p.sensor_type = ?";
  }

  sqlite3_stmt * stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(
      std::string("survey_index_query: prepare footprint failed: ") + sqlite3_errmsg(db));
  }

  // A set keyed by GridIndex dedups tiles a bag touched more than once and tiles
  // shared across the new bags.
  std::set<gggs::GridIndex> distinct;
  for (const auto & path : new_bag_paths) {
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    if (!sensor_filter.empty()) {
      sqlite3_bind_text(stmt, 2, sensor_filter.c_str(), -1, SQLITE_TRANSIENT);
    }
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      const auto level = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 0));
      const auto row = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 1));
      const auto col = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 2));
      distinct.insert(tileFromRowCol(level, row, col));
    }
    if (rc != SQLITE_DONE) {
      sqlite3_finalize(stmt);
      throw std::runtime_error(
        std::string("survey_index_query: step footprint failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);

  footprint.assign(distinct.begin(), distinct.end());
  return footprint;
}

// Expand a set of same-level footprint tiles by one tile in every direction
// (ADR-0002 margin) and return the enumerated tiles. The margin is one FULL
// index-level tile (~54 m at L14), deliberately larger than a single L14 cell
// (~6 cm): the margin exists to cover the per-sounding influence radius (<=3 m)
// that batch_regen applies at L10 but the index does not record, and one cell
// cannot span that (see ADR-0002). Tiles are grouped by level so a mixed-level
// footprint (e.g. an index that stores per-sensor native levels) is handled.
std::vector<gggs::GridIndex> expandFootprint(const std::vector<gggs::GridIndex> & footprint)
{
  std::vector<gggs::GridIndex> expanded;
  if (footprint.empty()) {
    return expanded;
  }

  std::map<std::uint8_t, std::vector<gggs::GridIndex>> by_level;
  for (const auto & tile : footprint) {
    by_level[tile.level()].push_back(tile);
  }

  for (const auto & [level, tiles] : by_level) {
    double lat_min = std::numeric_limits<double>::max();
    double lat_max = std::numeric_limits<double>::lowest();
    double lon_min = std::numeric_limits<double>::max();
    double lon_max = std::numeric_limits<double>::lowest();
    for (const auto & tile : tiles) {
      lat_min = std::min(lat_min, tile.southLatitude());
      lat_max = std::max(lat_max, tile.northLatitude());
      lon_min = std::min(lon_min, tile.westLongitude());
      lon_max = std::max(lon_max, tile.eastLongitude());
    }

    // Pad the bounding box by one tile span in each direction. Latitude spans
    // are uniform at a level; longitude spans widen toward the poles, so pad
    // with the widest span among the footprint rows to stay conservative.
    const gggs::LevelSpecs & spec = gggs::levels[level];
    double lon_span = 0.0;
    for (const auto & tile : tiles) {
      lon_span = std::max(lon_span, spec.gridLongitudinalSpan(tile.row()));
    }
    lat_min -= spec.grid_angular_span;
    lat_max += spec.grid_angular_span;
    lon_min -= lon_span;
    lon_max += lon_span;

    const auto per_level = marine_survey_index::tilesForBoundingBox(
      lat_min, lon_min, lat_max, lon_max, gggs::Level(level));
    expanded.insert(expanded.end(), per_level.begin(), per_level.end());
  }
  return expanded;
}

}  // namespace

std::vector<DirtyTile> dirtyL10Tiles(
  sqlite3 * db,
  const std::vector<std::string> & new_bag_paths,
  const gggs::Level & store_level,
  const std::string & sensor_filter)
{
  // 1. Footprint of the new bags (index level, e.g. L14).
  const std::vector<gggs::GridIndex> footprint =
    newBagFootprint(db, new_bag_paths, sensor_filter);
  if (footprint.empty()) {
    return {};  // nothing new indexed -> no dirty tiles (caller may full-regen).
  }

  // 2. Expand by one index-level tile (conservative margin, ADR-0002).
  const std::vector<gggs::GridIndex> expanded = expandFootprint(footprint);

  // 3. Roll each expanded tile up to the store level and deduplicate -> dirty set.
  //    Seed the map so a margin-only tile (no overlapping pass) still appears.
  std::map<gggs::GridIndex, std::vector<marine_survey_index::PassRow>> dirty;
  for (const auto & tile : expanded) {
    dirty[ancestorAtLevel(tile, store_level.level())];
  }

  // 4. Attach every contributing pass (all bags) over the expanded footprint,
  //    grouped by the store-level tile its own footprint tile rolls up to.
  const std::vector<marine_survey_index::PassRow> passes =
    marine_survey_index::queryPasses(db, expanded, sensor_filter);
  for (const auto & pass : passes) {
    const gggs::GridIndex pass_tile =
      tileFromRowCol(pass.level, pass.tile_row, pass.tile_col);
    const gggs::GridIndex rolled = ancestorAtLevel(pass_tile, store_level.level());
    auto it = dirty.find(rolled);
    if (it != dirty.end()) {
      it->second.push_back(pass);
    }
  }

  std::vector<DirtyTile> result;
  result.reserve(dirty.size());
  for (auto & [tile, tile_passes] : dirty) {
    result.push_back(DirtyTile{tile, std::move(tile_passes)});
  }
  return result;
}

}  // namespace cube
