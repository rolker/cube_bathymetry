# Plan: cube_bathymetry: retarget writers from SourceLayer::Survey to Draft/Processed

## Issue

https://github.com/rolker/cube_bathymetry/issues/133

## Context

ADR-0010 D8 re-splits the single `SourceLayer::Survey` value into `Draft` (live
on-boat product) and `Processed` (authoritative off-boat re-run). The store-side
implementation landed in rolker/unh_marine_autonomy#313 (feature/issue-308, head
`1707ea2`), introducing `SourceLayer::{Processed, Draft, Reference, Chart}` with
priority `Processed > Draft > Reference > Chart`, on-disk `draft/` + `processed/`
directories, per-cell draft clearing invoked by processed imports, and legacy
`survey/` auto-migration on load.

**This PR can only be built against the feature/issue-308 store** — see the
Build Verification section. The main-tree jazzy underlay still exposes
`SourceLayer::Survey`; the worktree underlay must be pointed at the
feature/issue-308 built `core_ws` for compilation to succeed.

Three writers (plus their test coverage and read-back sites) need updating:

| Writer | Target layer | Rationale |
|--------|-------------|-----------|
| `cube_bathymetry_node.cpp` | `Draft` | Live, immediate, regenerable from bags |
| `store_import.cpp` (`import_bag`) | `Processed` | Authoritative off-boat re-run; also clears superseded draft cells |
| `batch_regen.cpp` | `Processed` | Same authoritative re-run semantics; shares `ImportAccumulator` with store_import |

Backscatter store (`marine_mbes_backscatter_store::SourceLayer::Survey`) is
**explicitly excluded** — ADR-0010 D8 accepts the MBES backscatter store keeping
its single collapsed layer (display/QC product, not a nav-safety input).

## Approach

### Step 1 — Confirm the uma#313 store API before touching any source

Read `marine_bathymetry_store/include/marine_bathymetry_store/bathy_cell.hpp`
from the **feature/issue-308** worktree to confirm:
- The new `SourceLayer` enum values and their ordinal positions
- The `layerDirName()` return values for `Draft` and `Processed`
- The draft-clearing function signature (expected in `tile_io.hpp` or a new header)

This must be done first — the remainder of the plan depends on the confirmed API.

### Step 2 — Update `cube_bathymetry_node.cpp` (6 sites → `Draft`)

All six `SourceLayer::Survey` references in this file follow the draft path.
Replace each with `SourceLayer::Draft`:

| Line | Context |
|------|---------|
| 327 | Startup prime: `store.tiles(…::Survey)` → `Draft` |
| 330 | Startup prime: `loadIntoSheet(store, …::Survey, …)` → `Draft` |
| 351 | Startup prime: `layerDirName(…::Survey)` for the tile mtime walk → `Draft` |
| 1027 | Disk-serve scratch-tile walk: `scratch.tiles(…::Survey)` → `Draft` |
| 1240 | `reloadEvictedTile` scratch walk: `scratch.tiles(…::Survey)` → `Draft` |
| 1287 | `saveDirtyTiles` write path: `layerDirName(…::Survey)` → `Draft` |

