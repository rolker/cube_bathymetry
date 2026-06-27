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

// Draft-tile persistence round-trip (#21). Exercises the same save path the live
// node's saveDirtyTiles() uses: dirty grids -> geoGridToTile -> saveTile into
// <dir>/<layerDirName(Draft)>/ (flat single fused grid, unh_marine_autonomy#221),
// then store-level load() reads them back. Pins (a) the on-disk layout contract
// (the hand-built save path must match what load() scans) and (b) periodic-save
// == end-of-session export equivalence.

#include <gtest/gtest.h>

#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

namespace cube
{

namespace
{
std::vector<GeoSounding> makeSoundings()
{
  std::vector<GeoSounding> soundings;
  const double base_lat = 43.07;
  const double base_lon = -70.76;
  for (int rep = 0; rep < 20; ++rep) {
    for (int i = 0; i < 4; ++i) {
      gz4d::GeoPointLatLongDegrees point(
        base_lat + i * 1e-5, base_lon + i * 1e-5, -10.0 - i);
      GeoSounding s(point);
      s.sounding.vertical_error = 0.5f;
      s.sounding.horizontal_error = 0.1f;
      soundings.push_back(s);
    }
  }
  return soundings;
}

constexpr int64_t kStamp = 1234567890123456789LL;

// Mirror cube_bathymetry_node::saveDirtyTiles() at library level: write each
// dirty grid as a draft tile under <dir>/draft/ (flat, no epoch segment).
std::size_t saveDirty(
  GeoMapSheet & sheet, const std::string & dir, int64_t ts_ns)
{
  const std::set<gggs::GridIndex> dirty = sheet.dirtyGrids();
  const std::string out =
    dir + "/" +
    marine_bathymetry_store::layerDirName(
    marine_bathymetry_store::SourceLayer::Draft);
  std::filesystem::create_directories(out);
  std::size_t written = 0;
  for (const auto & index : dirty) {
    auto grid = sheet.gridAt(index);
    if (!grid) {
      continue;
    }
    marine_bathymetry_store::BathymetryTile tile =
      geoGridToTile(*grid, ts_ns, 0);
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

std::string makeTempDir(const std::string & tag)
{
  const auto base = std::filesystem::temp_directory_path() /
    ("cube_persist_" + tag + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  return base.string();
}
}  // namespace

// confirm layerDirName(Draft) == "draft" so the hand-built save path matches the
// directory load() scans.
TEST(Persistence, DraftLayerDirNameIsDraft)
{
  EXPECT_EQ(
    marine_bathymetry_store::layerDirName(
      marine_bathymetry_store::SourceLayer::Draft),
    "draft");
}

// Save dirty tiles, then load the whole store back and verify the depth/
// uncertainty values round-trip into the Draft layer.
TEST(Persistence, SaveDirtyThenStoreLoadRoundTrips)
{
  const std::string dir = makeTempDir("roundtrip");

  GeoMapSheet sheet(1.0f);
  sheet.addSoundings(makeSoundings());
  ASSERT_FALSE(sheet.dirtyGrids().empty());

  const std::size_t written = saveDirty(sheet, dir, kStamp);
  ASSERT_GT(written, 0u);
  EXPECT_TRUE(sheet.dirtyGrids().empty()) << "save must clear the dirty set";

  // Reference: convert the same sheet to tiles directly (end-of-session export).
  auto reference = mapSheetToTiles(sheet, kStamp, 0);
  ASSERT_FALSE(reference.empty());

  // Load the store back through the public store-level load().
  marine_bathymetry_store::BathymetryStore loaded =
    marine_bathymetry_store::BathymetryStore::fromCellSize(1.0f);
  const std::size_t loaded_count = marine_bathymetry_store::load(loaded, dir);
  EXPECT_EQ(loaded_count, written);

  const auto & draft_tiles =
    loaded.tiles(marine_bathymetry_store::SourceLayer::Draft);
  ASSERT_FALSE(draft_tiles.empty()) << "draft tiles must be present after load";

  // Every finite reference cell must match a loaded cell (depth + uncertainty).
  std::size_t checked = 0;
  for (const auto & grid_tile : reference) {
    const auto & ref_tile = grid_tile.second;
    auto loaded_tile_it = draft_tiles.find(grid_tile.first);
    ASSERT_NE(loaded_tile_it, draft_tiles.end());
    const auto & loaded_tile = loaded_tile_it->second;

    const std::vector<double> & rd = ref_tile.depthBand();
    const std::vector<double> & ru = ref_tile.uncertaintyBand();
    const std::vector<double> & ld = loaded_tile.depthBand();
    const std::vector<double> & lu = loaded_tile.uncertaintyBand();
    ASSERT_EQ(rd.size(), ld.size());
    for (std::size_t i = 0; i < rd.size(); ++i) {
      if (std::isnan(rd[i])) {
        EXPECT_TRUE(std::isnan(ld[i]));
      } else {
        // GeoTIFF Float64 round-trip is exact for these values.
        EXPECT_DOUBLE_EQ(ld[i], rd[i]);
        EXPECT_DOUBLE_EQ(lu[i], ru[i]);
        ++checked;
      }
    }
  }
  EXPECT_GT(checked, 0u);

  std::filesystem::remove_all(dir);
}

// A periodic save of all dirty grids must produce the same on-disk tiles as a
// single end-of-session export of the same sheet (the flush-per-interval concern
// the plan-review flagged).
TEST(Persistence, PeriodicSaveEqualsEndOfSessionExport)
{
  const std::string dir_periodic = makeTempDir("periodic");

  GeoMapSheet sheet(1.0f);
  sheet.addSoundings(makeSoundings());

  // Reference end-of-session export BEFORE saving (values() flushes the queue,
  // so take the reference from the same flushed state by importing into a store
  // and saving wholesale).
  GeoMapSheet sheet_ref(1.0f);
  sheet_ref.addSoundings(makeSoundings());
  auto ref_tiles = mapSheetToTiles(sheet_ref, kStamp, 0);

  // Periodic-style save of every dirty grid.
  const std::size_t written = saveDirty(sheet, dir_periodic, kStamp);
  ASSERT_EQ(written, ref_tiles.size());

  // Load the periodic store and compare to the reference tiles.
  marine_bathymetry_store::BathymetryStore loaded =
    marine_bathymetry_store::BathymetryStore::fromCellSize(1.0f);
  marine_bathymetry_store::load(loaded, dir_periodic);
  const auto & draft_tiles =
    loaded.tiles(marine_bathymetry_store::SourceLayer::Draft);
  ASSERT_FALSE(draft_tiles.empty());

  EXPECT_EQ(draft_tiles.size(), ref_tiles.size());
  for (const auto & grid_tile : ref_tiles) {
    auto loaded_it = draft_tiles.find(grid_tile.first);
    ASSERT_NE(loaded_it, draft_tiles.end());
    const std::vector<double> & rd = grid_tile.second.depthBand();
    const std::vector<double> & ld = loaded_it->second.depthBand();
    ASSERT_EQ(rd.size(), ld.size());
    for (std::size_t i = 0; i < rd.size(); ++i) {
      if (std::isnan(rd[i])) {
        EXPECT_TRUE(std::isnan(ld[i]));
      } else {
        EXPECT_DOUBLE_EQ(ld[i], rd[i]);
      }
    }
  }

  std::filesystem::remove_all(dir_periodic);
}

namespace
{
// Count finite (surveyed) cells in a loaded draft tile's depth band.
std::size_t finiteCellCount(
  const marine_bathymetry_store::BathymetryStore & store,
  const gggs::GridIndex & index)
{
  const auto & tiles = store.tiles(marine_bathymetry_store::SourceLayer::Draft);
  auto it = tiles.find(index);
  if (it == tiles.end()) {
    return 0;
  }
  std::size_t n = 0;
  for (double d : it->second.depthBand()) {
    if (!std::isnan(d)) {
      ++n;
    }
  }
  return n;
}

marine_bathymetry_store::BathymetryStore loadDraft(const std::string & dir)
{
  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(1.0f);
  marine_bathymetry_store::load(store, dir);
  return store;
}

// A sparse resurvey: a few soundings on ONE cell of the surveyed tile -- the
// pathological case that an overwrite-on-save would use to wipe the rest.
std::vector<GeoSounding> makeSparseResurvey()
{
  std::vector<GeoSounding> soundings;
  for (int rep = 0; rep < 20; ++rep) {
    gz4d::GeoPointLatLongDegrees point(43.07, -70.76, -10.5);
    GeoSounding s(point);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    soundings.push_back(s);
  }
  return soundings;
}
}  // namespace

// THE lossless-eviction guarantee (#70, ADR-0001): survey a tile, save it, evict
// it (drop from RAM), then revisit and SPARSELY resurvey one cell. Because
// saveTile overwrites the whole tile file, the only thing that keeps the
// un-resurveyed cells alive across the re-save is the settled-state reload
// (primeFromTile -> setSettledDepthAt). With the reload, the re-saved tile
// retains every original cell; the negative control (no reload) proves the test
// discriminates -- without it, the re-save wipes everything but the one cell.
TEST(Persistence, RevisitAfterEvictPreservesData)
{
  const std::string dir = makeTempDir("lossless");
  const gggs::Level level = gggs::Level::fromCellSize(1.0f);
  const gggs::GridIndex index = level.gridIndex(43.07, -70.76);

  // 1. Survey the tile fully and save it (the pre-eviction durable state).
  GeoMapSheet surveyed(1.0f);
  surveyed.addSoundings(makeSoundings());
  ASSERT_GT(saveDirty(surveyed, dir, kStamp), 0u);
  const std::size_t original_finite = finiteCellCount(loadDraft(dir), index);
  ASSERT_GT(original_finite, 1u) << "the survey must populate more than one cell";

  // 2. WITH reload (the fix): a fresh sheet reseeds settled state from disk
  //    (mimicking evict -> revisit-reload), then sparsely resurveys and re-saves.
  GeoMapSheet reloaded(1.0f);
  {
    marine_bathymetry_store::BathymetryStore store = loadDraft(dir);
    loadIntoSheet(store, marine_bathymetry_store::SourceLayer::Draft, reloaded);
  }
  reloaded.addSoundings(makeSparseResurvey());  // touches ~one cell
  ASSERT_GT(saveDirty(reloaded, dir, kStamp + 1), 0u);
  const std::size_t after_reload_finite = finiteCellCount(loadDraft(dir), index);
  EXPECT_GE(after_reload_finite, original_finite)
    << "reload+resurvey+save must NOT lose any previously-surveyed cell";

  // 3. NEGATIVE CONTROL: the same sparse resurvey on a FRESH (un-reloaded) sheet,
  //    re-saved over the tile, wipes everything but the resurveyed cell.
  const std::string dir_ctrl = makeTempDir("lossless_ctrl");
  GeoMapSheet seeded(1.0f);
  seeded.addSoundings(makeSoundings());
  ASSERT_GT(saveDirty(seeded, dir_ctrl, kStamp), 0u);
  GeoMapSheet no_reload(1.0f);
  no_reload.addSoundings(makeSparseResurvey());
  ASSERT_GT(saveDirty(no_reload, dir_ctrl, kStamp + 1), 0u);
  const std::size_t no_reload_finite = finiteCellCount(loadDraft(dir_ctrl), index);
  EXPECT_LT(no_reload_finite, original_finite)
    << "without reload the overwrite-on-save DOES lose cells -- "
       "this is what the reload prevents";

  std::filesystem::remove_all(dir);
  std::filesystem::remove_all(dir_ctrl);
}

}  // namespace cube
