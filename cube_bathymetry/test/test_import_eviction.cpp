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
#include <limits>
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

// RAII stderr capture. gtest's CaptureStderr/GetCapturedStderr is a SINGLE-capturer
// facility: a throw between the two leaves fd 2 redirected, and the next
// CaptureStderr then trips gtest's "already capturing" check and ABORTS the whole
// binary -- hiding the original failure and taking every later test with it. The
// destructor releases the capture so an exception in the code under test surfaces as
// an ordinary test failure instead.
class StderrCapture
{
public:
  StderrCapture() {testing::internal::CaptureStderr();}
  ~StderrCapture()
  {
    if (!released_) {
      testing::internal::GetCapturedStderr();
    }
  }
  StderrCapture(const StderrCapture &) = delete;
  StderrCapture & operator=(const StderrCapture &) = delete;
  /// Stop capturing and return what was written.
  std::string str()
  {
    released_ = true;
    return testing::internal::GetCapturedStderr();
  }

private:
  bool released_ = false;
};

// Occurrences of @p needle in @p haystack -- for "emitted EXACTLY once" assertions,
// which a plain find() cannot make (a duplicated warning would pass).
std::size_t countOccurrences(const std::string & haystack, const std::string & needle)
{
  std::size_t n = 0;
  for (std::size_t pos = haystack.find(needle); pos != std::string::npos;
    pos = haystack.find(needle, pos + needle.size()))
  {
    ++n;
  }
  return n;
}

