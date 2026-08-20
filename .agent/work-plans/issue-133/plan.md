# Plan: cube_bathymetry: retarget writers from SourceLayer::Survey to Draft/Processed

## Issue

https://github.com/rolker/cube_bathymetry/issues/133

## Context

ADR-0010 D8 re-splits the single `SourceLayer::Survey` value into `Draft` (live
on-boat product) and `Processed` (authoritative off-boat re-run). The store-side
implementation landed in rolker/unh_marine_autonomy#308 (feature/issue-308, head
`7048669`), introducing `SourceLayer::{Processed, Draft, Reference, Chart}` with
priority `Processed > Draft > Reference > Chart`, on-disk `draft/` + `processed/`
directories, a **public cross-layer anti-clobber API**
`BathymetryStore::clearOverlappedDraft` (added at commit `1d8c3a8`;
`importGeoTiff` delegates to it), and legacy `survey/` auto-migration to
`processed/` on load.

**This PR can only be built against the feature/issue-308 store** — see the
Build Verification section. The main-tree jazzy underlay still exposes
`SourceLayer::Survey`; the worktree build must overlay the feature/issue-308
built `core_ws` install for compilation to succeed.

Three writers (plus their read-back / prime sites and test coverage) need
updating:

| Writer | Target layer | Rationale |
|--------|-------------|-----------|
| `cube_bathymetry_node.cpp` | `Draft` (writes) | Live, immediate, regenerable from bags. **Reads prime from `Processed ∪ Draft`** so warm-start survives the `survey/`→`processed/` migration (MF2). |
| `store_import.cpp` (`import_bag`) | `Processed` | Authoritative off-boat re-run; also clears superseded draft cells via `clearOverlappedDraft` |
| `batch_regen.cpp` | `Processed` | Same authoritative re-run semantics; shares `ImportAccumulator::persistBathyTile` with store_import (so it clears draft transitively) |

Backscatter store (`marine_mbes_backscatter_store::SourceLayer::Survey`) is
**explicitly excluded** — ADR-0010 D8 accepts the MBES backscatter store keeping
its single collapsed layer (display/QC product, not a nav-safety input). The
`mbs = marine_mbes_backscatter_store` alias (store_import.cpp:479, 825) marks
every backscatter site; those `SourceLayer::Survey` references stay unchanged.

## Confirmed store API (uma#308, verified against the built install header)

Read from
`issue-unh_marine_autonomy-308/core_ws/install/marine_bathymetry_store/include/marine_bathymetry_store/`:

- **Enum** (`bathy_cell.hpp:71`): `enum class SourceLayer : uint8_t { Processed=0,
  Draft=1, Reference=2, Chart=3 }`. Numeric value **is** the priority rank
  (0 = highest), so `Processed 0 > Draft 1 > Reference 2 > Chart 3`.
