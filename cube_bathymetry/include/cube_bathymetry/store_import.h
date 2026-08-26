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
#include <utility>
#include <vector>

#include "cube_bathymetry/geo_grid.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/hypothesis.h"
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
/// `marine_bathymetry_store` `draft/` layer tiles via `tile_io` (a direct,
/// flush/close-checked write — not crash-atomic) with NO lossy Cartesian→geographic
/// resample. The single
/// fused `draft` grid (no per-day epochs, unh_marine_autonomy#221) accumulates
/// newest-value-wins per cell. The costmap `bathymetry_layer` (#164) and the sim
/// live loop (#77) then read exactly what CUBE writes.
///
/// Restart recovery primes `Node::setPredictedDepth` from the loaded draft depth
/// (warm-start for slope correction) -- it does NOT reconstruct CUBE hypothesis,
/// queue, or pre-filter state (full Node deserialization is out of scope). CUBE
/// continues accumulating from scratch on top of the seeded prediction surface.

/// @brief Confidence-interval scale applied to the backscatter standard error on
///        write and divided back out on reconstruction (uma#248 A.1).
///
/// Mirrors the bathy store's `stddev_to_confidence_interval_scale` convention so
/// the stored `MbesCell::standard_error` is a confidence-scaled quantity, exactly
/// as the bathy uncertainty band is. A single shared constant on both the write
/// (@ref geoGridToBackscatterCells) and reconstruct (@ref welfordFromCell) sides
/// makes the Welford round-trip self-consistent regardless of its numeric value.
  inline constexpr float kBackscatterConfidenceScale = 1.96f;

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
/// it is called exactly **once** per grid here. The pre-#248 per-cell
/// `timestamp`/`source_index` bands were dropped (`BathyCell` is now 2-band
/// `{depth, uncertainty}`); coarse provenance lives in the store-level
/// `StoreMetadata` sidecar instead.
///
/// @param grid          The CUBE grid to convert.
/// @return A `BathymetryTile` for `grid.index()` holding the finite cells.
  marine_bathymetry_store::BathymetryTile geoGridToTile(const GeoGrid & grid);

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
  std::map < gggs::GridIndex, marine_bathymetry_store::BathymetryTile >
  mapSheetToTiles(const GeoMapSheet & map_sheet);

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
/// `cube_bathymetry`).
///
/// Each `NodeRecord {intensity (mean), intensity_var (variance of the mean),
/// n_samples}` is encoded into the 3-band `MbesCell {mean, standard_error,
/// sample_sd}` Welford sufficient statistic (uma#248 A.1):
/// - **n = 1 sentinel** (`intensity_var` is NaN, a single beam): `mean =
///   intensity`, `standard_error = 0`, `sample_sd = 0` (no dispersion; a finite
///   mean with `sample_sd == 0` reconstructs to `n = 1`).
/// - **n ≥ 2**: `sample_sd = sqrt(intensity_var * n_samples)` (the sample stddev
///   of the beams) and `standard_error = kBackscatterConfidenceScale *
///   sqrt(intensity_var)` (the confidence-scaled standard error of the mean).
///
/// @ref welfordFromCell is the exact inverse, so the estimate round-trips
/// losslessly through the store on an off-boat re-run. `nodeRecords()` flushes the
/// median pre-filter, so it is called once per grid.
///
/// @param grid          The CUBE grid to convert.
/// @return A `gggs::CellIndex -> MbesCell` map of the finite-intensity cells.
  std::map < gggs::CellIndex, marine_mbes_backscatter_store::MbesCell >
  geoGridToBackscatterCells(const GeoGrid & grid);

/// @brief Convert every grid of a @ref GeoMapSheet into one backscatter-cell map.
///
/// Iterates `map_sheet.grids()` and merges each grid's
/// @ref geoGridToBackscatterCells result. Grids cover disjoint GGGS cells, so the
/// merge never collides. The caller writes the cells into a
/// `marine_mbes_backscatter_store::MbesBackscatterStore` via `set()` (Survey
/// layer, #80). Deterministic for a fixed map sheet.
///
/// @param map_sheet     The CUBE map sheet to convert.
  std::map < gggs::CellIndex, marine_mbes_backscatter_store::MbesCell >
  mapSheetToBackscatterCells(const GeoMapSheet & map_sheet);

