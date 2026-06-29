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


#ifndef CUBE_BATHYMETRY__STORE_IMPORT_H_
#define CUBE_BATHYMETRY__STORE_IMPORT_H_

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cube_bathymetry/geo_grid.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/bathy_cell.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_mbes_backscatter_store/mbes_cell.hpp"
#include "marine_mbes_backscatter_store/registry.hpp"

namespace cube
{

/// @brief Design note (issue #21) — live CUBE draft-tile persistence.
///
/// The live `cube_bathymetry_node` accumulates into a geographic
/// @ref GeoMapSheet (migrated from the Cartesian `MapSheet`, #21) so that
/// `geoGridToTile` / `mapSheetToTiles` persist live data directly into the
/// `marine_bathymetry_store` `draft/` layer tiles via `tile_io` (atomic
/// temp-then-rename) with NO lossy Cartesian→geographic resample. The single
/// fused `draft` grid (no per-day epochs, unh_marine_autonomy#221) accumulates
/// newest-value-wins per cell. The costmap `bathymetry_layer` (#164) and the sim
/// live loop (#77) then read exactly what CUBE writes.
///
/// Restart recovery primes `Node::setPredictedDepth` from the loaded draft depth
/// (warm-start for slope correction) -- it does NOT reconstruct CUBE hypothesis,
/// queue, or pre-filter state (full Node deserialization is out of scope). CUBE
/// continues accumulating from scratch on top of the seeded prediction surface.

/// @brief Convert one CUBE @ref GeoGrid into a marine_bathymetry_store tile.
///
/// `GeoGrid::values()` is a flat, positional `vector<DepthAndUncertainty>` in
/// `gggs::CellAreaIterator` order over `grid.index()` (row 0 = south, column 0 =
/// west, row-major; NaN-filled where no estimate exists). `BathymetryTile::set`
/// uses the **same** GGGS cell order (`TiledRasterTile::offset(row,col)` is
/// row-major, row 0 = south), so we walk a second `CellAreaIterator` over the
/// same index in lockstep with `values()` and read `(*it).row()/.column()` to
/// recover each value's destination cell. Only **finite-depth** cells are
/// written; NaN cells are skipped (the tile defaults to NaN no-data).
///
/// `values()` returns `float`; the store cell is `double`, so each field is
/// widened. `values()` mutates node state (it flushes the median pre-filter), so
/// it is called exactly **once** per grid here.
///
/// @param grid          The CUBE grid to convert.
/// @param timestamp_ns  Acquisition/import time written into every finite cell
///                      (nanoseconds since the Unix epoch). A single value per
///                      import keeps the result deterministic.
/// @param source_index  Registry source index written into every finite cell.
/// @return A `BathymetryTile` for `grid.index()` holding the finite cells.
  marine_bathymetry_store::BathymetryTile geoGridToTile(
    const GeoGrid & grid, int64_t timestamp_ns, uint16_t source_index);

/// @brief Convert every grid of a @ref GeoMapSheet into a per-grid tile map.
///
/// Iterates `map_sheet.grids()` and converts each through @ref geoGridToTile.
/// A grid that produces **no** finite cells is omitted (an empty tile would
/// just persist as an all-no-data file). The result is keyed by
/// `gggs::GridIndex` and is suitable to pass straight to
/// `marine_bathymetry_store::BathymetryStore::importTiles`.
///
/// Deterministic for a fixed map sheet: `grids()` returns grids in
/// `gggs::GridIndex` map order, and each grid converts deterministically.
///
/// @param map_sheet     The CUBE map sheet to convert.
/// @param timestamp_ns  Acquisition/import time for every finite cell.
/// @param source_index  Registry source index for every finite cell.
  std::map < gggs::GridIndex, marine_bathymetry_store::BathymetryTile >
  mapSheetToTiles(
    const GeoMapSheet & map_sheet, int64_t timestamp_ns, uint16_t source_index);

/// @brief Convert one CUBE @ref GeoGrid into its finite-backscatter cells (#80).
///
/// The offline backscatter counterpart to @ref geoGridToTile. Walks
/// `grid.nodeRecords()` (the enriched per-node output, ADR-0007 D5) in lockstep
/// with a `gggs::CellAreaIterator` over `grid.index()` — the same positional
/// scheme @ref geoGridToTile uses for the bathy tile — and emits one
/// `marine_mbes_backscatter_store::MbesCell` per cell whose co-estimated
/// `intensity` is finite. Cells with NaN intensity (no intensity-bearing beams)
/// are skipped, mirroring the NaN-depth skip in the bathy path.
///
/// The returned map is keyed by `gggs::CellIndex` so the caller can write each
/// cell with the store's public `set()` (no bulk import API is added to the
/// separate `marine_mbes_backscatter_store` package; #80 stays within
/// `cube_bathymetry`). `intensity_var` (NaN with < 2 samples) maps to the cell's
/// `intensity_variance` quality band (ADR-0007 D6). `timestamp_ns`/`source_index`
/// are stamped into every emitted cell, exactly as the bathy path does — so the
/// Processed product carries provenance, not timestamp=0/source_index=0.
///
/// `nodeRecords()` flushes the median pre-filter, so it is called once per grid.
///
/// @param grid          The CUBE grid to convert.
/// @param timestamp_ns  Acquisition/import time written into every emitted cell.
/// @param source_index  Registry source index written into every emitted cell.
/// @return A `gggs::CellIndex -> MbesCell` map of the finite-intensity cells.
  std::map < gggs::CellIndex, marine_mbes_backscatter_store::MbesCell >
  geoGridToBackscatterCells(
    const GeoGrid & grid, int64_t timestamp_ns, uint16_t source_index);

/// @brief Convert every grid of a @ref GeoMapSheet into one backscatter-cell map.
///
/// Iterates `map_sheet.grids()` and merges each grid's
/// @ref geoGridToBackscatterCells result. Grids cover disjoint GGGS cells, so the
/// merge never collides. The caller writes the cells into a
/// `marine_mbes_backscatter_store::MbesBackscatterStore` via `set()` (Processed
/// layer, #80). Deterministic for a fixed map sheet.
///
/// @param map_sheet     The CUBE map sheet to convert.
/// @param timestamp_ns  Acquisition/import time for every emitted cell.
/// @param source_index  Registry source index for every emitted cell.
  std::map < gggs::CellIndex, marine_mbes_backscatter_store::MbesCell >
  mapSheetToBackscatterCells(
    const GeoMapSheet & map_sheet, int64_t timestamp_ns, uint16_t source_index);

/// @brief Seed predicted depths in @p map_sheet from every finite cell of @p tile.
///
/// For each finite-depth cell of @p tile, finds or creates the matching
/// GeoGrid/Node in @p map_sheet and calls `Node::setPredictedDepth` via
/// `GeoMapSheet::setPredictedDepthAt`. The seeded variance is the stored 1-sigma
/// uncertainty squared, floored at a small positive epsilon (Node::setPredictedDepth
/// requires a finite positive variance; a single-sample CUBE cell can persist a
/// zero or non-finite uncertainty, so every finite-depth cell is still seeded).
/// Does NOT mark the sheet dirty.
///
/// @param seed_settled When true (default), ALSO reseed each cell's SETTLED depth
///   as a CUBE hypothesis (`setSettledDepthAt`) so the cell round-trips through
///   `values()` and survives the next whole-tile save -- the lossless warm-start
///   reload of a persisted draft tile (ADR-0001, #21/#70). When false, seed ONLY
///   the predicted surface: the cell carries a **blunder-rejection prior** (it
///   turns on `Node::insert`'s predicted-surface gate so a false-deep sounding
///   below `target - blunder_scalar*sqrt(var)` is rejected) but does NOT
///   fill/settle the cell -- no hypothesis, no `values()` output, the sheet stays
///   clean. This is the `Chart` (contour) prior path (cube#89): settling coarse
///   contour depths would contaminate the survey layer (and its co-estimated
///   backscatter) with non-measured fill, so the prior only gates, it does not
///   fill; survey-falls-through-to-chart gap-filling stays a query-time concern.
  void primeFromTile(
    const marine_bathymetry_store::BathymetryTile & tile, GeoMapSheet & map_sheet,
    bool seed_settled = true);

/// @brief Load every tile of @p layer from @p store into @p map_sheet,
///        priming predicted depths cell-by-cell.
///
/// Iterates the layer's single fused tile set (unh_marine_autonomy#221, no
/// per-day epochs) and calls @ref primeFromTile on each. Only finite-depth cells
/// are seeded. Warm-starts slope correction from persisted draft tiles on
/// restart; does not reconstruct CUBE hypothesis state (#21). A no-op if @p layer
/// holds no tiles.
///
/// @param seed_settled Forwarded to @ref primeFromTile (default true =
///   settled+predicted warm-start reload; false = predicted-only blunder-rejection
///   prior, e.g. priming the predicted surface from a `Chart` layer, cube#89).
  void loadIntoSheet(
    const marine_bathymetry_store::BathymetryStore & store,
    marine_bathymetry_store::SourceLayer layer,
    GeoMapSheet & map_sheet,
    bool seed_settled = true);

/// @brief Configuration for @ref ImportAccumulator (cube_bathymetry#92).
  struct ImportAccumulatorConfig
  {
  /// Output bathymetry-store directory (the importer's `-o`). Evicted and
  /// final-resident bathy tiles are written here under the @ref bathy_layer
  /// subdirectory; a revisited tile is reloaded from here. Empty = no persistence
  /// (eviction is then disabled to stay lossless — there is nowhere to drop to).
    std::string store_dir;
  /// Bathy store layer the import writes (`Processed` default, `Draft` opt-in, #85).
    marine_bathymetry_store::SourceLayer bathy_layer =
      marine_bathymetry_store::SourceLayer::Processed;
  /// Registry source index stamped into every persisted bathy cell.
    uint16_t source_index = 0;
  /// Deterministic per-cell acquisition timestamp (ns since the Unix epoch).
    int64_t timestamp_ns = 0;
  /// Survey nominal cell size (m); fixes the GGGS level of the scratch stores used
  /// to reload an evicted tile and to merge backscatter. Must equal the
  /// GeoMapSheet's `nominalCellSizeMeters()` so the levels line up.
    float cell_size_m = 1.0f;
  /// Optional MBES backscatter store directory (the importer's `--bs-store`).
  /// Empty disables backscatter co-persistence.
    std::string bs_store_dir;
  /// Registry source index stamped into every persisted backscatter cell.
    uint16_t bs_source_index = 0;
  /// Maximum resident GeoGrid tiles before persist-then-drop eviction runs.
  /// 0 = unbounded (never evict — the pre-#92 whole-survey-in-RAM behavior).
    std::size_t max_resident_tiles = 0;
  };

/// @brief Bounded-RAM offline import accumulator (cube_bathymetry#92).
///
/// Ports the live node's cube#70 persist-then-drop eviction + lossless
/// reload-on-revisit (ADR-0001) to the offline `import_bag` path, so resident RAM
/// is bounded by tile COUNT rather than surveyed AREA (a multi-day survey used to
/// OOM the importer because `grids_` grew with coverage). The host feeds one
/// batch of soundings per call (one ping in `import_bag`, a synthetic region in
/// tests); the accumulator drives the underlying @ref GeoMapSheet and persists to
/// the configured stores.
///
/// **Persist-then-drop (lossless):** when the resident tile count exceeds the
/// budget, each cold tile is written to disk — bathy via @ref geoGridToTile +
/// `saveTile`, and (if a `--bs-store` is configured) backscatter via a
/// newest-finite-wins merge — BEFORE it is dropped from RAM. A tile whose persist
/// throws is left resident (never dropped), so a disk failure costs transient RAM,
/// never data.
///
/// **Reload-on-revisit (settled-output reload, NOT full estimator state):** before
/// a batch's soundings that land on a previously-evicted tile are next persisted,
/// the tile's SETTLED 2-band output (depth + uncertainty) is reloaded from the
/// `-o` store (`loadWindow` + @ref primeFromTile, seed_settled), so a resurveyed
/// area is refined, not overwritten with an empty grid. Keyed off the sheet's
/// dirty set (not the sounding centres) so a neighbour tile a batch spills into
/// near a GGGS seam is not missed (cube#70 review). This is faithful for a
/// SETTLED/converged tile (the eviction first flushes the median pre-filter queue,
/// so no pending samples are lost), but only an APPROXIMATION for a tile evicted
/// mid-disambiguation: CUBE's competing depth hypotheses and exact sample counts
/// are NOT persisted, so the reload reconstructs a SINGLE hypothesis (a Bayesian
/// prior from the stored depth + variance, ADR-0001's narrow sense), and the
/// re-derived uncertainty drifts slightly from the unbounded build. Depth is
/// preserved; the uncertainty drift is a hypothesis-state collapse, not data loss.
///
/// **Backscatter caveat (newest-finite-wins, not Welford-merged):**
/// @ref primeFromTile restores depth but NOT the co-estimated intensity, so an
/// evicted tile's intensity must be persisted at eviction and merged on disk:
/// the new pass's finite cells overwrite, every other on-disk cell is preserved
/// (@ref geoGridToBackscatterCells emits only finite cells, so a NaN cell never
/// clobbers a stored finite one). A cell surveyed across an eviction boundary
/// therefore keeps the *newest* visit's intensity estimate rather than a Welford
/// blend of both visits — the documented divergence from an unbounded build
/// (ADR-0007 D7 newest-valid-wins). For a cell surveyed only once (or with
/// identical samples each visit) the result matches the unbounded build.
  class ImportAccumulator
  {
public:
  /// @param sheet  The map sheet to accumulate into (lifetime must outlast this).
  /// @param config Persistence + budget configuration.
    ImportAccumulator(GeoMapSheet & sheet, ImportAccumulatorConfig config);

