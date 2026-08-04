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

// Bounded == unbounded equivalence for the offline import accumulator (#92).
//
// The proof that the cube#70 persist-then-drop + reload-on-revisit scheme, ported
// from the live node to import_bag, is LOSSLESS: a survey run with a tiny resident
// budget (forcing eviction + reload) must produce the SAME on-disk bathy store
// (depth + uncertainty) AND backscatter store (intensity + variance) as the same
// survey run unbounded (everything in RAM, persisted only at the end). A
// data-losing eviction would drop or NaN-out cells and the comparison would fail.

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_mbes_backscatter_store/mbes_store.hpp"
#include "marine_mbes_backscatter_store/registry.hpp"
#include "marine_mbes_backscatter_store/tile_io.hpp"

namespace cube
{
namespace
{
constexpr float kCellSize = 1.0f;

std::string makeTempDir(const std::string & tag)
{
  const auto base = std::filesystem::temp_directory_path() /
    ("cube_import_evict_" + tag + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  return base.string();
}

// One cell's worth of survey: several soundings at one position so CUBE settles a
// finite depth AND co-estimates a finite intensity (a single sample may not
// resolve a variance). A constant intensity keeps the Welford mean exact.
std::vector<GeoSounding> surveyCell(
  double lat, double lon, float depth, float intensity, int reps = 8)
{
  std::vector<GeoSounding> soundings;
  for (int rep = 0; rep < reps; ++rep) {
    gz4d::GeoPointLatLongDegrees point(lat, lon, -depth - rep * 0.001);
    GeoSounding s(point);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    s.sounding.intensity = intensity;
    s.sounding.beam_angle = 0.0f;
    s.sounding.slant_range = depth;
    soundings.push_back(s);
  }
  return soundings;
}

ImportAccumulatorConfig makeConfig(
  const std::string & store_dir, const std::string & bs_dir, std::size_t budget)
{
  ImportAccumulatorConfig cfg;
  cfg.store_dir = store_dir;
  cfg.cell_size_m = kCellSize;
  cfg.bs_store_dir = bs_dir;
  cfg.max_resident_tiles = budget;
  return cfg;
}

// Drive one full import: feed every batch through the accumulator, then finalize.
void runImport(
  const std::vector<std::vector<GeoSounding>> & batches,
  const std::string & store_dir, const std::string & bs_dir, std::size_t budget)
{
  GeoMapSheet sheet(kCellSize);
  ImportAccumulator accumulator(sheet, makeConfig(store_dir, bs_dir, budget));
  for (const auto & batch : batches) {
    accumulator.addBatch(batch);
  }
  // No StoreMetadata needed for the equivalence assertions (uma#248 moved provenance
  // to an optional store-level sidecar; the tile bands carry the data under test).
  accumulator.finalize();
}

// Load every finite bathy cell of a store layer into a comparable map.
std::map<gggs::CellIndex, std::pair<double, double>> loadBathyCells(
  const std::string & store_dir)
{
  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
  marine_bathymetry_store::load(store, store_dir);
  std::map<gggs::CellIndex, std::pair<double, double>> out;
  for (const auto & grid_tile : store.tiles(
      marine_bathymetry_store::SourceLayer::Survey))
  {
    const auto & depth = grid_tile.second.depthBand();
    const auto & unc = grid_tile.second.uncertaintyBand();
    gggs::CellAreaIterator it(grid_tile.second.index());
    std::size_t k = 0;
    for (; it.valid() && k < depth.size(); it.next(), ++k) {
      if (std::isfinite(depth[k])) {
        out.emplace(*it, std::make_pair(depth[k], unc[k]));
      }
    }
  }
  return out;
}

// Count finite cells in a store's `reference` layer (what a measured seed WOULD
// have contributed, for the reference-seed enforcement test).
std::size_t countReferenceFiniteCells(const std::string & store_dir)
{
  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
  marine_bathymetry_store::load(store, store_dir);
  std::size_t n = 0;
  for (const auto & grid_tile : store.tiles(
      marine_bathymetry_store::SourceLayer::Reference))
  {
    for (double d : grid_tile.second.depthBand()) {
      if (std::isfinite(d)) {++n;}
    }
  }
  return n;
}

// Load every finite backscatter cell of a store into a comparable map. Compares
// the RAW stored 3-band statistic (mean, sample_sd) so bounded vs unbounded is a
// byte-level losslessness check (no reconstruction, so no n=1-sentinel ambiguity).
std::map<gggs::CellIndex, std::pair<float, float>> loadBackscatterCells(
  const std::string & bs_dir)
{
  marine_mbes_backscatter_store::MbesBackscatterStore store =
    marine_mbes_backscatter_store::MbesBackscatterStore::fromCellSize(kCellSize);
  marine_mbes_backscatter_store::load(store, bs_dir);
  std::map<gggs::CellIndex, std::pair<float, float>> out;
  for (const auto & grid_tile : store.tiles(
      marine_mbes_backscatter_store::SourceLayer::Survey))
  {
    gggs::CellAreaIterator it(grid_tile.second.index());
    for (; it.valid(); it.next()) {
      const marine_mbes_backscatter_store::MbesCell c =
        grid_tile.second.get((*it).row(), (*it).column());
      if (c.hasData()) {
        out.emplace(*it, std::make_pair(c.mean, c.sample_sd));
      }
    }
  }
  return out;
}

// Compare two store dirs cell-for-cell. Cell COUNT must match exactly (a dropped
// or NaN-ed cell is data loss). Depth and backscatter (intensity + variance) are
// compared tightly: eviction+reload+merge must reproduce them, not approximate.
//
// @param uncertainty_tol Bathy-uncertainty tolerance. For a cell that is evicted
//   and never revisited (or never evicted), the uncertainty is bit-reproduced
//   (pass 1e-3). For a cell reloaded on revisit, the warm-start restores the
//   settled DEPTH losslessly but RE-DERIVES the uncertainty from the reseeded
//   single hypothesis rather than the original sample history (ADR-0001 / #21:
//   "does NOT reconstruct CUBE hypothesis, queue, or pre-filter state"), so its
//   uncertainty drifts slightly — a documented, depth-preserving artifact, not
//   data loss. Such tests pass a looser bound.
void expectStoresEqual(
  const std::string & bounded_dir, const std::string & unbounded_dir,
  const std::string & bounded_bs, const std::string & unbounded_bs,
  double uncertainty_tol)
{
  const auto bathy_b = loadBathyCells(bounded_dir);
  const auto bathy_u = loadBathyCells(unbounded_dir);
  ASSERT_FALSE(bathy_u.empty()) << "unbounded build produced no bathy cells";
  ASSERT_EQ(bathy_b.size(), bathy_u.size())
    << "bounded build lost or gained bathy cells";
  for (const auto & [cell, du] : bathy_u) {
    auto it = bathy_b.find(cell);
    ASSERT_NE(it, bathy_b.end()) << "bounded build is missing a bathy cell";
    EXPECT_NEAR(it->second.first, du.first, 1e-3) << "depth differs";
    EXPECT_NEAR(it->second.second, du.second, uncertainty_tol)
      << "uncertainty differs";
  }

  const auto bs_b = loadBackscatterCells(bounded_bs);
  const auto bs_u = loadBackscatterCells(unbounded_bs);
  ASSERT_FALSE(bs_u.empty()) << "unbounded build produced no backscatter cells";
  ASSERT_EQ(bs_b.size(), bs_u.size())
    << "bounded build lost or gained backscatter cells";
  for (const auto & [cell, iv] : bs_u) {
    auto it = bs_b.find(cell);
    ASSERT_NE(it, bs_b.end()) << "bounded build is missing a backscatter cell";
    EXPECT_NEAR(it->second.first, iv.first, 1e-3) << "backscatter mean differs";
    EXPECT_NEAR(it->second.second, iv.second, 1e-3) << "backscatter sample_sd differs";
  }
}
}  // namespace

// A disjoint multi-tile survey: a tiny budget forces every tile to be evicted
// once (and never revisited), so the bounded store is the union of the evicted
// tiles. It must equal the unbounded store exactly.
TEST(ImportEviction, BoundedEqualsUnboundedNoRevisit)
{
  std::vector<std::vector<GeoSounding>> batches;
  const int n_tiles = 12;
  for (int i = 0; i < n_tiles; ++i) {
    // 0.02 deg (~2.2 km) apart -> every batch lands on a distinct GGGS tile.
    batches.push_back(
      surveyCell(43.0 + 0.02 * i, -70.0, 10.0f + i, 30.0f + i));
  }

  const std::string root = makeTempDir("norevisit");
  const std::string b_dir = root + "/bounded";
  const std::string u_dir = root + "/unbounded";
  const std::string b_bs = root + "/bounded_bs";
  const std::string u_bs = root + "/unbounded_bs";

  runImport(batches, b_dir, b_bs, /*budget=*/3);   // forces eviction
  runImport(batches, u_dir, u_bs, /*budget=*/0);   // unbounded

  // No revisit: every cell is bit-reproduced, uncertainty included.
  expectStoresEqual(b_dir, u_dir, b_bs, u_bs, /*uncertainty_tol=*/1e-3);
  std::filesystem::remove_all(root);
}

// A tile surveyed, evicted (by surveying many other tiles), then revisited at
// DIFFERENT cells within it. The reload-on-revisit must restore the first visit's
// settled cells (bathy) and the on-disk merge must preserve their backscatter, so
// the final store has BOTH visits' cells -- identical to the unbounded build.
TEST(ImportEviction, RevisitEqualsUnbounded)
{
  std::vector<std::vector<GeoSounding>> batches;
  // Visit 1: tile A (cell near its SW).
  batches.push_back(surveyCell(43.00000, -70.00000, 12.0f, 40.0f));
  // Evict A by surveying 10 distinct far-away tiles.
  for (int i = 1; i <= 10; ++i) {
    batches.push_back(surveyCell(43.0 + 0.02 * i, -71.0, 15.0f + i, 25.0f + i));
  }
  // Visit 2: revisit tile A at a DIFFERENT cell (~55 m N, same ~960 m tile).
  batches.push_back(surveyCell(43.00050, -70.00000, 18.0f, 55.0f));

  const std::string root = makeTempDir("revisit");
  const std::string b_dir = root + "/bounded";
  const std::string u_dir = root + "/unbounded";
  const std::string b_bs = root + "/bounded_bs";
  const std::string u_bs = root + "/unbounded_bs";

  runImport(batches, b_dir, b_bs, /*budget=*/3);   // A is evicted then reloaded
  runImport(batches, u_dir, u_bs, /*budget=*/0);   // A never leaves RAM

  // No cell is lost (count matches) and every depth + backscatter value is
  // reproduced. The first visit's reloaded cells keep their depth losslessly but
  // re-derive uncertainty from the warm-start reseed (ADR-0001), so uncertainty
  // is compared with a looser bound.
  expectStoresEqual(b_dir, u_dir, b_bs, u_bs, /*uncertainty_tol=*/0.05);
  std::filesystem::remove_all(root);
}

// Lossless backscatter blend (cube#92 Option A): a cell resurveyed AFTER its tile
// was evicted blends BOTH visits' backscatter, matching a never-evicted build.
// The raw per-beam intensity samples are spilled to scratch on eviction and
// restored onto the reloaded hypothesis BEFORE the revisit's beams accrete, so the
// node-output intensity is the full population, not just the newest visit. Both
// visits survey the SAME depth so they land on one hypothesis (a consistent
// re-survey -- the overlap case of crossing survey lines).
TEST(ImportEviction, ResurveyedBackscatterBlendsLosslessly)
{
  constexpr double kLat = 43.0;
  constexpr double kLon = -70.0;
  constexpr float kDepth = 12.0f;
  constexpr float kI1 = 20.0f;  // visit-1 intensity
  constexpr float kI2 = 60.0f;  // visit-2 intensity (same cell, same depth)

  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(kLat, kLon, kDepth, kI1));       // visit 1
  for (int i = 1; i <= 10; ++i) {                               // evict the tile
    batches.push_back(surveyCell(43.0 + 0.02 * i, -71.0, 15.0f + i, 25.0f + i));
  }
  batches.push_back(surveyCell(kLat, kLon, kDepth, kI2));       // visit 2, SAME cell

