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

| ADR | Triggered | How addressed |
|---|---|---|
| cube_bathymetry ADR-0001 | Yes | Addendum: two-rung seed precedence generalizes the reload/eviction prime paths |
| cube_bathymetry ADR-0007 addendum | Yes | Addendum: 3-band MbesCell sufficient statistics, reconstruction formula, n=1 sentinel |

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
