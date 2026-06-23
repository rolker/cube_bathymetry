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

/// @file
/// @brief `import_chart_prior` — convert the legacy raw contour-prior GeoTIFF
/// tiles (depth below the lake surface) into ellipsoidal Chart tiles that
/// `marine_bathymetry_store` can load (cube_bathymetry #44, closing
/// rolker/unh_marine_autonomy#163).
///
/// The raw tiles are the **pre-#178 single-file 3-band Float64** format
/// (band 1 = depth below surface negative-down, band 2 = uncertainty, band 3 =
/// legacy Float64-seconds timestamp), 960×960, GGGS-tiled, named
/// `<level>_<row>_<col>.tif`. Post-#178 the store's `loadTile` *rejects* that
/// format, so we read each tile with GDAL directly, convert per cell via the
/// pure `chartCellFromContour` core, and write through `BathymetryStore::set`
/// into the read-only `Chart` layer (the store is constructed
/// `chart_writable`). The datum offset is resolved **once per tile** via
/// `mru_transform::resolve_datum` (constant within the lake polygon) and added
/// at import (ADR-0002 §D7). The Chart prior is a single read-only source, so
/// cells are written with `source_index = 0` and `save` is passed no
/// `SourceRegistry`.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "gdal_priv.h"
#include "ogr_spatialref.h"

#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/epoch.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "mru_transform/datum_config.hpp"

#include "chart_import.hpp"

namespace
{

void usage()
{
  std::cout
    << "usage: import_chart_prior --contour-dir <dir> --store <out dir> "
    << "--datum-config <yaml> [--level 11]\n"
    << "  --contour-dir   directory of legacy 3-band Float64 contour tiles\n"
    << "                  (named <level>_<row>_<col>.tif), in a <date>/ subdir\n"
    << "                  whose name becomes the Chart epoch\n"
    << "  --store         output store directory (chart/<date>/ tiles written)\n"
    << "  --datum-config  polygon->datum YAML (mru_transform::load_datum_config)\n"
    << "  --level         GGGS level of the contour tiles (default 11)\n";
}

/// RAII wrapper so an early return still closes the GDAL dataset.
struct GdalDataset
{
  GDALDataset * ds = nullptr;
  ~GdalDataset()
  {
    if (ds != nullptr) {
      GDALClose(static_cast<GDALDatasetH>(ds));
    }
  }
};

}  // namespace

