# Plan: Adopt simplified cube stores: seed precedence, batch-regen, sufficient-statistic backscatter

## Issue

https://github.com/rolker/cube_bathymetry/issues/96

## Context

uma#248 has landed (jazzy), introducing:
- Two-layer store: `SourceLayer::Survey` + `SourceLayer::Reference` (replacing
  `Draft`/`Processed`/`Chart`).
- 3-band `MbesCell {mean, standard_error, sample_sd}` sufficient statistics
  (replacing `{intensity, intensity_variance, timestamp, source_index}`).
- `layerDirName()` now returns `"survey"` / `"reference"`.

The current `cube_bathymetry` code does not compile against the new library.  
This issue adds three features on top of the migration:

1. **Seed precedence** — lazy per-tile seeding when a tile is first touched
   (`survey` → settled, `reference` → predicted-only, else blank).
2. **Batch-regen** — scatter/gather path: no eviction → bit-exact output.
3. **Backscatter 3-band write/reload** — encode/reconstruct the Welford triplet
   via the new `MbesCell` format.

Greenfield: no backwards-compat required.

## Approach

### Step 1 — SourceLayer migration (compile fix for uma#248)

Update every occurrence of the old enum values and MbesCell fields:

| Old | New |
|-----|-----|
| `SourceLayer::Processed` | `SourceLayer::Survey` |
| `SourceLayer::Draft` | `SourceLayer::Survey` |
| `SourceLayer::Chart` | `SourceLayer::Reference` |
| `MbesCell::intensity` | `MbesCell::mean` |
| `MbesCell::intensity_variance` | computed from `standard_error`/`sample_sd` |

Remove `--bathy-layer draft\|processed` CLI flag (both collapsed to `Survey`).
Remove `timestamp_ns`/`source_index` from all `MbesCell` construction sites.

### Step 2 — Backscatter 3-band write (`geoGridToBackscatterCells`)

Convert `NodeRecord {intensity, intensity_var, n_samples}` → `MbesCell`:

- **n = 1 sentinel** (`intensity_var` is NaN): `{mean=intensity, standard_error=0, sample_sd=0}`.
- **n ≥ 2**: `sample_sd = sqrt(intensity_var * n_samples)`;
  `standard_error = kScale * sqrt(intensity_var)` where `kScale = 1.96f`.

### Step 3 — Backscatter Welford reconstruction (seed from store)

New helper `welfordFromCell(MbesCell) -> IntensityWelford`:

- `sample_sd == 0 && isfinite(mean)` → `{n=1, mean=mean, m2=0}` (n=1 sentinel).
- Else: `SE = standard_error / kScale`; `n = round((sample_sd / SE)^2)`;
  `m2 = sample_sd^2 * (n - 1)`.

**Edge case (review-plan suggestion): `sample_sd==0` for n≥2** — a cell whose
n≥2 samples are all *identical* has `M2=0` → `sample_sd=0`, indistinguishable
from the n=1 sentinel on reload (collapses to n=1). For continuous corrected-dB
data this is astronomically rare (exact float equality across ≥2 beams), and the
only consequence is a slightly under-counted `n` on a subsequent re-survey blend
of that one cell — the mean is still exact. **Documented as an accepted
limitation in the ADR-0007 addendum** (not guarded in code: a guard would need a
separate "n but zero-variance" encoding, not worth a band for a non-occurring
case). `kScale` is a single shared constant (matches the store's bathy
`stddev_to_confidence_interval_scale` convention) so write/read round-trip is
self-consistent.

Used in Step 4 when reloading backscatter from the survey layer on first tile touch.

### Step 4 — Seed precedence (`ImportAccumulator`)

Add `std::set<gggs::GridIndex> seeded_` to `ImportAccumulator`. Add private
`seedNewTile(index)`.

`ImportAccumulatorConfig` gains `std::string reference_store_dir` (replaces
`--prior`; the survey seed always comes from `store_dir`).

`addBatch` now: for each tile the batch touches —

1. If in `evicted_`: run existing `reloadEvictedTile` + spill restore.
2. Else if NOT in `seeded_`: run `seedNewTile`:
   a. Load bathy tile from `store_dir/survey/`; if found: `primeFromTile(..., seed_settled=true)`,
      then reconstruct and restore each cell's `IntensityWelford` via `welfordFromCell`.
   b. Else load bathy tile from `reference_store_dir/reference/`; if found:
      `primeFromTile(..., seed_settled=false)` (no backscatter seed for reference).
   c. Add index to `seeded_`.

Remove the upfront whole-sheet `loadIntoSheet` call from `import_bag_main.cpp`
`--prior` handling. Replace `--prior` with `--reference-store <dir>` that sets
`accumulator_config.reference_store_dir`.

### Step 5 — Batch-regen (`batch_regen_bag` binary)

New `src/batch_regen_main.cpp` and `CMakeLists.txt` entry.

**Scatter phase**: same TF + detection-reading loop as `import_bag_main.cpp`.
For each projected ping, instead of CUBE accumulation: call
`sheet_.gridIndexForSounding(s)` to determine tile, then serialize the
`GeoSounding` to a per-tile binary bucket file (`<scratch>/<level>_<row>_<col>.bin`).

**Gather phase**: iterate tile bucket files. For each:
- Create a fresh `GeoMapSheet`; add all soundings from the bucket in one pass
  (no eviction possible — single tile, bounded RAM).
- Seed from survey layer first if a tile already exists (same `seedNewTile` logic).
- Write the tile once via `ImportAccumulator::finalize` equivalent.