  const std::string root = makeTempDir("resurvey");
  const std::string b_dir = root + "/bounded";
  const std::string u_dir = root + "/unbounded";
  const std::string b_bs = root + "/bounded_bs";
  const std::string u_bs = root + "/unbounded_bs";
  runImport(batches, b_dir, b_bs, /*budget=*/3);    // evict + reload-before-add
  runImport(batches, u_dir, u_bs, /*budget=*/0);    // never evicts

  // The resurveyed cell's CellIndex (deterministic for a fixed position).
  marine_mbes_backscatter_store::MbesBackscatterStore probe =
    marine_mbes_backscatter_store::MbesBackscatterStore::fromCellSize(kCellSize);
  const gggs::CellIndex xcell = probe.cellIndex(kLat, kLon);

  const auto bs_b = loadBackscatterCells(b_bs);
  const auto bs_u = loadBackscatterCells(u_bs);
  ASSERT_TRUE(bs_b.count(xcell)) << "bounded build lost the resurveyed cell";
  ASSERT_TRUE(bs_u.count(xcell)) << "unbounded build lost the resurveyed cell";

  const float bounded_i = bs_b.at(xcell).first;
  const float unbounded_i = bs_u.at(xcell).first;

  // Bounded == unbounded == the blend of both equal-count visits ~ (I1 + I2) / 2.
  EXPECT_NEAR(unbounded_i, 0.5f * (kI1 + kI2), 0.5f) << "unbounded should blend";
  EXPECT_NEAR(bounded_i, unbounded_i, 1e-3f)
    << "bounded build must reproduce the unbounded blend (lossless backscatter)";
  // And the sample standard deviation (the stored dispersion band) matches too --
  // same sample population => same sample_sd.
  EXPECT_NEAR(bs_b.at(xcell).second, bs_u.at(xcell).second, 1e-3f)
    << "bounded build must reproduce the unbounded backscatter sample_sd";
}

// The scratch spill directory is created during eviction and DELETED by finalize.
TEST(ImportEviction, ScratchDirCleanedUpAfterFinalize)
{
  const std::string root = makeTempDir("scratch");
  const std::string store_dir = root + "/store";
  const std::string bs_dir = root + "/bs";

  GeoMapSheet sheet(kCellSize);
  ImportAccumulator accumulator(sheet, makeConfig(store_dir, bs_dir, /*budget=*/3));
  for (int i = 0; i < 12; ++i) {  // >> budget -> forces eviction (and a spill)
    accumulator.addBatch(surveyCell(43.0 + 0.02 * i, -70.0, 10.0f + i, 30.0f + i));
  }
  const std::string scratch = accumulator.scratchDir();
  ASSERT_FALSE(scratch.empty()) << "eviction should have created a scratch dir";
  EXPECT_TRUE(std::filesystem::exists(scratch)) << "scratch dir should exist mid-run";

  accumulator.finalize();

  EXPECT_FALSE(std::filesystem::exists(scratch))
    << "finalize must delete the scratch spill dir";
  EXPECT_TRUE(accumulator.scratchDir().empty())
    << "finalize must clear the scratch path";

  std::filesystem::remove_all(root);
}

// Lossless guarantee: if a cold tile's persist FAILS, it must NOT be dropped
// (that would lose unsaved data). Force the failure by planting a regular file
// where the layer directory must be created, then survey past the budget.
TEST(ImportEviction, NeverDropsTileWhenPersistFails)
{
  const std::string root = makeTempDir("persistfail");
  const std::string store_dir = root + "/store";
  std::filesystem::create_directories(store_dir);
  // layerDirName(Survey) == "survey": plant a FILE there so
  // create_directories() throws and persistBathyTile cannot write.
  {
    std::ofstream blocker(
      store_dir + "/" +
      marine_bathymetry_store::layerDirName(
        marine_bathymetry_store::SourceLayer::Survey));
    blocker << "not a directory";
  }

  GeoMapSheet sheet(kCellSize);
  const std::size_t budget = 2;
  ImportAccumulator accumulator(sheet, makeConfig(store_dir, "", budget));

  const int n_tiles = 6;  // >> budget
  for (int i = 0; i < n_tiles; ++i) {
    accumulator.addBatch(surveyCell(43.0 + 0.02 * i, -70.0, 10.0f + i, 30.0f));
  }

  // No bathy tile could be written (the layer dir can't be created), so no
  // DATA-bearing tile was dropped: all surveyed (finite-data) tiles remain
  // resident and RAM grew past the budget rather than losing unsaved data.
  // (An empty tile a survey spilled into across a GGGS seam carries no data; the
  // accumulator may drop such an empty tile harmlessly -- losing nothing -- so we
  // assert on the data-bearing invariant, not an exact resident/evicted count.)
  EXPECT_EQ(accumulator.bathyTilesPersisted(), static_cast<std::size_t>(0));
  EXPECT_GE(accumulator.residentTileCount(), static_cast<std::size_t>(n_tiles));
  EXPECT_GT(accumulator.residentTileCount(), budget);

  std::filesystem::remove_all(root);
}

// Reference-layer seed precedence (#96): a tile seeded from the `reference` prior
// must NOT be counted as measured data. The coarse prior gates blunder rejection
// (seed_settled=false) but never settles a cell, so a sparse survey over a
// reference-seeded tile writes ONLY the resurveyed cells -- identical in count to
// the same survey with NO reference at all, and far fewer than the reference
// tile's own finite cells.
TEST(ImportEviction, ReferenceSeedDoesNotAddMeasuredData)
{
  const std::string root = makeTempDir("refseed");
  const std::string ref_dir = root + "/reference_store";

  // Build a DENSE reference tile (many finite cells) and write it to the store's
  // reference/ layer. Depth ~ 20 m so a same-depth survey later is accepted (not
  // blunder-rejected) by the seeded predicted surface.
  {
    GeoMapSheet ref_sheet(kCellSize);
    for (int i = 0; i < 20; ++i) {
      // ~1.1 m steps (1e-5 deg) keep the cells within one ~960 m tile.
      ref_sheet.addSoundings(
        surveyCell(43.0 + i * 1e-5, -70.0 + i * 1e-5, 20.0f, 30.0f));
    }
    auto tiles = mapSheetToTiles(ref_sheet);
    ASSERT_FALSE(tiles.empty());
    marine_bathymetry_store::BathymetryStore ref_store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/true);
    ref_store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(ref_store, ref_dir);
  }
  const std::size_t reference_finite = countReferenceFiniteCells(ref_dir);
  ASSERT_GT(reference_finite, 1u) << "reference tile should cover many cells";

