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

// Long-duration bounding (#70, ADR-0001). A unit-portable proxy for the bounded-
// RSS acceptance criterion: a synthetic survey track crosses far more tiles than
// the resident budget; the node's persist-then-drop eviction loop (mirrored here
// at library level) must keep the resident tile COUNT bounded while the ON-DISK
// tile count keeps growing -- i.e. RAM stays flat as coverage (and the durable
// store) grows. Measuring actual RSS is not portable in a unit test; the resident
// grid count is the faithful, deterministic proxy (each grid is a fixed 960x960
// cells, so bounded count == bounded RAM).

#include <gtest/gtest.h>

#include <unistd.h>

#include <cstddef>
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
// Add several soundings at one location so CUBE settles a finite estimate (a
// single sample may not resolve) -- one tile's worth of survey.
void surveyTile(GeoMapSheet & sheet, double lat, double lon)
{
  std::vector<GeoSounding> soundings;
  for (int rep = 0; rep < 8; ++rep) {
    gz4d::GeoPointLatLongDegrees point(lat, lon, -10.0 - rep * 0.01);
    GeoSounding s(point);
    s.sounding.vertical_error = 0.5f;
    s.sounding.horizontal_error = 0.1f;
    soundings.push_back(s);
  }
  sheet.addSoundings(soundings);
}

// Persist one tile as the node's saveDirtyTiles() / eviction does, returning
// true if a tile file was written.
bool persistTile(
  GeoMapSheet & sheet, const gggs::GridIndex & index,
  const std::string & draft_dir)
{
  auto grid = sheet.gridAt(index);
  if (!grid) {
    return false;
  }
  marine_bathymetry_store::BathymetryTile tile = geoGridToTile(*grid);
  if (!tile.dirty()) {
    return false;
  }
  marine_bathymetry_store::saveTile(
    tile, draft_dir + "/" + marine_bathymetry_store::tileFilename(index));
  return true;
}

std::string makeTempDir(const std::string & tag)
{
  const auto base = std::filesystem::temp_directory_path() /
    ("cube_evict_" + tag + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base);
  return base.string();
}

std::size_t countValueTiles(const std::string & draft_dir)
{
  std::size_t n = 0;
  if (!std::filesystem::exists(draft_dir)) {
    return 0;
  }
  for (const auto & e : std::filesystem::directory_iterator(draft_dir)) {
    const std::string name = e.path().filename().string();
    // Value tiles are "<level>_<row>_<col>.tif"; skip the _time/_source companions.
    if (e.path().extension() == ".tif" &&
      name.find("_time.tif") == std::string::npos &&
      name.find("_source.tif") == std::string::npos)
    {
      ++n;
    }
  }
  return n;
}
}  // namespace

// A long track over many tiles, evicted to a fixed budget, keeps the resident
// count bounded while the on-disk store grows past the budget.
TEST(TileEvictionRss, ResidentCountBoundedWhileDiskGrows)
{
  const std::string dir = makeTempDir("rss");
  const std::string draft_dir =
    dir + "/" +
    marine_bathymetry_store::layerDirName(
    marine_bathymetry_store::SourceLayer::Draft);
  std::filesystem::create_directories(draft_dir);

  GeoMapSheet sheet(1.0f);
  const gggs::Level level = gggs::Level::fromCellSize(1.0f);
  const std::size_t budget = 10;
  const int n_tiles = 60;  // >> budget

  std::set<gggs::GridIndex> evicted;
  for (int i = 0; i < n_tiles; ++i) {
    // 0.02 deg (~2.2 km) apart -- each well beyond a single ~960 m grid span, so
    // every step lands on a fresh tile.
    surveyTile(sheet, 43.0 + 0.02 * i, -70.0);

    // Node-style persist-then-drop eviction (#70): flush + drop the coldest.
    for (const auto & idx : sheet.coldTiles(budget)) {
      persistTile(sheet, idx, draft_dir);  // lossless: on disk before drop
      sheet.dropTile(idx);
      evicted.insert(idx);
    }
    // The invariant under test: resident RAM never exceeds the budget.
    EXPECT_LE(sheet.residentTileCount(), budget)
      << "resident tile count exceeded the budget at step " << i;
  }

  EXPECT_LE(sheet.residentTileCount(), budget);
  EXPECT_GT(evicted.size(), budget) << "the track should have evicted many tiles";
  EXPECT_GT(countValueTiles(draft_dir), budget)
    << "the durable store must keep growing past the resident budget";

  std::filesystem::remove_all(dir);
}

}  // namespace cube
