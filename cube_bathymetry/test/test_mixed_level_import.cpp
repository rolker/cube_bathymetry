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

// Mixed-level import (cube_bathymetry#143). Three properties the depth-adaptive
// writer must have before it touches a live survey store:
//
//   1. SINGLE-LEVEL EQUIVALENCE -- with the policy pinned to one level, the
//      multi-level path writes a store BYTE-IDENTICAL to today's fixed-level
//      path over the same soundings: the same set of files under processed/,
//      each with identical bytes. This is the load-bearing regression guard for
//      the decoupling of the estimation grid from the store tiling. It runs the
//      WHOLE import path -- recon -> level plan -> spill replay -- because
//      CUBE's sliding-median pre-filter is order-dependent, so a spill that did
//      not replay chronologically would break the identity while an
//      addBatch-only test still passed.
//   2. PARENTS UNDER CHILDREN -- a deep plain with a shoal writes tiles at more
//      than one level, and the coarse parent over the shoal holds a complete
//      estimate (no holes where the fine children are).
//   3. ONE RAM BUDGET -- with N per-level accumulators, max_resident_tiles is
//      the store-wide total, evicted globally coldest first, and nothing is lost.

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/multi_level_accumulator.h"
#include "cube_bathymetry/recon.h"
#include "cube_bathymetry/store_import.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

