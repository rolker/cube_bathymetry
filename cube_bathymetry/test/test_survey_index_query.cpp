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
// and the no-op cases (no new bags / a bag not in the index). A second fixture
// runs the real batch_regen_bag dry-run against an on-disk index to pin the
// DIRTY_TILES_JSON marker contract (emitted on a hit, suppressed on a miss).

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
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

// Shared survey-index fixture: opens an index (in-memory for the pure query
// tests, on disk for the CLI-contract tests) and fills it with synthetic rows.
class IndexFixture : public testing::Test
{
protected:
  void openIndex(const std::string & path) {db_ = marine_survey_index::openIndexDb(path);}

  // The close is CHECKED, not fire-and-forget: sqlite3_close returns SQLITE_BUSY
  // while any prepared statement or blob handle on the connection is still open,
  // so a future statement leak in the query code -- precisely the bug class
  // StmtGuard exists to prevent -- would otherwise pass silently. Asserting
  // SQLITE_OK here makes every test in this file a standing leak guard.
  // sqlite3_errstr(rc), not sqlite3_errmsg(db_): on success the handle is freed,
  // so reading a message off it afterwards would be use-after-free.
  void TearDown() override
  {
    if (db_ == nullptr) {return;}
    const int rc = sqlite3_close(db_);
    db_ = nullptr;
    EXPECT_EQ(rc, SQLITE_OK) << "sqlite3_close: " << sqlite3_errstr(rc) <<
      " -- a prepared statement or blob handle was leaked by the query code";
  }

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

class DirtyTileQuery : public IndexFixture
{
protected:
  void SetUp() override {openIndex(":memory:");}
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

// A dirty L10 tile's contributing set must include an old-bag pass that lives in
// a DIFFERENT L14 sub-tile of that same L10 tile — one outside the new bag's
// footprint and its one-tile margin. This is the case the whole-L10-extent pass
// query exists for: querying passes over only the new-bag footprint (plus margin)
// would silently drop this pass, under-reporting the contributing bags and
// breaking a tile-scoped rebuild's byte-identity (cube#111 review must-fix).
TEST_F(DirtyTileQuery, ContributingPassesIncludeOldBagInSeparateL14Subtile)
{
  const gggs::GridIndex p10 = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  const double center_lat = 0.5 * (p10.southLatitude() + p10.northLatitude());
  const double center_lon = 0.5 * (p10.westLongitude() + p10.eastLongitude());
  const gggs::GridIndex center14 =
    gggs::Level(kIndexLevel).gridIndex(center_lat, center_lon);

  // p10's south-west-most L14 sub-tile: same L10 parent as the centre footprint,
  // but 16 L14 tiles away — far outside the centre footprint's 3x3 margin block.
  const double eps = 1e-7;
  const gggs::GridIndex corner14 = gggs::Level(kIndexLevel).gridIndex(
    p10.southLatitude() + eps, p10.westLongitude() + eps);
  ASSERT_NE(center14, corner14) << "test needs two distinct L14 sub-tiles of p10";

  insertBag(1, "/data/bagNew");
  insertBag(2, "/data/bagOld");
  insertPass(1, center14, "mbes-bathy", "/mbes", 1000, 1100, 40);  // new, centre
  insertPass(2, corner14, "mbes-bathy", "/mbes", 100, 200, 55);    // old, SW corner

  const auto dirty =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel));

  const cube::DirtyTile * dt = find(dirty, p10);
  ASSERT_NE(dt, nullptr);
  std::set<std::string> bags;
  for (const auto & p : dt->passes) {
    bags.insert(p.bag_path);
  }
  EXPECT_EQ(bags.count("/data/bagNew"), 1u);
  EXPECT_EQ(bags.count("/data/bagOld"), 1u)
    << "old-bag pass in a separate L14 sub-tile of the dirty L10 tile must contribute";
}

