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

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/slope_run.h"
#include "marine_autonomy/gggs.h"
#include "marine_backscatter/seabed_geometry.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_mbes_backscatter_store/tile_io.hpp"

/// @file
/// @brief The slope run's own bookkeeping (cube_bathymetry#173), without bags:
///        the request rules, the early refusals of `SlopeRun::create`, the
///        contributor attribution and its first-touch refusal, and the publish
///        (a tile left with no cells is removed, an interrupted publish is
///        completed by the next run).

namespace cube
{
namespace
{

namespace fs = std::filesystem;
namespace mbs = marine_mbes_backscatter_store;
namespace seabed = marine_backscatter::seabed;

constexpr double kLat = 43.0035;   // inside the level-10 tile north of lat 43.0
constexpr double kLon = -70.6;

std::string freshDir(const std::string & tag)
{
  static int counter = 0;
  const fs::path dir = fs::temp_directory_path() /
    ("cube_slope_run_" + tag + "_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
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

std::map<std::string, std::string> snapshot(const std::string & dir)
{
  std::map<std::string, std::string> out;
  for (const auto & entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().filename() != DerivedTiles::kLockFilename) {
      out[fs::relative(entry.path(), dir).string()] = readFile(entry.path());
    }
  }
  return out;
}

std::string shaOf(char c) {return std::string(64, c);}

/// A flat processed tile over the level-10 tile containing (kLat, kLon).
gggs::GridIndex writeSurface(const std::string & root)
{
  const gggs::Level level(10);
  const gggs::GridIndex index = level.gridIndex(kLat, kLon);
  namespace store = marine_bathymetry_store;
  store::BathymetryTile tile(index);
  for (uint16_t r = 0; r < store::BathymetryTile::edge; ++r) {
    for (uint16_t c = 0; c < store::BathymetryTile::edge; ++c) {
      store::BathyCell cell;
      cell.depth = -20.0;
      cell.uncertainty = 0.1;
      tile.set(r, c, cell);
    }
  }
  const fs::path dir = fs::path(root) / store::layerDirName(store::SourceLayer::Processed);
  fs::create_directories(dir);
  store::saveTile(tile, (dir / store::tileFilename(index)).string());
  return index;
}

struct Fixture
{
  Fixture()
  : root(freshDir("fx")), surface(root + "/surface"), bs(root + "/bs")
  {
    tile = writeSurface(surface);
    tile_name = mbs::tileFilename(tile);
    config.surface_dir = surface;
    config.bs_store_dir = bs;
    config.baseline_deg = 5.0;
    config.inputs.slope_baseline_deg = 5.0;
    config.inputs.slope_layers = "processed";
    config.inputs.level = 10;
    config.bags = {{"bag_a", shaOf('a')}, {"bag_b", shaOf('b')}};
  }
  ~Fixture() {fs::remove_all(root);}

  std::vector<GeoSounding> soundings(double east_m = 0.0) const
  {
    std::vector<GeoSounding> out;
    const auto mpd = seabed::metresPerDegree(kLat);
    for (int i = 0; i < 5; ++i) {
      GeoSounding gs(gz4d::GeoPointLatLongDegrees(
          kLat + 0.5 * i / mpd.north, kLon + (east_m + 0.5 * i) / mpd.east, -20.0));
      gs.sounding.beam_dir_enu = {0.0, 0.0, -1.0};
      gs.sounding.slant_range = 20.0f;
      gs.sounding.vertical_error = 0.01f;
      gs.sounding.horizontal_error = 0.01f;
      out.push_back(gs);
    }
    return out;
  }

  std::string root, surface, bs, tile_name;
  gggs::GridIndex tile;
  SlopeRunConfig config;
  GeoMapSheet sheet {1.0f, "order1a"};
};

}  // namespace

TEST(SlopeRunRequest, TheBeamBasisRefusesTheSlopeOnlyFlags)
{
  SlopeRunRequest r;
  EXPECT_TRUE(slopeRunRequestProblem(r).empty());
  r.surface_dir = "/x";
  EXPECT_NE(slopeRunRequestProblem(r).find("--slope-surface needs"), std::string::npos);
  r.surface_dir.clear();
  r.baseline_given = true;
  EXPECT_NE(slopeRunRequestProblem(r).find("--slope-baseline-deg needs"), std::string::npos);
}

TEST(SlopeRunRequest, TheBeamLedgerIsForTheSlopeRunOnly)
{
  SlopeRunRequest r;
  r.beam_ledger_out = "/tmp/ledger.bin";
  EXPECT_NE(slopeRunRequestProblem(r).find("--beam-ledger-out needs"), std::string::npos);
  r.basis = BackscatterAngleBasis::Incidence;
  r.surface_dir = "/surface";
  r.bs_store_dir = "/bs";
  EXPECT_TRUE(slopeRunRequestProblem(r).empty());
}

TEST(SlopeRunRequest, TheIncidenceBasisNeedsItsStoresAndRefusesTheRest)
{
  SlopeRunRequest ok;
  ok.basis = BackscatterAngleBasis::Incidence;
  ok.surface_dir = "/surface";
  ok.bs_store_dir = "/bs";
  EXPECT_TRUE(slopeRunRequestProblem(ok).empty());

  struct Case
  {
    std::function<void(SlopeRunRequest &)> change;
    const char * expect;
  };
  const std::vector<Case> cases = {
    {[](SlopeRunRequest & r) {r.surface_dir.clear();}, "needs --slope-surface"},
    {[](SlopeRunRequest & r) {r.bs_store_dir.clear();}, "needs --bs-store"},
    {[](SlopeRunRequest & r) {r.store_dir = "/o";}, "-o is refused"},
    {[](SlopeRunRequest & r) {r.depth_adaptive = true;}, "--depth-adaptive is refused"},
    {[](SlopeRunRequest & r) {r.ping_limit = 100;}, "-l is refused"},
    {[](SlopeRunRequest & r) {r.baseline_deg = 0.0;}, "must be in (0, 30]"},
    {[](SlopeRunRequest & r) {r.baseline_deg = 30.5;}, "must be in (0, 30]"},
    {[](SlopeRunRequest & r) {r.baseline_deg = std::nan("");}, "must be in (0, 30]"},
  };
  for (const auto & c : cases) {
    SlopeRunRequest r = ok;
    c.change(r);
    EXPECT_NE(slopeRunRequestProblem(r).find(c.expect), std::string::npos) << c.expect;
  }
  SlopeRunRequest edge = ok;
  edge.baseline_deg = 30.0;
  EXPECT_TRUE(slopeRunRequestProblem(edge).empty());
}

TEST(SlopeRunRequest, OnlyNoneOrATableCanBeAppliedOnIncidence)
{
  EXPECT_TRUE(slopeCorrectionProblem("", "").empty());
  EXPECT_TRUE(slopeCorrectionProblem("table", "t.csv").empty());
  EXPECT_NE(slopeCorrectionProblem("curve", "c.csv").find("single curve has no incidence form"),
    std::string::npos);
  const std::string sonar_info = slopeCorrectionProblem("curve", "sonar_info:/a/sonar_info");
  EXPECT_NE(sonar_info.find("beam-angle curve"), std::string::npos) << sonar_info;
  EXPECT_NE(sonar_info.find("--backscatter-correction none"), std::string::npos) << sonar_info;
}

TEST(SlopeRunRequest, TheSurfaceLayersAreTheOwnSurveyOnesThatHoldTiles)
{
  const std::string root = freshDir("layers");
  EXPECT_TRUE(slopeSurfaceLayers(root).empty());
  for (const char * dir : {"reference", "chart", "overviews"}) {
    writeFile(fs::path(root) / dir / "10_1_1.tif", "t");
  }
  EXPECT_TRUE(slopeSurfaceLayers(root).empty());
  // An empty directory is not a layer: creating one must not change a store's inputs.
  fs::create_directories(fs::path(root) / "draft");
  EXPECT_TRUE(slopeSurfaceLayers(root).empty());
  writeFile(fs::path(root) / "draft" / "readme.txt", "not a tile");
  EXPECT_TRUE(slopeSurfaceLayers(root).empty());
  writeFile(fs::path(root) / "draft" / "10_1_1.tif", "t");
  EXPECT_EQ(slopeSurfaceLayers(root), (std::vector<std::string>{"draft"}));
  writeFile(fs::path(root) / "processed" / "10_1_1.tif", "t");
  EXPECT_EQ(slopeSurfaceLayers(root), (std::vector<std::string>{"processed", "draft"}));
  fs::remove_all(root);
}

TEST(SlopeRunCreate, RefusesAStoreThatIsNotEmptyOrNotASlopeStore)
{
  Fixture f;
  writeFile(fs::path(f.bs) / "survey" / f.tile_name, "a beam-angle tile");
  std::string error;
  EXPECT_FALSE(SlopeRun::create(f.config, error));
  EXPECT_NE(error.find("beam-angle backscatter store"), std::string::npos) << error;
  EXPECT_EQ(readFile(fs::path(f.bs) / "survey" / f.tile_name), "a beam-angle tile");
}

TEST(SlopeRunCreate, RefusesAStoreWithOtherInputs)
{
  Fixture f;
  DerivedTiles previous;
  previous.inputs = f.config.inputs;
  previous.inputs.slope_baseline_deg = 7.0;
  previous.write(f.bs);
  std::string error;
  EXPECT_FALSE(SlopeRun::create(f.config, error));
  EXPECT_NE(error.find("slope baseline angle differs"), std::string::npos) << error;
  EXPECT_NE(error.find("One store holds one correction"), std::string::npos) << error;
}

TEST(SlopeRunCreate, RefusesAnUnusableSurface)
{
  Fixture f;
  fs::remove_all(fs::path(f.surface) / "processed");
  fs::create_directories(fs::path(f.surface) / "processed");   // a layer with no tiles
  std::string error;
  EXPECT_FALSE(SlopeRun::create(f.config, error));
  EXPECT_NE(error.find("--slope-surface"), std::string::npos) << error;
  Fixture g;
  g.config.surface_dir = g.root + "/mistyped";
  EXPECT_FALSE(SlopeRun::create(g.config, error));
  EXPECT_NE(error.find("--slope-surface"), std::string::npos) << error;
  Fixture h;
  h.config.inputs.slope_layers = "";
  EXPECT_FALSE(SlopeRun::create(h.config, error));
  EXPECT_NE(error.find("neither a processed nor a draft"), std::string::npos) << error;
}

TEST(SlopeRunCreate, ASecondRunOnTheStoreIsRefusedAndTheScratchIsRemoved)
{
  Fixture f;
  std::string error;
  auto first = SlopeRun::create(f.config, error);
  ASSERT_TRUE(first) << error;
  const std::string scratch = first->scratchDir();
  EXPECT_TRUE(fs::is_directory(scratch));
  EXPECT_EQ(fs::path(scratch).parent_path(), fs::path(f.bs).parent_path())
    << "the default scratch root is the parent of the backscatter store";
  EXPECT_EQ(fs::path(scratch).filename().string().rfind("slope-run-", 0), 0u);
  EXPECT_FALSE(SlopeRun::create(f.config, error));
  EXPECT_NE(error.find("another run (an import or a slope run) holds"), std::string::npos) << error;
  first.reset();
  EXPECT_FALSE(fs::exists(scratch)) << "removed on every exit path";
  EXPECT_TRUE(SlopeRun::create(f.config, error)) << error;
}

TEST(SlopeRunCreate, TheScratchRootIsChosenAndLeftoversAreNamed)
{
  Fixture f;
  f.config.scratch_root = f.root + "/fast";
  fs::create_directories(fs::path(f.config.scratch_root) / "slope-run-OLD123");
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  EXPECT_EQ(fs::path(run->scratchDir()).parent_path(), fs::path(f.config.scratch_root));
  EXPECT_NE(run->leftoverText().find("slope-run-OLD123"), std::string::npos)
    << run->leftoverText();
  EXPECT_NE(run->leftoverText().find("can be deleted"), std::string::npos);
  EXPECT_TRUE(fs::exists(fs::path(f.config.scratch_root) / "slope-run-OLD123"));
}

TEST(SlopeRunPrepare, AttributesTheBagToTheTilesAndStampsIncidence)
{
  Fixture f;
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto a = f.soundings();
  ASSERT_TRUE(run->prepare(a, 0, 100, f.sheet, error)) << error;
  EXPECT_NEAR(a[0].sounding.incidence_angle, 0.0, 1e-6) << "a flat surface and a nadir beam";
  auto b = f.soundings(1.0);
  ASSERT_TRUE(run->prepare(b, 1, 100, f.sheet, error)) << error;
  const auto * contributors = run->contributorsOf(f.tile);
  ASSERT_NE(contributors, nullptr);
  EXPECT_EQ(*contributors, (std::set<std::size_t>{0, 1}));
  EXPECT_GE(run->tilesReached(), 1u);
  EXPECT_EQ(run->counters().beams, 10u);
  EXPECT_EQ(run->counters().from_surface, 10u);
}

// The recorded contributors of a tile this batch reaches must all be among the
// bags given; the run stops there, before anything is published.
TEST(SlopeRunPrepare, ATileWhoseRecordedBagsAreNotAllGivenIsRefusedAtFirstTouch)
{
  Fixture f;
  DerivedTiles previous;
  previous.inputs = f.config.inputs;
  TileRecord record;
  record.contributors = {{"bag_a", shaOf('a')}, {"bag_gone", shaOf('c')}};
  record.written = "2026-11-12T00:00:00Z";
  record.code_version = kDerivedTilesCodeVersion;
  previous.tiles[f.tile_name] = record;
  previous.write(f.bs);
  const auto before = snapshot(f.bs);
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto s = f.soundings();
  EXPECT_FALSE(run->prepare(s, 0, 100, f.sheet, error));
  EXPECT_NE(error.find("tile " + f.tile_name), std::string::npos) << error;
  EXPECT_NE(error.find("bag_gone"), std::string::npos) << error;
  EXPECT_NE(error.find("Nothing has been published"), std::string::npos) << error;
  EXPECT_NE(error.find("conservative reach"), std::string::npos) << error;
  run.reset();
  EXPECT_EQ(snapshot(f.bs), before);

  // With every recorded bag given, the same store is accepted.
  previous.tiles[f.tile_name].contributors = {{"bag_a", shaOf('a')}};
  previous.write(f.bs);
  run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  s = f.soundings();
  EXPECT_TRUE(run->prepare(s, 0, 100, f.sheet, error)) << error;
}

TEST(SlopeRunPublish, PublishesTheTilesRecordsThemAndDropsATileLeftWithNoCells)
{
  Fixture f;
  // An earlier publish: the tile we will reach again, and one next door we will not touch.
  const gggs::GridIndex other = gggs::Level(10).gridIndex(kLat + 0.02, kLon);
  const std::string other_name = mbs::tileFilename(other);
  DerivedTiles previous;
  previous.inputs = f.config.inputs;
  TileRecord record;
  record.contributors = {{"bag_a", shaOf('a')}};
  record.written = "2026-11-12T00:00:00Z";
  record.code_version = kDerivedTilesCodeVersion;
  previous.tiles[f.tile_name] = record;
  previous.tiles[other_name] = record;
  previous.write(f.bs);
  writeFile(fs::path(f.bs) / "survey" / f.tile_name, "old tile");
  writeFile(fs::path(f.bs) / "survey" / other_name, "untouched tile");

  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto s = f.soundings();
  ASSERT_TRUE(run->prepare(s, 0, 100, f.sheet, error)) << error;
  // A second tile is reached and does get cells (a run with none at all fails).
  const gggs::GridIndex fresh_tile = gggs::Level(10).gridIndex(kLat - 0.02, kLon);
  std::vector<GeoSounding> elsewhere;
  {
    const auto mpd = seabed::metresPerDegree(kLat);
    GeoSounding gs(gz4d::GeoPointLatLongDegrees(kLat - 0.02, kLon + 0.5 / mpd.east, -20.0));
    gs.sounding.beam_dir_enu = {0.0, 0.0, -1.0};
    gs.sounding.slant_range = 20.0f;
    gs.sounding.vertical_error = 0.01f;
    gs.sounding.horizontal_error = 0.01f;
    elsewhere.push_back(gs);
  }
  ASSERT_TRUE(run->prepare(elsewhere, 0, 100, f.sheet, error)) << error;
  writeFile(fs::path(run->scratchBsDir()) / "survey" / mbs::tileFilename(fresh_tile), "fresh");

  // The first tile was reached but produced no cells: no scratch tile exists.
  std::string summary;
  ASSERT_TRUE(run->publish(error, summary)) << error;
  EXPECT_NE(summary.find("1 reached with no cells"), std::string::npos) << summary;
  EXPECT_EQ(readFile(fs::path(f.bs) / "survey" / mbs::tileFilename(fresh_tile)), "fresh");
  EXPECT_FALSE(fs::exists(fs::path(f.bs) / "survey" / f.tile_name)) << "the stale tile is removed";
  EXPECT_EQ(readFile(fs::path(f.bs) / "survey" / other_name), "untouched tile");
  const auto after = DerivedTiles::load(f.bs);
  ASSERT_TRUE(after);
  EXPECT_EQ(after->tiles.count(f.tile_name), 0u) << "its entry is dropped";
  EXPECT_EQ(after->tiles.count(other_name), 1u) << "an untouched tile keeps its entry";
  ASSERT_TRUE(after->last_publish);
  EXPECT_EQ(after->last_publish->removed, (std::vector<std::string>{f.tile_name}));
}

TEST(SlopeRunPublish, RecordsContributorsDepthTilesAndTheMargin)
{
  Fixture f;
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto a = f.soundings();
  auto b = f.soundings(1.0);
  ASSERT_TRUE(run->prepare(a, 1, 100, f.sheet, error)) << error;
  ASSERT_TRUE(run->prepare(b, 0, 100, f.sheet, error)) << error;
  writeFile(fs::path(run->scratchBsDir()) / "survey" / f.tile_name, "new tile");
  writeFile(fs::path(run->scratchBsDir()) / "registry.json", "{\"platform\": \"p\"}");
  std::string summary;
  ASSERT_TRUE(run->publish(error, summary)) << error;
  EXPECT_EQ(readFile(fs::path(f.bs) / "survey" / f.tile_name), "new tile");
  EXPECT_EQ(readFile(fs::path(f.bs) / "registry.json"), "{\"platform\": \"p\"}");
  const auto m = DerivedTiles::load(f.bs);
  ASSERT_TRUE(m);
  const TileRecord & t = m->tiles.at(f.tile_name);
  EXPECT_EQ(t.contributors.size(), 2u);
  EXPECT_EQ(t.contributors[0].name, "bag_a") << "sorted";
  ASSERT_EQ(t.depth_tiles.size(), 1u);
  EXPECT_EQ(t.depth_tiles[0].path, "processed/" + marine_bathymetry_store::tileFilename(f.tile));
  EXPECT_EQ(t.depth_tiles[0].sha256.size(), 64u);
  EXPECT_GT(t.stencil_margin_m, 0.0);
  EXPECT_TRUE(m->inputs == f.config.inputs);
}

// The beam ledger: made only by a run that publishes, keyed by the beam index, and
// removed again if the publish fails after it was made.
TEST(SlopeRunLedger, IsWrittenByACompletedRunOnly)
{
  Fixture f;
  f.config.beam_ledger_path = f.root + "/ledger.bin";
  std::string error;
  {
    auto run = SlopeRun::create(f.config, error);
    ASSERT_TRUE(run) << error;
    auto s = f.soundings();
    for (std::size_t i = 0; i < s.size(); ++i) {
      s[i].sounding.beam_index = static_cast<uint16_t>(10 + i);
    }
    ASSERT_TRUE(run->prepare(s, 1, 4242, f.sheet, error)) << error;
    EXPECT_FALSE(fs::exists(f.config.beam_ledger_path));
    // No cells: the publish fails and no ledger appears.
    std::string summary;
    EXPECT_FALSE(run->publish(error, summary));
  }
  EXPECT_FALSE(fs::exists(f.config.beam_ledger_path));
  EXPECT_FALSE(fs::exists(f.config.beam_ledger_path + ".partial"));

  {
    auto run = SlopeRun::create(f.config, error);
    ASSERT_TRUE(run) << error;
    auto s = f.soundings();
    for (std::size_t i = 0; i < s.size(); ++i) {
      s[i].sounding.beam_index = static_cast<uint16_t>(10 + i);
    }
    ASSERT_TRUE(run->prepare(s, 1, 4242, f.sheet, error)) << error;
    writeFile(fs::path(run->scratchBsDir()) / "survey" / f.tile_name, "new tile");
    std::string summary;
    ASSERT_TRUE(run->publish(error, summary)) << error;
    EXPECT_NE(summary.find("beam ledger"), std::string::npos) << summary;
  }
  const BeamLedger ledger = readBeamLedger(f.config.beam_ledger_path);
  ASSERT_EQ(ledger.pings.size(), 1u);
  EXPECT_EQ(ledger.pings[0].stamp_ns, 4242);
  EXPECT_EQ(ledger.pings[0].bag_index, 1u);
  ASSERT_EQ(ledger.pings[0].beams.size(), 5u);
  EXPECT_EQ(ledger.pings[0].beams[2].beam_index, 12u);
  EXPECT_EQ(ledger.pings[0].beams[2].source, 0) << "against the surface normal";
  EXPECT_EQ(ledger.pings[0].beams[2].incidence_centideg, 0) << "nadir beam on a flat surface";
}

TEST(SlopeRunLedger, IsRemovedWhenThePublishFailsAfterIt)
{
  Fixture f;
  f.config.beam_ledger_path = f.root + "/ledger.bin";
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto s = f.soundings();
  ASSERT_TRUE(run->prepare(s, 0, 1, f.sheet, error)) << error;
  writeFile(fs::path(run->scratchBsDir()) / "survey" / f.tile_name, "new tile");
  const PublishFailpoint crash = [](const std::string & point) {
      if (point == "incoming-copied") {
        throw std::runtime_error("injected crash");
      }
    };
  std::string summary;
  EXPECT_FALSE(run->publish(error, summary, crash));
  EXPECT_FALSE(fs::exists(f.config.beam_ledger_path));
}

TEST(SlopeRunLedger, AnUnwritableLedgerPathRefusesTheRunAtOnce)
{
  Fixture f;
  f.config.beam_ledger_path = "/nonexistent_dir_for_ledger/ledger.bin";
  std::string error;
  EXPECT_FALSE(SlopeRun::create(f.config, error));
  EXPECT_NE(error.find("cannot create the beam ledger"), std::string::npos) << error;
}

// A run with no finite backscatter at all is a failure, not a product: it names the
// reason and writes nothing.
TEST(SlopeRunPublish, ARunWithNoCellsFailsAndWritesNothing)
{
  Fixture f;
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto s = f.soundings();
  ASSERT_TRUE(run->prepare(s, 0, 100, f.sheet, error)) << error;
  std::string summary;
  EXPECT_FALSE(run->publish(error, summary));
  EXPECT_NE(error.find("no cell had finite backscatter"), std::string::npos) << error;
  EXPECT_NE(error.find("Nothing was published"), std::string::npos) << error;
  EXPECT_FALSE(fs::exists(fs::path(f.bs) / DerivedTiles::kFilename));
}

// The depth tiles are hashed before the run reads them; one rewritten meanwhile
// fails the publish before the commit point.
TEST(SlopeRunPublish, ASurfaceTileChangedDuringTheRunFailsBeforeTheCommit)
{
  Fixture f;
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto s = f.soundings();
  ASSERT_TRUE(run->prepare(s, 0, 100, f.sheet, error)) << error;
  writeFile(fs::path(run->scratchBsDir()) / "survey" / f.tile_name, "new tile");
  const fs::path depth = fs::path(f.surface) / "processed" /
    marine_bathymetry_store::tileFilename(f.tile);
  {
    std::ofstream(depth, std::ios::binary | std::ios::app) << "tampered";
  }
  const auto before = snapshot(f.bs);
  std::string summary;
  EXPECT_FALSE(run->publish(error, summary));
  EXPECT_NE(error.find("changed while the run was reading it"), std::string::npos) << error;
  EXPECT_EQ(snapshot(f.bs), before) << "nothing reached the destination";
  EXPECT_FALSE(fs::exists(fs::path(f.bs) / DerivedTiles::kPendingFilename));
}

// How many pings the run dropped is visible in every tile entry.
TEST(SlopeRunPublish, DroppedGeoreferencingIsRecordedInTheEntries)
{
  Fixture f;
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto s = f.soundings();
  ASSERT_TRUE(run->prepare(s, 0, 100, f.sheet, error)) << error;
  writeFile(fs::path(run->scratchBsDir()) / "survey" / f.tile_name, "new tile");
  run->setDroppedGeoreferencing(7);
  std::string summary;
  ASSERT_TRUE(run->publish(error, summary)) << error;
  EXPECT_NE(summary.find("7 ping(s) were dropped"), std::string::npos) << summary;
  EXPECT_EQ(DerivedTiles::load(f.bs)->tiles.at(f.tile_name).dropped_georef_pings, 7u);
}

// The scratch store holding a tile no batch was attributed to is a bookkeeping
// bug; publishing then would write a tile with no recorded contributor.
TEST(SlopeRunPublish, AScratchTileNoBatchReachedStopsThePublish)
{
  Fixture f;
  std::string error;
  auto run = SlopeRun::create(f.config, error);
  ASSERT_TRUE(run) << error;
  auto s = f.soundings();
  ASSERT_TRUE(run->prepare(s, 0, 100, f.sheet, error)) << error;
  writeFile(fs::path(run->scratchBsDir()) / "survey" / "10_1_1.tif", "stray");
  std::string summary;
  EXPECT_FALSE(run->publish(error, summary));
  EXPECT_NE(error.find("no batch was attributed"), std::string::npos) << error;
  EXPECT_FALSE(fs::exists(fs::path(f.bs) / DerivedTiles::kFilename));
}

// A publish that dies after the commit point is completed by the next run on
// the store, whatever bags it is given.
TEST(SlopeRunPublish, AnInterruptedPublishIsRolledForwardByTheNextRun)
{
  Fixture f;
  std::string error;
  {
    auto run = SlopeRun::create(f.config, error);
    ASSERT_TRUE(run) << error;
    auto s = f.soundings();
    ASSERT_TRUE(run->prepare(s, 0, 100, f.sheet, error)) << error;
    writeFile(fs::path(run->scratchBsDir()) / "survey" / f.tile_name, "new tile");
    std::string summary;
    const PublishFailpoint crash = [](const std::string & point) {
        if (point == "pending-written") {
          throw std::runtime_error("injected crash");
        }
      };
    EXPECT_FALSE(run->publish(error, summary, crash));
    EXPECT_NE(error.find("The next slope run"), std::string::npos) << error;
  }
  EXPECT_TRUE(fs::exists(fs::path(f.bs) / DerivedTiles::kPendingFilename));
  EXPECT_FALSE(fs::exists(fs::path(f.bs) / "survey" / f.tile_name));
  f.config.bags = {{"another_bag", shaOf('d')}};
  auto next = SlopeRun::create(f.config, error);
  ASSERT_TRUE(next) << error;
  EXPECT_NE(next->recoveryText().find("had committed"), std::string::npos) << next->recoveryText();
  EXPECT_EQ(readFile(fs::path(f.bs) / "survey" / f.tile_name), "new tile");
  EXPECT_TRUE(fs::exists(fs::path(f.bs) / DerivedTiles::kFilename));
  EXPECT_FALSE(fs::exists(fs::path(f.bs) / DerivedTiles::kPendingFilename));
}

}  // namespace cube
