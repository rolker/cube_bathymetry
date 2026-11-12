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

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cube_bathymetry/build_fingerprint.h"
#include "cube_bathymetry/derived_tiles.h"
#include "marine_autonomy/gggs.h"

/// @file
/// @brief The slope-run store record and its crash-safe publish
///        (cube_bathymetry#173): manifest round trip and strict loader, bag
///        identity, depth-tile selection, the lock, and publish/recovery with an
///        injected failure at every point.

namespace cube
{
namespace
{

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string freshDir(const std::string & tag)
{
  static int counter = 0;
  const fs::path dir = fs::temp_directory_path() /
    ("cube_derived_" + tag + "_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

void writeFile(const fs::path & path, const std::string & text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string readFile(const fs::path & path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Every file under @p dir (relative path -> bytes), the lock file left out.
std::map<std::string, std::string> snapshot(const std::string & dir)
{
  std::map<std::string, std::string> out;
  for (const auto & entry : fs::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().filename() == DerivedTiles::kLockFilename) {
      continue;
    }
    out[fs::relative(entry.path(), dir).string()] = readFile(entry.path());
  }
  return out;
}

std::string shaA() {return std::string(64, 'a');}
std::string shaB() {return std::string(64, 'b');}

StoreInputs sampleInputs()
{
  StoreInputs in;
  in.slope_baseline_deg = 5.0;
  in.slope_layers = "processed,draft";
  in.correction = "table:" + shaA();
  in.gain_declaration_sha256 = shaB();
  in.gain_reference_state = "pre_step";
  in.level = 14;
  return in;
}

TileRecord sampleTile(const std::string & bag)
{
  TileRecord t;
  t.contributors = {{bag, shaA()}};
  t.depth_tiles = {{"processed/14_9_8.tif", shaB()}};
  t.stencil_margin_m = 12.5;
  t.written = "2026-11-12T12:00:00Z";
  t.code_version = kDerivedTilesCodeVersion;
  return t;
}

DerivedTiles sampleManifest()
{
  DerivedTiles d;
  d.inputs = sampleInputs();
  d.tiles["14_9_8.tif"] = sampleTile("bag_a");
  d.tiles["14_9_9.tif"] = sampleTile("bag_b");
  return d;
}

}  // namespace

// ---- manifest -------------------------------------------------------------

TEST(DerivedTiles, RoundTripsThroughTextAndDisk)
{
  DerivedTiles d = sampleManifest();
  d.last_publish = PublishRecord{"run1", {"14_9_8.tif"}, {"14_9_9.tif"}, true};
  const DerivedTiles back = DerivedTiles::fromJson(d.toJson());
  EXPECT_EQ(back.toJson(), d.toJson());
  EXPECT_TRUE(back.inputs == d.inputs);
  ASSERT_EQ(back.tiles.size(), 2u);
  EXPECT_EQ(back.tiles.at("14_9_8.tif").contributors[0].name, "bag_a");
  ASSERT_TRUE(back.last_publish);
  EXPECT_TRUE(back.last_publish->registry);

  const std::string dir = freshDir("roundtrip");
  EXPECT_FALSE(DerivedTiles::load(dir).has_value());
  EXPECT_FALSE(hasDerivedTilesRecord(dir));
  d.write(dir);
  EXPECT_TRUE(hasDerivedTilesRecord(dir));
  const auto loaded = DerivedTiles::load(dir);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded->toJson(), d.toJson());
  EXPECT_FALSE(fs::exists(fs::path(dir) / "derived_tiles.json.tmp"));
  fs::remove_all(dir);
}

TEST(DerivedTiles, APendingManifestAloneCountsAsARecord)
{
  const std::string dir = freshDir("pending_record");
  writeFile(fs::path(dir) / DerivedTiles::kPendingFilename, "{}");
  EXPECT_TRUE(hasDerivedTilesRecord(dir));
  fs::remove_all(dir);
}

TEST(DerivedTiles, TheLoaderIsStrict)
{
  const json good = json::parse(sampleManifest().toJson());
  struct Case
  {
    const char * tag;
    std::function<void(json &)> mutate;
    const char * expect;
  };
  const std::vector<Case> cases = {
    {"version", [](json & j) {j["schema_version"] = 2;}, "unsupported schema_version"},
    {"version_text", [](json & j) {j["schema_version"] = "1";}, "unsupported schema_version"},
    {"format", [](json & j) {j["format"] = "derived-tiles v2";}, "unsupported format"},
    {"no_inputs", [](json & j) {j.erase("inputs");}, "lacks 'inputs'"},
    {"unknown_top", [](json & j) {j["extra"] = 1;}, "unknown key 'extra'"},
    {"unknown_input", [](json & j) {j["inputs"]["extra"] = 1;}, "unknown key 'extra'"},
    {"basis", [](json & j) {j["inputs"]["angle_basis"] = "beam";}, "must be 'incidence'"},
    {"baseline_type", [](json & j) {j["inputs"]["slope_baseline_deg"] = "5";}, "must be a number"},
    {"correction", [](json & j) {j["inputs"]["correction"] = "table:xyz";}, "inputs.correction"},
    {"level_type", [](json & j) {j["inputs"]["level"] = 14.5;}, "must be an integer"},
    {"tile_name", [](json & j) {j["tiles"]["../x.tif"] = j["tiles"]["14_9_8.tif"];},
      "not a tile file name"},
    {"tile_missing_key", [](json & j) {j["tiles"]["14_9_8.tif"].erase("written");},
      "lacks 'written'"},
    {"tile_unknown_key", [](json & j) {j["tiles"]["14_9_8.tif"]["x"] = 1;}, "unknown key 'x'"},
    {"contributor_hash", [](json & j) {
        j["tiles"]["14_9_8.tif"]["contributors"][0]["metadata_sha256"] = "abc";
      },
      "bare bag name and a sha256"},
    {"contributor_path", [](json & j) {
        j["tiles"]["14_9_8.tif"]["contributors"][0]["name"] = "a/b";
      },
      "bare bag name"},
    {"contributor_dup", [](json & j) {
        auto c = j["tiles"]["14_9_8.tif"]["contributors"][0];
        j["tiles"]["14_9_8.tif"]["contributors"].push_back(c);
      },
      "sorted and without duplicates"},
    {"depth_abs", [](json & j) {
        j["tiles"]["14_9_8.tif"]["depth_tiles"][0]["path"] = "/etc/x";
      },
      "relative path"},
    {"margin_negative", [](json & j) {j["tiles"]["14_9_8.tif"]["stencil_margin_m"] = -1.0;},
      "must not be negative"},
    {"publish_name", [](json & j) {
        j["last_publish"] = {{"run", "r"}, {"written", {"x.tif"}}, {"removed", json::array()},
          {"registry", false}};
      },
      "not a tile file name"},
  };
  for (const auto & c : cases) {
    json j = good;
    c.mutate(j);
    try {
      DerivedTiles::fromJson(j.dump());
      ADD_FAILURE() << c.tag << ": accepted";
    } catch (const std::exception & e) {
      EXPECT_NE(std::string(e.what()).find(c.expect), std::string::npos)
        << c.tag << ": " << e.what();
    }
  }
}

TEST(DerivedTiles, ADuplicateKeyIsRefusedAnywhere)
{
  std::string text = sampleManifest().toJson();
  // A second "level" in inputs, and a second tile entry.
  const std::string level = "\"level\": 14";
  text.replace(text.find(level), level.size(), level + ",\n    " + level);
  try {
    DerivedTiles::fromJson(text);
    ADD_FAILURE() << "accepted a duplicate key";
  } catch (const std::exception & e) {
    EXPECT_NE(std::string(e.what()).find("'level' is given twice"), std::string::npos) << e.what();
  }
  std::string text2 = sampleManifest().toJson();
  const std::string tile = "\"14_9_8.tif\": {";
  text2.replace(text2.find(tile), tile.size(), "\"14_9_9.tif\": {}, " + tile);
  EXPECT_THROW(DerivedTiles::fromJson(text2), std::runtime_error);
}

TEST(DerivedTiles, MalformedFilesAreErrorsNotDefaults)
{
  EXPECT_THROW(DerivedTiles::fromJson(""), std::runtime_error);
  EXPECT_THROW(DerivedTiles::fromJson("[]"), std::runtime_error);
  EXPECT_THROW(DerivedTiles::fromJson("{\"format\": "), std::runtime_error);
  const std::string dir = freshDir("malformed");
  writeFile(fs::path(dir) / DerivedTiles::kFilename, "not json");
  try {
    DerivedTiles::load(dir);
    ADD_FAILURE() << "loaded a malformed file";
  } catch (const std::exception & e) {
    EXPECT_NE(std::string(e.what()).find(DerivedTiles::kFilename), std::string::npos) << e.what();
  }
  fs::remove_all(dir);
}

TEST(DerivedTiles, TheStoreInputsNameTheirFirstDifference)
{
  const StoreInputs a = sampleInputs();
  EXPECT_TRUE(a == sampleInputs());
  EXPECT_TRUE(a.difference(sampleInputs()).empty());
  StoreInputs b = a;
  b.slope_baseline_deg = 6.0;
  EXPECT_NE(a.difference(b).find("slope baseline angle"), std::string::npos);
  b = a;
  b.correction = "none";
  EXPECT_NE(a.difference(b).find("the correction differs"), std::string::npos);
  b = a;
  b.gain_declaration_sha256 = "";
  b.gain_reference_state = "";
  EXPECT_NE(a.difference(b).find("gain declaration"), std::string::npos);
  b = a;
  b.level = 13;
  EXPECT_NE(a.difference(b).find("store level"), std::string::npos);
  b = a;
  b.slope_layers = "processed";
  EXPECT_NE(a.difference(b).find("surface layers"), std::string::npos);
}

// ---- bags -----------------------------------------------------------------

TEST(BagIdentity, NameIsNormalisedAndHashIsOfMetadata)
{
  const std::string a = freshDir("bags_a");
  const std::string b = freshDir("bags_b");
  const std::string bag_a = (fs::path(a) / "survey_bag").string();
  const std::string bag_b = (fs::path(b) / "survey_bag").string();
  writeFile(fs::path(bag_a) / "metadata.yaml", "version: 9\n");
  writeFile(fs::path(bag_b) / "metadata.yaml", "version: 9\n");
  EXPECT_EQ(normalizedBagName(bag_a + "///"), "survey_bag");
  const BagIdentity id = bagIdentity(bag_a + "/");
  EXPECT_EQ(id.name, "survey_bag");
  EXPECT_EQ(id.metadata_sha256, sha256Text("version: 9\n"));
  // The same bag copied to another disk is the same bag.
  EXPECT_TRUE(bagIdentity(bag_b) == id);
  // A different bag of the same name is told apart by its metadata.
  writeFile(fs::path(bag_b) / "metadata.yaml", "version: 10\n");
  const BagIdentity other = bagIdentity(bag_b);
  EXPECT_FALSE(other == id);
  EXPECT_NE(duplicateBagProblem({id, other}).find("two bags are named 'survey_bag'"),
    std::string::npos);
  EXPECT_NE(duplicateBagProblem({id, id}).find("only once"), std::string::npos);
  EXPECT_TRUE(duplicateBagProblem({id, BagIdentity{"another", shaA()}}).empty());
  fs::remove_all(a);
  fs::remove_all(b);
}

TEST(BagIdentity, ABagThatCannotBeIdentifiedIsAnError)
{
  const std::string dir = freshDir("bags_bad");
  EXPECT_THROW(bagIdentity((fs::path(dir) / "missing").string()), std::runtime_error);
  fs::create_directories(fs::path(dir) / "no_metadata");
  EXPECT_THROW(bagIdentity((fs::path(dir) / "no_metadata").string()), std::runtime_error);
  writeFile(fs::path(dir) / "a_file.mcap", "x");
  EXPECT_THROW(bagIdentity((fs::path(dir) / "a_file.mcap").string()), std::runtime_error);
  EXPECT_THROW(normalizedBagName(""), std::runtime_error);
  EXPECT_THROW(normalizedBagName("/"), std::runtime_error);
  fs::remove_all(dir);
}

TEST(ContributorProblem, EveryRecordedBagMustBeGiven)
{
  TileRecord t = sampleTile("bag_a");
  t.contributors = {{"bag_a", shaA()}, {"bag_b", shaB()}};
  const std::vector<BagIdentity> both = {{"bag_b", shaB()}, {"bag_a", shaA()}, {"bag_c", shaA()}};
  EXPECT_TRUE(contributorProblem("14_9_8.tif", t, both).empty()) << "a new bag may be added";
  const std::string subset = contributorProblem("14_9_8.tif", t, {{"bag_a", shaA()}});
  EXPECT_NE(subset.find("tile 14_9_8.tif"), std::string::npos) << subset;
  EXPECT_NE(subset.find("bag_b"), std::string::npos) << subset;
  EXPECT_EQ(subset.find("bag_a,"), std::string::npos) << "only the missing bag is named";
  const std::string changed = contributorProblem("14_9_8.tif", t,
      {{"bag_a", shaA()}, {"bag_b", shaA()}});
  EXPECT_NE(changed.find("not the same bag"), std::string::npos) << changed;
  EXPECT_TRUE(contributorProblem("14_9_8.tif", TileRecord{}, {}).empty());
}

// ---- surface tiles --------------------------------------------------------

namespace
{
std::string tileName(const gggs::GridIndex & i)
{
  return std::to_string(i.level()) + "_" + std::to_string(i.row()) + "_" +
         std::to_string(i.column()) + ".tif";
}
}  // namespace

TEST(TileBounds, FollowTheGgggsGrid)
{
  const gggs::Level level(14);
  const gggs::GridIndex index = level.gridIndex(43.0, -70.6);
  const auto b = tileBoundsFromName(tileName(index));
  ASSERT_TRUE(b);
  EXPECT_NEAR(b->south, index.southLatitude(), 1e-12);
  EXPECT_NEAR(b->north, index.northLatitude(), 1e-12);
  EXPECT_NEAR(b->west, index.westLongitude(), 1e-12);
  EXPECT_NEAR(b->east, index.eastLongitude(), 1e-12);
  EXPECT_TRUE(isTileFileName("14_9_8.tif"));
  for (const char * bad : {"14_9_8.tiff", "x_9_8.tif", "14_9.tif", "../14_9_8.tif",
      "14_9_8.tif/"})
  {
    EXPECT_FALSE(isTileFileName(bad)) << bad;
  }
  EXPECT_FALSE(tileBoundsFromName("99_1_1.tif")) << "no such level";
  EXPECT_FALSE(tileBoundsFromName("14_999999999_1.tif")) << "no such row";
}

// The depth tiles a tile's record covers: any level, any listed layer, by
// extent, with the margin; never an overview sidecar; hashed by content.
TEST(SurfaceTileIndex, SelectsByExtentAndHashesContent)
{
  const std::string root = freshDir("surface");
  const gggs::Level fine(14);
  const gggs::Level coarse(12);
  const gggs::GridIndex tile = fine.gridIndex(43.0, -70.6);
  const gggs::GridIndex east_neighbour = fine.gridIndex(43.0, tile.eastLongitude() + 1e-4);
  const gggs::GridIndex far_away = fine.gridIndex(43.5, -70.6);
  const gggs::GridIndex coarse_over = coarse.gridIndex(43.0, -70.6);
  writeFile(fs::path(root) / "processed" / tileName(tile), "tile");
  writeFile(fs::path(root) / "processed" / tileName(east_neighbour), "east");
  writeFile(fs::path(root) / "processed" / tileName(far_away), "far");
  writeFile(fs::path(root) / "draft" / tileName(coarse_over), "coarse");
  writeFile(fs::path(root) / "reference" / tileName(tile), "not a listed layer");
  writeFile(fs::path(root) / "processed" / "overviews" / tileName(tile), "overview");
  writeFile(fs::path(root) / "processed" / "registry.json", "{}");

  SurfaceTileIndex index(root, {"processed", "draft", "absent_layer"});
  EXPECT_EQ(index.tileCount(), 4u) << "overviews, sidecars and other layers are not tiles";
  const TileBounds bounds = *tileBoundsFromName(tileName(tile));

  // No margin: the tile itself, and the coarse tile that covers it. The east
  // neighbour only touches the edge, which counts (the stencil may step over it).
  auto near_zero = index.tilesNear(bounds, 0.0);
  std::vector<std::string> paths;
  for (const auto & d : near_zero) {
    paths.push_back(d.path);
  }
  EXPECT_NE(std::find(paths.begin(), paths.end(), "processed/" + tileName(tile)), paths.end());
  EXPECT_NE(std::find(paths.begin(), paths.end(), "draft/" + tileName(coarse_over)), paths.end());
  EXPECT_EQ(std::find(paths.begin(), paths.end(), "processed/" + tileName(far_away)), paths.end());
  EXPECT_TRUE(std::is_sorted(paths.begin(), paths.end()));
  for (const auto & d : near_zero) {
    if (d.path == "processed/" + tileName(tile)) {
      EXPECT_EQ(d.sha256, sha256Text("tile"));
    }
  }
  // A margin of 100 km reaches the far tile.
  const auto wide = index.tilesNear(bounds, 100000.0);
  EXPECT_EQ(wide.size(), 4u);
  EXPECT_EQ(index.hashedCount(), 4u);
  // Hashed once per run, however often it is asked for.
  index.tilesNear(bounds, 0.0);
  EXPECT_EQ(index.hashedCount(), 4u);

  // A changed depth tile changes the recorded hash.
  writeFile(fs::path(root) / "processed" / tileName(tile), "tile, rebuilt");
  SurfaceTileIndex rebuilt(root, {"processed", "draft"});
  for (const auto & d : rebuilt.tilesNear(bounds, 0.0)) {
    if (d.path == "processed/" + tileName(tile)) {
      EXPECT_EQ(d.sha256, sha256Text("tile, rebuilt"));
    }
  }
  fs::remove_all(root);
}

// ---- lock -----------------------------------------------------------------

TEST(StoreLock, ASecondRunIsRefusedAtOnce)
{
  const std::string dir = freshDir("lock");
  std::string error;
  auto first = StoreLock::acquire(dir, error);
  ASSERT_TRUE(first) << error;
  auto second = StoreLock::acquire(dir, error);
  EXPECT_FALSE(second);
  EXPECT_NE(error.find("another slope run holds"), std::string::npos) << error;
  first.reset();
  auto third = StoreLock::acquire(dir, error);
  EXPECT_TRUE(third) << error;
  fs::remove_all(dir);
}

// ---- publish and recovery -------------------------------------------------

namespace
{

constexpr const char * kT1 = "14_1_1.tif";  // new
constexpr const char * kT2 = "14_1_2.tif";  // replaces an older tile
constexpr const char * kT3 = "14_1_3.tif";  // reached, no cells: deleted
constexpr const char * kT4 = "14_1_4.tif";  // untouched by the run

/// A destination store holding an earlier slope-run publish, and a scratch
/// store holding this run's tiles.
struct PublishFixture
{
  explicit PublishFixture(const std::string & tag)
  : store(freshDir(tag + "_store")), scratch(freshDir(tag + "_scratch"))
  {
    writeFile(fs::path(store) / "survey" / kT2, "T2 old");
    writeFile(fs::path(store) / "survey" / kT3, "T3 old");
    writeFile(fs::path(store) / "survey" / kT4, "T4 untouched");
    writeFile(fs::path(store) / "registry.json", "registry old");
    DerivedTiles old;
    old.inputs = sampleInputs();
    old.tiles[kT2] = sampleTile("bag_a");
    old.tiles[kT3] = sampleTile("bag_a");
    old.tiles[kT4] = sampleTile("bag_z");
    old.write(store);

    writeFile(fs::path(scratch) / "survey" / kT1, "T1 new");
    writeFile(fs::path(scratch) / "survey" / kT2, "T2 new");
    writeFile(fs::path(scratch) / "registry.json", "registry new");
  }

  ~PublishFixture()
  {
    fs::remove_all(store);
    fs::remove_all(scratch);
  }

  PublishPlan plan() const
  {
    PublishPlan p;
    p.store_dir = store;
    p.run_id = "run-test";
    p.scratch_survey_dir = (fs::path(scratch) / "survey").string();
    p.put_tiles = {kT1, kT2};
    p.delete_tiles = {kT3};
    p.registry_source = (fs::path(scratch) / "registry.json").string();
    DerivedTiles m;
    m.inputs = sampleInputs();
    m.tiles[kT1] = sampleTile("bag_a");
    m.tiles[kT2] = sampleTile("bag_a");
    m.tiles[kT4] = sampleTile("bag_z");
    p.manifest = m;
    return p;
  }

  std::string store;
  std::string scratch;
};

/// Run the publish with a crash at @p at_point (empty = none).
void publishCrashingAt(const PublishPlan & plan, const std::string & at_point)
{
  const PublishFailpoint crash = [&](const std::string & point) {
      if (point == at_point) {
        throw std::runtime_error("injected crash at " + point);
      }
    };
  publishDerived(plan, crash);
}

}  // namespace

TEST(Publish, ACleanPublishInstallsTilesDeletesStaleOnesAndRecordsIt)
{
  PublishFixture f("clean");
  publishDerived(f.plan());
  const auto files = snapshot(f.store);
  EXPECT_EQ(files.at(std::string("survey/") + kT1), "T1 new");
  EXPECT_EQ(files.at(std::string("survey/") + kT2), "T2 new");
  EXPECT_EQ(files.count(std::string("survey/") + kT3), 0u) << "reached, no cells: deleted";
  EXPECT_EQ(files.at(std::string("survey/") + kT4), "T4 untouched");
  EXPECT_EQ(files.at("registry.json"), "registry new");
  for (const auto & [name, bytes] : files) {
    EXPECT_EQ(name.find(".incoming"), std::string::npos) << name;
    EXPECT_NE(name, DerivedTiles::kPendingFilename);
  }
  const auto manifest = DerivedTiles::load(f.store);
  ASSERT_TRUE(manifest);
  EXPECT_EQ(manifest->tiles.size(), 3u);
  EXPECT_EQ(manifest->tiles.count(kT3), 0u) << "its entry is dropped with the tile";
  ASSERT_TRUE(manifest->last_publish);
  EXPECT_EQ(manifest->last_publish->written, (std::vector<std::string>{kT1, kT2}));
  EXPECT_EQ(manifest->last_publish->removed, (std::vector<std::string>{kT3}));
  EXPECT_TRUE(manifest->last_publish->registry);
  EXPECT_TRUE(recoverInterruptedPublish(f.store).nothing());
}

// A failure before the commit point leaves the destination as it was; the next
// run removes the incoming directory and says so.
TEST(Publish, AFailureBeforeTheCommitPointLeavesTheStoreByteIdentical)
{
  PublishFixture f("before_commit");
  const auto before = snapshot(f.store);
  EXPECT_THROW(publishCrashingAt(f.plan(), "incoming-copied"), std::runtime_error);

  // The incoming directory is there; nothing else in the store moved.
  auto during = snapshot(f.store);
  bool saw_incoming = false;
  for (auto it = during.begin(); it != during.end(); ) {
    if (it->first.find(".incoming-") != std::string::npos) {
      saw_incoming = true;
      it = during.erase(it);
    } else {
      ++it;
    }
  }
  EXPECT_TRUE(saw_incoming);
  EXPECT_EQ(during, before);

  const RecoveryReport report = recoverInterruptedPublish(f.store);
  EXPECT_FALSE(report.rolled_forward);
  ASSERT_EQ(report.removed_incoming.size(), 1u);
  EXPECT_NE(report.text().find("never committed"), std::string::npos) << report.text();
  EXPECT_EQ(snapshot(f.store), before);
  EXPECT_TRUE(recoverInterruptedPublish(f.store).nothing());
}

// A failure at any point after it is rolled forward by the next run, to the
// bytes a clean publish gives, with no bags involved.
TEST(Publish, AFailureAfterTheCommitPointIsRolledForwardToTheCleanBytes)
{
  PublishFixture clean("roll_clean");
  publishDerived(clean.plan());
  const auto expected = snapshot(clean.store);

  for (const char * point : {"pending-written", "tile-moved:0", "tile-moved:1", "registry-moved",
      "tiles-deleted", "manifest-moved"})
  {
    PublishFixture f(std::string("roll_") + point[0] + std::to_string(::getpid()));
    EXPECT_THROW(publishCrashingAt(f.plan(), point), std::runtime_error) << point;
    const RecoveryReport report = recoverInterruptedPublish(f.store);
    if (std::string(point) == "manifest-moved") {
      // Already complete but for the incoming directory.
      EXPECT_FALSE(report.rolled_forward) << point;
    } else {
      EXPECT_TRUE(report.rolled_forward) << point;
      EXPECT_EQ(report.rolled_run, "run-test");
      EXPECT_NE(report.text().find("had committed"), std::string::npos) << report.text();
    }
    EXPECT_EQ(snapshot(f.store), expected) << "after a crash at " << point;
    EXPECT_TRUE(recoverInterruptedPublish(f.store).nothing()) << point;
  }
}

// Recovery needs a tile the commit names to be in one of its two places.
TEST(Publish, ARollForwardThatCannotCompleteIsAnError)
{
  PublishFixture f("lost_tile");
  EXPECT_THROW(publishCrashingAt(f.plan(), "pending-written"), std::runtime_error);
  fs::remove(fs::path(f.store) / "survey" / ".incoming-run-test" / kT1);
  try {
    recoverInterruptedPublish(f.store);
    ADD_FAILURE() << "recovered with a tile missing";
  } catch (const std::exception & e) {
    EXPECT_NE(std::string(e.what()).find(kT1), std::string::npos) << e.what();
  }
}

TEST(Publish, AMalformedPendingManifestIsNotGuessedAt)
{
  PublishFixture f("bad_pending");
  writeFile(fs::path(f.store) / DerivedTiles::kPendingFilename, "{ torn");
  try {
    recoverInterruptedPublish(f.store);
    ADD_FAILURE() << "accepted a torn commit record";
  } catch (const std::exception & e) {
    EXPECT_NE(std::string(e.what()).find("commit record"), std::string::npos) << e.what();
  }
}

TEST(Publish, RefusesNamesThatAreNotTiles)
{
  PublishFixture f("names");
  PublishPlan plan = f.plan();
  plan.put_tiles = {"../escape.tif"};
  EXPECT_THROW(publishDerived(plan), std::runtime_error);
  plan = f.plan();
  plan.run_id = "a/b";
  EXPECT_THROW(publishDerived(plan), std::runtime_error);
}

}  // namespace cube