**Test**: `test_batch_regen.cpp` — assert bit-exact depth/backscatter output vs
a single-pass unbounded `ImportAccumulator` run over the same soundings.

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/store_import.h` | `ImportAccumulatorConfig`: remove `bathy_layer`/`bs_source_index`, add `reference_store_dir`; update doc comments |
| `src/store_import.cpp` | Steps 2–4: `geoGridToBackscatterCells`, `persistBackscatterTile`, `reloadEvictedTile`, `addBatch`, new `seedNewTile`/`welfordFromCell` |
| `src/cube_bathymetry_node.cpp` | **Live-node SourceLayer migration (compile-breaker): `Draft`→`Survey`.** The live boat node now writes the `survey` layer directly (draft+processed collapsed; reference is read-only prior). Bounded/approximate boat-side product; batch-regen overwrites it as authoritative. Operator-confirmed behavior; document in the ADR-0001 addendum. |
| `test/test_persistence.cpp` | SourceLayer/MbesCell migration (compile-breaker: uses removed `Draft`/`Chart`) |
| `test/test_tile_eviction_rss.cpp` | SourceLayer/MbesCell migration (compile-breaker: uses removed `Draft`/`Chart`) |
| `src/import_bag_main.cpp` | Remove `--bathy-layer`, replace `--prior` with `--reference-store`, remove upfront loadIntoSheet block |
| `test/test_store_import.cpp` | SourceLayer + MbesCell field migration |
| `test/test_import_eviction.cpp` | SourceLayer + MbesCell migration; add reference-layer seed test (asserts no sample count increment) |
| `test/test_batch_regen.cpp` (new) | Bit-exact batch-regen equivalence test |
| `src/batch_regen_main.cpp` (new) | Batch-regen scatter/gather binary |
| `CMakeLists.txt` | Add `batch_regen_bag` executable target |
| `docs/decisions/0001-tile-eviction-and-incremental-publish.md` | Addendum: two-rung seed precedence semantics + `seed_settled` contract; batch-regen as the exact rebuild path |
| `docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md` | Addendum: 3-band MbesCell write/reconstruct formulas, n=1 sentinel, Welford round-trip guarantee |
| `README.md` (or equivalent) | Document seed precedence, batch-regen mode, backscatter fidelity notes |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | `--help` + README cover seed precedence rungs, batch-regen, backscatter notes |
| Enforcement over documentation | Reference-layer seed explicitly asserts `seed_settled=false`; test enforces no sample count increment |
| Capture decisions | ADR-0001 addendum documents two-rung precedence; ADR-0007 addendum documents 3-band formula |
| A change includes its consequences | `build_massabesic_store.sh` must be updated to remove `--append` grep guard; follow-up ops PR |
| Only what's needed | Batch-regen is a new binary, not an overload of the existing accumulator |

## ADR Compliance

The issue body's **"ADR-0002 (store)" citation is unh_marine_autonomy's ADR-0002**
(the bathy-store ADR) — already amended by #248 on the store side. This CUBE-side
work does **not** touch it; all decisions here are documented in cube_bathymetry's
own ADRs (ADR-0001 + ADR-0007 addenda) per the operator's Issue-Review resolution.

| ADR | Triggered | How addressed |
|---|---|---|
| cube_bathymetry ADR-0001 | Yes | Addendum: two-rung seed precedence generalizes the reload/eviction prime paths; live-node now targets the `survey` layer |
| cube_bathymetry ADR-0007 addendum | Yes | Addendum: 3-band MbesCell sufficient statistics, reconstruction formula, n=1 sentinel + the n≥2 identical-sample limitation |

## Consequences

| If we change… | Also update… | Included? |
|---|---|---|
| Remove `--bathy-layer` | `build_massabesic_store.sh` (unh_echoboats_project11 #352) | No — follow-up ops PR (#353 update) |
| `MbesCell` 3-band format | Any consumer that reads `intensity`/`intensity_variance` | In scope: `test_import_eviction.cpp`; node.cpp already uses the new Welford triplet |
| Replace `--prior` with lazy seeding | Existing users of `--prior` flag | Document in `--help`; ops script update is follow-up |

## Open Questions

- None — uma#248 has landed; layer names (`survey`/`reference`) are finalized; all design decisions are specified in the issue header resolutions.

## Estimated Scope

Single PR (all three features share the `store_import.{h,cpp}` tile-seeding infrastructure and the uma#248 migration is a prerequisite for all three).

## Deviations during implementation

- **Registry migration (unplanned but required).** uma#248 also replaced the
  per-cell `SourceRegistry`/`SourceRecord` interning table with a store-level
  `StoreMetadata` sidecar. The plan's Files-to-Change was silent on this, but the
  old types no longer compile. `ImportAccumulator::finalize` now takes
  `const StoreMetadata *` pointers; `import_bag` builds `StoreMetadata` from
  `--platform`/`--sensor`/`--campaign`(→survey). `--source-id`/`--sensor-class`
  were dropped (greenfield; no per-cell source id exists anymore).
- **Dead conversion params removed.** Because `BathyCell` (2-band) and `MbesCell`
  (3-band) no longer carry `timestamp`/`source_index`, those args were removed from
  `geoGridToTile`/`mapSheetToTiles`/`geoGridToBackscatterCells`/
  `mapSheetToBackscatterCells`, and `source_index`/`timestamp_ns` were removed from
  `ImportAccumulatorConfig` (plan only named `bathy_layer`/`bs_source_index`).
- **Batch-regen is a library class, not just a binary.** `BatchRegen`
  (`batch_regen.{h,cpp}`) lives in the `cube_bathymetry_store_import` library so
  `test_batch_regen` can drive it directly (a bag-only binary is not unit-testable).
  Added `ImportAccumulator::persistResidentTile` for the single-tile gather write.
  `batch_regen_main.cpp` + the `batch_regen_bag` target are as planned.
