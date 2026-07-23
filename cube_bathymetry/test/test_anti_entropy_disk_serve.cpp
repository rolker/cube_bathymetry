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

// Anti-entropy phase 2 (#106): catalog + serving from disk. Exercises the full
// eviction/catch-up loop at library level: populate past the resident budget ->
// evict -> catalog advertises the FULL store (not the resident window) -> a
// cold consumer's reconcile requests everything -> each request is servable
// from disk via a SCRATCH store/sheet WITHOUT touching the live sheet (no LRU
// churn) -> the consumer converges (second reconcile requests nothing).
//
// KNOWN LIMITATION (plan-review S1, acknowledged): the changed node methods
// (publishCatalog, tileRequestCallback, the startup prime, drainDiskServeQueue)
// are private to cube_bathymetry_node.cpp with no test harness, so this test
// guards the INVARIANTS via the same library primitives the node composes --
// it cannot catch a regression in the node wiring itself (e.g. re-introducing
// a grids()-based catalog). Node-wiring acceptance is the post-merge field
// validation tracked on #104, plus review.

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "builtin_interfaces/msg/time.hpp"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/quantize_tile.h"
#include "cube_bathymetry/store_import.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_tiled_raster_store/tile_catalog.hpp"

namespace cube
{

namespace
{

constexpr float kCellSize = 1.0f;
// Survey N tiles with a resident budget of 2 -> most tiles end up evicted.
constexpr int kTileCount = 6;
constexpr std::size_t kResidentBudget = 2;
// One level-10 tile spans ~960 m (~0.0086 deg) at 1 m cells; 0.02 deg strides
// guarantee each cluster lands in its own tile.
constexpr double kTileStrideDeg = 0.02;

// A dense cluster of soundings around (lat, lon) -- enough to settle cells.
std::vector<GeoSounding> makeCluster(double lat, double lon)
{
  std::vector<GeoSounding> soundings;
  for (int rep = 0; rep < 20; ++rep) {
    for (int i = 0; i < 4; ++i) {
      gz4d::GeoPointLatLongDegrees point(
        lat + i * 1e-5, lon + i * 1e-5, -10.0 - i);
      GeoSounding s(point);
      s.sounding.vertical_error = 0.5f;
      s.sounding.horizontal_error = 0.1f;
      soundings.push_back(s);
    }
  }
  return soundings;
}

// Mirror cube_bathymetry_node::saveDirtyTiles() at library level (same helper
// pattern as test_persistence.cpp).
std::size_t saveDirty(GeoMapSheet & sheet, const std::string & dir)
{
  const std::set<gggs::GridIndex> dirty = sheet.dirtyGrids();
  const std::string out =
    dir + "/" +
    marine_bathymetry_store::layerDirName(
    marine_bathymetry_store::SourceLayer::Survey);
  std::filesystem::create_directories(out);
  std::size_t written = 0;
  for (const auto & index : dirty) {
    auto grid = sheet.gridAt(index);
    if (!grid) {
      continue;
    }
    marine_bathymetry_store::BathymetryTile tile = geoGridToTile(*grid);
    if (!tile.dirty()) {
      continue;
    }
    marine_bathymetry_store::saveTile(
      tile, out + "/" + marine_bathymetry_store::tileFilename(index));
    ++written;
  }
  sheet.clearDirtyGrids();
  return written;
}

// Mirror the node's trimResidentToBudget() via the same library primitives
// (the node method is private; plan-review S4): drop clean cold tiles beyond
// the budget, recording them as evicted.
std::set<gggs::GridIndex> trimToBudget(GeoMapSheet & sheet, std::size_t budget)
{
  std::set<gggs::GridIndex> evicted;
  const std::set<gggs::GridIndex> still_dirty = sheet.dirtyGrids();
  for (const auto & index : sheet.coldTiles(budget)) {
    if (still_dirty.count(index)) {
      continue;
    }
    sheet.dropTile(index);
    evicted.insert(index);
  }
  return evicted;
}

std::string makeTempDir(const std::string & tag)
{
  const auto base = std::filesystem::temp_directory_path() /
    ("cube_anti_entropy_" + tag + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  return base.string();
}

}  // namespace

// The complete loop: populate -> save -> evict -> full catalog -> cold-consumer
// reconcile -> disk-serve every request without LRU churn -> converge.
TEST(AntiEntropyDiskServe, ColdConsumerConvergesToFullStoreAcrossEviction)
{
  const std::string dir = makeTempDir("loop");
  const double base_lat = 43.07;
  const double base_lon = -70.76;

  // 1. Populate: N clusters, each in its own tile; save everything.
  GeoMapSheet sheet(kCellSize);
  for (int i = 0; i < kTileCount; ++i) {
    sheet.addSoundings(makeCluster(base_lat + i * kTileStrideDeg, base_lon));
  }
  ASSERT_EQ(sheet.residentTileCount(), static_cast<std::size_t>(kTileCount))
    << "each cluster must land in its own tile (stride too small?)";
  ASSERT_EQ(saveDirty(sheet, dir), static_cast<std::size_t>(kTileCount));

  // 2. Seed the catalog registry from the FULL persisted store (the #106
  //    startup-prime order: seed BEFORE the trim, so eviction cannot drop
  //    tiles from the catalog).
  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
  marine_bathymetry_store::load(store, dir);
  const auto & store_tiles =
    store.tiles(marine_bathymetry_store::SourceLayer::Survey);
  ASSERT_EQ(store_tiles.size(), static_cast<std::size_t>(kTileCount));

  marine_tiled_raster_store::TileCatalogBuilder builder;
  const std::int64_t prime_version = 1'000'000'000LL;
  for (const auto & [tile_index, tile] : store_tiles) {
    builder.update(tile_index, prime_version);
  }
  EXPECT_EQ(builder.size(), static_cast<std::size_t>(kTileCount))
    << "catalog registry must cover the whole store, not the resident window";

  // 3. Evict down to the budget (after the seed, as in the fixed node order).
  const std::set<gggs::GridIndex> evicted = trimToBudget(sheet, kResidentBudget);
  ASSERT_EQ(sheet.residentTileCount(), kResidentBudget);
  ASSERT_EQ(evicted.size(), static_cast<std::size_t>(kTileCount) - kResidentBudget);
  EXPECT_EQ(builder.size(), static_cast<std::size_t>(kTileCount))
    << "eviction must not remove tiles from the catalog registry";

  // 4. Cold-consumer reconcile: an empty consumer must request ALL N tiles.
  const marine_tiled_raster_store::TileCatalog catalog =
    builder.buildCatalog(prime_version + 1);
  EXPECT_EQ(catalog.entries.size(), static_cast<std::size_t>(kTileCount));
  marine_tiled_raster_store::TileCatalogReconciler consumer;
  const auto first = consumer.reconcile(catalog);
  EXPECT_EQ(first.to_request.size(), static_cast<std::size_t>(kTileCount));
  EXPECT_TRUE(first.to_prune.empty());

  // 5. Disk-serve every requested tile via a SCRATCH store/sheet -- the #106
  //    drain path -- and assert the live sheet is never touched (no LRU churn).
  builtin_interfaces::msg::Time stamp;
  stamp.sec = 2;
  const std::size_t resident_before = sheet.residentTileCount();
  std::size_t served = 0;
  for (const auto & index : first.to_request) {
    marine_bathymetry_store::BathymetryStore scratch =
      marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
    marine_bathymetry_store::loadWindow(
      scratch, dir, index.southWestPosition(), index.northEastPosition(),
      nullptr);
    const auto & tiles =
      scratch.tiles(marine_bathymetry_store::SourceLayer::Survey);
    const auto it = tiles.find(index);
    ASSERT_NE(it, tiles.end()) << "every cataloged tile must load from disk";

    GeoMapSheet scratch_sheet(kCellSize);
    primeFromTile(it->second, scratch_sheet);
    auto grid = scratch_sheet.gridAt(index);
    ASSERT_TRUE(grid) << "primeFromTile must materialize the requested tile";

    const auto vt = quantizeTile(*grid, stamp);
    ASSERT_TRUE(vt.has_value()) << "a surveyed tile must quantize non-empty";
    EXPECT_FALSE(vt->bands.empty());
    ++served;

    EXPECT_EQ(sheet.residentTileCount(), resident_before)
      << "disk-serve must NOT insert into the live sheet (no LRU churn)";
  }
  EXPECT_EQ(served, first.to_request.size());

  // 6. Convergence: after marking everything held at the served version, a
  //    second reconcile requests nothing and prunes nothing.
  for (const auto & index : first.to_request) {
    consumer.markHave(index, prime_version);
  }
  const auto second = consumer.reconcile(catalog);
  EXPECT_TRUE(second.to_request.empty());
  EXPECT_TRUE(second.to_prune.empty());

  std::filesystem::remove_all(dir);
}

// The regression the old order caused (#106 startup-prime bug): seeding the
// catalog from the post-trim RESIDENT set loses the just-evicted tiles, and a
// consumer that already holds them prunes valid coverage on the next
// reconcile. This is the negative control proving the seed-before-trim order
// is load-bearing.
TEST(AntiEntropyDiskServe, ResidentOnlyCatalogPrunesValidCoverage)
{
  const std::string dir = makeTempDir("negctrl");
  const double base_lat = 43.07;
  const double base_lon = -70.76;

  GeoMapSheet sheet(kCellSize);
  for (int i = 0; i < kTileCount; ++i) {
    sheet.addSoundings(makeCluster(base_lat + i * kTileStrideDeg, base_lon));
  }
  ASSERT_EQ(saveDirty(sheet, dir), static_cast<std::size_t>(kTileCount));

  // A warm consumer that holds the full store (e.g. from a prior session).
  const std::int64_t held_version = 1'000'000'000LL;
  marine_tiled_raster_store::TileCatalogReconciler consumer;
  {
    marine_bathymetry_store::BathymetryStore store =
      marine_bathymetry_store::BathymetryStore::fromCellSize(kCellSize);
    marine_bathymetry_store::load(store, dir);
    for (const auto & [tile_index, tile] :
      store.tiles(marine_bathymetry_store::SourceLayer::Survey))
    {
      consumer.markHave(tile_index, held_version);
    }
  }
  ASSERT_EQ(consumer.size(), static_cast<std::size_t>(kTileCount));

  // OLD (buggy) order: trim first, then seed the catalog from the residents.
  trimToBudget(sheet, kResidentBudget);
  marine_tiled_raster_store::TileCatalogBuilder resident_only;
  for (const auto & grid : sheet.grids()) {
    if (grid) {
      resident_only.update(grid->index(), held_version);
    }
  }
  ASSERT_EQ(resident_only.size(), kResidentBudget);

  // The warm consumer reconciles against the resident-only catalog: every
  // evicted tile is absent and older than the generation time -> pruned.
  const auto result =
    consumer.reconcile(resident_only.buildCatalog(held_version + 1));
  EXPECT_EQ(
    result.to_prune.size(),
    static_cast<std::size_t>(kTileCount) - kResidentBudget)
    << "a resident-only catalog erodes a warm consumer's valid coverage -- "
       "the failure mode the #106 store-backed catalog eliminates";

  std::filesystem::remove_all(dir);
}

}  // namespace cube
