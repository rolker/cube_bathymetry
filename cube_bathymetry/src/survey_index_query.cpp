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
  // Validate the DB-sourced level BEFORE indexing gggs::levels (a std::array of
  // 21 specs whose operator[] is unchecked): gggs::Level's ctor throws
  // std::out_of_range for level >= 21, so a corrupted index row surfaces as a
  // catchable exception — the dry-run's catch falls back to full regen — rather
  // than out-of-bounds undefined behaviour.
  const gggs::Level tile_level(level);
  const gggs::LevelSpecs & spec = gggs::levels[level];
  const double center_lat = -96.0 + (static_cast<double>(row) + 0.5) * spec.grid_angular_span;
  const double center_lon =
    -180.0 + (static_cast<double>(col) + 0.5) * spec.gridLongitudinalSpan(row);
  return tile_level.gridIndex(center_lat, center_lon);
}

// Roll a tile up the GGGS quadtree to `target_level` by iterating gggs::parent()
// one level per step (ADR-0002: L14 -> L10 is four applications). The index
// footprint is never coarser than the store level in practice (L14 >= L10), so
// the roll-up only ever moves toward the coarser target.
//
// Preconditions/postconditions are enforced, not just assumed: rolling a tile
// that is already coarser than the target (store level finer than the index
// footprint) can't reach `target_level` and would silently emit a wrong-level
// dirty tile, and an invalid result tile collapses every such case onto one key
// in the caller's dirty map (merging unrelated tiles). Both throw so the CLI
// dry-run's catch falls back to full regen instead of trusting a mis-levelled
// dirty set.
gggs::GridIndex ancestorAtLevel(gggs::GridIndex tile, std::uint8_t target_level)
{
  if (tile.valid() && tile.level() < target_level) {
    throw std::invalid_argument(
      "survey_index_query: store level is finer than the index footprint level "
      "(cannot roll a tile up to a finer level)");
  }
  while (tile.valid() && tile.level() > target_level) {
    tile = gggs::parent(tile);
  }
  if (!tile.valid() || tile.level() != target_level) {
    throw std::runtime_error(
      "survey_index_query: could not roll tile up to the store level "
      "(invalid tile in the index footprint)");
  }
  return tile;
}

// RAII owner for a prepared statement. Every exit path from the step loop below
// — including a throw out of tileFromRowCol on a corrupt (level >= 21) index row
// — must finalize the statement: an unfinalized statement makes the caller's
// sqlite3_close(db) return SQLITE_BUSY and leak the db handle too. The dry-run
// CLI is process-exit-bounded, but PR2 reuses dirtyL10Tiles from a long-lived
// rebuild path, so the invariant is enforced structurally rather than by
// remembering a finalize call at each throw site.
class StmtGuard
{
public:
  explicit StmtGuard(sqlite3_stmt * stmt)
  : stmt_(stmt) {}
  ~StmtGuard()
  {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }
  StmtGuard(const StmtGuard &) = delete;
  StmtGuard & operator=(const StmtGuard &) = delete;

private:
  sqlite3_stmt * stmt_;
};