Update the stale comments at lines 324–325 ("live node writes...the `survey`
layer") and 1278–1283 ("writes the `survey` layer") to reflect `draft`.

### Step 3 — Update `store_import.cpp` (3 bathy sites → `Processed`)

**Bathy write path (line 458, `persistBathyTile`):**
- `layerDirName(…::Survey)` → `layerDirName(…::Processed)`
- After the `saveTile()` call, invoke the store-side draft-clearing function to
  remove any overlapped draft cell for this tile (exact call site and signature
  confirmed in Step 1 from uma#313 headers).

**Bathy read-back in `reloadEvictedTile` (line 592):**
- `scratch.tiles(…::Survey)` → `scratch.tiles(…::Processed)` (reads from the
  output store, which now writes `Processed`)

**Bathy seed in `primeInitialTile` (line 815):**
- `scratch.tiles(…::Survey)` → `scratch.tiles(…::Processed)` (reads the
  existing processed surface as the warm-start seed)

**Backscatter paths (lines 490, 827 — mbs namespace): unchanged.**

Update stale comments at lines 454–455 ("authoritative product: always the `survey`
layer") and 800–806 ("Rung 1 -- survey:...") to reflect `processed`.

### Step 4 — Update `batch_regen.cpp` (1 site → `Processed`)

Line 258 (`finalize()` warning for non-empty output layer):
- `layerDirName(…::Survey)` → `layerDirName(…::Processed)`

Note: `batch_regen` routes actual tile writes through `ImportAccumulator`
(`store_import.cpp`), so Step 3's `persistBathyTile` change covers batch_regen's
actual write path. This step corrects only the directory-check at finalize.

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

Update test comments that reference "survey" to match the new layer names.

### Step 6 — Build and test against uma#313

Because the worktree's core underlay is pre-split jazzy, a normal
`./sensors_ws/build.sh cube_bathymetry` will fail on the renamed enum.
Build and test procedure:

1. Locate or build the feature/issue-308 `core_ws` (from the uma repo's
   `feature/issue-308` branch, or confirm it was built as part of uma#313 merge).
2. Source `setup.bash` with the feature/issue-308 `core_ws/install` overlaying
   the worktree's underlay — or edit the worktree's `sensors_ws/build.sh` to
   layer the feature/issue-308 install path.
3. Build: `./sensors_ws/build.sh cube_bathymetry`
4. Test: `./sensors_ws/test.sh cube_bathymetry`
5. Record the build result (pass/fail + SHA of the uma#313 head used) in the PR
   description. If the combined build cannot run locally (uma#313 not yet merged),
   state that explicitly and request that the host run a combined CI check.

### Step 7 — Update stale documentation and comments

After source changes are confirmed correct:
- Update any stale inline comments that reference "survey" in the context of
  the live-node or import paths (covered in Steps 2–4 above).
- No package README update needed — the `draft`/`processed` semantics are
  documented in uma#313's `marine_bathymetry_store/README.md`.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | 6 sites: `Survey` → `Draft`; update 2 stale comments |
| `cube_bathymetry/src/store_import.cpp` | 3 bathy sites: `Survey` → `Processed`; add draft-clearing call; update 2 stale comments |
| `cube_bathymetry/src/batch_regen.cpp` | 1 site: `Survey` → `Processed` |
| `cube_bathymetry/test/test_persistence.cpp` | 6 sites: `Survey` → `Draft` |
| `cube_bathymetry/test/test_anti_entropy_disk_serve.cpp` | 4 sites: `Survey` → `Draft` |
| `cube_bathymetry/test/test_tile_eviction_rss.cpp` | 1 site: `Survey` → `Draft` |
| `cube_bathymetry/test/test_store_import.cpp` | 3 sites: `Survey` → `Processed` |
| `cube_bathymetry/test/test_batch_regen.cpp` | 1 bathy site: `Survey` → `Processed`; 1 backscatter site: unchanged |
| `cube_bathymetry/test/test_import_eviction.cpp` | 2 bathy sites: `Survey` → `Processed`; 1 backscatter site: unchanged |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | All test files enumerated and assigned correct target layer; backscatter exclusion stated explicitly |
| Test what breaks | Tests for each writer updated to the corresponding layer; backscatter tests explicitly excluded |
| Only what's needed | No refactoring of unrelated code; draft-clearing mechanism called only where specified |
| Human control and transparency | Operator sees `draft/` vs `processed/` directories; semantics match the on-boat/off-boat boundary |
| Enforcement over documentation | Build breaks without this PR against uma#313; lockstep enforces co-land |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0010 D8 (quality-axis re-split) | Yes | This PR is the writer-side half; store-side in uma#313 |
| ADR-0002 (bathymetry store layers) | Yes | `Draft`/`Processed` replace `Survey` per the re-split |
| ADR-0008 (ROS 2 conventions) | Yes | No convention changes; pure enum substitution |
| ADR-0002 A2.1 (write gates) | Yes | `Draft` is always writable (live ingest); `Processed` write gate to be confirmed against uma#313 — may require a constructor flag (like `reference_writable`) |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `SourceLayer::Survey` write in `cube_bathymetry_node.cpp` | Startup prime reads, scratch-tile walks, `layerDirName` path | Yes — all 6 node sites in Step 2 |
| `SourceLayer::Survey` write in `store_import.cpp` | Seed reads, eviction reload reads | Yes — Steps 3–4 |
| `SourceLayer::Survey` in `batch_regen.cpp` | Output directory warning | Yes — Step 4 |
| All source changes | Test files that mirror those paths | Yes — Step 5 |

## Build Verification Strategy

The worktree's `sensors_ws` core underlay is **main-tree jazzy (pre-split)** — it
still has `SourceLayer::Survey`. Building this PR requires the uma#313
feature/issue-308 store as the core underlay. Two options:

**Option A (preferred if uma#313 is merged or available as a built install):**
Temporarily source the uma#313 `core_ws/install` before sourcing this worktree's
`sensors_ws/install` to overlay the new headers. Document the SHA of the
uma#313 head in the PR description.

**Option B (if uma#313 is not yet available):**
Implement all changes, note the build cannot complete until the lockstep merge,
and request the host run a combined CI check against the feature/issue-308 head.

Either way, the PR description must state the uma#313 build status and the SHA
used for verification (or explicitly state that full verification is deferred to
the combined host CI check).

## Documentation & Instruction Impact

- **Stale docs**: Inline comments in `cube_bathymetry_node.cpp`, `store_import.cpp`,
  and `batch_regen.cpp` that reference "the `survey` layer" will be updated in this
  PR to reflect `draft` or `processed` as appropriate. No other package documentation
  needs updating in this PR — the `marine_bathymetry_store` README update is in
  uma#313.
- **Agent-instruction candidates**: None — this is a straightforward enum substitution
  following a pre-specified design. No new patterns or pitfalls beyond what's already
  documented in the issue.

## Open Questions

- Confirm the draft-clearing function signature from uma#313 before implementing
  the call in `persistBathyTile()` — the exact API (free function vs. method, args)
  is not visible from the main-tree jazzy headers.
- Confirm whether `SourceLayer::Processed` in uma#313 requires a constructor write-gate
  flag (analogous to `reference_writable`) or is always writable (like the current `Survey`).
- Confirm whether `batch_regen.cpp` should also invoke draft clearing (likely yes,
  since it shares `ImportAccumulator` and has the same authoritative-re-run semantics,
  but the issue only names `store_import` explicitly).

## Estimated Scope

Single PR. All changes are enum substitutions + one new call site in
`persistBathyTile`. Must co-land with uma#313 (feature/issue-308) — cannot merge
independently.