// An exact sensor_filter scopes BOTH the new-bag footprint and the
// contributing-pass set to that sensor_type. A new bag whose mbes and sidescan
// passes fall in geographically separate L10 tiles marks only the mbes tile
// dirty under an "mbes-bathy" filter, and that tile's contributing set excludes
// the co-located sidescan pass. Without a filter, both tiles are dirty.
TEST_F(DirtyTileQuery, SensorFilterExactScopesFootprintAndPasses)
{
  const gggs::GridIndex p10a = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex fp14a = gggs::Level(kIndexLevel).gridIndex(
    0.5 * (p10a.southLatitude() + p10a.northLatitude()),
    0.5 * (p10a.westLongitude() + p10a.eastLongitude()));

  // A second L10 tile ~0.5° east — dozens of L10 tiles away, so its margin never
  // reaches p10a and the two dirty sets stay disjoint.
  const gggs::GridIndex p10b = gggs::Level(kStoreLevel).gridIndex(kLat, kLon + 0.5);
  const gggs::GridIndex fp14b = gggs::Level(kIndexLevel).gridIndex(
    0.5 * (p10b.southLatitude() + p10b.northLatitude()),
    0.5 * (p10b.westLongitude() + p10b.eastLongitude()));
  ASSERT_NE(p10a, p10b) << "the two regions must be distinct L10 tiles";

  insertBag(1, "/data/bagNew");
  insertPass(1, fp14a, "mbes-bathy", "/mbes", 100, 200, 40);       // region A, mbes
  insertPass(1, fp14a, "sidescan_port", "/ss", 100, 200, 40);      // region A, sidescan
  insertPass(1, fp14b, "sidescan_port", "/ss", 100, 200, 40);      // region B, sidescan

  // Unfiltered: both regions are dirty.
  const auto all = cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel));
  EXPECT_TRUE(contains(all, p10a));
  EXPECT_TRUE(contains(all, p10b));

  // Filtered to mbes: only region A is dirty, and its passes are mbes-only.
  const auto mbes =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel), "mbes-bathy");
  EXPECT_TRUE(contains(mbes, p10a)) << "mbes footprint tile is dirty";
  EXPECT_FALSE(contains(mbes, p10b)) << "sidescan-only region must be excluded";
  const cube::DirtyTile * dt = find(mbes, p10a);
  ASSERT_NE(dt, nullptr);
  for (const auto & p : dt->passes) {
    EXPECT_EQ(p.sensor_type, "mbes-bathy") << "contributing set must be mbes-only";
  }
}

// The literal "sidescan" filter expands to the channel-split `LIKE 'sidescan%'`
// (matching newBagFootprint to marine_survey_index::queryPasses), so a
// "sidescan_port" pass is caught by both the "sidescan" alias and its exact
// sensor_type, while an unrelated "mbes-bathy" filter matches nothing.
TEST_F(DirtyTileQuery, SidescanFilterMatchesChannelSplitSensors)
{
  const gggs::GridIndex p10 = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(
    0.5 * (p10.southLatitude() + p10.northLatitude()),
    0.5 * (p10.westLongitude() + p10.eastLongitude()));

  insertBag(1, "/data/bagNew");
  insertPass(1, fp14, "sidescan_port", "/ss", 100, 200, 40);

  const auto alias =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel), "sidescan");
  EXPECT_TRUE(contains(alias, p10)) << "\"sidescan\" alias must match sidescan_port";

  const auto exact =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel), "sidescan_port");
  EXPECT_TRUE(contains(exact, p10)) << "exact sensor_type must match";

  const auto miss =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel), "mbes-bathy");
  EXPECT_TRUE(miss.empty()) << "non-matching filter yields no footprint";
}

