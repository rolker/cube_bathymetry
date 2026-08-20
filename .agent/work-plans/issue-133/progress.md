---
issue: 133
---

# Issue #133 — cube_bathymetry: retarget writers from SourceLayer::Survey to Draft/Processed

## Issue Review
**Status**: complete
**When**: 2026-08-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #133
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Lockstep breaking change: retarget three `cube_bathymetry` writers from the
retired `SourceLayer::Survey` enum value to the new `Draft` (live CUBE node)
and `Processed` (`store_import`, `batch_regen`) values introduced in
`rolker/unh_marine_autonomy#308`. The store-side split is **done and published**
(uma#313, head 1707ea2). This PR cannot build or merge independently — it
must be co-landed with the store.

**Affected source files:**
- `cube_bathymetry_node.cpp` — 6 `Survey` references (lines 327, 330, 351,
  1027, 1240, 1287); all → `Draft`. Lines 327–351 are the write path; lines
  1027, 1240, 1287 are read-back / scratch-tile walks that follow the rename.
- `store_import.cpp` — 4 `Survey` references (lines 258, 458, 490, 592, 815)
  → `Processed`; also picks up the per-cell draft-clearing call that lands
  store-side.
- `batch_regen.cpp` — 1 `Survey` reference (line 258) → `Processed`.

**Affected test files (must update in same PR):**
- `test_store_import.cpp` (lines 245, 249, 279 use `Survey`)
- `test_batch_regen.cpp` (line 139 uses `marine_bathymetry_store::SourceLayer::Survey`)
- `test_anti_entropy_disk_serve.cpp` (lines 98, 170, 210, 270)
- `test_persistence.cpp` (lines 79, 115, 144, 202, 231, 294)
- `test_tile_eviction_rss.cpp` (line 126)
- `test_import_eviction.cpp` (line 127, 382)

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Draft vs Processed semantics well-explained in issue; operator coverage view rationale clear |
| Enforcement over documentation | OK | Build-time enforcement: compile break against renamed enum prevents partial merges |
| Capture decisions, not just implementations | OK | Design rationale captured in issue and referenced ADR-0010 D8 |
| A change includes its consequences | Watch | Test files referencing `Survey` (enumerated above) must update in same PR; issue body doesn't enumerate them explicitly |
| Only what's needed | OK | Tightly scoped to enum substitution |
| Improve incrementally | OK | Correctly broken out from the store-side split |
| Test what breaks | Watch | Test files listed above cover the write/read paths — confirm each is updated to use `Draft` or `Processed` as appropriate for the operation being tested |
| Workspace vs. project separation | OK | Change is correctly in the project repo |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 — Adopt ADRs | No | Decision rationale already captured in ADR-0010 D8 |
| ADR-0002 — Worktree isolation | Yes | Satisfied — `feature/issue-133` worktree exists |
| ADR-0008 — ROS 2 conventions | Yes | Package modification; no deviations from ROS 2 conventions expected |
| ADR-0013 — progress.md vocabulary | Yes | This entry |

### Consequences

- **Test files**: All `SourceLayer::Survey` references in test files must be updated in the same PR (see list above). Tests for the live-node path → `Draft`; tests for import/batch-regen paths → `Processed`.
- **Backscatter store**: `marine_mbes_backscatter_store::SourceLayer::Survey` appears in `test_batch_regen.cpp` (line 170) and `test_import_eviction.cpp` (line 171). If the backscatter store also underwent a similar split in uma#308, those references need updating here too. Verify against uma#313 and clarify scope.
- **Build verification**: The worktree's core underlay is `main-tree jazzy` (pre-split). The plan must address how to verify the build against the feature/issue-308 store (uma#313) — options: temporarily point the core underlay at the issue-308 built `core_ws`, or defer full build verification to a combined host-side check.

### Recommendations

- Clarify whether `marine_mbes_backscatter_store::SourceLayer::Survey` in the test files is in scope — if the backscatter store also split in uma#308, it should be handled here.
- The plan should include an explicit build-verification strategy against the feature/issue-308 store before marking the PR ready.
- Enumerate the scratch-tile-walk read sites in `cube_bathymetry_node.cpp` (lines 1027, 1240, 1287) explicitly in the plan so they're not missed as "just read sites."

### Actions
- [ ] Update all test files that reference `SourceLayer::Survey` in the same PR (see list in Consequences)
- [ ] Confirm backscatter store `SourceLayer::Survey` scope — update or explicitly exclude in plan
- [ ] Plan must include build-verification strategy against the uma#313 / feature/issue-308 store
- [ ] Include scratch-tile-walk read sites (cube_bathymetry_node.cpp lines 1027, 1240, 1287) explicitly in the implementation plan

## Plan Authored
**Status**: complete
**When**: 2026-08-20 17:41 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-133/plan.md` at `bbcde41`
**Branch**: feature/issue-133 at `bbcde41`
**Phases**: single

### Open questions
- [ ] Confirm draft-clearing function signature from uma#313 before implementing call in `persistBathyTile()`
- [ ] Confirm whether `SourceLayer::Processed` requires a constructor write-gate flag in uma#313
- [ ] Confirm whether `batch_regen.cpp` should also invoke draft clearing (likely yes via shared `ImportAccumulator`, but only `store_import` is explicitly named in the issue)

## Plan Review
**Status**: complete
**When**: 2026-08-20 17:50 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-133/plan.md` at `bbcde41`
**PR**: PR-less (`--issue 133`, layer worktree `feature/issue-133`)
**Verdict**: changes-requested

Note: `gh` was unauthenticated in this environment; the linked issue was read
from the `## Issue Review` entry above (not a fresh `gh issue view`). The
uma#308 store API was verified directly against the local `feature/issue-308`
worktree headers (`issue-unh_marine_autonomy-308/core_ws/.../marine_bathymetry_store`).

### Findings
- [ ] (must-fix) Step 3's "invoke the store-side draft-clearing function" after `saveTile()` assumes an API that does not exist. The uma#308 public surface is `layerDirName` / `saveTile` / `set` / `importTiles` / `tiles` — there is **no standalone clear-draft function**. Cell-wise draft clearing is an internal side effect of `importGeoTiff(store, Processed, path)` (returns `ProcessedImportResult`), reached only via the GeoTIFF-file import path. `persistBathyTile` writes an in-memory tile via **direct `saveTile()`**, which bypasses `importGeoTiff` entirely — so there is nothing to "invoke." Correctness does **not** depend on clearing (the query overlay resolves `Processed > Draft`, per `geotiff_import.hpp`/`bathymetry_store.hpp`); clearing is a space + display-cache-invalidation optimization (camp#171/#172). Reframe Step 3: either (a) omit clearing on the `saveTile` path and rely on read-time priority (document this), or (b) implement explicit cell-wise `store.set(Draft, cell, {})` — a materially larger change than a one-line call. — `plan.md:67-69`, `plan.md:222-224`
- [ ] (must-fix) Missing consequence: legacy `survey/` auto-migrates to **`processed/`** on load (`migrateLegacySurveyDir`, single rename; `tile_io.hpp:53-55`). Step 2 retargets the live node's startup prime (`store.tiles(...)`, `loadIntoSheet(...)`, mtime walk — node.cpp:327/330/351) to `Draft`. On the first boot after co-land on a boat with prior on-disk `survey/`, that data is relabeled `processed/`, so the node primes from an **empty `Draft`** — losing its warm-start GeoMapSheet seed and its disk-serve tile-version catalog (node.cpp:333+). The live node's own historical output was really draft/live data, yet migration marks it `Processed` (now out-ranking new draft writes). The plan must address this: prime the node from `Processed` (or Processed∪Draft) to preserve warm-start across the migration boundary, or explicitly confirm empty-draft-first-boot is acceptable. — `plan.md:54-58`
- [ ] (suggestion) Open Question #2 is answerable now and can be closed: `Processed` and `Draft` are **both freely writable** (`bathy_cell.hpp:57-58`); only `Reference`/`Chart` are constructor-gated (`reference_writable` / `chart_staging_writable`). No write-gate flag is needed for `Processed`. Drop the ADR-0002 A2.1 caveat at `plan.md:179` and the open question at `plan.md:225-226`. — `plan.md:179`
- [ ] (suggestion) Build verification is more achievable than the plan assumes. The `feature/issue-308` store is **built and present locally** at `issue-unh_marine_autonomy-308/core_ws/install/marine_bathymetry_store` — Build Verification Option A (overlay that install) can run now. Commit to a local build+test rather than deferring to combined host CI. — `plan.md:196-207`
- [ ] (positive) File targeting verified exhaustively correct against `grep`: all node/store_import/batch_regen `SourceLayer::Survey` sites and every test-file line map exactly to the plan's tables; bathy-vs-backscatter (`mbs = marine_mbes_backscatter_store`, store_import.cpp:479) and Draft-vs-Processed classification are all accurate. Enum ordinals confirmed: `Processed=0 > Draft=1 > Reference=2 > Chart=3`.

### Summary
Scope, structure, and file targeting are excellent — a well-partitioned single-PR
enum retarget with every site correctly enumerated and classified. The plan's one
piece of net-new logic (draft-clearing) rests on an incorrect assumption about the
store API, and it omits the consequence of the legacy `survey/`→`processed/`
migration on the live node's now-`Draft` startup prime. Both are small, targeted
amendments (not a rewrite); the plan's Step-1 "confirm the API first" gate already
creates space to resolve them. Address the two must-fix items — ideally amend the
plan inline — before implementation.

### Recommended Actions
- [ ] Reframe Step 3's draft-clearing: decide omit-and-rely-on-`Processed > Draft` vs. explicit cell-wise `set(Draft, cell, {})`; correct the "invoke the store-side draft-clearing function" wording (no such function exists).
- [ ] Add a consequence + decision for the `survey/`→`processed/` migration vs. the node's `Draft` startup prime (warm-start / disk-serve catalog on first post-co-land boot).
- [ ] Close Open Question #2 (Processed is freely writable — no gate flag) and drop the ADR-0002 A2.1 caveat.
- [ ] Adopt local Build Verification Option A against the built `issue-308` install.

## Implementation
**Status**: complete
**When**: 2026-08-20 18:35 +00:00
**By**: Claude Opus

**Plan**: `.agent/work-plans/issue-133/plan.md` at `955805d` (revised per Plan Review)
**Branch**: feature/issue-133
**Commits**:
- `955805d` — plan revision (MF1 `clearOverlappedDraft`, MF2 Processed∪Draft prime, close OQ#2, commit Build Verification Option A, record confirmed store API)
- `87f11e9` — implementation (retarget writers + prime + draft clearing + tests)

### What landed

Plan revised first (committed before any source change), then implemented per the
revised plan and the operator's two must-fix decisions:

- **MF1 — direct-write draft clearing via the shared store API.** `store_import.cpp`
  `persistBathyTile` now writes `Processed` and, after its direct `saveTile`, calls
  the store's public `BathymetryStore::clearOverlappedDraft(const BathymetryTile&)`
  (uma#308 `1d8c3a8`). Because this path holds no persistent store, it runs a scoped
  `loadWindow` → `clearOverlappedDraft(tile)` → `save` cycle over the tile's window
  (persists only the dirtied draft tiles). Clearing is an optimization (the query
  overlay already resolves `Processed > Draft`), so a failure is logged to `std::cerr`
  and swallowed — the authoritative processed write is never lost nor the import
  aborted. `batch_regen` routes its writes through the same `persistBathyTile`, so it
  clears draft transitively (its finalize output-layer check → `Processed`).
- **MF2 — live node primes from Processed ∪ Draft.** The node still **writes** `Draft`
  (`saveDirtyTiles`, node.cpp:1287), but its **reads** fuse both layers so warm-start
  survives the legacy `survey/`→`processed/` migration on first post-co-land boot:
  startup GeoMapSheet seed (Draft then Processed so Processed wins), disk-serve
  tile-version catalog (seeded from both layer dirs; builder is newest-wins per index),
  the disk-serve scratch read (node.cpp:1027), and the eviction-reload read
  (node.cpp:1240) all best-source across Processed ∪ Draft.
- **Plan Review suggestions folded in.** Open Question #2 closed (`Processed`/`Draft`
  both freely writable per `bathy_cell.hpp:57-58`; ADR-0002 A2.1 caveat dropped).
  Backscatter store (`marine_mbes_backscatter_store::SourceLayer::Survey`,
  store_import.cpp:519/858, test_batch_regen.cpp:170, test_import_eviction.cpp:171)
  left untouched. Enum ordinals confirmed `Processed=0 > Draft=1 > Reference=2 > Chart=3`.

**Source**: `cube_bathymetry_node.cpp` (startup prime seed+catalog, disk-serve read,
eviction-reload read → Processed∪Draft; write → Draft), `store_import.cpp`
(`persistBathyTile` → Processed + clearOverlappedDraft; `reloadEvictedTile` /
`primeInitialTile` reads → Processed), `batch_regen.cpp` (finalize check → Processed).
Stale `survey`-layer comments updated throughout.

**Tests**: node-path sites → `Draft` (`test_persistence`, `test_anti_entropy_disk_serve`,
`test_tile_eviction_rss`); import/batch-regen sites → `Processed` (`test_store_import`,
`test_batch_regen`, `test_import_eviction`); backscatter sites unchanged. Renamed/fixed
the `layerDirName` assertion test (`DraftLayerDirNameIsDraft`, now expects `"draft"`).
Added `ImportEviction.ProcessedImportClearsOverlappedDraft` covering the net-new
direct-write draft clearing (overlapped draft cleared to no-data; processed
authoritative; unrelated draft in a different tile survives — clearing is scoped).

### Build Verification — Option A (local, not deferred to host CI)

Nothing was pre-built locally, so the dependency stack was bootstrapped to overlay the
split store:
- `geodesy` built in the shared `underlay_ws` (`main/underlay_ws`) — needed transitively
  by `marine_sidescan_mosaic` ← `marine_survey_index`.
- The **uma#308 `feature/issue-308` core** (`issue-unh_marine_autonomy-308/core_ws`, head
  `7048669`; `clearOverlappedDraft` at `1d8c3a8`) completed to a consistent install:
  `marine_sidescan_mosaic`, `marine_survey_index`, `marine_mbes_backscatter_store` built
  (a stale pre-geodesy CMake cache was cleaned first). Installed header confirmed split
  (`Processed`/`Draft`) with the public `clearOverlappedDraft`.
- `cube_bathymetry` built and tested against that overlay
  (`/opt/ros/jazzy` + `main/underlay_ws/install` + `issue-308 core_ws/install`).

**Result**: build OK; `colcon test` → **553 tests, 0 errors, 0 failures, 68 skipped**
(the initial run's lone failure was a uncrustify split-string indent in `store_import.cpp`,
fixed in the same commit; not a test-logic failure). `ProcessedImportClearsOverlappedDraft`
ran and passed. No tests skipped or disabled.

### Lockstep note

This PR cannot build against the pre-split main-tree jazzy core (still exposes
`SourceLayer::Survey`); it must co-land with uma#308 (feature/issue-308). Verified
locally against that store — host CI should run the combined build to confirm.
