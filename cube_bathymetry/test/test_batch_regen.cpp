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

// Batch-regen exactness (cube_bathymetry#96). The scatter/gather rebuild builds
// each tile in a single unbounded pass over the COMPLETE set of soundings that
// touch it, so its output is BIT-EXACT against a single-pass unbounded
// ImportAccumulator over the same soundings -- depth, uncertainty, and the 3-band
// backscatter all reproduced to the last bit (no eviction, no reload, no
// approximation). Also asserts the scatter scratch dir is cleaned up.

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cube_bathymetry/batch_regen.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_mbes_backscatter_store/mbes_store.hpp"
#include "marine_mbes_backscatter_store/mbes_tile.hpp"
#include "marine_mbes_backscatter_store/tile_io.hpp"

namespace cube
{
namespace
{
constexpr float kCellSize = 1.0f;

std::string makeTempDir(const std::string & tag)
{
  const auto base = std::filesystem::temp_directory_path() /
    ("cube_batch_regen_" + tag + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  return base.string();
}

// One cell's worth of survey: several soundings at one position so CUBE settles a
// finite depth and co-estimates a finite intensity. Varying intensity gives real
// backscatter dispersion (so the 3-band statistic is non-trivial).
std::vector<GeoSounding> surveyCell(
  double lat, double lon, float depth, float intensity, int reps = 8)
{
  std::vector<GeoSounding> soundings;
  for (int rep = 0; rep < reps; ++rep) {
    gz4d::GeoPointLatLongDegrees point(lat, lon, -depth - rep * 0.001);
    GeoSounding s(point);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    s.sounding.intensity = intensity + rep * 0.7f;  // real dispersion (n>=2)
    s.sounding.beam_angle = 0.0f;
    s.sounding.slant_range = depth;
    soundings.push_back(s);
  }
  return soundings;
}

BatchRegen::SheetFactory sheetFactory()
{
  return []() {return std::make_unique<GeoMapSheet>(kCellSize);};
}

ImportAccumulatorConfig makeConfig(
  const std::string & store_dir, const std::string & bs_dir)
{
  ImportAccumulatorConfig cfg;
  cfg.store_dir = store_dir;
  cfg.cell_size_m = kCellSize;
  cfg.bs_store_dir = bs_dir;
  cfg.max_resident_tiles = 0;
  return cfg;
}

// Single-pass unbounded reference build.
void runSinglePass(
  const std::vector<std::vector<GeoSounding>> & batches,
  const std::string & store_dir, const std::string & bs_dir)
{
  GeoMapSheet sheet(kCellSize);
  ImportAccumulator acc(sheet, makeConfig(store_dir, bs_dir));
  for (const auto & batch : batches) {
    acc.addBatch(batch);
  }
  acc.finalize();
}

// Batch-regen scatter/gather build.
void runBatchRegen(
  const std::vector<std::vector<GeoSounding>> & batches,
  const std::string & store_dir, const std::string & bs_dir)
{
  BatchRegen regen(sheetFactory(), makeConfig(store_dir, bs_dir));
  for (const auto & batch : batches) {
    regen.addBatch(batch);
  }
  regen.finalize();
}

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

// The stored 3-band backscatter statistic per finite cell (mean, standard_error,
// sample_sd) -- compared byte-for-byte between the two builds.
struct BsBands
{
  float mean;
  float standard_error;
  float sample_sd;
};

std::map<gggs::CellIndex, BsBands> loadBackscatterCells(const std::string & bs_dir)
{
  marine_mbes_backscatter_store::MbesBackscatterStore store =
    marine_mbes_backscatter_store::MbesBackscatterStore::fromCellSize(kCellSize);
  marine_mbes_backscatter_store::load(store, bs_dir);
  std::map<gggs::CellIndex, BsBands> out;
  for (const auto & grid_tile : store.tiles(
      marine_mbes_backscatter_store::SourceLayer::Survey))
  {
    gggs::CellAreaIterator it(grid_tile.second.index());
    for (; it.valid(); it.next()) {
      const marine_mbes_backscatter_store::MbesCell c =
        grid_tile.second.get((*it).row(), (*it).column());
      if (c.hasData()) {
        out.emplace(*it, BsBands{c.mean, c.standard_error, c.sample_sd});
      }
    }
  }
  return out;
}

// Assert batch-regen == single-pass, bit-exact, cell-for-cell.
void expectExactMatch(
  const std::string & regen_dir, const std::string & single_dir,
  const std::string & regen_bs, const std::string & single_bs)
{
  const auto bathy_r = loadBathyCells(regen_dir);
  const auto bathy_s = loadBathyCells(single_dir);
  ASSERT_FALSE(bathy_s.empty()) << "single-pass build produced no bathy cells";
  ASSERT_EQ(bathy_r.size(), bathy_s.size())
    << "batch-regen bathy cell count differs from single-pass";
  for (const auto & [cell, ds] : bathy_s) {
    auto it = bathy_r.find(cell);
    ASSERT_NE(it, bathy_r.end()) << "batch-regen is missing a bathy cell";
    EXPECT_DOUBLE_EQ(it->second.first, ds.first) << "depth differs (not bit-exact)";
    EXPECT_DOUBLE_EQ(it->second.second, ds.second)
      << "uncertainty differs (not bit-exact)";
  }

  const auto bs_r = loadBackscatterCells(regen_bs);
  const auto bs_s = loadBackscatterCells(single_bs);
  ASSERT_FALSE(bs_s.empty()) << "single-pass build produced no backscatter cells";
  ASSERT_EQ(bs_r.size(), bs_s.size())
    << "batch-regen backscatter cell count differs from single-pass";
  for (const auto & [cell, bs] : bs_s) {
    auto it = bs_r.find(cell);
    ASSERT_NE(it, bs_r.end()) << "batch-regen is missing a backscatter cell";
    EXPECT_FLOAT_EQ(it->second.mean, bs.mean) << "backscatter mean differs";
    EXPECT_FLOAT_EQ(it->second.standard_error, bs.standard_error)
      << "backscatter standard_error differs";
    EXPECT_FLOAT_EQ(it->second.sample_sd, bs.sample_sd)
      << "backscatter sample_sd differs";
  }
}
}  // namespace

// A disjoint multi-tile survey: batch-regen must reproduce the single-pass store
// exactly.
TEST(BatchRegen, MultiTileExactMatch)
{
  std::vector<std::vector<GeoSounding>> batches;
  for (int i = 0; i < 12; ++i) {
    // ~2.2 km apart -> every batch lands on a distinct GGGS tile.
    batches.push_back(surveyCell(43.0 + 0.02 * i, -70.0, 10.0f + i, 30.0f + i));
  }

  const std::string root = makeTempDir("multitile");
  const std::string r_dir = root + "/regen";
  const std::string s_dir = root + "/single";
  const std::string r_bs = root + "/regen_bs";
  const std::string s_bs = root + "/single_bs";

  runBatchRegen(batches, r_dir, r_bs);
  runSinglePass(batches, s_dir, s_bs);

  expectExactMatch(r_dir, s_dir, r_bs, s_bs);
  std::filesystem::remove_all(root);
}

// A tile visited, then other tiles surveyed, then the first tile REVISITED at a
// different cell. In batch-regen both visits land in the same tile bucket and are
// gathered in one pass; the single-pass keeps the tile resident the whole time.
// Both must agree exactly.
TEST(BatchRegen, RevisitExactMatch)
{
  std::vector<std::vector<GeoSounding>> batches;
  batches.push_back(surveyCell(43.00000, -70.00000, 12.0f, 40.0f));  // visit 1, tile A
  for (int i = 1; i <= 5; ++i) {                                     // other tiles
    batches.push_back(surveyCell(43.0 + 0.02 * i, -71.0, 15.0f + i, 25.0f + i));
  }
  batches.push_back(surveyCell(43.00050, -70.00000, 18.0f, 55.0f));  // visit 2, tile A

  const std::string root = makeTempDir("revisit");
  const std::string r_dir = root + "/regen";
  const std::string s_dir = root + "/single";
  const std::string r_bs = root + "/regen_bs";
  const std::string s_bs = root + "/single_bs";

  runBatchRegen(batches, r_dir, r_bs);
  runSinglePass(batches, s_dir, s_bs);

  expectExactMatch(r_dir, s_dir, r_bs, s_bs);
  std::filesystem::remove_all(root);
}

// A survey straddling a GGGS tile seam with a MULTI-CELL deposit radius. This is
// the case the earlier per-sounding ±1-cell scatter got WRONG: GeoGrid::insert
// spreads a sounding out to radius ~= CONF_99PC*sqrt(horizontal_error) (several
// cells for realistic TPU), so a sounding two-plus cells inside one tile still
// deposits into the neighbour tile's cells across the seam. A single unbounded pass
// makes those cross-seam deposits (the neighbour grid receives the whole batch);
// per-sounding scatter dropped them because the sounding's own ±1-cell window never
// reached the neighbour tile. The other tests use a sub-cell horizontal_error (0.1),
// so they never cross a seam and cannot catch this -- this test pins the fix.
TEST(BatchRegen, SeamCrossingExactMatch)
{
  const gggs::Level level = gggs::Level::fromCellSize(kCellSize);
  // East edge of the tile containing (43, -70) == west edge of its east neighbour.
  const double seam_lon = level.gridIndex(43.0, -70.0).eastLongitude();
  const double lat = 43.0;
  const double deg_per_m_lon = 1.0 / (111320.0 * std::cos(lat * M_PI / 180.0));

  // horizontal_error = 4 m -> max_radius = CONF_99PC*sqrt(4) = 5.152 m. With a small
  // vertical_error the effective radius (sqrt(ratio-1) - max_radius, clamped) settles
  // near 3 m -- about three 1 m cells. vertical_error is chosen so ratio =
  // maxVarianceAllowed(~12 m, order1a)/vertical_error ~= 67, i.e. sqrt(ratio-1) ~=
  // 8.15 and radius ~= 3 m. Survey cells sit 1..4 m either side of the seam, so the
  // ones >= 2 m from it deposit across the seam yet lie outside their own ±1-cell
  // window -- exactly the deposits the old scatter dropped.
  const float kHorizErr = 4.0f;
  const float kVertErr = 0.00106f;

  std::vector<GeoSounding> batch;
  for (int c = -4; c <= 4; ++c) {
    if (c == 0) {continue;}
    const double lon = seam_lon + c * 1.0 * deg_per_m_lon;
    for (int rep = 0; rep < 8; ++rep) {
      gz4d::GeoPointLatLongDegrees point(lat, lon, -12.0 - rep * 0.001);
      GeoSounding s(point);
      s.sounding.vertical_error = kVertErr;
      s.sounding.horizontal_error = kHorizErr;
      s.sounding.intensity = 30.0f + c + rep * 0.7f;  // real dispersion (n>=2)
      s.sounding.beam_angle = 0.0f;
      s.sounding.slant_range = 12.0f;
      batch.push_back(s);
    }
  }
  const std::vector<std::vector<GeoSounding>> batches{batch};

  const std::string root = makeTempDir("seam");
  const std::string r_dir = root + "/regen";
  const std::string s_dir = root + "/single";
  const std::string r_bs = root + "/regen_bs";
  const std::string s_bs = root + "/single_bs";

  runBatchRegen(batches, r_dir, r_bs);
  runSinglePass(batches, s_dir, s_bs);

  // The survey must actually span both tiles, else the seam was never crossed and
  // the test would be vacuous.
  const auto cells = loadBathyCells(s_dir);
  std::set<gggs::GridIndex> grids;
  for (const auto & kv : cells) {
    grids.insert(kv.first.grid());
  }
  ASSERT_GE(grids.size(), 2u)
    << "seam survey must span >= 2 tiles to exercise cross-seam deposits";

  expectExactMatch(r_dir, s_dir, r_bs, s_bs);
  std::filesystem::remove_all(root);
}

// The scatter scratch directory is created during scatter and DELETED by finalize.
TEST(BatchRegen, ScratchDirCleanedUpAfterFinalize)
{
  const std::string root = makeTempDir("scratch");
  const std::string store_dir = root + "/store";
  const std::string bs_dir = root + "/bs";

  BatchRegen regen(sheetFactory(), makeConfig(store_dir, bs_dir));
  for (int i = 0; i < 6; ++i) {
    regen.addBatch(surveyCell(43.0 + 0.02 * i, -70.0, 10.0f + i, 30.0f + i));
  }
  const std::string scratch = regen.scratchDir();
  ASSERT_FALSE(scratch.empty()) << "scatter should have created a scratch dir";
  EXPECT_TRUE(std::filesystem::exists(scratch)) << "scratch dir should exist mid-run";

  regen.finalize();

  EXPECT_FALSE(std::filesystem::exists(scratch))
    << "finalize must delete the scatter scratch dir";
  EXPECT_TRUE(regen.scratchDir().empty()) << "finalize must clear the scratch path";
  EXPECT_GT(regen.bathyTilesPersisted(), 0u);

  std::filesystem::remove_all(root);
}

}  // namespace cube
