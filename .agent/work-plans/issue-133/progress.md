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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 18:45 +00:00
**By**: Claude Opus
**Verdict**: changes-requested

**Branch**: feature/issue-133 at `73db43b`
**Mode**: pre-push
**Depth**: Deep (reason: cross-layer lockstep breaking change touching store write semantics + net-new draft-clearing path)
**Must-fix**: 1 | **Suggestions**: 3
**Round**: 1 | **Ship**: continue — one genuine correctness concern in the fused prime warrants a fix + independent re-read

Specialists: static analysis run (uncrustify/cpplint/cppcheck clean on changed lines); 2 Claude Adversarial passes (Lens A + Lens B); Local Adversarial skipped (Ollama not responding on :11434); Copilot off (default). Store API verified directly against built issue-308 headers.

### Findings
- [x] (must-fix) Fused Processed∪Draft prime does not deliver the claimed "Processed > Draft per cell on overlap" for the reported settled depth: CUBE `chooseHypothesis` (strict `>`, node.cpp:376) tie-breaks two 1-sample settled hypotheses to the first-seeded (Draft), so Draft-then-Processed reports Draft on overlap. Predicted-prior wants Processed primed last, settled wants it first — no single order satisfies both. Happy path is masked by `clearOverlappedDraft`; residual overlap serves the non-authoritative surface. Fix the logic or correct the three comment blocks. — `cube_bathymetry_node.cpp:330-339,1044-1063,1266-1281`
- [x] (suggestion) No test covers fused overlapping-cell priority; a conflicting-depth Draft+Processed prime test would pin the contract and catch the must-fix. — `test/test_import_eviction.cpp`
- [x] (suggestion) `seedNewTile` catch treats the permanent both-exist `survey/`+`processed/` migration throw as a transient per-tile read error, silently degrading an import to a near-no-op; distinguish and abort loudly. — `store_import.cpp:~875`
- [x] (suggestion) Two-layer transient-RAM comment understates the peak (Processed + Draft both fully resident before the trim at node.cpp:390). — `cube_bathymetry_node.cpp:383-389`

## Implementation
**Status**: complete
**When**: 2026-08-20 19:05 +00:00
**By**: Claude Opus

**Branch**: feature/issue-133 at `9dd0cc2`
**Addressed**: Local Review (Pre-Push) (2026-08-20 18:45 +00:00, at `73db43b`)
**Commits**: `af6c785`, `c13b2b3`, `dde422e`, `9dd0cc2`

### What landed

All four findings of the source review were **fixed** (none deferred). The must-fix
resolves the fused-prime priority BEFORE CUBE's hypothesis machinery (per the host
steer), so overlap is deterministic regardless of `chooseHypothesis`'s tie-break.

- **MF — fused prime is now Processed-seed-then-Draft-skip-Processed, not seed-order.**
  New `primeFromTileSkippingMask(tile, mask, sheet, seed_settled)` primes a Draft tile
  but SKIPS every cell an overlapping same-GridIndex Processed tile already covers with
  finite depth (the mask); `primeFromTile` is now a thin `mask=nullptr` forwarder, and
  new `loadDraftSkippingProcessed(store, sheet)` layers Draft over a fully-seeded
  Processed layer using each Draft tile's same-index Processed tile as its mask. All
  three sites now **seed Processed fully first, then Draft-skip-Processed**: startup
  prime (node.cpp:330-347), disk-serve scratch read (node.cpp:1053-1082), and
  eviction-reload read (node.cpp:1284-1308). The overlapped cell carries ONLY the
  Processed hypothesis (both its settled depth and its predicted/blunder prior) —
  deterministic Processed-wins, matching the store's query-side `Processed > Draft`
  walk. The three comment blocks were rewritten to describe this mechanism (they
  previously claimed last-write-wins/seed-order, which the tie-break defeats).