namespace cube
{

namespace
{
std::string makeTempDir(const std::string & tag)
{
  const auto base = std::filesystem::temp_directory_path() /
    ("cube_mixed_level_" + tag + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  return base.string();
}

// Several soundings at one position so CUBE settles a finite depth.
std::vector<GeoSounding> surveyCell(double lat, double lon, float depth, int reps = 8)
{
  std::vector<GeoSounding> soundings;
  for (int rep = 0; rep < reps; ++rep) {
    gz4d::GeoPointLatLongDegrees point(lat, lon, -depth - rep * 0.001);
    GeoSounding s(point);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    s.sounding.intensity = -20.0f;
    s.sounding.beam_angle = 0.0f;
    s.sounding.slant_range = depth;
    // Nadir beam: the sonar-frame z is positive-down and is the WATER DEPTH
    // under the transducer, which is what the recon decides levels on (#143).
    s.sounding.sonar_relative_position.z = depth;
    soundings.push_back(s);
  }
  return soundings;
}

// A lawn of cells over a rectangle: `n x n` positions `step_deg` apart, all at
// `depth`, one batch per position (a "ping" each).
std::vector<std::vector<GeoSounding>> lawn(
  double lat0, double lon0, int n, double step_deg, float depth)
{
  std::vector<std::vector<GeoSounding>> batches;
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      batches.push_back(surveyCell(lat0 + i * step_deg, lon0 + j * step_deg, depth));
    }
  }
  return batches;
}

// Every value tile under <store>/processed/, name -> bytes.
std::map<std::string, std::string> readTiles(const std::string & store_dir)
{
  std::map<std::string, std::string> out;
  const auto layer = std::filesystem::path(store_dir) /
    marine_bathymetry_store::layerDirName(marine_bathymetry_store::SourceLayer::Processed);
  if (!std::filesystem::exists(layer)) {
    return out;
  }
  for (const auto & e : std::filesystem::directory_iterator(layer)) {
    if (e.path().extension() != ".tif") {
      continue;
    }
    std::ifstream in(e.path(), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out.emplace(e.path().filename().string(), std::move(bytes));
  }
  return out;
}

// Finite cells of every processed tile at every level, keyed by (level, cell).
std::map<gggs::CellIndex, double> loadFiniteCells(const std::string & store_dir)
{
  std::map<gggs::CellIndex, double> out;
  marine_bathymetry_store::BathymetryStore store(10);
  marine_bathymetry_store::load(store, store_dir);
  for (const auto & [grid, tile] : store.tiles(marine_bathymetry_store::SourceLayer::Processed)) {
    const auto & depth = tile.depthBand();
    gggs::CellAreaIterator it(tile.index());
    std::size_t k = 0;
    for (; it.valid() && k < depth.size(); it.next(), ++k) {
      if (std::isfinite(depth[k])) {
        out.emplace(*it, depth[k]);
      }
    }
  }
  return out;
}

LevelPlan planFor(
  const std::vector<std::vector<GeoSounding>> & batches, const LevelPlanPolicy & policy,
  const Parameters & params)
{
  ReconCollector recon(policy, "");
  for (const auto & b : batches) {
    recon.add(b, params);
  }
  return recon.plan();
}
}  // namespace

TEST(RequestedCellSize, ResolvesToExactlyThatLevel)
{
  for (int level = 0; level <= 20; ++level) {
    const float requested = requestedCellSizeFor(static_cast<uint8_t>(level));
    EXPECT_EQ(gggs::Level::fromCellSize(requested).level(), level) << "level " << level;
    EXPECT_NEAR(requested, gggs::Level(level).cellSize(), 1e-4 * gggs::Level(level).cellSize());
  }
}

// Property 1. The fixed path is GeoMapSheet + ImportAccumulator at the requested
// resolution requestedCellSizeFor(10); the adaptive path is a MultiLevelAccumulator
// over a plan pinned to coarsest == finest == 10, whose sheet for level 10 the
// accumulator builds at that same requested resolution. Both gate at the same
// spacing term (the same distance_scale), so the only thing that can differ is
// the decoupling itself.
TEST(MixedLevelImport, SingleLevelPolicyIsByteIdenticalToTheFixedPath)
{
  const std::string fixed_dir = makeTempDir("fixed");
  const std::string adaptive_dir = makeTempDir("adaptive");
  const float requested = requestedCellSizeFor(10);

  // A lawn of ~5x5 cells, ~3.3 m apart, at 12 m, plus a second patch across a
  // level-10 tile seam so near-seam neighbour tiles are exercised.
  auto batches = lawn(43.07, -70.76, 5, 3.0e-5, 12.0f);
  const auto seam_grid = gggs::Level(10).gridIndex(43.07, -70.76);
  const double seam_lat = seam_grid.northLatitude();
  for (const auto & b : lawn(seam_lat - 2.0e-5, -70.76, 3, 2.0e-5, 12.0f)) {
    batches.push_back(b);
  }

  // Fixed-level path.
  {
    GeoMapSheet sheet(requested);
    ImportAccumulatorConfig cfg;
    cfg.store_dir = fixed_dir;
    cfg.cell_size_m = static_cast<float>(sheet.nominalCellSizeMeters());
    cfg.max_resident_tiles = 0;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
  }

  // Adaptive path, pinned to one level, through the real two-pass import:
  // recon (count + spill) -> level plan -> chronological spill replay.
  const std::string spill_dir = makeTempDir("spill");
  std::filesystem::remove(spill_dir);  // the collector creates it, and refuses a used one
  {
    LevelPlanPolicy policy;
    policy.depth.coarsest_level = 10;
    policy.depth.finest_level = 10;
    Parameters params{CellSizes(requested), "order1a"};

    ReconCollector recon(policy, spill_dir);
    std::size_t soundings = 0;
    for (const auto & b : batches) {
      recon.add(b, params);
      soundings += b.size();
    }
    ASSERT_EQ(recon.soundingsSpilled(), soundings) << "every sounding must reach the spill";
    auto plan = std::make_shared<LevelPlan>(recon.plan());
    ASSERT_EQ(plan->levels(), std::set<uint8_t>{10});

    MultiLevelAccumulatorConfig cfg;
    cfg.store_dir = adaptive_dir;
    cfg.max_resident_tiles = 0;
    cfg.sheet_factory = [requested](uint8_t level) {
        EXPECT_EQ(level, 10);
        return std::make_unique<GeoMapSheet>(requested);
      };
    MultiLevelAccumulator acc(plan, cfg);
    // Same chunking as import_bag's phase two.
    constexpr std::size_t kChunk = 256;
    std::vector<GeoSounding> chunk;
    std::size_t replayed = 0;
    recon.forEachSpilled([&](const GeoSounding & s) {
        chunk.push_back(s);
        if (chunk.size() >= kChunk) {
          acc.addBatch(chunk);
          replayed += chunk.size();
          chunk.clear();
        }
      });
    if (!chunk.empty()) {
      acc.addBatch(chunk);
      replayed += chunk.size();
    }
    EXPECT_EQ(replayed, soundings);
    acc.finalize();
    recon.cleanup();
  }
  EXPECT_FALSE(std::filesystem::exists(spill_dir)) << "the spill must be cleaned up";

  const auto fixed = readTiles(fixed_dir);
  const auto adaptive = readTiles(adaptive_dir);
  ASSERT_FALSE(fixed.empty());
  ASSERT_GE(fixed.size(), 2u) << "the seam patch should produce more than one tile";
  ASSERT_EQ(adaptive.size(), fixed.size()) << "different tile sets";
  for (const auto & [name, bytes] : fixed) {
    auto it = adaptive.find(name);
    ASSERT_NE(it, adaptive.end()) << "adaptive path is missing " << name;
    EXPECT_TRUE(it->second == bytes) << "tile bytes differ: " << name;
  }
  std::filesystem::remove_all(fixed_dir);
  std::filesystem::remove_all(adaptive_dir);
}

// A plan that does not span the data being replayed (another survey's plan, or
// a re-run over changed bags) routes those soundings NOWHERE: every level's
// admission refuses them. The replay's own accounting counts records read back
// from the spill, not soundings any accumulator took, so without this counter
// the store would be finalized and fingerprinted as a complete build over
// partial coverage (cube#143 triage).
TEST(MixedLevelImport, SoundingsNoLevelAdmitsAreCountedAsUnrouted)
{
  const std::string dir = makeTempDir("unrouted");
  const auto here = lawn(43.07, -70.76, 4, 3.0e-5, 12.0f);
  LevelPlanPolicy policy;
  Parameters params{CellSizes(1.0f), "order1a"};
  auto plan = std::make_shared<LevelPlan>(planFor(here, policy, params));
  ASSERT_FALSE(plan->tiles().empty());

  MultiLevelAccumulatorConfig cfg;
  cfg.store_dir = dir;
  cfg.max_resident_tiles = 0;
  MultiLevelAccumulator acc(plan, cfg);

  // Soundings over the ground the plan WAS built for are routed.
  std::size_t routed = 0;
  for (const auto & b : here) {
    acc.addBatch(b);
    routed += b.size();
  }
  EXPECT_EQ(acc.unroutedSoundings(), 0u);

  // The same survey, 100 km away: no emitted tile at any level intersects it.
  std::size_t elsewhere_soundings = 0;
  for (const auto & b : lawn(44.0, -69.0, 4, 3.0e-5, 12.0f)) {
    acc.addBatch(b);
    elsewhere_soundings += b.size();
  }
  EXPECT_GT(elsewhere_soundings, 0u);
  EXPECT_EQ(acc.unroutedSoundings(), elsewhere_soundings);

  acc.finalize();
  std::filesystem::remove_all(dir);
}

// Property 2. A deep plain (40 m -> required level 9) with a shoal (3 m ->
// required 13), both densely sounded: the store holds native tiles at several
// levels, and the parent tiles over the shoal are complete estimates.
TEST(MixedLevelImport, ParentsStayCompleteUnderChildren)
{
  const std::string dir = makeTempDir("parents");
  // Plain: a 6x6 lawn ~3.3 m apart at 40 m. Shoal: a 6x6 lawn ~0.55 m apart at
  // 3 m inside the plain's footprint.
  auto batches = lawn(43.07, -70.76, 6, 3.0e-5, 40.0f);
  for (const auto & b : lawn(43.07005, -70.75995, 6, 5.0e-6, 3.0f)) {
    batches.push_back(b);
  }
  LevelPlanPolicy policy;
  Parameters params{CellSizes(1.0f), "order1a"};
  auto plan = std::make_shared<LevelPlan>(planFor(batches, policy, params));
  ASSERT_GE(plan->levels().size(), 2u) << "expected a multi-level plan";
  EXPECT_TRUE(plan->levels().count(8));

  MultiLevelAccumulatorConfig cfg;
  cfg.store_dir = dir;
  MultiLevelAccumulator acc(plan, cfg);
  for (const auto & b : batches) {
    acc.addBatch(b);
  }
  acc.finalize();

  // Files exist at more than one level.
  std::set<int> file_levels;
  for (const auto & [name, bytes] : readTiles(dir)) {
    file_levels.insert(std::stoi(name.substr(0, name.find('_'))));
  }
  EXPECT_GE(file_levels.size(), 2u);
  EXPECT_TRUE(file_levels.count(8));

  // The level-8 parent over the shoal holds finite cells over BOTH the plain and
  // the shoal: every shoal sounding position resolves to a finite parent cell.
  const auto cells = loadFiniteCells(dir);
  ASSERT_FALSE(cells.empty());
  const gggs::Level l8(8);
  std::size_t shoal_parent_hits = 0;
  for (const auto & b : lawn(43.07005, -70.75995, 6, 5.0e-6, 3.0f)) {
    const auto & s = b.front();
    if (cells.count(l8.cellIndex(gggs::geoPoint(s.latitude, s.longitude)))) {
      ++shoal_parent_hits;
    }
  }
  EXPECT_EQ(shoal_parent_hits, 36u) << "parent tile has holes under the shoal";
  // And the finest emitted level holds finite cells at the shoal. A CUBE node
  // sits at its cell's south-west corner (GeoGrid::insert measures distance to
  // CellIndex::position(), the SW corner), so a sounding settles the nearest
  // lattice node, which may be indexed by the cell to its north, east or
  // north-east; accept any of the four corner nodes around the position.
  const uint8_t finest = *plan->levels().rbegin();
  EXPECT_GT(finest, 8);
  const gggs::Level lf(finest);
  std::size_t fine_hits = 0;
  for (const auto & b : lawn(43.07005, -70.75995, 6, 5.0e-6, 3.0f)) {
    const auto & s = b.front();
    const auto home = lf.cellIndex(gggs::geoPoint(s.latitude, s.longitude));
    bool hit = false;
    for (int dr = 0; dr <= 1 && !hit; ++dr) {
      for (int dc = 0; dc <= 1 && !hit; ++dc) {
        const int r = home.row() + dr;
        const int c = home.column() + dc;
        if (r >= gggs::cell_rows_per_grid || c >= gggs::cell_columns_per_grid) {
          continue;  // the corner node belongs to a neighbouring grid
        }
        hit = cells.count(gggs::CellIndex(home.grid(),
              static_cast<uint16_t>(r), static_cast<uint16_t>(c))) > 0;
      }
    }
    if (hit) {++fine_hits;}
  }
  EXPECT_EQ(fine_hits, 36u) << "fine tiles missing shoal cells at level "
                            << static_cast<int>(finest);
  std::filesystem::remove_all(dir);
}

// Property 3. The budget is store-wide: a multi-level run with
// max_resident_tiles = N never holds more than N tiles across levels, evicts
// (the globally coldest first, shared clock), and the finished store equals an
// unbounded run's finite cells.
TEST(MixedLevelImport, OneResidentBudgetAcrossLevels)
{
  const std::string bounded_dir = makeTempDir("bounded");
  const std::string unbounded_dir = makeTempDir("unbounded");
  // Widely spaced patches (~2.2 km apart) so each lands on fresh tiles at every
  // level, alternating deep and shallow so several levels are active.
  std::vector<std::vector<GeoSounding>> batches;
  for (int i = 0; i < 6; ++i) {
    const float depth = (i % 2) ? 3.0f : 40.0f;
    for (const auto & b : lawn(43.0 + 0.02 * i, -70.0, 3, 5.0e-6, depth)) {
      batches.push_back(b);
    }
  }
  LevelPlanPolicy policy;
  Parameters params{CellSizes(1.0f), "order1a"};
  auto plan = std::make_shared<LevelPlan>(planFor(batches, policy, params));
  ASSERT_GE(plan->levels().size(), 2u);

  const std::size_t budget = 3;
  std::size_t peak = 0;
  {
    MultiLevelAccumulatorConfig cfg;
    cfg.store_dir = bounded_dir;
    cfg.max_resident_tiles = budget;
    MultiLevelAccumulator acc(plan, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
      peak = std::max(peak, acc.residentTileCount());
    }
    EXPECT_GT(acc.evictedTileCount(), 0u);
    acc.finalize();
  }
  EXPECT_LE(peak, budget);
  {
    MultiLevelAccumulatorConfig cfg;
    cfg.store_dir = unbounded_dir;
    cfg.max_resident_tiles = 0;
    MultiLevelAccumulator acc(plan, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
  }
  const auto bounded = loadFiniteCells(bounded_dir);
  const auto unbounded = loadFiniteCells(unbounded_dir);
  ASSERT_FALSE(unbounded.empty());
  ASSERT_EQ(bounded.size(), unbounded.size()) << "bounded run lost or gained cells";
  for (const auto & [cell, depth] : unbounded) {
    auto it = bounded.find(cell);
    ASSERT_NE(it, bounded.end());
    EXPECT_NEAR(it->second, depth, 1e-3);
  }
  std::filesystem::remove_all(bounded_dir);
  std::filesystem::remove_all(unbounded_dir);
}

#ifdef IMPORT_BAG_EXE
namespace
{
// Run import_bag with @p args; merged stdout+stderr, exit status via @p status.
std::string runImportBag(const std::string & args, int * status)
{
  const std::string command = std::string(IMPORT_BAG_EXE) + " " + args + " 2>&1";
  std::string output;
  FILE * pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    *status = -1;
    return output;
  }
  std::array<char, 4096> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  const int rc = pclose(pipe);
  *status = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
  return output;
}
}  // namespace

// The depth-adaptive policy is validated before any bag is opened: a finest
// level past 14 breaks ADR-0002's dirty-set guarantee and must be refused at
// startup with the reason, not hours into a pass.
TEST(ImportBagCli, RefusesAFinestLevelPastTheSurveyIndex)
{
  int status = -1;
  const std::string out = runImportBag(
    "--depth-adaptive --depth-adaptive-finest 15 -o /nonexistent/store -d /t /nonexistent.bag",
    &status);
  EXPECT_NE(status, 0) << out;
  EXPECT_NE(out.find("finest_level 15 exceeds 14"), std::string::npos) << out;
  EXPECT_EQ(out.find("cannot open"), std::string::npos)
    << "validation must fail before any bag is opened\n" << out;
}

TEST(ImportBagCli, RefusesPlanFlagsWithoutDepthAdaptive)
{
  int status = -1;
  const std::string out = runImportBag(
    "--level-plan-out /tmp/x.json -o /nonexistent/store -d /t /nonexistent.bag", &status);
  EXPECT_NE(status, 0) << out;
  EXPECT_NE(out.find("need --depth-adaptive"), std::string::npos) << out;
}

TEST(ImportBagCli, RefusesAnInvalidCaptureSpacingScale)
{
  int status = -1;
  const std::string out = runImportBag(
    "--capture-spacing-scale 0 -o /nonexistent/store -d /t /nonexistent.bag", &status);
  EXPECT_NE(status, 0) << out;
  EXPECT_NE(out.find("--capture-spacing-scale must be a positive number"), std::string::npos)
    << out;
}

// The resident count-tile budget has a real floor (CountGrid::kMinResidentTiles
// = the 3x3 level-of-aggregation neighbourhood plus headroom). It must be
// refused at parse time with the true range, not accepted by the option parser
// and thrown out by the ReconCollector constructor after the orphan warning and
// the spill banner have already printed.
TEST(ImportBagCli, RefusesACountResidentBudgetBelowTheRealMinimum)
{
  const std::vector<std::string> below{"-1", "0", "15"};
  for (const std::string & value : below) {
    int status = -1;
    const std::string out = runImportBag(
      "--depth-adaptive --count-resident-tiles " + value +
      " -o /nonexistent/store -d /t /nonexistent.bag", &status);
    EXPECT_NE(status, 0) << out;
    EXPECT_NE(
      out.find("'--count-resident-tiles' expects a count >= " +
      std::to_string(CountGrid::kMinResidentTiles)), std::string::npos) << out;
    // Refused before anything about the pass is printed.
    EXPECT_EQ(out.find("Recon spill:"), std::string::npos) << out;
  }

  int status = -1;
  const std::string out = runImportBag(
    "--depth-adaptive --count-resident-tiles " +
    std::to_string(CountGrid::kMinResidentTiles) +
    " -o /nonexistent/store -d /t /nonexistent.bag", &status);
  EXPECT_EQ(out.find("expects a count >="), std::string::npos)
    << "the minimum itself must be accepted\n" << out;
}

// The count-tile allowance doubles what the preflight free-space check demands,
// which can refuse a dense survey over little ground whose spill would have fit.
// It is tunable, validated, and (like its siblings) meaningless without
// --depth-adaptive.
TEST(ImportBagCli, CountSpillAllowanceIsTunableAndValidated)
{
  int status = -1;
  std::string out = runImportBag(
    "--depth-adaptive --count-spill-allowance -0.5 -o /nonexistent/store "
    "-d /t /nonexistent.bag", &status);
  EXPECT_NE(status, 0) << out;
  EXPECT_NE(
    out.find("'--count-spill-allowance' expects a finite factor >= 0"),
    std::string::npos) << out;

  out = runImportBag(
    "--count-spill-allowance 0.5 -o /nonexistent/store -d /t /nonexistent.bag", &status);
  EXPECT_NE(status, 0) << out;
  EXPECT_NE(out.find("need --depth-adaptive"), std::string::npos) << out;

  out = runImportBag(
    "--depth-adaptive --count-spill-allowance 0 -o /nonexistent/store "
    "-d /t /nonexistent.bag", &status);
  EXPECT_EQ(out.find("expects a finite factor"), std::string::npos)
    << "0 removes the allowance; it is not an invalid value\n" << out;
}

// TEMPORARY refusal (cube#143 triage): marine_mbes_backscatter_store is
// single-level by construction, and every level of a depth-adaptive run is
// handed the same store root -- the run would write a backscatter store nothing
// can load. The refusal must name uma#383 (the per-level layout) so the operator
// knows it is gated work, not a dead end. Delete this test with the refusal when
// the PR that consumes uma#383 lands.
TEST(ImportBagCli, RefusesBackscatterOnTheMixedLevelPathNamingThePrerequisite)
{
  int status = -1;
  const std::string out = runImportBag(
    "--depth-adaptive --bs-store /tmp/bs -o /nonexistent/store -d /t /nonexistent.bag",
    &status);
  EXPECT_NE(status, 0) << out;
  EXPECT_NE(out.find("--bs-store is not supported with --depth-adaptive"), std::string::npos)
    << out;
  EXPECT_NE(out.find("unh_marine_autonomy/issues/383"), std::string::npos)
    << "the refusal must name the prerequisite\n" << out;
  EXPECT_EQ(out.find("cannot open"), std::string::npos)
    << "the refusal must land before any bag is opened\n" << out;

  // Fixed-level runs are untouched: --bs-store there is not refused (this run
  // fails later, on the bag, not on the option).
  const std::string fixed = runImportBag(
    "--bs-store /tmp/bs -o /nonexistent/store -d /t /nonexistent.bag", &status);
  EXPECT_EQ(fixed.find("--bs-store is not supported"), std::string::npos) << fixed;
}

TEST(ImportBagCli, UsageDocumentsTheDepthAdaptiveFlags)
{
  int status = -1;
  const std::string out = runImportBag("-h", &status);
  EXPECT_NE(out.find("--depth-adaptive"), std::string::npos);
  EXPECT_NE(out.find("--level-plan-out"), std::string::npos);
  EXPECT_NE(out.find("--capture-spacing-scale"), std::string::npos);
  EXPECT_NE(out.find("--scratch-dir"), std::string::npos);
  EXPECT_NE(out.find("--count-spill-allowance"), std::string::npos);
  // The real minimum, not ">= 0", is what --help states.
  EXPECT_NE(out.find("--count-resident-tiles <N> (256, minimum 16)"), std::string::npos);
}
#endif  // IMPORT_BAG_EXE

}  // namespace cube