  /// @brief Accumulate one batch, then reload-on-revisit + evict to budget.
    void addBatch(
      const std::vector < GeoSounding > &soundings,
      std::chrono::steady_clock::time_point time =
      std::chrono::steady_clock::now());

  /// @brief Persist every still-resident tile (bathy + backscatter) and write
  ///        both registries. Evicted tiles are already durable. Call once at
  ///        end-of-stream.
    void finalize(
      const marine_bathymetry_store::SourceRegistry & bathy_registry,
      const marine_mbes_backscatter_store::SourceRegistry * bs_registry = nullptr);

  /// @brief Tiles currently resident in RAM.
    std::size_t residentTileCount() const;
  /// @brief Indices evicted to disk and not yet reloaded.
    const std::set < gggs::GridIndex > & evictedIndices() const {
      return evicted_;
    }
  /// @brief Cumulative bathy tile writes (eviction + finalize).
    std::size_t bathyTilesPersisted() const {return bathy_persisted_;}
  /// @brief Cumulative backscatter tile writes (eviction + finalize).
    std::size_t backscatterTilesPersisted() const {return bs_persisted_;}

private:
    void evictColdTiles();
    void persistBathyTile(const gggs::GridIndex & index);
    void persistBackscatterTile(const gggs::GridIndex & index);
    bool reloadEvictedTile(const gggs::GridIndex & index);

    GeoMapSheet & sheet_;
    ImportAccumulatorConfig cfg_;
    std::set < gggs::GridIndex > evicted_;
    std::size_t bathy_persisted_ = 0;
    std::size_t bs_persisted_ = 0;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__STORE_IMPORT_H_
