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

// Dirty-tile query unit tests (cube_bathymetry#111, ADR-0002). An in-memory
// SQLite survey index is populated with synthetic L14 passes spanning known
// tiles; the tests assert the L14->L10 rollup (four gggs::parent() applications),
// the one-tile conservative margin, the contributing-bag set (old + new bags),
// and the no-op cases (no new bags / a bag not in the index).

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "cube_bathymetry/survey_index_query.h"
#include "marine_autonomy/gggs.h"
#include "marine_survey_index/schema.hpp"

namespace
{

// A temperate survey point (Massabesic-ish) so the polar longitude scale factor
// is 1 and L14 rolls up to L10 in the ordinary 2x2-per-level quadtree.
constexpr double kLat = 43.02;
constexpr double kLon = -71.36;

// Store level (bathy store) and index level per ADR-0002: L14 -> L10 is four
// gggs::parent() applications.
constexpr std::uint8_t kStoreLevel = 10;
constexpr std::uint8_t kIndexLevel = 14;

bool contains(const std::vector<cube::DirtyTile> & dirty, const gggs::GridIndex & tile)
{
  return std::any_of(
    dirty.begin(), dirty.end(),
    [&](const cube::DirtyTile & dt) {return dt.tile == tile;});
}

const cube::DirtyTile * find(
  const std::vector<cube::DirtyTile> & dirty, const gggs::GridIndex & tile)
{
  for (const auto & dt : dirty) {
    if (dt.tile == tile) {return &dt;}
  }
  return nullptr;
}

class DirtyTileQuery : public testing::Test
{
protected:
  void SetUp() override {db_ = marine_survey_index::openIndexDb(":memory:");}
  void TearDown() override {sqlite3_close(db_);}

  void exec(const std::string & sql)
  {
    ASSERT_EQ(sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, nullptr), SQLITE_OK)
      << sqlite3_errmsg(db_);
  }

  void insertBag(int id, const std::string & path)
  {
    exec(
      "INSERT INTO bags (id, path, size_bytes, mtime_ns, indexed_at_ns) VALUES (" +
      std::to_string(id) + ", '" + path + "', 0, 0, 0)");
  }

  void insertPass(
    int bag_id, const gggs::GridIndex & tile, const std::string & sensor,
    const std::string & topic, std::int64_t t0, std::int64_t t1, int pings)
  {
    exec(
      "INSERT INTO passes (bag_id, level, tile_row, tile_col, sensor_type, topic,"
      " t_start_ns, t_end_ns, ping_count) VALUES (" +
      std::to_string(bag_id) + ", " + std::to_string(tile.level()) + ", " +
      std::to_string(tile.row()) + ", " + std::to_string(tile.column()) + ", '" +
      sensor + "', '" + topic + "', " + std::to_string(t0) + ", " +
      std::to_string(t1) + ", " + std::to_string(pings) + ")");
  }

  sqlite3 * db_ = nullptr;
};

// A single new bag whose L14 footprint sits in the MIDDLE of an L10 tile rolls up
// to exactly that one L10 tile (four gggs::parent() applications), and the margin
// does not spill into a neighbour because the footprint is far from every edge.
TEST_F(DirtyTileQuery, RollsL14FootprintUpToItsL10Parent)
{
  const gggs::GridIndex p10 = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  const double center_lat = 0.5 * (p10.southLatitude() + p10.northLatitude());
  const double center_lon = 0.5 * (p10.westLongitude() + p10.eastLongitude());
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(center_lat, center_lon);
  ASSERT_EQ(fp14.level(), kIndexLevel);

  insertBag(1, "/data/bagNew");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 100, 200, 40);

  const auto dirty =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel));

  ASSERT_EQ(dirty.size(), 1u) << "centre footprint should mark exactly one L10 tile";
  EXPECT_EQ(dirty[0].tile, p10);
  EXPECT_EQ(dirty[0].tile.level(), kStoreLevel);
  EXPECT_EQ(dirty[0].passes.size(), 1u);
  EXPECT_EQ(dirty[0].passes[0].bag_path, "/data/bagNew");
}