// The index-level (L14) footprint tiles the given new bags touched, read
// straight from the passes/bags join. Returns distinct tiles. `sensor_filter`,
// when non-empty, restricts the footprint to that sensor with the SAME semantics
// as marine_survey_index::queryPasses: the literal "sidescan" expands to the
// channel-split `LIKE 'sidescan%'`, any other value is an exact match.
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
  // Mirror marine_survey_index::appendSensorClause exactly so this footprint
  // scopes sensors identically to queryPasses (dirtyL10Tiles step 5 below):
  // "sidescan" expands to the channel-split LIKE and binds nothing; any other
  // non-empty filter is an exact match with a bound value. Diverging (exact-only)
  // would make a "sidescan"-scoped dirty query silently return an empty footprint.
  std::string sensor_bind_value;
  if (sensor_filter == "sidescan") {
    sql += " AND p.sensor_type LIKE 'sidescan%'";
  } else if (!sensor_filter.empty()) {
    sql += " AND p.sensor_type = ?";
    sensor_bind_value = sensor_filter;
  }

  sqlite3_stmt * stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error(
      std::string("survey_index_query: prepare footprint failed: ") + sqlite3_errmsg(db));
  }
  // Owns `stmt` from here on: every return/throw below finalizes it.
  StmtGuard stmt_guard(stmt);

  // A set keyed by GridIndex dedups tiles a bag touched more than once and tiles
  // shared across the new bags.
  std::set<gggs::GridIndex> distinct;
  for (const auto & path : new_bag_paths) {
    // Check bind return codes like the prepare/step handling above and below:
    // a silent bind failure would run the query with a stale/missing parameter.
    if (sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
      (!sensor_bind_value.empty() &&
      sqlite3_bind_text(stmt, 2, sensor_bind_value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK))
    {
      throw std::runtime_error(
        std::string("survey_index_query: bind footprint failed: ") + sqlite3_errmsg(db));
    }
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      const auto level = static_cast<std::uint8_t>(sqlite3_column_int(stmt, 0));
      const auto row = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 1));
      const auto col = static_cast<std::uint32_t>(sqlite3_column_int64(stmt, 2));
      distinct.insert(tileFromRowCol(level, row, col));
    }
    if (rc != SQLITE_DONE) {
      throw std::runtime_error(
        std::string("survey_index_query: step footprint failed: ") + sqlite3_errmsg(db));
    }
    sqlite3_reset(stmt);
  }

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

  // One bounding box per level over ALL hit tiles at that level. Disjoint
  // footprints (e.g. two separate survey areas in one incremental batch) are
  // merged into a single box, so the padded box spans the gap between them and
  // marks the intervening tiles dirty. That stays a conservative superset (the
  // extra tiles rebuild bit-identically); accepted for simplicity — see
  // ADR-0002 Consequences.
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

  // 4. Enumerate the COMPLETE index-level extent of every dirty store-level tile.
  //    `expanded` is only the new bags' footprint plus one-tile margin, so it
  //    covers a boundary dirty tile only partially — querying passes over it
  //    would omit contributing passes (typically old bags) that fall in the
  //    dirty tile's other index-level sub-tiles. That under-reports the
  //    contributing-bag set here and, more seriously, would break PR2 byte-
  //    identity (a tile-scoped rebuild must replay EVERY pass touching the tile).
  //    So re-enumerate each dirty tile's full extent at each index level present
  //    in the footprint (queryPasses matches (level,row,col) exactly, so the
  //    query tiles must be at the passes' own level, not the store level) and
  //    union — a conservative superset of the tiles' contributing passes.
  std::set<std::uint8_t> index_levels;
  for (const auto & tile : footprint) {
    index_levels.insert(tile.level());
  }
  std::set<gggs::GridIndex> query_tiles;
  for (const auto & entry : dirty) {
    const gggs::GridIndex & dirty_tile = entry.first;
    for (const auto index_level : index_levels) {
      const auto cover = marine_survey_index::tilesForBoundingBox(
        dirty_tile.southLatitude(), dirty_tile.westLongitude(),
        dirty_tile.northLatitude(), dirty_tile.eastLongitude(),
        gggs::Level(index_level));
      query_tiles.insert(cover.begin(), cover.end());
    }
  }

  // 5. Attach every contributing pass (all bags) over that full extent, grouped
  //    by the store-level tile its own footprint tile rolls up to. A pass rolling
  //    up to a tile outside the dirty set (an edge neighbour the bounding-box
  //    enumeration picked up) has no map entry and is dropped.
  const std::vector<gggs::GridIndex> query_vec(query_tiles.begin(), query_tiles.end());
  const std::vector<marine_survey_index::PassRow> passes =
    marine_survey_index::queryPasses(db, query_vec, sensor_filter);
  for (const auto & pass : passes) {
    const gggs::GridIndex pass_tile =
      tileFromRowCol(pass.level, pass.tile_row, pass.tile_col);
    const gggs::GridIndex rolled = ancestorAtLevel(pass_tile, store_level.level());
    auto it = dirty.find(rolled);
    if (it != dirty.end()) {
      it->second.push_back(pass);
    }
  }

  // 6. Sort each dirty tile's passes so the header's ordering contract holds
  //    INDEPENDENTLY of queryPasses' internals. queryPasses currently sorts its
  //    merged result itself, so today this is a no-op — it decouples this
  //    function's documented ordering from that implementation detail rather
  //    than relying on it.
  //
  //    The key is (bag_path, t_start_ns, topic, t_end_ns, tile_row, tile_col):
  //    std::sort is NOT stable, so any tie left unbroken has a toolchain- and
  //    input-order-dependent relative order, and one bag can contribute several
  //    passes to the same store tile (multiple sonar topics, several index-level
  //    sub-tiles). Those extra keys make the emitted order — and therefore the
  //    CLI's DIRTY_TILES_JSON bytes, which PR2's byte-identity check leans on —
  //    a total, reproducible order over the rows a tile can hold.
  std::vector<DirtyTile> result;
  result.reserve(dirty.size());
  for (auto & [tile, tile_passes] : dirty) {
    std::sort(
      tile_passes.begin(), tile_passes.end(),
      [](const marine_survey_index::PassRow & a, const marine_survey_index::PassRow & b) {
        if (a.bag_path != b.bag_path) {
          return a.bag_path < b.bag_path;
        }
        if (a.t_start_ns != b.t_start_ns) {
          return a.t_start_ns < b.t_start_ns;
        }
        if (a.topic != b.topic) {
          return a.topic < b.topic;
        }
        if (a.t_end_ns != b.t_end_ns) {
          return a.t_end_ns < b.t_end_ns;
        }
        if (a.tile_row != b.tile_row) {
          return a.tile_row < b.tile_row;
        }
        return a.tile_col < b.tile_col;
      });
    result.push_back(DirtyTile{tile, std::move(tile_passes)});
  }
  return result;
}

}  // namespace cube