// A new bag with passes recorded at two DIFFERENT index levels (L14 and L13, as a
// per-sensor native-level index would) exercises expandFootprint's by-level
// grouping and the multi-index-level contributing-pass re-enumeration: each level
// rolls up independently to its L10 store tile and every dirty tile lands at the
// store level.
TEST_F(DirtyTileQuery, MixedLevelFootprintRollsEachLevelToStore)
{
  const gggs::GridIndex p10a = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(
    0.5 * (p10a.southLatitude() + p10a.northLatitude()),
    0.5 * (p10a.westLongitude() + p10a.eastLongitude()));

  const gggs::GridIndex p10b = gggs::Level(kStoreLevel).gridIndex(kLat, kLon + 0.5);
  const gggs::GridIndex fp13 = gggs::Level(13).gridIndex(
    0.5 * (p10b.southLatitude() + p10b.northLatitude()),
    0.5 * (p10b.westLongitude() + p10b.eastLongitude()));
  ASSERT_EQ(fp14.level(), kIndexLevel);
  ASSERT_EQ(fp13.level(), 13u);
  ASSERT_NE(p10a, p10b);

  insertBag(1, "/data/bagNew");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 100, 200, 40);   // L14 pass
  insertPass(1, fp13, "mbes-bathy", "/mbes", 100, 200, 40);   // L13 pass

  const auto dirty =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel));

  const cube::DirtyTile * dta = find(dirty, p10a);
  const cube::DirtyTile * dtb = find(dirty, p10b);
  ASSERT_NE(dta, nullptr) << "L14-level pass must produce its L10 dirty tile";
  ASSERT_NE(dtb, nullptr) << "L13-level pass must produce its L10 dirty tile";
  EXPECT_EQ(dta->passes.size(), 1u);
  EXPECT_EQ(dtb->passes.size(), 1u);
  for (const auto & dt : dirty) {
    EXPECT_EQ(dt.tile.level(), kStoreLevel) << "every dirty tile rolls up to the store level";
  }
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

// A bag path not present in the index contributes no footprint -- `bags.path` is
// matched EXACTLY, so a never-indexed (or differently-spelled) path selects no
// rows. The query itself just returns an empty set; distinguishing that miss from
// "indexed, nothing dirty" is the CLI's job, and it does so by suppressing the
// DIRTY_TILES_JSON marker (see DryRunCliContract.IndexMissEmitsNoMarker).
TEST_F(DirtyTileQuery, BagNotInIndexYieldsNoDirtyTiles)
{
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(kLat, kLon);
  insertBag(1, "/data/bagIndexed");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 100, 200, 40);

  const auto dirty =
    cube::dirtyL10Tiles(db_, {"/data/bagNeverIndexed"}, gggs::Level(kStoreLevel));
  EXPECT_TRUE(dirty.empty());
}

// Rows of one bag that tie on (bag_path, t_start_ns) -- all queryPasses itself
// orders on, with a non-stable sort -- still come back in a deterministic order,
// broken by topic, then sensor type, then ping count. Inserted in reverse of the
// expected order so a pass-through of index/insert order would fail. This is the
// property the CLI's byte-stable DIRTY_TILES_JSON rests on.
TEST_F(DirtyTileQuery, TiedPassesComeBackInADeterministicOrder)
{
  const gggs::GridIndex p10 = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(
    0.5 * (p10.southLatitude() + p10.northLatitude()),
    0.5 * (p10.westLongitude() + p10.eastLongitude()));

  insertBag(1, "/data/bagNew");
  insertPass(1, fp14, "sonarB", "/mbes", 100, 200, 40);
  insertPass(1, fp14, "sonarA", "/mbes", 100, 200, 40);
  insertPass(1, fp14, "sonarA", "/mbes", 100, 200, 10);
  insertPass(1, fp14, "mbes-bathy", "/alpha", 100, 200, 40);

  const auto dirty =
    cube::dirtyL10Tiles(db_, {"/data/bagNew"}, gggs::Level(kStoreLevel));

  ASSERT_EQ(dirty.size(), 1u);
  const auto & passes = dirty[0].passes;
  ASSERT_EQ(passes.size(), 4u);
  const std::vector<std::tuple<std::string, std::string, std::int64_t>> expected{
    {"/alpha", "mbes-bathy", 40}, {"/mbes", "sonarA", 10},
    {"/mbes", "sonarA", 40}, {"/mbes", "sonarB", 40}};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(
      std::make_tuple(passes[i].topic, passes[i].sensor_type, passes[i].ping_count),
      expected[i]) << "pass " << i << " out of contract order";
  }
}