  // A sparse survey: soundings on ONE cell of that tile, at the same ~20 m depth.
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(43.00000, -70.00000, 20.0f, 40.0f));

  // Run WITH reference seeding.
  const std::string with_ref = root + "/with_ref";
  {
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(with_ref, "", /*budget=*/0);
    cfg.reference_store_dir = ref_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
  }

  // Run WITHOUT reference seeding (baseline).
  const std::string no_ref = root + "/no_ref";
  runImport(batches, no_ref, "", /*budget=*/0);

  const auto with = loadBathyCells(with_ref);
  const auto without = loadBathyCells(no_ref);

  ASSERT_FALSE(with.empty()) << "the sparse survey should settle at least one cell";
  // The reference seed contributed ZERO measured cells: the two survey stores hold
  // the same cell COUNT (only the sparse survey's cells)...
  EXPECT_EQ(with.size(), without.size())
    << "reference seeding must not add measured cells";
  // ...and that is far fewer than the reference tile's own finite cells (the prior
  // fill was gated-only, never settled).
  EXPECT_LT(with.size(), reference_finite)
    << "the reference prior's cells must not be settled as survey data";

  std::filesystem::remove_all(root);
}

// Cross-level reference blunder gate (#115): a reference store holding ONLY tiles at
// a COARSER GGGS level than the survey (e.g. ENC exports at L5/L7/L8 under an L10
// survey) must still gate a false-deep blunder. `loadWindow` returns the coarse tile
// but keys it by its own (coarser) GridIndex, so the same-level `tiles.find(index)`
// in seedNewTile rung 2 misses it; the level-walk fallback (Phase B) resamples the
// coarse shallow prior onto the fine survey cells so the gate turns on. Without that
// fallback the exact-level find misses, nothing gates, and the deep sounding enters
// the survey layer unchallenged -- the exact silent-miss this issue fixes.
TEST(ImportEviction, CoarseLevelReferenceSeedRejectsDeepBlunder)
{
  // A COARSER cell size -> a coarser GGGS level than the 1 m survey (L10). 8 m -> L7.
  constexpr float kCoarseCellSize = 8.0f;
  ASSERT_LT(gggs::Level::fromCellSize(kCoarseCellSize).level(),
    gggs::Level::fromCellSize(kCellSize).level())
    << "the reference must be at a coarser level to exercise the level-walk";

  const std::string root = makeTempDir("xlevel_refseed");
  const std::string ref_dir = root + "/reference_store";

  // Survey the INTERIOR of a single L10 tile (its center), not a shared tile corner:
  // a corner sounding routes ambiguously to one of four tiles and can settle in a
  // near-corner cell. GGGS nesting puts this L10 tile wholly inside one L7 tile.
  const gggs::GridIndex survey_grid =
    gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;

  // Build the coarse reference tile DIRECTLY (not via CUBE) so its coverage is
  // deterministic: fill the ENTIRE L7 tile that contains the survey tile with a
  // SHALLOW (-20 m) prior. Every fine survey cell then resamples to a finite shallow
  // coarse cell -- a real ENC prior is likewise a dense filled surface, not the
  // sparse point pattern a handful of synthetic soundings would settle.
  const gggs::Level coarse_level = gggs::Level::fromCellSize(kCoarseCellSize);
  const gggs::GridIndex coarse_grid = coarse_level.gridIndex(survey_lat, survey_lon);
  ASSERT_LT(coarse_grid.level(), survey_grid.level())
    << "the reference tile must be at a coarser level than the survey tile";
  {
    marine_bathymetry_store::BathymetryTile ctile(coarse_grid);
    for (gggs::CellAreaIterator cit(coarse_grid); cit.valid(); cit.next()) {
      ctile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(coarse_grid, std::move(ctile));
    marine_bathymetry_store::BathymetryStore ref_store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCoarseCellSize, /*reference_writable=*/true);
    ref_store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(ref_store, ref_dir);
  }
  ASSERT_GT(countReferenceFiniteCells(ref_dir), 1u)
    << "the coarse reference tile should carry many finite cells";

  // A clearly-too-deep blunder (~ -150 m) at the survey location, where the coarse
  // prior says ~ -20 m -- unambiguously below any shallow-surface blunder limit.
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));

  // Run WITH the coarse reference seeding.
  const std::string with_ref = root + "/with_ref";
  {
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(with_ref, "", /*budget=*/0);
    cfg.reference_store_dir = ref_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
  }

  // Baseline: no reference at all.
  const std::string no_ref = root + "/no_ref";
  runImport(batches, no_ref, "", /*budget=*/0);

  const auto with = loadBathyCells(with_ref);
  const auto without = loadBathyCells(no_ref);

  // Without a prior, Node::insert skips the blunder gate: the deep sounding is
  // accepted and settles a deep (< -100 m) cell.
  ASSERT_FALSE(without.empty())
    << "without a prior the deep sounding must settle a cell";
  bool baseline_is_deep = false;
  for (const auto & [cell, du] : without) {
    if (du.first < -100.0) {baseline_is_deep = true;}
  }
  EXPECT_TRUE(baseline_is_deep)
    << "the baseline deep sounding should settle a deep (< -100 m) cell";

  // With the COARSER reference, the level-walk fallback seeds the shallow predicted
  // surface and the blunder gate rejects the deep sounding -- so NO cell settles.
  // (Revert the level-walk and the exact-level find misses the L7 tile, nothing
  // gates, and `with` gains the deep cell -> this fails.)
  EXPECT_TRUE(with.empty())
    << "the cross-level reference must gate the deep blunder (level-walk fallback)";

  std::filesystem::remove_all(root);
}

}  // namespace cube