- **S1 — regression test pinning the contract.** `ImportEviction.FusedPrimeProcessedWinsOverConflictingDraft`
  (test_import_eviction.cpp) primes a sheet from a store with conflicting Processed
  (−50 m) and Draft (−11 m) depths on the SAME cell plus a Draft-only cell in another
  tile; asserts the overlapped cell reports the Processed depth (not the stale Draft
  depth) and the Draft-only cell survives (skip is scoped, not a blanket Draft drop).
- **S2 — `seedNewTile` aborts loudly on the permanent ambiguous store.** New filesystem
  helper `hasAmbiguousSurveyMigration(store_dir)` detects a store holding BOTH `survey/`
  and `processed/` dirs directly (not by matching the throw's message). On a survey-seed
  `loadWindow` throw, if that store-wide condition holds the catch now RETHROWS a clear
  fatal error instead of returning false — otherwise every tile would degrade to a
  dropped-soundings no-op and silently emit a near-empty store. Uncaught → `addBatch` →
  import_bag main (whose ping loop catches only `TransformException`) → process
  terminates non-zero, no partial store finalized. Genuine transient per-tile read
  errors keep the existing drop-and-retry behavior.
- **S3 — two-layer transient-RAM peak comment corrected** (node.cpp:390): the peak is
  now the two-layer union — `store` holds BOTH Processed and Draft tiles resident and
  the sheet holds the fused surface on top — higher than a single-layer prime.

### Build Verification — Option A (local, issue-308 store overlay)

Same bootstrap as the prior Implementation entry: `/opt/ros/jazzy` +
`main/underlay_ws/install` + the built **uma#308 `feature/issue-308` core**
(`issue-unh_marine_autonomy-308/core_ws/install`; installed `marine_bathymetry_store`
confirmed split with the public `BathymetryStore::clearOverlappedDraft`). Rebuilt and
tested `cube_bathymetry` against that overlay.

**Result**: build OK; `colcon test` → **554 tests, 0 errors, 0 failures, 68 skipped**
(was 553 pre-change; +1 is the new fused-prime test, which ran and passed). Pre-commit
hooks (uncrustify/cpplint) ran on every commit; none used `--no-verify`.

### Actions
- [x] Resolve fused-prime priority before the hypothesis machinery (Processed-seed-then-Draft-skip-Processed) at all three sites + rewrite the comments — `cube_bathymetry_node.cpp:330-347,1053-1082,1284-1308`, `store_import.cpp` (`primeFromTileSkippingMask`, `loadDraftSkippingProcessed`)
- [x] Conflicting-depth Draft+Processed prime test pinning per-cell priority — `test/test_import_eviction.cpp` (`FusedPrimeProcessedWinsOverConflictingDraft`)
- [x] `seedNewTile` distinguishes the permanent both-exist migration throw and aborts loudly — `store_import.cpp` (`hasAmbiguousSurveyMigration` + seedNewTile catch)
- [x] Correct the two-layer transient-RAM peak comment — `cube_bathymetry_node.cpp:390`

### Next step

Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 133 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 19:17 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-133 at `d802a30`
**Mode**: pre-push
**Depth**: Deep (reason: cross-layer lockstep breaking change touching store write semantics + net-new fused-prime / anti-clobber paths)
**Must-fix**: 0 | **Suggestions**: 5
**Round**: 2 | **Ship**: recommended — no must-fix; round-1 must-fix (fused-prime priority) verified resolved and pinned by a new test; remaining items are hardening/doc suggestions on an already-added safety net

Specialists: static analysis run (ament_cpplint "No problems found", ament_uncrustify no divergence; cppcheck emitted only GTest-macro parse noise); 2 Claude Adversarial passes (Lens A logic/correctness + Lens B systemic/safety), both clean of must-fix. Copilot off (default); Local Adversarial not invoked. Store API (clearOverlappedDraft / save-dirty-only / SourceLayer ordinals / survey->processed migration incl. symlink-refusal) verified directly against the built uma#308 (feature/issue-308) headers. Build relied on the prior Implementation entry's clean run (554 tests, 0 failures) against the uma#308 overlay — source is byte-identical since (9dd0cc2..HEAD touches only progress.md).

### Findings
- [x] (suggestion) hasAmbiguousSurveyMigration detects only the both-dirs case; store also throws (permanently) on a symlinked `survey/`, so that variant still degrades tile-by-tile to a silent near-empty store — key on "survey/ persists after loadWindow threw" instead — `src/store_import.cpp:~877`
- [x] (suggestion) reloadEvictedTile loadWindow catch lacks the ambiguous-store guard; safe only by call-ordering invariant — add a note or mirror the guard — `src/store_import.cpp:~676`
- [x] (suggestion) Live node on_configure swallows an ambiguous-store load() throw (WARN + empty start), asymmetric with the importer's loud abort; loses warm-start + catalog silently — add an explicit decision/comment — `src/cube_bathymetry_node.cpp:~402`
- [x] (suggestion) No test exercises the hasAmbiguousSurveyMigration abort/rethrow path — `test/`
- [x] (suggestion) Post-migration Draft shadowing (draft over old coverage hidden under Processed > Draft until reprocessed) — worth an operator-facing note (inherited ADR-0010 D8 semantics) (deferred: out of this round's host scope, which is the 4 ambiguous-store safety-net items; this is a documentation-only, inherited-semantics note with no code change, best folded into the operator-facing store docs rather than this hardening pass)

### Next step

Verdict is **approved** (0 must-fix). Lifecycle: **Local Review** -> push / open PR -> **triage-reviews**. The 5 suggestions are non-blocking hardening/doc items; the host may optionally route them to address-findings first, but none gate the push. This is a lockstep change — the push/PR must co-land with uma#308 (feature/issue-308); host CI should run the combined build.

## Implementation
**Status**: complete
**When**: 2026-08-20 19:42 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-133 at `1ac0e8a` (code; a trailing progress commit follows)
**Addressed**: Local Review (Pre-Push) (2026-08-20 19:17 +00:00, at `d802a30`)
**Commits**: `df83ce8`, `0ae6a90`, `646561e`, `1ac0e8a`

### What landed

Pre-publish hardening round on the ambiguous-store safety-net seam. The four
in-scope suggestions from the round-2 Local Review were **fixed**; the fifth
(operator-facing Draft-shadowing note) is **deferred** with a reason (out of this
round's host scope — documentation-only, inherited ADR-0010 D8 semantics, no code
change). All four fixes build + test clean against the uma#308 store overlay.

- **F1 — generalize the guard to the persisting-`survey/` condition.** Renamed the
  anonymous-namespace `hasAmbiguousSurveyMigration` (both-dirs only) to a public
  `cube::legacySurveyDirPersists(store_dir)` (declared in `store_import.h`) that keys
  on the `survey/` PATH still being present after a load/loadWindow threw — the shared
  signature of all three permanent ADR-0010 D8 migration refusals (symlinked `survey/`,
  ambiguous both-dirs, or an uncommittable rename), since a *successful* migration
  renames `survey/` away. Uses `is_directory` (follows the link) OR `is_symlink` (the
  variant an is_directory-only probe missed and silently degraded tile-by-tile).
  `seedNewTile`'s abort message updated to name the generalized condition. —
  `src/store_import.cpp` (`legacySurveyDirPersists`, `seedNewTile`), `include/cube_bathymetry/store_import.h`
- **F2 — mirror the guard in `reloadEvictedTile`.** Its `loadWindow` catch now runs
  the same `legacySurveyDirPersists` → rethrow-loudly check before the drop-and-retry
  path, with a comment noting it is belt-and-suspenders: by call ordering
  `seedNewTile` would already have aborted an ambiguous store on first touch, but the
  guard keeps the invariant local rather than resting on that ordering. —
  `src/store_import.cpp:~676`
- **F3 — live node `on_configure` fails loudly, symmetric with the importer.** The
  startup-prime catch now, on `legacySurveyDirPersists(draft_dir_)`, logs an
  `RCLCPP_ERROR` explaining the store needs manual attention and returns
  `CallbackReturn::FAILURE` instead of the silent WARN + cold (empty) start — a boat
  operator learns the store must be fixed rather than losing warm-start + the
  disk-serve catalog on every reboot with no signal. Genuine transient/missing-store
  errors keep the WARN-and-continue-empty behavior. — `src/cube_bathymetry_node.cpp:~402`
- **F4 — test the abort/rethrow path.** Two new `ImportEviction` tests:
  `AmbiguousSurveyStoreAbortsImport` (a both-`survey/`-and-`processed/` store — copies
  a written `processed/` to `survey/` — asserts `addBatch` throws with the loud
  `ABORTING` message) and `SymlinkedSurveyStoreAbortsImport` (a symlinked `survey/`
  with no `processed/` dir — the variant the old both-dirs check would miss — asserts
  it also aborts), each first asserting `legacySurveyDirPersists` detects the store. —
  `test/test_import_eviction.cpp`

### Build Verification — Option A (local, issue-308 store overlay)

Same overlay as the prior Implementation entries: `/opt/ros/jazzy` + the built
**uma#308 `feature/issue-308` core** (`issue-unh_marine_autonomy-308/core_ws/install`;
installed `marine_bathymetry_store` confirmed split with the public
`BathymetryStore::clearOverlappedDraft`). Rebuilt and tested `cube_bathymetry` against
that overlay.

**Result**: build OK (only pre-existing unrelated `-Wunused-but-set-variable` warning
in `test_tile_eviction_rss.cpp`); `colcon test` → **29/29 CTest suites passed, 0
failed** (all linters clean — cpplint/uncrustify/cppcheck/flake8/etc.; gtest 21
executables). The two new gtest cases ran and passed (ImportEviction suite now 15
tests, was 13). Pre-commit hooks ran on every commit; none used `--no-verify`.

### Lockstep note

Unchanged: this PR cannot build against the pre-split main-tree jazzy core; it must
co-land with uma#308 (feature/issue-308). Verified locally against that store — host
CI should run the combined build.

### Actions
- [x] Generalize the ambiguous-store guard to any persisting `survey/` layer (symlink + both-dirs + uncommittable rename) — `src/store_import.cpp` (`legacySurveyDirPersists`), `include/cube_bathymetry/store_import.h`
- [x] Mirror the guard in `reloadEvictedTile` (belt-and-suspenders + call-ordering note) — `src/store_import.cpp:~676`
- [x] Live node `on_configure` fails loudly on an ambiguous/refused store (ERROR + `CallbackReturn::FAILURE`), symmetric with the importer's abort — `src/cube_bathymetry_node.cpp:~402`
- [x] Test the abort/rethrow path — both-dirs and symlinked variants — `test/test_import_eviction.cpp`
- [x] Post-migration Draft-shadowing operator note (deferred: out of this round's host scope; documentation-only, inherited ADR-0010 D8 semantics, no code change — fold into operator-facing store docs)

### Next step

Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 133 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-20 19:50 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-133 at `881429c`
**Mode**: pre-push
**Depth**: Deep (reason: 200+ lines / 10+ files; safety-relevant persistence, eviction, and lifecycle paths)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 3 | **Ship**: recommended — 0 must-fix; round-1 fused-prime must-fix stays resolved + test-pinned; round-2's 4 hardening suggestions all landed (F1-F4); this round's 2 items are optional low-value defensive hardening on an already-solid safety net.

Specialists: static analysis run (ament_cpplint/uncrustify clean on changed lines — HEAD byte-identical to last-tested `1ac0e8a`, only progress.md differs; cppcheck's 3 `useStlAlgorithm` hits all on untouched context lines, dropped); 2 Claude Adversarial passes (Lens A logic + Lens B systemic) — both surfaced only false positives (a non-existent OOB: line 247 short-circuits `k < mask_depth->size()` before indexing; and a "use RCLCPP_WARN" that misreads the offline import_bag library `store_import.cpp` — 0 rclcpp usages, 8 existing std::cerr — as a ROS node) plus the 2 hardening suggestions below. Copilot off (default). Local Adversarial skipped (Ollama not reachable on :11434). Store API (clearOverlappedDraft / save-dirty-only / SourceLayer ordinals / survey->processed migration refusals) re-verified against the built uma#308 (feature/issue-308) install headers. Build relies on the prior Implementation entry's clean run (29/29 CTest suites) against the uma#308 overlay — source unchanged since.

### Findings
- [ ] (suggestion) `legacySurveyDirPersists` ignores the `error_code`s from `is_directory`/`is_symlink`; a genuine stat failure returns false and silently degrades instead of the loud abort — fail safe toward abort on ec set — `src/store_import.cpp:910`
- [ ] (suggestion) Defensive `assert(!mask || mask->index() == tile.index())` in `primeFromTileSkippingMask` to pin the same-GridIndex mask contract (all current callers already satisfy it) — `src/store_import.cpp:222`

### Next step
Verdict is **approved** (0 must-fix). Lifecycle: **Local Review** -> push / open PR -> **triage-reviews**. The 2 suggestions are optional, non-blocking hardening; the host may route them to address-findings or defer. Lockstep change: the push/PR must co-land with uma#308 (feature/issue-308); host CI should run the combined build.

## Integrated Review
**Status**: complete
**When**: 2026-08-20 16:08 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #134 at `9f98343`
**Sources**: 3 (Copilot R1 @ `9f98343`, Local Review (Pre-Push) R3 @ `881429c`, CI rollup)
**Cross-source confirmations**: 1
**CI**: failures-noted (expected co-land break, see below)

Copilot reviewed all 12 files, 2 inline comments, 0 conversation comments. R3
(`881429c`) is one progress-only commit behind head, so its findings describe
the identical code Copilot reviewed. Copilot's error_code comment independently
re-derives R3's first open suggestion — a cross-source confirmation and the
strongest signal this round.

**CI**: the `ROS 2 Jazzy (industrial_ci)` failure is the EXPECTED lockstep
break, not a regression: hosted CI builds against jazzy's pre-split
`marine_bathymetry_store` where `SourceLayer::Processed` does not exist
(`'Processed' is not a member of SourceLayer`). It resolves when
rolker/unh_marine_autonomy#313 (uma#308 D8 split) merges — the co-land partner
this PR's plan and every prior review entry already document. Do not gate on it
locally; the merge gate is the combined build.

### Findings
- [x] (cross-confirmed: Copilot + Local Review R3) `legacySurveyDirPersists`
  ignores the `std::error_code`s from `is_directory`/`is_symlink`; a genuine
  stat failure (EACCES/EIO/ELOOP) returns false and the callers treat a
  permanent migration refusal as transient — silently degrading instead of the
  loud abort the function exists to guarantee (ADR-0010 D8 fail-loud intent).
  Fail safe toward abort when either ec is set. Note the short-circuit: when
  `is_directory` returns true, `is_symlink` never runs — restructure to check
  `dir_ec` before probing the symlink, rather than OR-ing both ecs at the end.
  `fs::is_directory(p, ec)` does NOT set ec for a plain nonexistent path
  (not_found is not an error), so ec-set = genuine stat failure and the normal
  fresh-store path cannot misfire. — `cube_bathymetry/src/store_import.cpp:910`
- [x] (minor, Copilot) Unused structured binding `tile` in the `seed_catalog`
  lambda's `for (const auto & [tile_index, tile] : tiles)` — dead name; iterate
  the pair and use `.first` (or equivalent). Copilot's sub-claim that it
  "introduces an unused-variable warning" is inaccurate: GCC/Clang under
  `-Wall -Wextra` do not warn when only one name of a decomposition is unused,
  and the prior clean build (29/29 suites) confirms none fired; the pattern is
  also inherited verbatim from the removed pre-split loop. Valid as a trivial
  dead-code cleanup, not a warning fix. — `cube_bathymetry/src/cube_bathymetry_node.cpp:375`
- [x] (suggestion, Local Review R3 — not raised by Copilot) Defensive
  `assert(!mask || mask->index() == tile.index())` in
  `primeFromTileSkippingMask` to pin the same-GridIndex mask contract (all
  current callers satisfy it). Optional hardening; carried forward so the open
  set lives in one entry. — `cube_bathymetry/src/store_import.cpp:222`

### False positives
- (CI rollup) `ROS 2 Jazzy (industrial_ci)` red is not a finding against this
  PR — it is the documented expected co-land break (hosted CI lacks the D8
  `Draft`/`Processed` split until rolker/unh_marine_autonomy#313 merges);
  classified per the lockstep contract in plan.md and all three pre-push
  review entries.

### Next step
Lifecycle: **Integrated Review** -> address-findings. Recommended: fix the
cross-confirmed `legacySurveyDirPersists` ec hardening + the trivial unused
binding in one small commit (the R3 assert item may ride along or stay
deferred — optional). Then re-run local build/tests against the uma#308
overlay and push. Merge gate remains the combined co-land with
rolker/unh_marine_autonomy#313.

## Implementation
**Status**: complete
**When**: 2026-08-20 20:16 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-133 at `baec567` (code; a trailing progress commit follows)
**Addressed**: Integrated Review (2026-08-20 16:08 -04:00, PR #134 at `9f98343`)
**Commits**: `0393e13`, `baec567`

### What landed

All three open findings from the latest `## Integrated Review` were **fixed**
(none deferred), in two atomic commits split by file. Rebuilt + retested against
the uma#308 (`feature/issue-308`) store overlay — all tests green.

- **F1 (cross-confirmed: Copilot + Local Review R3) — `legacySurveyDirPersists`
  fails loud on genuine stat errors.** The probe now fails SAFE toward the
  ADR-0010 D8 loud abort: `is_directory(survey, dir_ec)` is checked together with
  `dir_ec` BEFORE the symlink probe (`fs::is_directory(survey, dir_ec) || dir_ec`),
  and the symlink fallback likewise treats a set `link_ec` as a positive. Since
  `fs::is_directory`/`fs::is_symlink` do NOT set the ec for a plain nonexistent
  path (`not_found` is not an error), a set ec is a REAL stat failure
  (EACCES/EIO/ELOOP) — so a permanent migration refusal can no longer be misread
  as "no survey/, store is clean" and silently degrade to a transient per-tile
  skip. Checking `dir_ec` before the symlink probe (rather than OR-ing both ecs at
  the end) matches the short-circuit: `is_directory` follows the link and returns
  true for a symlinked-to-real-dir `survey/`, so the symlink probe only runs when
  `is_directory` was a clean false. The normal fresh-store path (no `survey/`)
  cannot misfire. — `src/store_import.cpp:889` (`legacySurveyDirPersists`)
- **F2 (minor, Copilot) — unused `tile` binding dropped.** The `seed_catalog`
  lambda decomposed each map entry as `[tile_index, tile]` but used only
  `tile_index`; now iterates the pair and takes `.first`. Trivial dead-code
  cleanup, no behavior change. — `src/cube_bathymetry_node.cpp:375`
- **F3 (suggestion, Local Review R3) — same-GridIndex mask contract asserted.**
  Added `assert(!mask || mask->index() == tile.index())` (plus `<cassert>`) at the
  top of `primeFromTileSkippingMask`, pinning the invariant the cell-for-cell `k`
  indexing (`mask_depth[k]`) relies on. All current callers satisfy it (each pairs
  a Draft tile with its same-index Processed tile); the assert trips a future
  violating caller in debug rather than silently masking the wrong cells. The full
  test run (asserts enabled) passed, confirming no current caller violates it. —
  `src/store_import.cpp:222` (`primeFromTileSkippingMask`)

### Build Verification — Option A (local, issue-308 store overlay)

Same overlay as the prior Implementation entries: `/opt/ros/jazzy` + the built
**uma#308 `feature/issue-308` core**
(`issue-unh_marine_autonomy-308/core_ws/install`; installed
`marine_bathymetry_store` confirmed split with the public
`BathymetryStore::clearOverlappedDraft`; `geodesy` resolved from `/opt/ros/jazzy`).
Rebuilt and tested `cube_bathymetry` against that overlay.

**Result**: build OK; `colcon test` → **556 tests, 0 errors, 0 failures, 68
skipped**. The new `assert` did not fire (asserts are compiled in for the test
build), confirming the same-GridIndex mask contract holds for every caller
exercised. Pre-commit hooks (uncrustify/cpplint) ran on both fix commits; none
used `--no-verify`.

### Lockstep note

Unchanged: this PR cannot build against the pre-split main-tree jazzy core; it
must co-land with uma#308 (feature/issue-308) — the hosted `ROS 2 Jazzy
(industrial_ci)` red is the expected co-land break, not a regression. Verified
locally against the split store; host CI runs the combined build.

### Actions
- [x] `legacySurveyDirPersists` fails loud on genuine stat errors (check `dir_ec` before the symlink probe) — `src/store_import.cpp:889`
- [x] Drop the unused `tile` structured binding in `seed_catalog` (iterate the pair, use `.first`) — `src/cube_bathymetry_node.cpp:375`
- [x] Defensive `assert(!mask || mask->index() == tile.index())` in `primeFromTileSkippingMask` — `src/store_import.cpp:222`

### Next step

Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to
a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 133 --skill review-code

Do not push — the host pushes to PR #134.

## Implementation
**Status**: complete
**When**: 2026-08-20 20:58 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-133 at `712fccd` (code; a trailing progress commit follows)
**Addressed**: Copilot review comment on PR #134 (`batch_regen.cpp:258`), host-verified
valid. NOTE: this finding post-dates the latest `## Integrated Review`
(2026-08-20 16:08 -04:00, whose 3 findings are all resolved/checked) and was **not**
separately triaged into a formal review entry — it arrived as a fresh Copilot PR
comment and its fix was partially staged in the worktree. Per the address-findings
`progress_read.py` parse, the latest review entry has **0 open findings**; this pass
therefore completes an out-of-band partial fix rather than a checkbox in a review
entry. The finding and the existing edits were re-verified independently against the
source before completing them.
**Commits**: `da339dd`, `712fccd`

### What landed

The pre-staged edits (batch_regen preflight + a refuses-before-writing test) were
reviewed against the finding and found correct in intent but **incomplete**: building
and testing them exposed a genuine, platform-dependent bug in `legacySurveyDirPersists`
that broke every clean store. Both the preflight and the newly-discovered bug are
fixed; all 557 cube_bathymetry tests pass against the split store overlay.

- **Finding fix — batch_regen refuses a persisting legacy `survey/` before any write.**
  `BatchRegen`'s constructor now runs a preflight (`legacySurveyDirPersists(cfg_.store_dir)`)
  and throws before a single tile is scattered or written. batch_regen is a write-path
  tool that never `load()`s the output store, so the ADR-0010 D8 `survey/`→`processed/`
  auto-migration never fires from it; without this guard the first `finalize()` write
  would create `processed/` alongside a surviving `survey/` — the ambiguous both-dirs
  state every future `load()` permanently refuses (a nightly regen would silently brick
  the store). The error tells the operator to migrate first (run a load-path tool, or
  rename `survey/` manually). Reuses the shared helper, honoring the documented
  "write-path tools do not migrate" invariant. — `src/batch_regen.cpp:96-119`
- **Prerequisite bug fix (discovered during build/test) — `legacySurveyDirPersists`
  treated a clean store as ambiguous on this libstdc++.** The prior round's "F1"
  hardening (Integrated Review, commit `baec567`) used `if (fs::is_directory(survey, ec)
  || ec) return true;` on the stated assumption that `is_directory(p, ec)` leaves `ec`
  unset for a nonexistent path. That assumption is **false on this platform**: an
  empirical check (and the 4 failing `BatchRegen …ExactMatch` tests) confirm
  `fs::is_directory`/`is_symlink` set `ec = ENOENT` (or `ENOTDIR`) for an absent
  `survey/`, so the guard returned `true` for **every** store without a `survey/`
  layer. The bug stayed latent because the only callers were catch-block guards
  (`seedNewTile` / `reloadEvictedTile` / node `on_configure`) that run *after* a real
  `loadWindow` throw — so it never hit the happy path until this preflight became the
  first unconditional caller. (Left unfixed it would also convert a transient
  per-tile `loadWindow` error on a clean store into a fatal import abort.) The fix
  classifies the `error_code`: `ENOENT`/`ENOTDIR` ⇒ `survey/` genuinely absent (clean
  store ⇒ `false`); any other error (EACCES/EIO/ELOOP) still fails safe toward the
  ADR-0010 D8 loud abort. Symlinked / both-dirs refusal detection is unchanged. —
  `src/store_import.cpp:898-937`
- **Test.** `BatchRegen.LegacySurveyStoreRefusedBeforeAnyWrite`: a store with a
  populated `survey/` makes construction throw (`std::runtime_error`) and asserts no
  `processed/` layer is created (refuses before any write). The existing
  `ImportEviction.{AmbiguousSurveyStoreAbortsImport,SymlinkedSurveyStoreAbortsImport}`
  tests continue to pass, confirming the ec fix did not weaken the real-`survey/`
  refusal path; the 4 `BatchRegen …ExactMatch` tests (clean stores) now pass instead
  of throwing. — `test/test_batch_regen.cpp`

### Build Verification — Option A (local, split store overlay)

The prior Implementation entries' `issue-unh_marine_autonomy-308` worktree is gone, so
the split store was rebuilt from the now-merged source in `main/core_ws`
(`marine_bathymetry_store` confirmed split: `SourceLayer::Processed=0`/`Draft=1`, public
`clearOverlappedDraft`). Overlay: `/opt/ros/jazzy` + `main/underlay_ws/install`
(`geodesy` built for `geodesics.h`) + the built `main/core_ws/install` (9-package
up-to set: stores + `marine_survey_index` and deps; a stale pre-geodesy CMake cache on
`marine_sidescan_mosaic` was cleaned first). Rebuilt and tested `cube_bathymetry`
against that overlay (its stale build dir, cached against the deleted 308 overlay, was
cleaned first).

**Result**: build OK (only the pre-existing unrelated `-Wunused-but-set-variable` in
`test_tile_eviction_rss.cpp`); `colcon test` → **557 tests, 0 errors, 0 failures, 68
skipped**. `ament_uncrustify`/`ament_cpplint` clean on all three changed files
(test file reformatted before commit). No `--no-verify`.

### Lockstep note

Unchanged: this PR cannot build against the pre-split main-tree jazzy core; it co-lands
with uma#308 (feature/issue-308) — now merged to `main/core_ws` (the split source this
build used). The hosted `ROS 2 Jazzy (industrial_ci)` red remains the expected co-land
break until that lands in CI's core. Verified locally against the split store.

### Actions
- [x] batch_regen refuses a persisting legacy `survey/` before any write (constructor preflight) — `src/batch_regen.cpp:96-119`
- [x] Fix `legacySurveyDirPersists` ec classification so an absent `survey/` (ENOENT/ENOTDIR) reads as a clean store, not an abort — `src/store_import.cpp:898-937`
- [x] Refuses-before-writing test + confirm existing abort/ExactMatch tests still pass — `test/test_batch_regen.cpp`

### Next step

Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 133 --skill review-code

Note for the re-review: the `legacySurveyDirPersists` ec fix is broader than the
original batch_regen finding (it corrects previously-reviewed code from round `baec567`)
because the preflight exposed that latent bug — re-read it on its own merits. Do not
push — the host pushes to PR #134.