int main(int argc, char * argv[])
{
  std::vector<std::string> arguments(argv + 1, argv + argc);

  std::string contour_dir;
  std::string store_dir;
  std::string datum_config_path;
  int level = 11;

  for (auto arg = arguments.begin(); arg != arguments.end(); ++arg) {
    if (*arg == "-h" || *arg == "--help") {
      usage();
      return 0;
    } else if (*arg == "--contour-dir") {
      if (++arg == arguments.end()) {usage(); return 1;}
      contour_dir = *arg;
    } else if (*arg == "--store") {
      if (++arg == arguments.end()) {usage(); return 1;}
      store_dir = *arg;
    } else if (*arg == "--datum-config") {
      if (++arg == arguments.end()) {usage(); return 1;}
      datum_config_path = *arg;
    } else if (*arg == "--level") {
      if (++arg == arguments.end()) {usage(); return 1;}
      level = std::stoi(*arg);
    } else {
      std::cerr << "unknown argument: " << *arg << "\n";
      usage();
      return 1;
    }
  }

  if (contour_dir.empty() || store_dir.empty() || datum_config_path.empty()) {
    usage();
    return 1;
  }
  if (level < 0 || level > 20) {
    std::cerr << "level " << level << " out of GGGS range [0, 20]\n";
    return 1;
  }

  namespace fs = std::filesystem;
  if (!fs::is_directory(contour_dir)) {
    std::cerr << "contour-dir is not a directory: " << contour_dir << "\n";
    return 1;
  }

  // Epoch = the contour directory's own name (e.g. "2026-06-12"), so the
  // on-disk chart/<date>/ layout is visible to downstream loaders.
  const std::string epoch =
    fs::path(fs::path(contour_dir).has_filename() ? contour_dir : contour_dir + "/..")
    .lexically_normal().filename().string();
  try {
    marine_bathymetry_store::validateEpochLabel(epoch);
  } catch (const std::exception & e) {
    std::cerr << "contour-dir name is not a usable epoch label (\"" << epoch
              << "\"): " << e.what() << "\n";
    return 1;
  }

  std::vector<mru_transform::DatumEntry> datum_entries;
  try {
    datum_entries = mru_transform::load_datum_config(datum_config_path);
  } catch (const std::exception & e) {
    std::cerr << "failed to load datum config \"" << datum_config_path
              << "\": " << e.what() << "\n";
    return 1;
  }

  GDALAllRegister();

  const gggs::Level gggs_level(static_cast<uint8_t>(level));
  // chart_writable: this importer is the one tool allowed to write the
  // read-only Chart prior (per-cell set()).
  marine_bathymetry_store::BathymetryStore store(
    static_cast<uint8_t>(level), /*chart_writable=*/true);

  std::cout << "import_chart_prior\n"
            << "  contour-dir:  " << contour_dir << "\n"
            << "  store:        " << store_dir << "\n"
            << "  datum-config: " << datum_config_path << "\n"
            << "  level:        " << level << "\n"
            << "  epoch:        " << epoch << "\n";

  std::size_t tiles_seen = 0;
  std::size_t tiles_imported = 0;
  std::size_t tiles_skipped_outside = 0;
  std::size_t tiles_skipped_bad = 0;
  std::size_t cells_written = 0;

  const int edge = static_cast<int>(gggs::cell_rows_per_grid);   // 960

  for (const auto & entry : fs::directory_iterator(contour_dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".tif") {
      continue;
    }
    const std::string path = entry.path().string();
    const std::string filename = entry.path().filename().string();

    // The filename's level prefix must match the requested level; mixing
    // levels in one contour dir is not expected for this single-level prior.
    uint8_t file_level = 0;
    try {
      file_level = marine_bathymetry_store::levelFromTileFilename(filename);
    } catch (const std::exception & e) {
      std::cerr << "  skip (unparseable filename): " << filename << " — "
                << e.what() << "\n";
      ++tiles_skipped_bad;
      continue;
    }
    if (file_level != static_cast<uint8_t>(level)) {
      std::cerr << "  skip (level " << static_cast<int>(file_level)
                << " != requested " << level << "): " << filename << "\n";
      ++tiles_skipped_bad;
      continue;
    }
    ++tiles_seen;

    GdalDataset src;
    src.ds = static_cast<GDALDataset *>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (src.ds == nullptr) {
      std::cerr << "  skip (GDAL could not open): " << filename << "\n";
      ++tiles_skipped_bad;
      continue;
    }

    const int width = src.ds->GetRasterXSize();
    const int height = src.ds->GetRasterYSize();
    const int bands = src.ds->GetRasterCount();
    if (width != edge || height != edge || bands < 3) {
      std::cerr << "  skip (unexpected layout " << width << "x" << height
                << " bands=" << bands << ", expected " << edge << "x" << edge
                << " 3-band): " << filename << "\n";
      ++tiles_skipped_bad;
      continue;
    }

    double geo_transform[6];
    if (src.ds->GetGeoTransform(geo_transform) != CE_None) {
      std::cerr << "  skip (no geotransform): " << filename << "\n";
      ++tiles_skipped_bad;
      continue;
    }
    const double west = geo_transform[0];
    const double pixel_x = geo_transform[1];
    const double north = geo_transform[3];
    const double pixel_y = geo_transform[5];   // negative: north-up

    // Tile-center lat/lon — used both to reconstruct the GridIndex (the
    // GridIndex private ctor is friend-only, so we round-trip through
    // Level::gridIndex like the store's gridIndexFromTileFilename does) and to
    // resolve the datum once per tile (constant inside the lake polygon).
    const double center_lon = west + pixel_x * (edge / 2.0);
    const double center_lat = north + pixel_y * (edge / 2.0);

    const std::optional<mru_transform::DatumResult> datum =
      mru_transform::resolve_datum(
      center_lat, center_lon,
      /*lake_datum=*/std::nullopt, /*lake_datum_mhhw=*/std::nullopt,
      /*vdatum=*/std::nullopt, datum_entries);
    if (!datum.has_value()) {
      std::cerr << "  skip (tile center " << center_lat << ", " << center_lon
                << " outside every datum polygon): " << filename << "\n";
      ++tiles_skipped_outside;
      continue;
    }
    const double chart_datum_z = datum->chart_datum_z;

    const gggs::GridIndex grid = gggs_level.gridIndex(center_lat, center_lon);

    GDALRasterBand * band_depth = src.ds->GetRasterBand(1);
    GDALRasterBand * band_uncert = src.ds->GetRasterBand(2);
    GDALRasterBand * band_time = src.ds->GetRasterBand(3);

    // Read row by row. GDAL row 0 = north (top); the store's GGGS cell order is
    // row 0 = south. So GGGS cell row r maps to GDAL raster row (edge-1-r) —
    // the exact inverse of marine_tiled_raster_store::saveTile's flipRows.
    // Columns match directly (both west-to-east).
    std::vector<double> depth_row(edge);
    std::vector<double> uncert_row(edge);
    std::vector<double> time_row(edge);
    std::size_t tile_cells = 0;

    for (int gdal_row = 0; gdal_row < edge; ++gdal_row) {
      if (band_depth->RasterIO(
          GF_Read, 0, gdal_row, edge, 1, depth_row.data(), edge, 1,
          GDT_Float64, 0, 0) != CE_None ||
        band_uncert->RasterIO(
          GF_Read, 0, gdal_row, edge, 1, uncert_row.data(), edge, 1,
          GDT_Float64, 0, 0) != CE_None ||
        band_time->RasterIO(
          GF_Read, 0, gdal_row, edge, 1, time_row.data(), edge, 1,
          GDT_Float64, 0, 0) != CE_None)
      {
        std::cerr << "  skip (RasterIO read error at row " << gdal_row << "): "
                  << filename << "\n";
        ++tiles_skipped_bad;
        tile_cells = 0;
        break;
      }

      const int gggs_row = edge - 1 - gdal_row;
      for (int col = 0; col < edge; ++col) {
        const marine_bathymetry_store::BathyCell cell =
          cube_bathymetry::chartCellFromContour(
          depth_row[col], uncert_row[col], time_row[col], chart_datum_z);
        if (!cell.hasData()) {
          continue;   // band-1 NaN no-data — nothing to write
        }
        const gggs::CellIndex cell_index(
          grid, static_cast<uint16_t>(gggs_row), static_cast<uint16_t>(col));
        if (store.set(
            marine_bathymetry_store::SourceLayer::Chart, epoch, cell_index, cell))
        {
          ++tile_cells;
        }
      }
    }

    if (tile_cells > 0) {
      ++tiles_imported;
      cells_written += tile_cells;
    }
  }

  std::cout << "writing store to " << store_dir << " ...\n";
  std::size_t tiles_written = 0;
  try {
    // No SourceRegistry: the Chart prior is a single read-only source
    // (source_index 0), distinct from the multi-source importGeoTiff path.
    tiles_written = marine_bathymetry_store::save(store, store_dir, nullptr);
  } catch (const std::exception & e) {
    std::cerr << "failed to write store: " << e.what() << "\n";
    return 1;
  }

  std::cout << "done.\n"
            << "  tiles seen:             " << tiles_seen << "\n"
            << "  tiles imported:         " << tiles_imported << "\n"
            << "  tiles skipped (outside polygon): " << tiles_skipped_outside << "\n"
            << "  tiles skipped (bad/unreadable):  " << tiles_skipped_bad << "\n"
            << "  cells written:          " << cells_written << "\n"
            << "  tile files written:     " << tiles_written << "\n";

  return 0;
}