#ifdef BATCH_REGEN_BAG_EXE
// CLI stdout contract (cube#111, ADR-0002). The dry-run's DIRTY_TILES_JSON marker
// is documented as AUTHORITATIVE, so its presence/absence -- not the exit code --
// is what a PR2 consumer keys off. These tests run the real batch_regen_bag
// binary against an on-disk survey index and pin both halves of that contract.
class DryRunCliContract : public IndexFixture
{
protected:
  void SetUp() override
  {
    dir_ = std::filesystem::temp_directory_path() /
      ("cube111_dryrun_" + std::to_string(static_cast<std::int64_t>(::getpid())));
    std::filesystem::remove_all(dir_);
    ASSERT_TRUE(std::filesystem::create_directories(dir_));
    db_path_ = (dir_ / "survey_index.db").string();
    openIndex(db_path_);
  }

  void TearDown() override
  {
    IndexFixture::TearDown();  // checked sqlite3_close
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  // Run the dry-run CLI for one bag path; returns merged stdout+stderr and
  // reports the process exit status through @p exit_status.
  std::string runDryRun(const std::string & bag_path, int * exit_status)
  {
    const std::string command =
      std::string(BATCH_REGEN_BAG_EXE) + " --index-db '" + db_path_ + "' '" +
      bag_path + "' 2>&1";
    std::string output;
    FILE * pipe = popen(command.c_str(), "r");
    EXPECT_NE(pipe, nullptr) << "could not run " << command;
    if (pipe == nullptr) {
      *exit_status = -1;
      return output;
    }
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
      output += buffer.data();
    }
    const int status = pclose(pipe);
    *exit_status = (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
    return output;
  }

  std::filesystem::path dir_;
  std::string db_path_;
};

// Positive half of the contract: a bag that IS in the index, with a pass, gets a
// marker carrying a non-empty dirty set.
TEST_F(DryRunCliContract, IndexedBagEmitsTheMarker)
{
  const gggs::GridIndex p10 = gggs::Level(kStoreLevel).gridIndex(kLat, kLon);
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(
    0.5 * (p10.southLatitude() + p10.northLatitude()),
    0.5 * (p10.westLongitude() + p10.eastLongitude()));
  insertBag(1, "/data/bagIndexed");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 100, 200, 40);

  int exit_status = -1;
  const std::string output = runDryRun("/data/bagIndexed", &exit_status);

  EXPECT_EQ(exit_status, 0) << output;
  EXPECT_NE(output.find("DIRTY_TILES_JSON:"), std::string::npos)
    << "an indexed bag must emit the authoritative marker\n" << output;
  EXPECT_EQ(output.find("\"dirty_tile_count\":0"), std::string::npos)
    << "an indexed bag with a pass must report a non-empty dirty set\n" << output;
}

// Negative half, the finding this test exists for: a bag the index does not know
// (paths are matched exactly) must NOT emit the marker. Emitting it with an empty
// set would read as "indexed, nothing dirty" and a consumer would skip the full
// regen it actually needs. Exit stays 0 -- a soft-dependency miss is not an error,
// which is exactly why the marker, not the exit code, is the signal.
TEST_F(DryRunCliContract, IndexMissEmitsNoMarker)
{
  const gggs::GridIndex fp14 = gggs::Level(kIndexLevel).gridIndex(kLat, kLon);
  insertBag(1, "/data/bagIndexed");
  insertPass(1, fp14, "mbes-bathy", "/mbes", 100, 200, 40);

  int exit_status = -1;
  const std::string output = runDryRun("/data/bagNeverIndexed", &exit_status);

  EXPECT_EQ(exit_status, 0) << output;
  EXPECT_EQ(output.find("DIRTY_TILES_JSON:"), std::string::npos)
    << "an index miss must suppress the authoritative marker\n" << output;
  EXPECT_NE(output.find("FULL regen"), std::string::npos)
    << "an index miss must say a real run falls back to full regen\n" << output;
}
#endif  // BATCH_REGEN_BAG_EXE

}  // namespace