/// @brief Reconstruct a corrected-intensity @ref IntensityWelford from a stored
///        3-band @ref MbesCell — the exact inverse of @ref geoGridToBackscatterCells.
///
/// Used to seed backscatter accumulation from a persisted `survey` backscatter
/// tile on first tile touch (seed precedence, #96) so an off-boat re-run blends
/// with the stored population rather than restarting it. From the confidence-
/// scaled bands (dividing @ref kBackscatterConfidenceScale back out of
/// `standard_error` to recover the true `SE = sample_sd / sqrt(n)`):
/// - `sample_sd == 0 && isfinite(mean)` → `{n = 1, mean, m2 = 0}` (n=1 sentinel).
/// - else `SE = standard_error / kBackscatterConfidenceScale`;
///   `n = round((sample_sd / SE)^2)`; `m2 = sample_sd^2 * (n - 1)`.
///
/// **Limitation (ADR-0007 addendum):** a cell whose n≥2 samples are all *exactly*
/// identical has `M2 = 0` → `sample_sd = 0`, indistinguishable from the n=1
/// sentinel on reload (it collapses to `n = 1`). For continuous corrected-dB data
/// this is astronomically rare (exact float equality across ≥2 beams); the only
/// consequence is a slightly under-counted `n` on a later re-survey blend of that
/// one cell — the mean stays exact. Accepted, not code-guarded.
///
/// @param cell A finite-mean (`hasData()`) backscatter cell. A no-data cell
///             (`mean` NaN) reconstructs to `{n = 0}` (empty Welford).
  IntensityWelford welfordFromCell(
    const marine_mbes_backscatter_store::MbesCell & cell);

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
///   clean. This is the `Reference` (contour/prior) path (cube#89, #96): settling
///   coarse prior depths would contaminate the survey layer (and its co-estimated
///   backscatter) with non-measured fill, so the prior only gates, it does not
///   fill; survey-falls-through-to-reference gap-filling stays a query-time concern.
///
/// @return the number of cells actually primed. Zero is a real answer: an all-NaN
///   (no-data-here) tile matches its GridIndex and primes nothing, so a caller
///   asking "is the blunder gate on for this tile?" must key on this count, never
///   on the fact that a tile was found (#137).
  std::size_t primeFromTile(
    const marine_bathymetry_store::BathymetryTile & tile, GeoMapSheet & map_sheet,
    bool seed_settled = true);