- **`Processed` and `Draft` are both freely writable** (`bathy_cell.hpp:57-58`);
  only `Reference`/`Chart` are constructor-gated (`reference_writable` /
  `chart_staging_writable`). **No write-gate flag is needed for `Processed`**
  (closes Open Question #2; drops the former ADR-0002 A2.1 caveat).
- **`layerDirName(SourceLayer)`** (`tile_io.hpp:79`) → `"processed"` / `"draft"`
  / `"reference"` / `"chart"`.
- **`clearOverlappedDraft`** (`bathymetry_store.hpp:246,255`) — public method,
  two overloads:
  - `DraftClearResult clearOverlappedDraft(const std::map<gggs::GridIndex,
    BathymetryTile> & processed_tiles);`
  - `DraftClearResult clearOverlappedDraft(const BathymetryTile & processed_tile);`
    (single-tile overload, documented for "incremental, per-tile callers … a
    live/replay writer clearing draft after each direct `saveTile` of a
    processed tile").
  For each processed grid the `Draft` layer **already holds a tile for**, it
  clears the overlapped `Draft` cells by writing them no-data (via
  `set(Draft, …, {})`), marking those draft tiles dirty. Cleared cells persist
  through the **normal dirty-tile save path** (`save(store, dir)`); nothing is
  deleted on disk. Returns `{cells_cleared, tiles_touched}`.
- **Persistence** (`tile_io.hpp`): `save(store, dir, metadata=nullptr)` writes
  every **dirty** tile and (only when `metadata != nullptr`) `registry.json`;
  `loadWindow(store, dir, sw, ne)` loads all layers whose tiles intersect the
  window and **auto-migrates a legacy `survey/`→`processed/`** before scanning.

## Approach

### Step 1 — API confirmation (done)

The uma#308 store API is confirmed above against the built install header. No
guessing: the enum ordinals, `layerDirName`, the `clearOverlappedDraft`
single-tile overload signature, and the `save`/`loadWindow` persistence contract
are all read directly from the installed headers.

### Step 2 — Update `cube_bathymetry_node.cpp`

The live node **writes** `Draft`, but its **reads prime from `Processed ∪ Draft`**
(MF2). On a boat's first boot after co-land, a prior on-disk `survey/` layer is
auto-migrated to `processed/` on the startup `load()`. If the node primed only
from `Draft`, that migrated data would be invisible to the prime and the node
would warm-start from an **empty `Draft`** — losing its GeoMapSheet seed and its
disk-serve tile-version catalog. Priming from both layers preserves warm-start
across the migration boundary. Writes still target `Draft` only.

| Line(s) | Path | Change |
|------|------|--------|
| 326–333 | Startup prime — GeoMapSheet seed | Seed from **both** layers: `loadIntoSheet(store, Draft, …)` then `loadIntoSheet(store, Processed, …)` (Processed second so `Processed > Draft` wins per cell, last-write-wins in the sheet). Guard runs if **either** layer is non-empty. Update the INFO log to report processed + draft tile counts. |
| 349–363 | Startup prime — tile-version catalog + mtime walk | Build the disk-serve catalog from **both** layers: walk `processed/` and `draft/` tile dirs, `catalog_builder_.update(index, mtime)` per tile in each (the builder is newest-wins on duplicate index, so a grid present in both takes the fresher mtime). |
| 1027 | Disk-serve scratch read | Best-source across `Processed ∪ Draft`: look the tile up in both layer maps; prime the scratch sheet from the `Draft` tile then the `Processed` tile (Processed wins); `continue` only if absent from both. |
| 1240 | `reloadEvictedTile` scratch read | Best-source across `Processed ∪ Draft` into `*geo_map_sheet_`: prime the `Draft` tile (if any) then the `Processed` tile (if any). |
| 1287 | `saveDirtyTiles` **write** path | `layerDirName(Draft)` — **writes go to Draft only.** |

Update the stale comments at lines 324–325 ("the live node writes … the `survey`
layer") and 1278–1283 ("writes the `survey` layer") to describe the new
Draft-write / Processed∪Draft-prime semantics.

### Step 3 — Update `store_import.cpp`

**Bathy write path (`persistBathyTile`, line 458) → `Processed` + draft clearing (MF1):**
- `layerDirName(Survey)` → `layerDirName(Processed)`.
- After the direct `saveTile()` of the processed tile, invoke the store's public
  `clearOverlappedDraft` so ADR-0010 D8's "regeneration clears overlapped draft"
  holds on this direct-write path (parity with `importGeoTiff`). Because this
  path writes tile-by-tile to disk with **no persistent in-memory store**, the
  clear is a load→clear→save cycle scoped to the tile's own window:
  1. Construct a scratch `BathymetryStore::fromCellSize(cfg_.cell_size_m)`.
  2. `loadWindow(scratch, cfg_.store_dir, sw, ne)` for the tile's bbox (pulls in
     any overlapping `draft/` tile; a store with no `draft/` loads nothing).
  3. `scratch.clearOverlappedDraft(tile)` (single-tile overload; `tile` is the
     processed tile just written).
  4. If `cells_cleared > 0`, `save(scratch, cfg_.store_dir)` persists only the
     dirtied (touched) draft tiles (loaded tiles are clean; `save` skips them
     and, with `metadata=nullptr`, does not touch `registry.json`).
  - Clearing is a **space + display-cache-invalidation optimization**, not a
    correctness requirement (the query overlay resolves `Processed > Draft`
    regardless). So wrap the cycle in `try/catch`: on failure, `std::cerr` a
    warning and continue — the authoritative `Processed` write already
    succeeded and must not be lost or the import aborted.

**Bathy read-back in `reloadEvictedTile` (line 592) → `Processed`:**
`scratch.tiles(Survey)` → `scratch.tiles(Processed)` — the off-boat re-run reads
back its own authoritative output.

**Bathy seed in `primeInitialTile` (line 815) → `Processed`:**
`scratch.tiles(Survey)` → `scratch.tiles(Processed)` — warm-start seed reads the
existing processed surface.

**Backscatter paths (lines 490, 827 — `mbs` namespace): unchanged.**

Update stale comments at lines 454–455 ("authoritative product: always the
`survey` layer") and 800–806 ("Rung 1 -- survey:…") to reflect `processed`.

### Step 4 — Update `batch_regen.cpp` (1 site → `Processed`)

Line 258 (`finalize()` warning for a non-empty output layer):
`layerDirName(Survey)` → `layerDirName(Processed)`, and update the surrounding
"output survey layer" wording.

`batch_regen` routes actual tile writes through `ImportAccumulator::persistBathyTile`
(Step 3), so it writes `Processed` and clears overlapped draft **transitively** —
no separate clearing call is needed here (this closes Open Question #3). This
step corrects only the finalize directory-check.

### Step 5 — Update test files

All test sites use the `marine_bathymetry_store` namespace (bathy, not
backscatter). Map each test to the write path it exercises:

**→ `Draft` (mirrors live-node paths):**

| File | Lines | Change |
|------|-------|--------|
| `test_persistence.cpp` | 79, 115, 144, 202, 231, 294 | `Survey` → `Draft` |
| `test_anti_entropy_disk_serve.cpp` | 98, 170, 210, 270 | `Survey` → `Draft` |
| `test_tile_eviction_rss.cpp` | 126 | `Survey` → `Draft` |

**→ `Processed` (mirrors import_bag / batch_regen paths):**

| File | Lines | Change |
|------|-------|--------|
| `test_store_import.cpp` | 245, 249, 279 | `Survey` → `Processed` |
| `test_batch_regen.cpp` | 139 | `marine_bathymetry_store::SourceLayer::Survey` → `Processed` |
| `test_import_eviction.cpp` | 127, 382 | `marine_bathymetry_store::SourceLayer::Survey` → `Processed` |

**Unchanged (backscatter store — excluded per ADR-0010 D8):**

| File | Lines | Note |
|------|-------|------|
| `test_batch_regen.cpp` | 170 | `marine_mbes_backscatter_store::SourceLayer::Survey` — excluded |
| `test_import_eviction.cpp` | 171 | `marine_mbes_backscatter_store::SourceLayer::Survey` — excluded |

Update test comments that reference "survey" to match the new layer names. The
node-path tests write `Draft` and read `Draft`, so they stay green under the
`Processed ∪ Draft` prime (the prime reads both, and `Draft` is present).

**New coverage (net-new logic — "test what breaks"):** the direct-write draft
clearing in `persistBathyTile` is the one piece of net-new cube behavior. Add a
focused test — in `test_import_eviction.cpp` or `test_store_import.cpp`,
whichever already has the `store_dir` + import scaffolding — that seeds a `Draft`
tile, runs a processed import/eviction over the overlapping cells, and asserts
the overlapped `Draft` cells were cleared (no-data) while `Processed` holds the
authoritative value. Keep it minimal and reuse existing fixtures.

### Step 6 — Build and test against the uma#308 store overlay

Build Verification **Option A, committed** (below): overlay the built
feature/issue-308 store install and run a full local build + test — do **not**
defer to host CI.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | Startup prime seeds + catalog from `Processed ∪ Draft`; disk-serve (1027) and eviction-reload (1240) best-source reads across both layers; `saveDirtyTiles` write (1287) → `Draft`; update stale comments |
| `cube_bathymetry/src/store_import.cpp` | `persistBathyTile` (458) → `Processed` + `clearOverlappedDraft` load→clear→save cycle; `reloadEvictedTile` (592) and `primeInitialTile` (815) reads → `Processed`; update comments; backscatter (490, 827) untouched |
| `cube_bathymetry/src/batch_regen.cpp` | finalize check (258) → `Processed`; writes clear draft transitively via `persistBathyTile` |
| `cube_bathymetry/test/test_persistence.cpp` | 6 sites: `Survey` → `Draft` |
| `cube_bathymetry/test/test_anti_entropy_disk_serve.cpp` | 4 sites: `Survey` → `Draft` |
| `cube_bathymetry/test/test_tile_eviction_rss.cpp` | 1 site: `Survey` → `Draft` |
| `cube_bathymetry/test/test_store_import.cpp` | 3 sites: `Survey` → `Processed` |
| `cube_bathymetry/test/test_batch_regen.cpp` | 1 bathy site: `Survey` → `Processed`; 1 backscatter site unchanged |
| `cube_bathymetry/test/test_import_eviction.cpp` | 2 bathy sites: `Survey` → `Processed`; 1 backscatter site unchanged; + draft-clearing coverage |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Node read paths retargeted to `Processed ∪ Draft` so migration warm-start holds; all test files enumerated and assigned the correct target layer; backscatter exclusion stated explicitly |
| Test what breaks | Tests for each writer updated to the corresponding layer; net-new draft-clearing gets focused coverage; backscatter tests explicitly excluded |
| Only what's needed | Enum retarget + the minimum read-path fusion MF2 requires + one clear cycle where MF1 requires it; no unrelated refactor; `draft_dir` param name kept (renaming is out of scope) |
| Human control and transparency | Operator sees `draft/` vs `processed/` directories; semantics match the on-boat/off-boat boundary; clearing keeps the authoritative surface un-shadowed by stale draft |
| Enforcement over documentation | Build breaks without this PR against uma#308; lockstep enforces co-land |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D8 (quality-axis re-split) | Yes | This PR is the writer-side half; store-side in uma#308. Direct-write draft clearing via the public `clearOverlappedDraft` gives parity with the importer path. |
| ADR-0002 (bathymetry store layers) | Yes | `Draft`/`Processed` replace `Survey` per the re-split |
| ADR-0008 (ROS 2 conventions) | Yes | No convention changes; enum substitution + read-path fusion |
| ADR-0013 (progress.md vocabulary) | Yes | `## Implementation` entry on completion |

## Consequences

| If we change… | Also update… | Included in plan? |
|---|---|---|
| Node `Survey` **write** → `Draft` | `saveDirtyTiles` write dir (1287) | Yes — Step 2 |
| Node **reads** must survive `survey/`→`processed/` migration | Startup prime seed + catalog, disk-serve read, eviction-reload read → `Processed ∪ Draft` | Yes — Step 2 (MF2) |
| `store_import` `Survey` write → `Processed` | `reloadEvictedTile` / `primeInitialTile` reads; add `clearOverlappedDraft` | Yes — Step 3 (MF1) |
| `batch_regen` `Survey` → `Processed` | finalize output-dir warning; writes clear draft via shared `persistBathyTile` | Yes — Step 4 |
| All source changes | Test files that mirror those paths + draft-clearing coverage | Yes — Step 5 |

## Build Verification Strategy — Option A (committed)

The worktree's `sensors_ws` core underlay is main-tree jazzy (pre-split) — it
still has `SourceLayer::Survey`. The feature/issue-308 store is **built and
present locally** at
`/home/roland/project11/layers/worktrees/issue-unh_marine_autonomy-308/core_ws/install/marine_bathymetry_store`
(and `…/marine_tiled_raster_store` for `TileCatalogBuilder`). Overlay that
install ahead of the worktree underlay (prepend to `CMAKE_PREFIX_PATH` /
`AMENT_PREFIX_PATH`, or source the issue-308 `core_ws/install` before this
worktree's `sensors_ws`), then run a **full local build + test** of
`cube_bathymetry`. Record the build result and the uma#308 head SHA (`7048669`,
`clearOverlappedDraft` at `1d8c3a8`) in the progress `## Implementation` entry.
Verification is **not** deferred to host CI.

## Documentation & Instruction Impact

- **Stale docs**: inline comments in `cube_bathymetry_node.cpp`, `store_import.cpp`,
  and `batch_regen.cpp` that reference "the `survey` layer" are updated in this
  PR. No package README change — `draft`/`processed` semantics are documented in
  uma#308's `marine_bathymetry_store` README/ADR-0002 A3.2.
- **Agent-instruction candidates**: None — a design-specified retarget. The one
  subtlety worth remembering (direct-`saveTile` producers must call
  `clearOverlappedDraft` themselves; the store owns the semantics) is already
  documented store-side in the `clearOverlappedDraft` header and ADR-0002 A3.2.

## Open Questions (resolved)

- ~~Draft-clearing function signature~~ — **Resolved.** Public
  `BathymetryStore::clearOverlappedDraft` (single-tile overload) added in uma#308
  `1d8c3a8`; signature confirmed against the install header (see Confirmed API).
- ~~`Processed` write-gate flag~~ — **Resolved (Open Question #2 closed).**
  `Processed` and `Draft` are both freely writable (`bathy_cell.hpp:57-58`); no
  gate flag needed; the former ADR-0002 A2.1 caveat is dropped.
- ~~Should `batch_regen` also clear draft?~~ — **Resolved.** Yes, transitively:
  its writes go through `ImportAccumulator::persistBathyTile`, which now clears.

## Estimated Scope

Single PR. Enum retarget + MF2 read-path fusion in the node (3 read sites) + MF1
`clearOverlappedDraft` load→clear→save cycle in `persistBathyTile` + test
retarget and one focused draft-clearing test. Must co-land with uma#308
(feature/issue-308) — cannot build or merge independently.