// Write a CHART-layer prior tile at @p coarse_grid: shallow (-20 m) everywhere
// EXCEPT the cells overlying survey tile @p hole, which are written as explicit
// no-data. The tile therefore CONTAINS the survey tile (so the containment walk
// selects it) and still primes NOT ONE survey cell -- the "matched but empty"
// case that made a tile MATCH look like a prime before #137's review round.
void writeChartPriorWithHole(
  const std::string & prior_dir, float coarse_cell_size,
  const gggs::GridIndex & coarse_grid, const gggs::GridIndex & hole)
{
  const double cell_lat =
    coarse_grid.latitudinalSpan() / gggs::GridIndex::cellRowCount();
  const double cell_lon =
    coarse_grid.longitudinalSpan() / gggs::GridIndex::cellColumnCount();
  // Pad the hole by one coarse cell: a survey cell's center always resolves into a
  // coarse cell whose own center lies within half a coarse cell of the survey tile,
  // so this guarantees every coarse cell the resample could read is no-data.
  const double south = hole.southLatitude() - cell_lat;
  const double north = hole.southLatitude() + hole.latitudinalSpan() + cell_lat;
  const double west = hole.westLongitude() - cell_lon;
  const double east = hole.westLongitude() + hole.longitudinalSpan() + cell_lon;
  const double kNoData = std::numeric_limits<double>::quiet_NaN();

  marine_bathymetry_store::BathymetryTile ctile(coarse_grid);
  for (gggs::CellAreaIterator cit(coarse_grid); cit.valid(); cit.next()) {
    const auto sw = (*cit).position();
    const double clat = sw.latitude + 0.5 * cell_lat;
    const double clon = sw.longitude + 0.5 * cell_lon;
    const bool in_hole = clat >= south && clat <= north &&
      clon >= west && clon <= east;
    ctile.set(
      (*cit).row(), (*cit).column(),
      marine_bathymetry_store::BathyCell{
          in_hole ? kNoData : -20.0, in_hole ? kNoData : 0.5});
  }
  std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
  tiles.emplace(coarse_grid, std::move(ctile));
  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(
    coarse_cell_size, /*reference_writable=*/false, /*chart_staging_writable=*/true);
  store.importTiles(marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
  marine_bathymetry_store::save(store, prior_dir);
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
      marine_bathymetry_store::SourceLayer::Processed))
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
  // layerDirName(Processed) == "processed": plant a FILE there so
  // create_directories() throws and persistBathyTile cannot write.
  {
    std::ofstream blocker(
      store_dir + "/" +
      marine_bathymetry_store::layerDirName(
        marine_bathymetry_store::SourceLayer::Processed));
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

// Chart-layer blunder gate (#119): since the reference->chart layer split,
// official chart products live in the Chart layer, which seedNewTile's rung 2
// never consulted -- a prior store holding ONLY chart/ tiles left the import
// blunder gate silently OFF for charted waters. The Chart exact-match rung fixes
// that: a same-level chart tile primes the predicted surface (predicted-only)
// and the false-deep sounding is rejected. (Revert the Chart rung and `with`
// gains the deep cell -> this fails.)
TEST(ImportEviction, ChartLayerSeedRejectsDeepBlunder)
{
  const std::string root = makeTempDir("chartseed");
  const std::string prior_dir = root + "/prior_store";

  // Survey the interior of a single survey-level tile (see the cross-level test
  // below for why the center, not a shared corner).
  const gggs::GridIndex survey_grid =
    gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;

  // A dense SHALLOW (-20 m) chart tile AT THE SURVEY LEVEL covering the whole
  // tile, written to the store's chart/ layer (staging-writable store, then
  // save -- the runtime read path loads it like any other layer).
  {
    marine_bathymetry_store::BathymetryTile ctile(survey_grid);
    for (gggs::CellAreaIterator cit(survey_grid); cit.valid(); cit.next()) {
      ctile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(survey_grid, std::move(ctile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/false, /*chart_staging_writable=*/true);
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  // A clearly-too-deep blunder (~ -150 m) where the chart prior says ~ -20 m.
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));

  // Run WITH the chart-only prior store.
  const std::string with_prior = root + "/with_prior";
  {
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(with_prior, "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
  }

  // Baseline: no prior at all.
  const std::string no_prior = root + "/no_prior";
  runImport(batches, no_prior, "", /*budget=*/0);

  const auto with = loadBathyCells(with_prior);
  const auto without = loadBathyCells(no_prior);

  ASSERT_FALSE(without.empty())
    << "without a prior the deep sounding must settle a cell";
  bool baseline_is_deep = false;
  for (const auto & [cell, du] : without) {
    if (du.first < -100.0) {baseline_is_deep = true;}
  }
  EXPECT_TRUE(baseline_is_deep)
    << "the baseline deep sounding should settle a deep (< -100 m) cell";

  // With the chart-layer prior, the Chart rung seeds the shallow predicted
  // surface and the blunder gate rejects the deep sounding -- NO cell settles.
  EXPECT_TRUE(with.empty())
    << "the chart-layer prior must gate the deep blunder (#119)";

  std::filesystem::remove_all(root);
}

// Evict/revisit keeps the prior gate (#118): a tile whose predicted surface came
// only from the reference prime used to return from eviction with the gate OFF —
// reloadEvictedTile restored the Processed layer only, so a cell never surveyed
// (prior-gated only) accepted a false-deep sounding on revisit. The fix re-primes
// the prior FIRST on reload (then the processed restore overwrites where measured
// data exists). Here: survey one cell of a reference-gated tile, force its
// eviction with spread batches, revisit with a deep blunder at a DIFFERENT cell
// of the same tile — the blunder must still be rejected. (Revert the reload
// re-prime and the deep cell settles -> this fails.)
TEST(ImportEviction, ReferenceOnlyTileEvictRevisitKeepsGate)
{
  const std::string root = makeTempDir("revisit_gate");
  const std::string ref_dir = root + "/reference_store";
  const std::string store_dir = root + "/store";

  const gggs::GridIndex survey_grid =
    gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;

  // Dense SHALLOW (-20 m) reference tile at the survey level covering the tile.
  {
    marine_bathymetry_store::BathymetryTile rtile(survey_grid);
    for (gggs::CellAreaIterator cit(survey_grid); cit.valid(); cit.next()) {
      rtile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(survey_grid, std::move(rtile));
    marine_bathymetry_store::BathymetryStore ref_store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/true);
    ref_store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(ref_store, ref_dir);
  }

  // Visit 1: a gate-passing shallow sounding on cell A. Then spread batches over
  // 6 other tiles (budget 3) to force the tile's eviction. Revisit: a deep
  // (~ -150 m) blunder at cell B, ~3 m away — a different, never-surveyed cell
  // of the same tile, whose only predicted surface was the reference prime.
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(survey_lat, survey_lon, 20.0f, 30.0f));
  for (int i = 0; i < 6; ++i) {
    batches.push_back(surveyCell(43.0 + 0.02 * i, -71.0, 15.0f + i, 25.0f + i));
  }
  batches.push_back(surveyCell(survey_lat + 3e-5, survey_lon, 150.0f, 40.0f));

  {
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(store_dir, "", /*budget=*/3);
    cfg.reference_store_dir = ref_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
  }

  const auto cells = loadBathyCells(store_dir);
  // The visit-1 shallow survey survived the evict/reload round-trip...
  bool shallow_found = false;
  bool deep_found = false;
  for (const auto & [cell, du] : cells) {
    if (du.first < -100.0) {deep_found = true;}
    if (du.first > -30.0 && du.first < -10.0) {shallow_found = true;}
  }
  EXPECT_TRUE(shallow_found)
    << "the shallow visit-1 survey should survive eviction and reload";
  // ...and the revisit blunder was rejected by the re-primed gate.
  EXPECT_FALSE(deep_found)
    << "the revisit deep blunder must be rejected -- the prior gate must be "
    "re-primed on reload (#118)";

  std::filesystem::remove_all(root);
}

// Prior READ failure on revisit keeps the tile EVICTED for retry (#118). The
// offline analog of the live-node must-fix (CubeBathymetryNode::reloadEvictedTile,
// which is not unit-test-exposed -- it lives in cube_bathymetry_node.cpp behind
// main(), no header/library target -- so this exercises the SHARED reload logic
// instead): when the reload's prior re-prime READ throws (corrupt/transient prior
// store) but the survey restore succeeds, reloadEvictedTile must NOT erase the
// evicted_ marker. Before the fix it returned "did I prime" (false, swallowed) and
// fell through to `return true`, so the caller erased the marker with no retry and
// the tile's prior-only cells ran ungated for the rest of the run -- a false-deep
// blunder settled. primePriorLayersForTile now reports read-success separately, so
// reloadEvictedTile returns false on a prior read error: the caller drops the tile
// (protecting the intact on-disk survey surface) and keeps it evicted to retry.
// Here: survey one cell of a reference-gated tile, evict it, CORRUPT the reference
// tile so the revisit re-prime read throws, then revisit with a deep blunder at a
// DIFFERENT never-surveyed cell -- the blunder must NOT settle (its sounding is
// dropped with the tile). (Restore `return true` unconditionally and the deep cell
// settles -> this fails.)
TEST(ImportEviction, PriorReadFailureOnRevisitKeepsTileEvicted)
{
  const std::string root = makeTempDir("prior_readfail");
  const std::string ref_dir = root + "/reference_store";
  const std::string store_dir = root + "/store";

  const gggs::GridIndex survey_grid =
    gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;

  // Dense SHALLOW (-20 m) reference tile at the survey level covering the tile.
  {
    marine_bathymetry_store::BathymetryTile rtile(survey_grid);
    for (gggs::CellAreaIterator cit(survey_grid); cit.valid(); cit.next()) {
      rtile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(survey_grid, std::move(rtile));
    marine_bathymetry_store::BathymetryStore ref_store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/true);
    ref_store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(ref_store, ref_dir);
  }

  // Visit 1: a gate-passing shallow sounding on cell A. Then spread over 6 other
  // tiles (budget 3) to force the reference-gated tile's eviction.
  std::vector<std::vector<GeoSounding>> pre_batches;
  pre_batches.push_back(surveyCell(survey_lat, survey_lon, 20.0f, 30.0f));
  for (int i = 0; i < 6; ++i) {
    pre_batches.push_back(surveyCell(43.0 + 0.02 * i, -71.0, 15.0f + i, 25.0f + i));
  }
  // Revisit: a deep (~ -150 m) blunder at cell B, ~3 m from cell A -- a different,
  // never-surveyed cell whose only predicted surface was the (now unreadable) prior.
  const std::vector<GeoSounding> revisit_batch =
    surveyCell(survey_lat + 3e-5, survey_lon, 150.0f, 40.0f);

  {
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(store_dir, "", /*budget=*/3);
    cfg.reference_store_dir = ref_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : pre_batches) {
      acc.addBatch(b);
    }
    // Corrupt the reference tile so the revisit's prior re-prime READ throws (GDAL
    // cannot open a non-GeoTIFF file -> loadTile throws -> loadWindow throws). The
    // survey restore (from store_dir) still succeeds, so ONLY the prior read fails
    // -- exactly the failed-prior + successful-survey case the fix guards.
    const std::string ref_tile = ref_dir + "/" +
      marine_bathymetry_store::layerDirName(
        marine_bathymetry_store::SourceLayer::Reference) + "/" +
      marine_bathymetry_store::tileFilename(survey_grid);
    ASSERT_TRUE(std::filesystem::is_regular_file(ref_tile))
      << "reference tile should exist at " << ref_tile;
    {
      std::ofstream corrupt(ref_tile, std::ios::binary | std::ios::trunc);
      corrupt << "not a geotiff";
    }
    acc.addBatch(revisit_batch);
    acc.finalize();
  }

  const auto cells = loadBathyCells(store_dir);
  bool shallow_found = false;
  bool deep_found = false;
  for (const auto & [cell, du] : cells) {
    if (du.first < -100.0) {deep_found = true;}
    if (du.first > -30.0 && du.first < -10.0) {shallow_found = true;}
  }
  // Visit-1 shallow survived eviction (persisted to disk before the drop).
  EXPECT_TRUE(shallow_found)
    << "the shallow visit-1 survey should survive eviction";
  // The revisit deep blunder must NOT settle: with the prior read failed, the tile
  // is kept evicted and its (ungated) revisit soundings are dropped rather than
  // erasing the marker and accepting the blunder (#118).
  EXPECT_FALSE(deep_found)
    << "a failed prior re-prime read must keep the tile evicted (dropping the "
    "revisit blunder), not erase the marker and accept the deep sounding (#118)";

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

// Boundary-flush cross-level gate (#115 round 2): the level-walk fallback must pick
// the coarse tile that CONTAINS the survey tile, not merely the finest coarse tile
// the load window returned. `loadWindow`'s overlap test is inclusive (tile_io.cpp
// tileOverlapsBox), so a survey tile flush against a coarse-tile boundary also pulls
// in the edge-adjacent coarse NEIGHBOR (same level, shares only the boundary line).
// A level-only selection can pick that neighbor; primeFromTileResample's
// grid-mismatch guard then skips every fine cell -> the blunder gate silently OFF
// again for boundary tiles. Here the survey L10 tile sits flush against the WEST
// edge of its containing L7 tile, and BOTH the container and its west neighbor L7
// tiles are on disk. The containment check must reject the neighbor and gate via the
// container. (Revert the containment check and the map-ordered neighbor -- lower
// column, iterated first -- wins the level-only tie, nothing gates, and the deep
// sounding settles -> this fails.)
TEST(ImportEviction, BoundaryFlushCrossLevelReferenceRejectsDeepBlunder)
{
  constexpr float kCoarseCellSize = 8.0f;  // L7, coarser than the 1 m survey (L10)
  const gggs::Level survey_level = gggs::Level::fromCellSize(kCellSize);
  const gggs::Level coarse_level = gggs::Level::fromCellSize(kCoarseCellSize);
  ASSERT_LT(coarse_level.level(), survey_level.level())
    << "the reference must be at a coarser level to exercise the level-walk";

  const std::string root = makeTempDir("flush_xlevel_refseed");
  const std::string ref_dir = root + "/reference_store";

  // The containing L7 tile, and a survey position inside its WESTMOST L10 sub-tile so
  // the survey tile's west edge is flush with the L7 west boundary. Latitude sits at
  // the L7 tile's mid-height so only the WEST neighbor (not a corner) is adjacent.
  const gggs::GridIndex container = coarse_level.gridIndex(43.0, -70.0);
  const double lat = container.southLatitude() + container.latitudinalSpan() * 0.5;
  // container.longitudinalSpan()/8 == one L10 tile span; /16 lands mid-width of the
  // westmost L10 sub-tile -> its west edge coincides with the L7 west boundary.
  const double lon = container.westLongitude() + container.longitudinalSpan() / 16.0;
  const gggs::GridIndex survey_grid = survey_level.gridIndex(lat, lon);
  ASSERT_EQ(survey_grid.level(), survey_level.level());
  ASSERT_NEAR(survey_grid.westLongitude(), container.westLongitude(), 1e-9)
    << "the survey tile must be flush against the coarse west boundary";

  // The L7 tile immediately WEST of the container (its east edge == the shared
  // boundary). Inclusive overlap makes loadWindow return it for the flush survey tile.
  const gggs::GridIndex west_neighbor = coarse_level.gridIndex(
    lat, container.westLongitude() - container.longitudinalSpan() * 0.5);
  ASSERT_EQ(west_neighbor.level(), container.level());
  ASSERT_NE(west_neighbor, container)
    << "the west neighbor must be a distinct coarse tile";

  // Fill BOTH coarse tiles shallow (-20 m) and write them to the reference layer.
  {
    marine_bathymetry_store::BathymetryStore ref_store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCoarseCellSize, /*reference_writable=*/true);
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    for (const gggs::GridIndex & cg : {container, west_neighbor}) {
      marine_bathymetry_store::BathymetryTile ctile(cg);
      for (gggs::CellAreaIterator cit(cg); cit.valid(); cit.next()) {
        ctile.set(
          (*cit).row(), (*cit).column(),
          marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
      }
      tiles.emplace(cg, std::move(ctile));
    }
    ref_store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(ref_store, ref_dir);
  }

  // A clearly-too-deep blunder (~ -150 m) where the coarse prior says ~ -20 m.
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(lat, lon, 150.0f, 40.0f));

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

  const std::string no_ref = root + "/no_ref";
  runImport(batches, no_ref, "", /*budget=*/0);

  const auto with = loadBathyCells(with_ref);
  const auto without = loadBathyCells(no_ref);

  // Baseline (no prior): the deep sounding is accepted and settles a deep cell.
  ASSERT_FALSE(without.empty())
    << "without a prior the deep sounding must settle a cell";
  bool baseline_is_deep = false;
  for (const auto & [cell, du] : without) {
    if (du.first < -100.0) {baseline_is_deep = true;}
  }
  EXPECT_TRUE(baseline_is_deep)
    << "the baseline deep sounding should settle a deep (< -100 m) cell";

  // With the container coarse tile selected (neighbor rejected), the gate fires and
  // no cell settles.
  EXPECT_TRUE(with.empty())
    << "the containing coarse tile must gate the deep blunder even when a flush "
       "survey tile also loads the edge-adjacent coarse neighbor";

  std::filesystem::remove_all(root);
}

// Cross-layer anti-clobber on the direct-write import path (ADR-0010 D8): a
// processed write must clear the overlapped draft cells via the store's public
// clearOverlappedDraft (parity with the GeoTIFF importer), so stale live-CUBE
// draft never shadows the authoritative re-run. Seed a draft cell that the import
// WILL overlap and another draft cell in a different, un-surveyed tile that it will
// NOT. After the import: processed holds the surveyed cell, the overlapped draft
// cell is cleared to no-data, and the unrelated draft cell survives (clearing is
// scoped to the processed tile, not a global wipe).
TEST(ImportEviction, ProcessedImportClearsOverlappedDraft)
{
  constexpr double kLat = 43.0;
  constexpr double kLon = -70.0;

  const std::string root = makeTempDir("draftclear");
  const std::string store_dir = root + "/store";
  std::filesystem::create_directories(store_dir);

  marine_bathymetry_store::BathymetryStore probe =
    marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
  const gggs::CellIndex overlapped = probe.cellIndex(kLat, kLon);
  // A cell in a clearly different tile the survey never visits.
  const gggs::CellIndex untouched = probe.cellIndex(kLat + 1.0, kLon - 1.0);
  ASSERT_NE(overlapped.grid(), untouched.grid())
    << "the untouched seed cell must live in a different tile";

  // Seed a DRAFT surface on disk with both cells.
  {
    marine_bathymetry_store::BathymetryStore seed =
      marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
    seed.set(
      marine_bathymetry_store::SourceLayer::Draft, overlapped, {-11.0, 0.5});
    seed.set(
      marine_bathymetry_store::SourceLayer::Draft, untouched, {-22.0, 0.5});
    ASSERT_GT(marine_bathymetry_store::save(seed, store_dir), 0u);
  }

  // Run a processed import that surveys ONLY the overlapped cell's position.
  runImport(
    {surveyCell(kLat, kLon, 12.0f, 30.0f)}, store_dir, /*bs_dir=*/"", /*budget=*/0);

  marine_bathymetry_store::BathymetryStore out =
    marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
  marine_bathymetry_store::load(out, store_dir);

  // Processed holds the authoritative surveyed cell.
  const auto processed_cell =
    out.get(marine_bathymetry_store::SourceLayer::Processed, overlapped);
  ASSERT_TRUE(processed_cell.has_value() && processed_cell->hasData())
    << "the processed re-run must persist the surveyed cell";

  // The overlapped draft cell was cleared to no-data by the processed write.
  const auto draft_overlapped =
    out.get(marine_bathymetry_store::SourceLayer::Draft, overlapped);
  EXPECT_TRUE(!draft_overlapped.has_value() || !draft_overlapped->hasData())
    << "the overlapped draft cell must be cleared by the processed write";

  // The unrelated draft cell (different tile) survives -- clearing is scoped, not
  // a global wipe.
  const auto draft_untouched =
    out.get(marine_bathymetry_store::SourceLayer::Draft, untouched);
  ASSERT_TRUE(draft_untouched.has_value() && draft_untouched->hasData())
    << "a draft cell the processed data does not cover must survive";
  EXPECT_NEAR(draft_untouched->depth, -22.0, 1e-6);

  std::filesystem::remove_all(root);
}

// Fused Processed-over-Draft prime priority (ADR-0010 D8, cube#133 review r1):
// where Processed and Draft carry CONFLICTING depths on the same cell, the primed
// sheet must report the PROCESSED depth -- the store's query-side `Processed > Draft`
// walk -- not the Draft depth. The original prime seeded Draft-then-Processed and
// leaned on seeding order; but two settled 1-sample hypotheses tie-break in CUBE's
// chooseHypothesis to the FIRST-seeded, so that reported Draft on overlap (the
// must-fix). The fix seeds Processed fully, then layers Draft with a per-cell filter
// (loadDraftSkippingProcessed) that skips any cell Processed already covers, so the
// overlapped cell carries only the Processed hypothesis -- deterministic regardless
// of the tie-break, and it also keeps the predicted-surface prior Processed's (no
// Draft write lands on the cell at all). This test pins the observable contract:
// the overlapped cell reports the Processed depth, while a Draft-only cell in a tile
// Processed does not cover is still primed from Draft (the skip is scoped, not a
// blanket Draft drop).
TEST(ImportEviction, FusedPrimeProcessedWinsOverConflictingDraft)
{
  constexpr double kLat = 43.0;
  constexpr double kLon = -70.0;
  constexpr double kProcessedDepth = -50.0;   // authoritative surface
  constexpr double kDraftOverlapDepth = -11.0;  // stale live-CUBE surface (must lose)
  constexpr double kDraftOnlyDepth = -22.0;    // Draft with no Processed cover

  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
  const gggs::CellIndex overlapped = store.cellIndex(kLat, kLon);
  const gggs::CellIndex draft_only = store.cellIndex(kLat + 1.0, kLon - 1.0);
  ASSERT_NE(overlapped.grid(), draft_only.grid())
    << "the Draft-only cell must live in a tile with no Processed coverage";

  // Same cell, conflicting depths in the two layers -> the overlap under test.
  store.set(
    marine_bathymetry_store::SourceLayer::Processed, overlapped, {kProcessedDepth, 0.5});
  store.set(
    marine_bathymetry_store::SourceLayer::Draft, overlapped, {kDraftOverlapDepth, 0.5});
  // A Draft cell Processed does not cover -> must still be primed from Draft.
  store.set(
    marine_bathymetry_store::SourceLayer::Draft, draft_only, {kDraftOnlyDepth, 0.5});

  // The node's fused startup prime: Processed FULLY first, then Draft-skip-Processed.
  GeoMapSheet sheet(kCellSize);
  loadIntoSheet(store, marine_bathymetry_store::SourceLayer::Processed, sheet);
  loadDraftSkippingProcessed(store, sheet);

  // Read the primed settled depths back out of the sheet.
  const auto tiles = mapSheetToTiles(sheet);
  const auto depthAt = [&](const gggs::CellIndex & cell) -> double {
      const auto t_it = tiles.find(cell.grid());
      if (t_it == tiles.end()) {return std::nan("");}
      const auto & depth = t_it->second.depthBand();
      gggs::CellAreaIterator it(t_it->second.index());
      std::size_t k = 0;
      for (; it.valid() && k < depth.size(); it.next(), ++k) {
        if (*it == cell) {return depth[k];}
      }
      return std::nan("");
    };

  const double primed_overlap = depthAt(overlapped);
  ASSERT_FALSE(std::isnan(primed_overlap))
    << "the overlapped cell must be primed from one of the two layers";
  EXPECT_NEAR(primed_overlap, kProcessedDepth, 1e-2)
    << "Processed must win the overlapped cell (got " << primed_overlap
    << "; Draft would be " << kDraftOverlapDepth << ")";
  EXPECT_GT(std::abs(primed_overlap - kDraftOverlapDepth), 1.0)
    << "the overlapped cell must NOT report the stale Draft depth";

  const double primed_draft_only = depthAt(draft_only);
  ASSERT_FALSE(std::isnan(primed_draft_only))
    << "a Draft cell with no Processed cover must still be primed from Draft";
  EXPECT_NEAR(primed_draft_only, kDraftOnlyDepth, 1e-2)
    << "the Draft-only cell must survive (the skip is scoped to Processed cells)";
}

// Ambiguous-store abort (ADR-0010 D8, cube#133 review r2): a store that holds BOTH a
// legacy `survey/` and a `processed/` layer dir is a permanent, whole-store migration
// REFUSAL -- loadWindow throws identically for every tile. seedNewTile must tell that
// apart from a transient per-tile read error (which drops one tile and retries) and
// abort the WHOLE import loudly, so import_bag terminates non-zero rather than
// silently emitting a near-empty store. This pins the abort/rethrow path.
TEST(ImportEviction, AmbiguousSurveyStoreAbortsImport)
{
  const std::string root = makeTempDir("ambiguous");
  const std::string store_dir = root + "/store";
  std::filesystem::create_directories(store_dir);

  // Write a real `processed/` layer with one tile, then COPY it to `survey/` so the
  // store holds both dirs -- exactly the half-migrated state the migration refuses.
  {
    marine_bathymetry_store::BathymetryStore seed =
      marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
    const gggs::CellIndex cell = seed.cellIndex(43.0, -70.0);
    seed.set(marine_bathymetry_store::SourceLayer::Processed, cell, {-12.0, 0.5});
    ASSERT_GT(marine_bathymetry_store::save(seed, store_dir), 0u);
  }
  std::filesystem::copy(
    std::filesystem::path(store_dir) / "processed",
    std::filesystem::path(store_dir) / "survey",
    std::filesystem::copy_options::recursive);
  ASSERT_TRUE(cube::legacySurveyDirPersists(store_dir))
    << "the both-dirs store must be detected as a persisting legacy survey/ layer";

  // First touch of the tile -> seedNewTile -> loadWindow -> migration refusal -> the
  // guard must RETHROW (abort), not swallow-and-degrade.
  GeoMapSheet sheet(kCellSize);
  ImportAccumulator accumulator(sheet, makeConfig(store_dir, /*bs_dir=*/"", /*budget=*/0));
  bool threw = false;
  try {
    accumulator.addBatch(surveyCell(43.0, -70.0, 12.0f, 30.0f));
  } catch (const std::exception & e) {
    threw = true;
    EXPECT_NE(std::string(e.what()).find("ABORTING"), std::string::npos)
      << "the abort must be the loud ambiguous-store message, got: " << e.what();
  }
  EXPECT_TRUE(threw) << "an ambiguous survey+processed store must abort the import";

  std::filesystem::remove_all(root);
}

// Generalized guard (cube#133 review r2): a SYMLINKED `survey/` is also a permanent
// migration refusal (renaming it would point `processed/` out of the store), but it
// need not be accompanied by a `processed/` dir -- so a both-dirs-only check would
// miss it and silently degrade tile-by-tile. legacySurveyDirPersists keys on the
// survey/ path (is_symlink included), so this variant aborts too.
TEST(ImportEviction, SymlinkedSurveyStoreAbortsImport)
{
  const std::string root = makeTempDir("symlinksurvey");
  const std::string store_dir = root + "/store";
  std::filesystem::create_directories(store_dir);

  // A real directory holding one legacy tile, OUTSIDE the store, then a `survey/`
  // symlink pointing at it -- no `processed/` dir present.
  const std::string target = root + "/legacy_survey_target";
  {
    marine_bathymetry_store::BathymetryStore seed =
      marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
    const gggs::CellIndex cell = seed.cellIndex(43.0, -70.0);
    seed.set(marine_bathymetry_store::SourceLayer::Processed, cell, {-12.0, 0.5});
    ASSERT_GT(marine_bathymetry_store::save(seed, store_dir), 0u);
  }
  // Move the just-written processed/ out to the external target and link survey/ to it.
  std::filesystem::rename(
    std::filesystem::path(store_dir) / "processed", target);
  std::filesystem::create_directory_symlink(
    target, std::filesystem::path(store_dir) / "survey");
  ASSERT_TRUE(cube::legacySurveyDirPersists(store_dir))
    << "a symlinked survey/ must be detected as a persisting legacy layer";

  GeoMapSheet sheet(kCellSize);
  ImportAccumulator accumulator(sheet, makeConfig(store_dir, /*bs_dir=*/"", /*budget=*/0));
  bool threw = false;
  try {
    accumulator.addBatch(surveyCell(43.0, -70.0, 12.0f, 30.0f));
  } catch (const std::exception & e) {
    threw = true;
    EXPECT_NE(std::string(e.what()).find("ABORTING"), std::string::npos)
      << "the abort must be the loud message, got: " << e.what();
  }
  EXPECT_TRUE(threw) << "a symlinked survey/ store must abort the import";

  std::filesystem::remove_all(root);
}

// Cross-level CHART blunder gate (#137). The sibling of
// CoarseLevelReferenceSeedRejectsDeepBlunder above, for the Chart layer -- and the
// case that matters most in the field, because an ENC chart product is built on the
// chart scale ladder (usage bands) and so essentially NEVER has a tile at the survey
// GGGS level. Before #137, Chart got only the exact-level `find`, the level-walk
// fallback was Reference-only, and a real ENC prior therefore gated NOTHING: present
// on disk, announced at startup, never consulted. Observed on the 2026-08-25 Isles of
// Shoals import, where an L2/L3/L6/L7/L8 chart layer under an L10 survey produced
// byte-identical output with and without --reference-store.
TEST(ImportEviction, CoarseLevelChartSeedRejectsDeepBlunder)
{
  // A COARSER cell size -> a coarser GGGS level than the 1 m survey (L10). 8 m -> L7.
  constexpr float kCoarseCellSize = 8.0f;
  ASSERT_LT(gggs::Level::fromCellSize(kCoarseCellSize).level(),
    gggs::Level::fromCellSize(kCellSize).level())
    << "the chart prior must be at a coarser level to exercise the level-walk";

  const std::string root = makeTempDir("xlevel_chartseed");
  const std::string prior_dir = root + "/chart_store";

  // Survey the INTERIOR of a single L10 tile (its center), not a shared tile corner:
  // a corner sounding routes ambiguously to one of four tiles. GGGS nesting puts this
  // L10 tile wholly inside one L7 tile.
  const gggs::GridIndex survey_grid =
    gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;

  // Fill the ENTIRE containing L7 tile with a SHALLOW (-20 m) chart prior, written to
  // the CHART layer (a staging-writable store, as import_geotiff's chart path uses).
  const gggs::Level coarse_level = gggs::Level::fromCellSize(kCoarseCellSize);
  const gggs::GridIndex coarse_grid = coarse_level.gridIndex(survey_lat, survey_lon);
  ASSERT_LT(coarse_grid.level(), survey_grid.level())
    << "the chart tile must be at a coarser level than the survey tile";
  {
    marine_bathymetry_store::BathymetryTile ctile(coarse_grid);
    for (gggs::CellAreaIterator cit(coarse_grid); cit.valid(); cit.next()) {
      ctile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(coarse_grid, std::move(ctile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCoarseCellSize, /*reference_writable=*/false, /*chart_staging_writable=*/true);
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  // A clearly-too-deep blunder (~ -150 m) where the coarse chart prior says ~ -20 m.
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));

  const std::string with_prior = root + "/with_prior";
  std::string audit;
  {
    // Capture the import log so the test pins the MECHANISM (the cross-level
    // fallback fired) and not merely the absence of a settled cell -- which an
    // unrelated gate could also produce.
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(with_prior, "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
    audit = capture.str();
  }
  EXPECT_NE(audit.find("chart blunder gate"), std::string::npos)
    << "the chart cross-level fallback must announce itself; stderr was:\n" << audit;
  EXPECT_NE(audit.find("seeded by cross-level resample"), std::string::npos)
    << "the gate must come from the LEVEL-WALK, not an exact-level match; "
    "stderr was:\n" << audit;
  EXPECT_EQ(audit.find("primed NOTHING"), std::string::npos)
    << "a chart prior that gates must not trip the silent-no-op warning; "
    "stderr was:\n" << audit;

  // Baseline: no prior at all.
  const std::string no_prior = root + "/no_prior";
  runImport(batches, no_prior, "", /*budget=*/0);

  const auto with = loadBathyCells(with_prior);
  const auto without = loadBathyCells(no_prior);

  ASSERT_FALSE(without.empty())
    << "without a prior the deep sounding must settle a cell";
  bool baseline_is_deep = false;
  for (const auto & [cell, du] : without) {
    if (du.first < -100.0) {baseline_is_deep = true;}
  }
  EXPECT_TRUE(baseline_is_deep)
    << "the baseline deep sounding should settle a deep (< -100 m) cell";

  // With the COARSER chart prior, Chart's Phase B seeds the shallow predicted surface
  // and the gate rejects the deep sounding -- so NO cell settles. Revert #137 (make
  // Chart exact-level-only again) and `with` gains the deep cell -> this fails.
  EXPECT_TRUE(with.empty())
    << "the cross-level CHART prior must gate the deep blunder (#137 level-walk)";

  std::filesystem::remove_all(root);
}

// Boundary-flush cross-level CHART gate (#137), mirroring
// BoundaryFlushCrossLevelReferenceRejectsDeepBlunder for the Chart layer. Chart now
// shares the SAME containment-checked walk, so it inherits the same hazard: a survey
// tile flush against a coarse-tile boundary also pulls in the edge-adjacent coarse
// NEIGHBOR through loadWindow's inclusive overlap test. Selecting on level alone
// would pick the neighbor, primeFromTileResample's grid-mismatch guard would skip
// every fine cell, and the gate would be silently OFF for boundary tiles again.
TEST(ImportEviction, BoundaryFlushCrossLevelChartRejectsDeepBlunder)
{
  constexpr float kCoarseCellSize = 8.0f;
  const gggs::Level survey_level = gggs::Level::fromCellSize(kCellSize);
  const gggs::Level coarse_level = gggs::Level::fromCellSize(kCoarseCellSize);
  ASSERT_LT(coarse_level.level(), survey_level.level())
    << "the chart prior must be at a coarser level to exercise the level-walk";

  const std::string root = makeTempDir("flush_xlevel_chartseed");
  const std::string prior_dir = root + "/chart_store";

  // A survey position inside the WESTMOST L10 sub-tile of its containing L7 tile, so
  // the survey tile's west edge is flush with the L7 west boundary. Latitude at the
  // L7 tile's mid-height so only the WEST neighbor (not a corner) is adjacent.
  const gggs::GridIndex container = coarse_level.gridIndex(43.0, -70.0);
  const double lat = container.southLatitude() + container.latitudinalSpan() * 0.5;
  const double lon = container.westLongitude() + container.longitudinalSpan() / 16.0;
  const gggs::GridIndex survey_grid = survey_level.gridIndex(lat, lon);
  ASSERT_EQ(survey_grid.level(), survey_level.level());
  ASSERT_NEAR(survey_grid.westLongitude(), container.westLongitude(), 1e-9)
    << "the survey tile must be flush against the coarse west boundary";

  const gggs::GridIndex west_neighbor = coarse_level.gridIndex(
    lat, container.westLongitude() - container.longitudinalSpan() * 0.5);
  ASSERT_EQ(west_neighbor.level(), container.level());
  ASSERT_NE(west_neighbor, container)
    << "the west neighbor must be a distinct coarse tile";

  // Fill BOTH coarse tiles shallow (-20 m) into the CHART layer.
  {
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCoarseCellSize, /*reference_writable=*/false, /*chart_staging_writable=*/true);
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    for (const gggs::GridIndex & cg : {container, west_neighbor}) {
      marine_bathymetry_store::BathymetryTile ctile(cg);
      for (gggs::CellAreaIterator cit(cg); cit.valid(); cit.next()) {
        ctile.set(
          (*cit).row(), (*cit).column(),
          marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
      }
      tiles.emplace(cg, std::move(ctile));
    }
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));

  const std::string with_prior = root + "/with_prior";
  {
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(with_prior, "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
  }

  // Baseline: the SAME soundings with no prior at all must settle a deep cell --
  // otherwise `with.empty()` below would pass for a reason unrelated to the gate
  // (the Reference sibling test runs this same control).
  const std::string no_prior = root + "/no_prior";
  runImport(batches, no_prior, "", /*budget=*/0);
  const auto without = loadBathyCells(no_prior);
  ASSERT_FALSE(without.empty())
    << "without a prior the deep sounding must settle a cell";
  bool baseline_is_deep = false;
  for (const auto & [cell, du] : without) {
    if (du.first < -100.0) {baseline_is_deep = true;}
  }
  EXPECT_TRUE(baseline_is_deep)
    << "the baseline deep sounding should settle a deep (< -100 m) cell";

  const auto with = loadBathyCells(with_prior);
  EXPECT_TRUE(with.empty())
    << "the containment check must pick the CONTAINING chart tile, not the "
       "edge-adjacent west neighbor, and gate the deep blunder";

  std::filesystem::remove_all(root);
}

// Silent-no-op diagnostic (#137, second defect). A run given --reference-store that
// primes NOT ONE tile must say so at finalize(). Before this, the operator saw the
// "Reference-prior seeding from ..." startup banner, the gate was off for the entire
// import, and nothing in the output distinguished that from a working gate -- which
// is precisely how the ENC-chart miss above went unnoticed.
TEST(ImportEviction, NoUsablePriorEmitsWarning)
{
  const std::string root = makeTempDir("no_usable_prior");
  const std::string prior_dir = root + "/prior_store";

  // A prior store whose only tile is FAR from the survey, so the windowed load
  // returns nothing usable and no rung ever primes.
  {
    const gggs::GridIndex far_grid =
      gggs::Level::fromCellSize(kCellSize).gridIndex(10.0, 10.0);
    marine_bathymetry_store::BathymetryTile tile(far_grid);
    for (gggs::CellAreaIterator cit(far_grid); cit.valid(); cit.next()) {
      tile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(far_grid, std::move(tile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/true);
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(43.0, -70.0, 20.0f, 40.0f));

  // NOTE: stderr capture is a standard gtest facility, but this is its first use in
  // this file -- the surrounding tests all assert on store contents rather than on
  // operator-facing output. The warning IS the behaviour under test here, so there is
  // nothing in the store to assert against instead. StderrCapture wraps it so a throw
  // in the code under test cannot leave fd 2 redirected and abort the binary.
  std::string warned;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(root + "/out", "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
    // A SECOND finalize() must not repeat the warning (the tally is never cleared,
    // so only the once-guard stops a duplicate).
    acc.finalize();
    warned = capture.str();
  }
  EXPECT_EQ(countOccurrences(warned, "primed NOTHING"), 1u)
    << "a prior store that never primes must warn EXACTLY once at finalize (a "
    "duplicated warning would pass a substring check); stderr was:\n" << warned;
  EXPECT_NE(warned.find("blunder gate was INACTIVE"), std::string::npos)
    << "the warning must say the gate was inactive; stderr was:\n" << warned;
  EXPECT_NE(warned.find("prior-seed attempt(s)"), std::string::npos)
    << "the warning must count ATTEMPTS, not tiles (a revisited tile re-primes and "
    "counts again); stderr was:\n" << warned;
  EXPECT_NE(warned.find("No chart or reference tiles overlap"), std::string::npos)
    << "an out-of-area prior with no read failure is a COVERAGE gap and must be "
    "reported as one; stderr was:\n" << warned;
  EXPECT_EQ(warned.find("FAILED to read"), std::string::npos)
    << "nothing failed to read here -- a coverage gap must not be reported as an "
    "unreadable store; stderr was:\n" << warned;

  // Companion no-false-positive check: a prior that DOES prime must NOT warn.
  const std::string good_prior = root + "/good_prior";
  {
    const gggs::GridIndex survey_grid =
      gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
    marine_bathymetry_store::BathymetryTile tile(survey_grid);
    for (gggs::CellAreaIterator cit(survey_grid); cit.valid(); cit.next()) {
      tile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(survey_grid, std::move(tile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/true);
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(store, good_prior);
  }
  std::string quiet;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(root + "/out2", "", /*budget=*/0);
    cfg.reference_store_dir = good_prior;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
    quiet = capture.str();
  }
  EXPECT_EQ(quiet.find("primed NOTHING"), std::string::npos)
    << "an ordinary import with a working prior must NOT warn; stderr was:\n"
    << quiet;

  std::filesystem::remove_all(root);
}

// A MATCHED prior tile is not a PRIMED one (#137 review). The containment walk can
// select a coarse chart tile that genuinely contains this survey tile and still prime
// NOT ONE cell, because the coarse cells overlying the survey area are no-data -- an
// ENC product with a hole exactly where the survey ran. Counting that match as a
// "hit" suppressed the silent-no-op warning permanently: the operator saw the startup
// banner, no warning, and an ungated import.
TEST(ImportEviction, MatchedButEmptyChartPriorStillWarns)
{
  constexpr float kCoarseCellSize = 8.0f;
  const std::string root = makeTempDir("empty_chart_prior");
  const std::string prior_dir = root + "/chart_store";

  const gggs::GridIndex survey_grid =
    gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;
  const gggs::GridIndex coarse_grid =
    gggs::Level::fromCellSize(kCoarseCellSize).gridIndex(survey_lat, survey_lon);
  ASSERT_LT(coarse_grid.level(), survey_grid.level());

  // Shallow chart data everywhere in the coarse tile EXCEPT over the survey tile.
  writeChartPriorWithHole(prior_dir, kCoarseCellSize, coarse_grid, survey_grid);

  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));

  const std::string out = root + "/out";
  std::string warned;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(out, "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
    warned = capture.str();
  }

  // The gate really was off -- the deep sounding settled.
  const auto cells = loadBathyCells(out);
  bool settled_deep = false;
  for (const auto & [cell, du] : cells) {
    if (du.first < -100.0) {settled_deep = true;}
  }
  ASSERT_TRUE(settled_deep)
    << "the empty prior gates nothing, so the deep sounding must settle -- "
    "otherwise this test is not exercising the ungated case";

  EXPECT_EQ(countOccurrences(warned, "primed NOTHING"), 1u)
    << "a prior tile that MATCHED but primed no cell must still trip the "
    "silent-no-op warning exactly once; stderr was:\n" << warned;
  EXPECT_EQ(warned.find("seeded by cross-level resample"), std::string::npos)
    << "nothing was primed, so no cross-level fallback line may claim otherwise; "
    "stderr was:\n" << warned;
  // The layers/levels ARE reported, so the operator can see it is a data hole in a
  // present chart layer, not a missing prior store.
  EXPECT_NE(warned.find("chart@L"), std::string::npos)
    << "the warning must name the layer@level actually found; stderr was:\n"
    << warned;

  std::filesystem::remove_all(root);
}

// Phase A must not short-circuit Phase B (#137 review). An exact-survey-level chart
// tile that is all no-data over this survey tile used to return "primed" and
// suppress the cross-level fallback -- so a multi-level ENC prior whose COARSER band
// has real data gated nothing, purely because a hollow same-level tile existed.
TEST(ImportEviction, EmptyExactLevelChartPriorFallsThroughToCoarsePrior)
{
  constexpr float kCoarseCellSize = 8.0f;
  const std::string root = makeTempDir("exact_empty_falls_through");
  const std::string prior_dir = root + "/chart_store";

  const gggs::GridIndex survey_grid =
    gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;
  const gggs::GridIndex coarse_grid =
    gggs::Level::fromCellSize(kCoarseCellSize).gridIndex(survey_lat, survey_lon);
  ASSERT_LT(coarse_grid.level(), survey_grid.level());

  // (a) an exact-survey-level chart tile written entirely as no-data.
  {
    marine_bathymetry_store::BathymetryTile hollow(survey_grid);
    const double kNoData = std::numeric_limits<double>::quiet_NaN();
    for (gggs::CellAreaIterator cit(survey_grid); cit.valid(); cit.next()) {
      hollow.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{kNoData, kNoData});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(survey_grid, std::move(hollow));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/false, /*chart_staging_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }
  // (b) a COARSER containing chart tile in the SAME store dir with real shallow data
  //     (the multi-level ENC ladder this issue is about).
  {
    marine_bathymetry_store::BathymetryTile ctile(coarse_grid);
    for (gggs::CellAreaIterator cit(coarse_grid); cit.valid(); cit.next()) {
      ctile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(coarse_grid, std::move(ctile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCoarseCellSize, /*reference_writable=*/false, /*chart_staging_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));

  const std::string out = root + "/out";
  std::string log;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(out, "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    for (const auto & b : batches) {
      acc.addBatch(b);
    }
    acc.finalize();
    log = capture.str();
  }

  EXPECT_TRUE(loadBathyCells(out).empty())
    << "the hollow exact-level tile must fall through to the coarser chart prior, "
    "which gates the deep blunder; import log was:\n" << log;
  EXPECT_NE(log.find("seeded by cross-level resample"), std::string::npos)
    << "the gate must come from the cross-level fallback; log was:\n" << log;
  EXPECT_EQ(log.find("primed NOTHING"), std::string::npos)
    << "the coarse prior DID prime, so no silent-no-op warning; log was:\n" << log;

  std::filesystem::remove_all(root);
}

// A read failure must be REPORTED even when another tile primed (#137 review).
// The run-level report used to return early whenever `hits > 0`, so the separately
// tallied `read_failures` could only ever reach the operator in the zero-hit case:
// a prior store that went unreadable for all but one tile emitted no run-level line
// at all. That is the same silently-inactive gate this issue exists to close,
// reached by a different route.
TEST(ImportEviction, ReadFailureIsReportedEvenWhenAnotherTilePrimed)
{
  const std::string root = makeTempDir("prior_read_failure");
  const std::string prior_dir = root + "/prior_store";

  // Two survey areas, far enough apart that each one's prior window sees only its
  // own prior tile: A gets a good reference tile, B's is corrupted on disk.
  constexpr double kLatA = 43.0;
  constexpr double kLonA = -70.0;
  constexpr double kLatB = 44.0;
  constexpr double kLonB = -71.0;
  const gggs::Level level = gggs::Level::fromCellSize(kCellSize);
  const gggs::GridIndex grid_a = level.gridIndex(kLatA, kLonA);
  const gggs::GridIndex grid_b = level.gridIndex(kLatB, kLonB);
  ASSERT_NE(grid_a, grid_b);
  {
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/true);
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    for (const gggs::GridIndex & g : {grid_a, grid_b}) {
      marine_bathymetry_store::BathymetryTile tile(g);
      for (gggs::CellAreaIterator cit(g); cit.valid(); cit.next()) {
        tile.set(
          (*cit).row(), (*cit).column(),
          marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
      }
      tiles.emplace(g, std::move(tile));
    }
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }
  // Corrupt ONLY tile B's raster: the filename still parses (so the window still
  // selects it) but the GDAL read throws -- an unreadable/corrupt store, not a
  // coverage gap.
  const std::string tile_b_path = prior_dir + "/" +
    marine_bathymetry_store::layerDirName(
    marine_bathymetry_store::SourceLayer::Reference) + "/" +
    marine_bathymetry_store::tileFilename(grid_b);
  ASSERT_TRUE(std::filesystem::is_regular_file(tile_b_path))
    << "test setup: expected the reference tile at " << tile_b_path;
  {
    std::ofstream corrupt(tile_b_path, std::ios::binary | std::ios::trunc);
    corrupt << "not a geotiff";
  }

  std::string warned;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(root + "/out", "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    acc.addBatch(surveyCell(kLatA, kLonA, 20.0f, 40.0f));
    acc.addBatch(surveyCell(kLatB, kLonB, 20.0f, 40.0f));
    acc.finalize();
    warned = capture.str();
  }

  EXPECT_EQ(countOccurrences(warned, "FAILED to read the prior store"), 1u)
    << "the read failure must be reported once at run level even though the OTHER "
    "tile primed; stderr was:\n" << warned;
  EXPECT_EQ(warned.find("primed NOTHING"), std::string::npos)
    << "one tile DID prime, so the no-op warning must not claim otherwise; stderr "
    "was:\n" << warned;
  EXPECT_NE(warned.find("primed only "), std::string::npos)
    << "partial coverage must be reported -- some attempts gated, some did not "
    "(#137 review); stderr was:\n" << warned;

  std::filesystem::remove_all(root);
}

// Positive coverage for the warm-start scoping clause (#137 review): the run-level
// warning must SAY that some tiles never reached the prior rung, and must name the
// layer they warm-started from correctly (`processed/` -- the layer rung 1 actually
// reads -- not "survey"), and must not imply the two counts partition the run.
TEST(ImportEviction, WarmStartedTilesAreScopedOutOfThePriorWarning)
{
  const std::string root = makeTempDir("warm_start_clause");
  const std::string prior_dir = root + "/prior_store";
  const std::string out = root + "/out";

  // A prior store whose only tile is FAR from the survey: nothing it holds can prime.
  {
    const gggs::GridIndex far_grid =
      gggs::Level::fromCellSize(kCellSize).gridIndex(10.0, 10.0);
    marine_bathymetry_store::BathymetryTile tile(far_grid);
    for (gggs::CellAreaIterator cit(far_grid); cit.valid(); cit.next()) {
      tile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(far_grid, std::move(tile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/true);
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  // First pass with no prior: writes a `processed/` tile at A that the second pass
  // will warm-start from (the incremental-import case).
  constexpr double kLatA = 43.0;
  constexpr double kLonA = -70.0;
  constexpr double kLatB = 44.0;
  constexpr double kLonB = -71.0;
  {
    std::vector<std::vector<GeoSounding>> first;
    first.push_back(surveyCell(kLatA, kLonA, 20.0f, 40.0f));
    runImport(first, out, "", /*budget=*/0);
  }
  ASSERT_FALSE(loadBathyCells(out).empty())
    << "test setup: the first pass must leave a processed tile to warm-start from";

  std::string warned;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(out, "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    acc.addBatch(surveyCell(kLatA, kLonA, 20.0f, 40.0f));   // warm-starts (rung 1)
    acc.addBatch(surveyCell(kLatB, kLonB, 20.0f, 40.0f));   // reaches the prior rung
    acc.finalize();
    warned = capture.str();
  }

  EXPECT_EQ(countOccurrences(warned, "primed NOTHING"), 1u)
    << "the tile that DID reach the prior rung primed nothing, so the warning must "
    "fire exactly once; stderr was:\n" << warned;
  EXPECT_NE(warned.find("tile touch(es) warm-started"), std::string::npos)
    << "the warning must scope its claim by reporting the warm-started tile; "
    "stderr was:\n" << warned;
  EXPECT_NE(warned.find("'processed/' layer"), std::string::npos)
    << "rung 1 reads the `processed/` layer -- naming a 'survey' layer sends the "
    "operator to a directory that no longer exists (ADR-0010 D8); stderr was:\n"
    << warned;

  std::filesystem::remove_all(root);
}

// A prior tile that OVERLAPS the survey window but cannot gate it must not be
// reported as a level MISMATCH (#137 review). loadWindow's overlap test is
// inclusive, so a survey tile flush against a coarse-tile boundary also pulls in the
// edge-adjacent coarse NEIGHBOUR -- a tile sharing no cell with it. Listing that
// under "prior tiles found over the surveyed area ... vs survey level N" reads as
// "your prior is at the wrong level" (regenerate it finer) when the real fault is a
// coverage gap at the tile edge (extend the prior's coverage).
TEST(ImportEviction, EdgeAdjacentCoarseNeighborIsNotReportedAsALevelMismatch)
{
  constexpr float kCoarseCellSize = 8.0f;
  const gggs::Level survey_level = gggs::Level::fromCellSize(kCellSize);
  const gggs::Level coarse_level = gggs::Level::fromCellSize(kCoarseCellSize);
  ASSERT_LT(coarse_level.level(), survey_level.level());

  const std::string root = makeTempDir("edge_adjacent_only");
  const std::string prior_dir = root + "/chart_store";

  // Same boundary-flush geometry as BoundaryFlushCrossLevelChartRejectsDeepBlunder,
  // but the chart data is written ONLY into the west NEIGHBOUR: nothing in the store
  // contains the surveyed tile, so nothing can gate it.
  const gggs::GridIndex container = coarse_level.gridIndex(43.0, -70.0);
  const double lat = container.southLatitude() + container.latitudinalSpan() * 0.5;
  const double lon = container.westLongitude() + container.longitudinalSpan() / 16.0;
  const gggs::GridIndex survey_grid = survey_level.gridIndex(lat, lon);
  ASSERT_NEAR(survey_grid.westLongitude(), container.westLongitude(), 1e-9)
    << "test setup: the survey tile must be flush against the coarse west boundary";
  const gggs::GridIndex west_neighbor = coarse_level.gridIndex(
    lat, container.westLongitude() - container.longitudinalSpan() * 0.5);
  ASSERT_NE(west_neighbor, container);

  {
    marine_bathymetry_store::BathymetryTile ctile(west_neighbor);
    for (gggs::CellAreaIterator cit(west_neighbor); cit.valid(); cit.next()) {
      ctile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(west_neighbor, std::move(ctile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCoarseCellSize, /*reference_writable=*/false, /*chart_staging_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;

  std::string warned;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(root + "/out", "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    acc.addBatch(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));
    acc.finalize();
    warned = capture.str();
  }

  EXPECT_EQ(countOccurrences(warned, "primed NOTHING"), 1u)
    << "nothing containing the survey tile exists, so the gate was off; stderr "
    "was:\n" << warned;
  EXPECT_NE(warned.find("a coverage gap, not a level mismatch"), std::string::npos)
    << "an edge-adjacent neighbour gates nothing and must be reported as a COVERAGE "
    "gap, not as a prior tile found over the surveyed area; stderr was:\n" << warned;
  EXPECT_NE(warned.find("coarse tiles that do not contain them"), std::string::npos)
    << "the unusable tiles must still be named, so the operator can tell 'the prior "
    "stops at this edge' from 'there is no prior at all'; stderr was:\n" << warned;
  EXPECT_EQ(warned.find("Prior tiles usable over the surveyed area"),
    std::string::npos)
    << "no tile here is usable for the surveyed tile(s); stderr was:\n" << warned;

  std::filesystem::remove_all(root);
}

// A SLIVER of data in the finest containing prior must not suppress a coarser prior
// that covers the whole tile (#137 review). The cross-level walk used to stop at the
// first candidate that primed >= 1 cell, so one coarse cell of data in the finest
// containing tile left the rest of the survey tile ungated -- and, because the tally
// counted that as a hit, nothing warned either. Every usable prior is now primed,
// coarsest first, so the gate covers their per-cell UNION with the finest prior
// winning where it has data.
TEST(ImportEviction, SliverInTheFinestPriorDoesNotSuppressAFullCoverageCoarserPrior)
{
  constexpr float kMidCellSize = 8.0f;
  constexpr float kCoarseCellSize = 64.0f;
  const gggs::Level survey_level = gggs::Level::fromCellSize(kCellSize);
  const gggs::Level mid_level = gggs::Level::fromCellSize(kMidCellSize);
  const gggs::Level coarse_level = gggs::Level::fromCellSize(kCoarseCellSize);
  ASSERT_LT(mid_level.level(), survey_level.level());
  ASSERT_LT(coarse_level.level(), mid_level.level());

  const std::string root = makeTempDir("prior_sliver");
  const std::string prior_dir = root + "/chart_store";

  const gggs::GridIndex survey_grid = survey_level.gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;
  const gggs::GridIndex mid_grid = mid_level.gridIndex(survey_lat, survey_lon);
  const gggs::GridIndex coarse_grid = coarse_level.gridIndex(survey_lat, survey_lon);

  // (a) the FINEST containing chart tile: no-data everywhere except the single cell
  //     overlying the survey tile's south-west corner -- far from the sounding.
  {
    const double kNoData = std::numeric_limits<double>::quiet_NaN();
    marine_bathymetry_store::BathymetryTile sliver(mid_grid);
    for (gggs::CellAreaIterator cit(mid_grid); cit.valid(); cit.next()) {
      sliver.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{kNoData, kNoData});
    }
    const gggs::CellIndex corner_cell = mid_level.cellIndex(
      gggs::geoPoint(
        survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.02,
        survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.02));
    ASSERT_EQ(corner_cell.grid(), mid_grid);
    sliver.set(
      corner_cell.row(), corner_cell.column(),
      marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(mid_grid, std::move(sliver));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kMidCellSize, /*reference_writable=*/false, /*chart_staging_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }
  // (b) a COARSER containing chart tile with shallow data everywhere.
  {
    marine_bathymetry_store::BathymetryTile ctile(coarse_grid);
    for (gggs::CellAreaIterator cit(coarse_grid); cit.valid(); cit.next()) {
      ctile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(coarse_grid, std::move(ctile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCoarseCellSize, /*reference_writable=*/false, /*chart_staging_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  const std::string out = root + "/out";
  std::string log;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(out, "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    acc.addBatch(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));
    acc.finalize();
    log = capture.str();
  }

  // Control: the same deep sounding with no prior at all settles a deep cell, so an
  // empty output really does mean the gate rejected it.
  const std::string no_prior = root + "/no_prior";
  {
    std::vector<std::vector<GeoSounding>> batches;
    batches.push_back(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));
    runImport(batches, no_prior, "", /*budget=*/0);
  }
  ASSERT_FALSE(loadBathyCells(no_prior).empty())
    << "control: without a prior the deep sounding must settle";

  EXPECT_TRUE(loadBathyCells(out).empty())
    << "the sliver in the finest containing prior covers one corner cell only; the "
    "full-coverage coarser prior must still gate the rest of the tile; import log "
    "was:\n" << log;
  EXPECT_EQ(log.find("primed NOTHING"), std::string::npos)
    << "the priors DID prime, so no silent-no-op warning; log was:\n" << log;

  std::filesystem::remove_all(root);
}

// An EMPTY processed tile in the output store must not short-circuit the prior rung
// (#137 review). Rung 1 returned on the mere existence of a `processed/` tile, so an
// all-no-data tile (a tile written for an area that produced no accepted soundings)
// left the survey tile ungated on a re-import -- and the run-level warning's scoping
// clause then positively vouched for it as "warm-started".
TEST(ImportEviction, EmptyProcessedTileFallsThroughToThePriorRung)
{
  const std::string root = makeTempDir("empty_processed_warm_start");
  const std::string prior_dir = root + "/prior_store";
  const std::string out = root + "/out";

  const gggs::GridIndex survey_grid =
    gggs::Level::fromCellSize(kCellSize).gridIndex(43.0, -70.0);
  const double survey_lat =
    survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double survey_lon =
    survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;

  // A shallow reference prior at the survey level: it WILL gate the deep blunder,
  // but only if rung 1 lets the tile reach it.
  {
    marine_bathymetry_store::BathymetryTile tile(survey_grid);
    for (gggs::CellAreaIterator cit(survey_grid); cit.valid(); cit.next()) {
      tile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/0.5});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(survey_grid, std::move(tile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kCellSize, /*reference_writable=*/true);
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Reference, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }
  // An existing OUTPUT store whose processed tile for this area is all no-data.
  {
    const double kNoData = std::numeric_limits<double>::quiet_NaN();
    marine_bathymetry_store::BathymetryTile hollow(survey_grid);
    for (gggs::CellAreaIterator cit(survey_grid); cit.valid(); cit.next()) {
      hollow.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{kNoData, kNoData});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(survey_grid, std::move(hollow));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
    store.importTiles(
      marine_bathymetry_store::SourceLayer::Processed, std::move(tiles));
    marine_bathymetry_store::save(store, out);
  }

  std::string log;
  {
    StderrCapture capture;
    GeoMapSheet sheet(kCellSize);
    ImportAccumulatorConfig cfg = makeConfig(out, "", /*budget=*/0);
    cfg.reference_store_dir = prior_dir;
    ImportAccumulator acc(sheet, cfg);
    acc.addBatch(surveyCell(survey_lat, survey_lon, 150.0f, 40.0f));
    acc.finalize();
    log = capture.str();
  }

  EXPECT_TRUE(loadBathyCells(out).empty())
    << "the empty processed tile warm-starts nothing, so the tile must reach the "
    "prior rung and be gated; import log was:\n" << log;
  EXPECT_EQ(log.find("warm-started"), std::string::npos)
    << "an empty processed tile must not be reported to the operator as a "
    "warm-started tile; import log was:\n" << log;

  std::filesystem::remove_all(root);
}

// Resample-gap relief allowance (#137, operator decision). A prior coarse enough to
// blend a shoal and a channel into one cell must NOT gate the survey as hard as an
// exact-level prior would. `Node::insert` takes the MINIMUM of its three blunder
// limits and min() selects the most PERMISSIVE, so an uncertainty-less chart cell
// seeding sigma = 1 cm makes the variance term the most restrictive and therefore
// discarded — the gate collapses to the flat blunder_minimum at every resolution.
// Inflating the seeded 1-sigma by (relief_slope * half-span) makes the variance term
// bind exactly when the prior is too coarse to speak for the cell.
//
// A ~28 m sounding under a ~20 m L7 (8 m/cell) prior is the discriminating case: it
// is deeper than blunder_minimum (10 m) allows... no it is not — so use a coarse L2
// prior, whose 232 m half-span buys metres of allowance, and a sounding just past
// what the flat limit permits. With the allowance the sounding survives; with the
// allowance disabled (slope 0, the pre-#137 behaviour) it is rejected.
TEST(ImportEviction, ResampleGapReliefAdmitsARealDeepUnderACoarsePrior)
{
  // L2 is the coarsest band an ENC export carries — ~232 m/cell.
  constexpr float kVeryCoarseCellSize = 232.0f * 960.0f / 960.0f;
  const gggs::Level survey_level = gggs::Level::fromCellSize(kCellSize);
  const gggs::Level coarse_level = gggs::Level::fromCellSize(kVeryCoarseCellSize);
  ASSERT_LT(coarse_level.level(), survey_level.level());
  const double half_span = 0.5 * coarse_level.cellSize();
  ASSERT_GT(half_span, 20.0) << "the coarse band must be coarse enough to matter";

  const gggs::GridIndex survey_grid = survey_level.gridIndex(43.0, -70.0);
  const double lat = survey_grid.southLatitude() + survey_grid.latitudinalSpan() * 0.5;
  const double lon = survey_grid.westLongitude() + survey_grid.longitudinalSpan() * 0.5;

  // A shallow (-20 m) coarse chart prior over the whole containing coarse tile.
  const std::string root = makeTempDir("relief_slope");
  const std::string prior_dir = root + "/chart_store";
  const gggs::GridIndex coarse_grid = coarse_level.gridIndex(lat, lon);
  {
    marine_bathymetry_store::BathymetryTile ctile(coarse_grid);
    for (gggs::CellAreaIterator cit(coarse_grid); cit.valid(); cit.next()) {
      ctile.set(
        (*cit).row(), (*cit).column(),
        marine_bathymetry_store::BathyCell{/*depth=*/-20.0, /*uncertainty=*/NAN});
    }
    std::map<gggs::GridIndex, marine_bathymetry_store::BathymetryTile> tiles;
    tiles.emplace(coarse_grid, std::move(ctile));
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      kVeryCoarseCellSize, /*reference_writable=*/false,
      /*chart_staging_writable=*/true);
    store.importTiles(marine_bathymetry_store::SourceLayer::Chart, std::move(tiles));
    marine_bathymetry_store::save(store, prior_dir);
  }

  // A REAL deeper-than-charted return: 35 m where the blended coarse cell says 20 m.
  // The flat blunder_minimum (10 m) rejects it; the relief allowance admits it.
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(lat, lon, 35.0f, 40.0f));

  const auto run = [&](double slope, const std::string & out) {
      GeoMapSheet sheet(kCellSize);
      ImportAccumulatorConfig cfg = makeConfig(out, "", /*budget=*/0);
      cfg.reference_store_dir = prior_dir;
      cfg.prior_relief_slope = slope;
      ImportAccumulator acc(sheet, cfg);
      for (const auto & b : batches) {
        acc.addBatch(b);
      }
      acc.finalize();
      return loadBathyCells(out);
    };

  // slope 0 == the pre-#137 behaviour: the coarse prior gates as hard as an
  // exact-level one and the real deep is rejected.
  const auto without = run(0.0, root + "/no_relief");
  EXPECT_TRUE(without.empty())
    << "with the allowance disabled the coarse prior should reject the real deep";

  // The shipped default admits it: half_span is ~116 m, so 0.05 buys ~5.8 m of
  // 1-sigma, and blunder_scalar * sigma exceeds the 10 m flat limit.
  const auto with = run(ImportAccumulatorConfig{}.prior_relief_slope,
      root + "/default_relief");
  bool kept_real_deep = false;
  for (const auto & [cell, du] : with) {
    if (du.first < -30.0) {kept_real_deep = true;}
  }
  EXPECT_TRUE(kept_real_deep)
    << "the resample-gap relief allowance must admit a real deeper-than-charted "
       "return under a very coarse prior";

  std::filesystem::remove_all(root);
}

}  // namespace cube