// A footprint tile hard against the south-west corner of its L10 parent: the
// one-tile ADR-0002 margin reaches across the L10 boundary, so the west and
// south L10 neighbours become dirty too (a conservative superset).
TEST_F(DirtyTileQuery, OneTileMarginMarksAdjacentL10Tiles)
{
  const gggs::GridIndex p10 = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  // The south-west-most L14 tile of p10 shares p10's south + west edges (L10
  // boundaries are exact multiples of the L14 span).
  const double eps = 1e-7;
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(
    p10.southLatitude() + eps, p10.westLongitude() + eps);

  insertBag(1, "/data/bagNew");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 100, 200, 40);

  const auto dirty =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel));

  const double mid_lat = 0.5 * (p10.southLatitude() + p10.northLatitude());
  const double mid_lon = 0.5 * (p10.westLongitude() + p10.eastLongitude());
  const gggs::GridIndex west =
    gggs::Level(kStoreLevel).gridIndex(mid_lat, p10.westLongitude() - 1e-6);
  const gggs::GridIndex south =
    gggs::Level(kStoreLevel).gridIndex(p10.southLatitude() - 1e-6, mid_lon);

  EXPECT_TRUE(contains(dirty, p10)) << "the footprint's own L10 tile is dirty";
  EXPECT_TRUE(contains(dirty, west)) << "west neighbour pulled in by the margin";
  EXPECT_TRUE(contains(dirty, south)) << "south neighbour pulled in by the margin";
  EXPECT_GE(dirty.size(), 3u);
  for (const auto & dt : dirty) {
    EXPECT_EQ(dt.tile.level(), kStoreLevel);
  }
}

// The contributing-pass set for a dirty tile includes passes from OLD bags that
// already cover it, not just the new bag being queried — the input a tile-scoped
// rebuild needs (rebuild each dirty tile from ALL bags that touch it).
TEST_F(DirtyTileQuery, ContributingPassesIncludeOldBags)
{
  const gggs::GridIndex p10 = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  const double center_lat = 0.5 * (p10.southLatitude() + p10.northLatitude());
  const double center_lon = 0.5 * (p10.westLongitude() + p10.eastLongitude());
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(center_lat, center_lon);

  insertBag(1, "/data/bagNew");
  insertBag(2, "/data/bagOld");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 1000, 1100, 40);
  insertPass(2, fp14, "mbes-bathy", "/mbes", 100, 200, 55);   // older, same tile

  const auto dirty =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel));

  const cube::DirtyTile * dt = find(dirty, p10);
  ASSERT_NE(dt, nullptr);
  std::set<std::string> bags;
  for (const auto & p : dt->passes) {
    bags.insert(p.bag_path);
                                                             }
  EXPECT_EQ(bags.count("/data/bagNew"), 1u);
  EXPECT_EQ(bags.count("/data/bagOld"), 1u) << "old bag over the tile must contribute";
}

// No new bags -> nothing is dirty (the incremental caller has no work to do).
TEST_F(DirtyTileQuery, NoNewBagsYieldsNoDirtyTiles)
{
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(kLat, kLon);
  insertBag(1, "/data/bagNew");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 100, 200, 40);

  const auto dirty = cube::dirtyL10Tiles(db_, {}, gggs::Level(kStoreLevel));
  EXPECT_TRUE(dirty.empty());
}

// A bag path not present in the index contributes no footprint (the CLI then
// falls back to full regen); the query itself just returns an empty set.
TEST_F(DirtyTileQuery, BagNotInIndexYieldsNoDirtyTiles)
{
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(kLat, kLon);
  insertBag(1, "/data/bagIndexed");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 100, 200, 40);

  const auto dirty =
    cube::dirtyL10Tiles(db_, {"/data/bagNeverIndexed"}, gggs::Level(kStoreLevel));
  EXPECT_TRUE(dirty.empty());
}

}  // namespace