/// @brief Prime @p tile into @p map_sheet, SKIPPING every cell that @p mask
///        already covers with a finite depth (per-cell overlap resolution).
///
/// The deterministic core of the fused Processed-over-Draft prime (ADR-0010 D8):
/// @p mask, when non-null, is an overlapping tile at the SAME GridIndex (the
/// Processed tile shadowing @p tile's Draft data). Because both tiles share the
/// grid geometry and cell size, their bands index cell-for-cell by the same `k`,
/// so a cell the mask covers is left untouched and the fused sheet keeps ONLY the
/// mask's hypothesis there -- Processed-wins-on-overlap regardless of CUBE's
/// chooseHypothesis tie-break, matching the store's query-side `Processed > Draft`
/// walk. A null @p mask primes every finite-depth cell (equivalent to
/// @ref primeFromTile). @p seed_settled forwards to the same warm-start vs.
/// predicted-only semantics documented on @ref primeFromTile.
///
/// @return the number of cells actually primed (see @ref primeFromTile).
  std::size_t primeFromTileSkippingMask(
    const marine_bathymetry_store::BathymetryTile & tile,
    const marine_bathymetry_store::BathymetryTile * mask, GeoMapSheet & map_sheet,
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
///   prior, e.g. priming the predicted surface from a `Reference` layer, cube#89).
  void loadIntoSheet(
    const marine_bathymetry_store::BathymetryStore & store,
    marine_bathymetry_store::SourceLayer layer,
    GeoMapSheet & map_sheet,
    bool seed_settled = true);

/// @brief Layer the `Draft` tiles of @p store into @p map_sheet, SKIPPING every
///        cell already covered by the overlapping `Processed` tile.
///
/// The second half of the fused Processed-over-Draft prime (ADR-0010 D8): the
/// caller seeds `Processed` FULLY first (@ref loadIntoSheet with
/// `SourceLayer::Processed`), then calls this to add `Draft` only where `Processed`
/// left a gap. Each Draft tile is primed via @ref primeFromTileSkippingMask with
/// its same-GridIndex Processed tile (if any) as the mask, so an overlapped cell
/// keeps ONLY its Processed hypothesis -- deterministic Processed-wins independent
/// of CUBE's chooseHypothesis tie-break, matching the store's query-side
/// `Processed > Draft` walk. A no-op if the `Draft` layer holds no tiles.
/// @p seed_settled forwards to @ref primeFromTile.
  void loadDraftSkippingProcessed(
    const marine_bathymetry_store::BathymetryStore & store,
    GeoMapSheet & map_sheet,
    bool seed_settled = true);

/// @brief Per-layer tile counts from @ref primeFromPriorLayers.
  struct PriorLayerPrimeResult
  {
  /// Exact-level tiles primed from the `Reference` layer.
    std::size_t reference_tiles = 0;
  /// Exact-level tiles primed from the `Chart` layer.
    std::size_t chart_tiles = 0;
  /// Tiles skipped because they are not at the sheet's survey level (a
  /// multi-level prior store); surfaced so the caller can log the coverage gap.
    std::size_t level_mismatched = 0;
    std::size_t total() const {return reference_tiles + chart_tiles;}
  };

/// @brief Prime @p map_sheet's predicted surface from BOTH prior layers of
///        @p store (predicted-only, seed_settled=false), exact-level tiles only.
///
/// The live-node prior prime (#91) and the offline Chart-gate fix (#119) share
/// the same semantics: `Chart` tiles are primed first (lowest-priority layer),
/// then `Reference` tiles on top, overwriting where the layers overlap — the
/// store's priority ordering. Neither seeds settled hypotheses: the prior gates
/// blunders and drives slope correction (#59) but never fills (cube#89,
/// ADR-0008). Does not mark the sheet dirty.
///
/// EXACT-LEVEL ONLY: a multi-level prior store (the #115 ENC case) holds coarse
/// tiles keyed at their own level, and @ref primeFromTile walks each tile's own
/// cell iterator with no cross-level guard — priming a coarse tile here would
/// seed wrong-geometry cells. Mismatched tiles are counted and skipped;
/// cross-level use needs the resampling prime that the per-tile importer applies
/// via its #115 level-walk fallback (a store-local helper in `store_import.cpp`),
/// which remains that importer's concern.
  PriorLayerPrimeResult primeFromPriorLayers(
    const marine_bathymetry_store::BathymetryStore & store,
    GeoMapSheet & map_sheet);

/// @brief Run-level tally of the prior (`chart/` + `reference/`) predicted-surface
///        prime, backing the silent-no-op guard (#137).
///
/// A prior store that is configured, announced at startup, and never actually
/// consulted leaves the blunder gate OFF for the whole run with nothing in the
/// output to say so — how a multi-level ENC chart prior behaved before Chart gained
/// the cross-level fallback. The tally is what @ref reportPriorPrimeOutcome turns
/// into that one loud line, and it is a plain struct (not private accumulator state)
/// because the authoritative off-boat rebuild path, `batch_regen`, drives ONE
/// @ref ImportAccumulator per tile and so has to merge per-tile tallies and report
/// once for the run.
  struct PriorPrimeTally
  {
  /// Per-tile prime CALLS: first touch plus every evicted-tile revisit, so a
  /// revisited tile counts more than once. Not a distinct-tile count — the warning
  /// says "attempt(s)" for exactly this reason.
    std::size_t attempts = 0;
  /// Attempts that primed at least one CELL. A matched-but-all-NaN prior tile is
  /// NOT a hit: it gates nothing (#137).
    std::size_t hits = 0;
  /// Attempts whose windowed load THREW (unreadable/corrupt/permission-denied
  /// store). Tracked apart from `hits` so "we could not read the prior" is never
  /// reported as "the prior does not cover the survey".
    std::size_t read_failures = 0;
  /// Tiles that warm-started from the output store's own survey layer (rung 1) and
  /// so returned before ever reaching the prior rung. They are NOT attempts, and
  /// the warning must not claim the gate was off for them.
    std::size_t survey_warm_starts = 0;
  /// Every (layer, GGGS level) pair present in the prior windows this run loaded,
  /// whether or not it primed. Reported by the warning so a level mismatch is
  /// visible ("chart@L7 vs survey level 10") instead of left to be inferred.
    std::set < std::pair < marine_bathymetry_store::SourceLayer, int >> layers_seen;
  /// (layer, level) pairs whose cross-level-fallback audit line has already been
  /// logged this run; the line is emitted once per pair, not once per tile.
    std::set < std::pair < marine_bathymetry_store::SourceLayer, int >> audit_seen;

  /// @brief Fold @p other into this tally (batch-regen merges one per gathered tile).
    void merge(const PriorPrimeTally & other);
  };

/// @brief Emit the run-level prior-store WARNING(s) describing how much of the run
///        a configured prior actually gated (#137).
///
/// Up to two lines, each independently gated:
///  - **Read failures** — emitted whenever ANY attempt failed to read the store,
///    *regardless of whether other tiles primed*. An unreadable/corrupt/permission-
///    denied store is a different fault with a different remedy than a coverage gap,
///    and tying it to the zero-hit case hid it completely on a run where a single
///    tile primed (#137 review).
///  - **Coverage** — "primed NOTHING on any of N attempt(s)" when nothing primed at
///    all, or "primed only M of N attempt(s)" when the prior gated part of the run.
///    Partial coverage is the likelier field failure than total non-coverage, so it
///    is reported rather than left to be assumed away. Both name the layer@level
///    pairs the prior windows held, so a level mismatch is visible.
///
/// Both coverage lines scope their claim to the tiles that actually consulted the
/// prior, since a tile warm-started from the output store's `processed/` layer
/// returns before the prior rung.
///
/// @param tally            Run-level tally (see @ref PriorPrimeTally).
/// @param prior_store_dir  The `--reference-store` dir, named in the warning.
/// @param cell_size_m      Survey cell size, reported as the survey GGGS level.
/// @param tool             Prefix for the line (`import_bag` / `batch_regen`).
/// @return true when at least one line was emitted.
  bool reportPriorPrimeOutcome(
    const PriorPrimeTally & tally, const std::string & prior_store_dir,
    float cell_size_m, const char * tool);

/// @brief True when a legacy `survey/` layer dir PERSISTS in @p store_dir — the
///        signature of a permanent, whole-store ADR-0010 D8 migration REFUSAL.
///
/// `load()`/`loadWindow()` auto-migrate a legacy `survey/` to `processed/` by a
/// single rename, but REFUSE (throw) three ways, each leaving `survey/` in place:
/// a **symlinked** `survey/` (renaming it would point `processed/` out of the
/// store), an **ambiguous** store holding BOTH `survey/` and `processed/`, or a
/// rename that **cannot commit** (e.g. a read-only filesystem). A *successful*
/// migration renames `survey/` away, and a store that never had one has none — so
/// "`survey/` still present *after* a load threw" cleanly separates this permanent,
/// every-tile failure from a transient single-tile read error. Callers use it in a
/// load/loadWindow catch to abort LOUDLY (the operator must fix the store by hand)
/// instead of degrading tile-by-tile to a silently near-empty result.
///
/// Keys on the PATH, not the migration's throw message (brittle) nor the both-dirs
/// case alone (misses the symlink variant): `is_directory` follows the link so it
/// catches a real dir or a link to one, and `is_symlink` additionally catches a
/// symlinked `survey/` whose target is missing/not-a-dir. Empty @p store_dir → false.
  bool legacySurveyDirPersists(const std::string & store_dir);

/// @brief Configuration for @ref ImportAccumulator (cube_bathymetry#92, #96).
  struct ImportAccumulatorConfig
  {
  /// Output bathymetry-store directory (the importer's `-o`). Evicted and
  /// final-resident bathy tiles are written here under the `survey/` layer
  /// subdirectory; a revisited tile is reloaded from here, and a first-touched
  /// tile is seeded from any pre-existing `survey/` tile (seed precedence #96).
  /// Empty = no persistence (eviction is then disabled to stay lossless — there
  /// is nowhere to drop to). The store always writes the `survey` layer (#248
  /// collapsed the draft/processed split; the off-boat CUBE re-run is authoritative).
    std::string store_dir;
  /// Optional reference-prior store directory (the importer's `--reference-store`,
  /// replacing the pre-#96 `--prior`). When a tile is first touched and no `survey`
  /// tile exists for it in @ref store_dir, a `reference/` tile here (if present)
  /// seeds the CUBE predicted surface ONLY (blunder-rejection gate, seed_settled=
  /// false): the coarse prior is never settled as measured data and never seeds
  /// backscatter. Empty disables reference seeding.
    std::string reference_store_dir;
  /// Survey nominal cell size (m); fixes the GGGS level of the scratch stores used
  /// to reload an evicted tile, to seed a first-touched tile, and to merge
  /// backscatter. Must equal the GeoMapSheet's `nominalCellSizeMeters()` so the
  /// levels line up.
    float cell_size_m = 1.0f;
  /// Optional MBES backscatter store directory (the importer's `--bs-store`).
  /// Empty disables backscatter co-persistence. Always writes the `survey` layer.
    std::string bs_store_dir;
  /// Maximum resident GeoGrid tiles before persist-then-drop eviction runs.
  /// 0 = unbounded (never evict — the pre-#92 whole-survey-in-RAM behavior).
    std::size_t max_resident_tiles = 0;
  /// When true, @ref ImportAccumulator::seedNewTile SKIPS the rung-1 survey
  /// warm-start (it still applies the rung-2 reference gate). Set by @ref BatchRegen
  /// for its gather: batch-regen replays a tile's COMPLETE sounding population in one
  /// pass, so warm-starting that same tile from a pre-existing `survey/` tile in the
  /// OUTPUT store would double-count it — the gather would blend onto data it is
  /// about to fully reproduce, silently corrupting an exact rebuild. Forcing
  /// from-scratch makes batch-regen a true rebuild regardless of the `-o` store's
  /// prior contents. The live import path leaves this false (its warm-start is the
  /// intended incremental-import behavior).
    bool skip_survey_seed = false;
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
/// `saveTile`, backscatter SUMMARY via `saveTile`, AND its per-cell intensity
/// Welford `(n, mean, M2)` spilled to a temporary scratch — BEFORE it is dropped
/// from RAM. A tile whose persist throws is left resident (never dropped), so a
/// disk failure costs transient RAM, never data.
///
/// **Reload-before-add (lossless blend):** the bathy tile stores only the depth
/// SUMMARY and the backscatter SUMMARY is the corrected mean — neither retains the
/// running Welford CUBE needs to keep blending. So eviction also spills each cell's
/// intensity Welford to a scratch file (cube#93: a 3-number sufficient statistic of
/// the CORRECTED intensity, since correction now runs at record), and the reload
/// runs BEFORE a batch's soundings are added: it computes the grids the batch will
/// touch (@ref GeoMapSheet::gridIndicesForSoundings), `loadWindow` +
/// @ref primeFromTile restores each cell's settled depth as one CUBE hypothesis,
/// then the spilled Welford is restored onto that hypothesis. The batch's new beams
/// then continue the Welford on the SAME reloaded hypothesis, so the node-output
/// intensity is the FULL pre+post-eviction blend — bit-for-bit equal to a
/// never-evicted build for a consistent re-survey (a Welford triplet is a perfect
/// sufficient statistic). **Backscatter is lossless under eviction.**
///
/// **Remaining approximation (bathy uncertainty only):** the reload reconstructs a
/// SINGLE depth hypothesis (a Bayesian prior from the stored depth + variance,
/// ADR-0001), seeded with one sample rather than the original count. The depth
/// VALUE is faithful (the prior is refined by the revisit), but the re-derived
/// depth UNCERTAINTY drifts slightly from the unbounded build for a tile evicted
/// mid-disambiguation — a hypothesis-state collapse, not data loss. The backscatter
/// estimate does not depend on the depth sample count, so it is unaffected.
///
/// **Transient disk cost:** the spill holds a fixed 24-byte Welford record per
/// surveyed cell for every currently-evicted tile (NOT per beam — cube#93 made
/// per-cell intensity state O(1)), in a scratch dir deleted in @ref finalize (and
/// by the destructor on an exception). Scratch-only and local/fast.
  class ImportAccumulator
  {
public:
  /// @param sheet  The map sheet to accumulate into (lifetime must outlast this).
  /// @param config Persistence + budget configuration.
    ImportAccumulator(GeoMapSheet & sheet, ImportAccumulatorConfig config);

  /// @brief Clean up the scratch spill directory (RAII safety net for finalize).
    ~ImportAccumulator();

  /// @brief Reload-before-add any evicted tile this batch touches, accumulate the
  ///        batch, then evict cold tiles back to budget.
    void addBatch(
      const std::vector < GeoSounding > &soundings,
      std::chrono::steady_clock::time_point time =
      std::chrono::steady_clock::now());

  /// @brief Persist every still-resident tile (bathy + backscatter), write both
  ///        store-level metadata sidecars, and delete the scratch spill. Evicted
  ///        tiles are already durable. Call once at end-of-stream.
  ///
  /// @param bathy_metadata Optional store-level `registry.json` provenance for the
  ///   bathy store (uma#248 replaced the per-cell `SourceRegistry` interning table
  ///   with a single coarse `StoreMetadata` at the store root). Written only when
  ///   non-null and not `empty()`.
  /// @param bs_metadata    Optional store-level provenance for the backscatter store.
  ///
  /// ALSO emits the run-level prior-store WARNING (#137,
  /// @ref reportPriorPrimeOutcome) when a configured `--reference-store` primed
  /// nothing for any tile that reached the prior rung — the blunder gate was off
  /// for the whole run despite the startup banner saying otherwise. Emitted
  /// FIRST, before the tile persists, so a persist that throws cannot swallow it,
  /// and once only, however many times this is called. `batch_regen` never calls
  /// this (see @ref persistResidentTile) and reports the same warning itself from
  /// the merged per-tile tallies.
    void finalize(
      const marine_bathymetry_store::StoreMetadata * bathy_metadata = nullptr,
      const marine_mbes_backscatter_store::StoreMetadata * bs_metadata = nullptr);

  /// @brief Persist ONE resident tile's bathy + backscatter, nothing else
  ///        (batch-regen gather, #96).
  ///
  /// Unlike @ref finalize this writes only @p index and no store-level metadata,
  /// so the batch-regen gather can process one tile bucket in a fresh sheet and
  /// write ONLY the target tile — never the neighbour grids a near-seam sounding
  /// also created in that sheet. Runs the same @ref seedNewTile precedence via the
  /// preceding @ref addBatch, so a survey/reference prior is honoured. Increments
  /// the persisted-tile counters.
    void persistResidentTile(const gggs::GridIndex & index);

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
  /// @brief This accumulator's prior-prime tally (#137). Exposed so a driver that
  ///        runs MANY accumulators — `batch_regen`, one per gathered tile — can
  ///        merge them and emit the run-level silent-no-op warning itself, which
  ///        @ref finalize cannot do for it (batch-regen calls
  ///        @ref persistResidentTile, never @ref finalize).
    const PriorPrimeTally & priorPrimeTally() const {return prior_tally_;}
  /// @brief The scratch spill directory (empty until the first eviction). Exposed
  ///        for tests that assert it is cleaned up after @ref finalize.
    const std::string & scratchDir() const {return scratch_dir_;}

private:
    void evictColdTiles();
    void persistBathyTile(const gggs::GridIndex & index);
    void persistBackscatterTile(const gggs::GridIndex & index);
    void spillIntensitySamples(const gggs::GridIndex & index);
    void restoreSpilledSamples(const gggs::GridIndex & index);
  /// Revisit reload: re-primes the reference/chart prior FIRST (#118, so a
  /// prior-gated tile never returns from eviction ungated), then restores the
  /// survey settled state (which overwrites the predicted surface where survey
  /// data exists) and the spilled backscatter samples.
  /// @return true when the resident grid is consistent with disk (reloaded, or
  ///   genuinely absent on disk). false on a survey OR prior read failure: the
  ///   caller must drop the partial grid and keep the tile evicted, so this
  ///   batch's soundings on the tile are dropped and the reload (including the
  ///   prior re-prime) retries on the next revisit.
    bool reloadEvictedTile(const gggs::GridIndex & index);
  /// @brief Seed a tile the batch is touching for the FIRST time (seed precedence
  ///        #96), then mark it @ref seeded_. Two-rung precedence:
  ///        1. survey — a `survey/` bathy tile in @ref store_dir (a pre-existing
  ///           store, or a resurvey of an already-written tile): prime settled
  ///           (`seed_settled=true`) AND restore each cell's backscatter Welford
  ///           via @ref welfordFromCell from the `survey/` backscatter tile.
  ///        2. prior — else a `chart/` or `reference/` tile in
  ///           @ref reference_store_dir: prime predicted-only
  ///           (`seed_settled=false`, blunder gate); NOT counted as measured data,
  ///           NO backscatter seed. `chart/` primes first and `reference/`
  ///           overwrites where both cover a cell; since #137 BOTH layers take an
  ///           exact-survey-level tile when there is one and otherwise fall back
  ///           to resampling the finest CONTAINING coarser tile that holds data
  ///           (the #115 level-walk, which was Reference-only before).
  ///        else blank (no prior). A no-op beyond marking @ref seeded_ when no
  ///        seed source is configured or found.
  /// @return false if the rung-1 survey seed threw (the on-disk survey tile exists
  ///        but could not be read): the caller then drops this tile like a failed
  ///        reload, protecting the intact-but-unreadable surface from being
  ///        overwritten with from-scratch data, and leaves it UNseeded so a later
  ///        batch retries. true otherwise (seeded, or nothing to seed).
    bool seedNewTile(const gggs::GridIndex & index);
    void cleanupScratch();

    GeoMapSheet & sheet_;
    ImportAccumulatorConfig cfg_;
    std::set < gggs::GridIndex > evicted_;
  /// Tiles already seeded (or confirmed blank) on first touch — seedNewTile runs
  /// at most once per tile (seed precedence #96). Distinct from @ref evicted_: an
  /// evicted tile was seeded, so it reloads (from its own written survey tile)
  /// rather than re-seeding from the reference prior.
    std::set < gggs::GridIndex > seeded_;
    std::string scratch_dir_;  // lazily created on first eviction; "" = none
    std::size_t bathy_persisted_ = 0;
    std::size_t bs_persisted_ = 0;
  /// Run-level prior-prime tally (#137), for the @ref finalize silent-no-op
  /// warning. Populated only when a `--reference-store` was configured. All
  /// attempts and no hits means the blunder gate never engaged for any tile that
  /// reached the prior rung — how a multi-level chart prior behaved before Chart
  /// gained the cross-level fallback, and worth one loud line rather than silence.
    PriorPrimeTally prior_tally_;
  /// Guards @ref finalize's warning against a second emission: the tally is never
  /// cleared (batch-regen merges it after the fact), so a second @ref finalize call
  /// would otherwise repeat the line.
    bool prior_outcome_reported_ = false;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__STORE_IMPORT_H_
