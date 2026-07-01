---
issue: 96
---

# Issue #96 — Adopt simplified cube stores: seed precedence, batch-regen, sufficient-statistic backscatter

## Issue Review
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #96
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

The three components (seed precedence, batch-regen, backscatter bands) are tightly
interdependent — all three consume the new uma#248 store format and share the
tile-reload path. Grouping them is appropriate. The batch-regen scatter/gather path
is the most novel and complex piece; it is explicitly scoped by an "bit-exact"
acceptance criterion that makes validation concrete.

**Hard dependency: unh_marine_autonomy#248 must land first.** The two named store
layers (`survey` + `reference`, finalized names per the issue header note) and the
3-band backscatter sufficient-statistic format are defined there. Implementation
cannot begin until uma#248 is merged.

**Right repo?** Yes — cube_bathymetry project repo. All three parts are CUBE-side
processing changes.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | `--help` + README coverage is an acceptance criterion; seed precedence semantics clearly specified with per-rung behavior |
| Enforcement over documentation | Watch | Acceptance criteria imply testable behaviors (bit-exact regen, Welford round-trip, n=1 sentinel) but test coverage for seed-precedence rung semantics (reference layer must NOT be accumulated as measured data) is not explicitly stated as a test requirement |
| Capture decisions, not just implementations | Action needed | Issue references "ADR-0002 (store) — addendum expected" but no ADR-0002 exists in `docs/decisions/`; the existing ADR-0007 addendum file notes that the canonical `0007-mbes-backscatter-store.md` has not yet been authored. Implementation must resolve which ADR documents are authored/addended (see Actions) |
| A change includes its consequences | OK | Acceptance criteria cover ops script (`build_massabesic_store.sh`), help/README, backscatter round-trip test |
| Only what's needed | OK | Explicitly greenfield; scoped to what uma#248 enables; no backwards-compatibility required |
| Improve incrementally | Watch | Scope is substantial (seed precedence + batch-regen + backscatter), but all three parts share the same store-interface change and cannot be split without partial uma#248 adoption — grouping is justified |
| Test what breaks | Watch | "bit-exact vs never-evicted pass" and "Welford round-trip incl. n=1 sentinel" are good regression targets; explicitly add a test asserting reference-layer seed does not increment accumulated sample counts |
| Workspace vs. project separation | OK | All changes in cube_bathymetry project repo |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Workspace ADR-0001 (adopt ADRs) | Yes | Design decisions being made; addendums expected as part of this work |
| Workspace ADR-0002 (worktree isolation) | Yes | Feature worktree already exists |
| cube_bathymetry ADR-0001 (lossless reload + eviction) | Yes | Seed precedence generalizes the startup prime and revisit-reload paths in ADR-0001 §1/§2; addendum required to document the two-rung precedence semantics and the `seed_settled` flag distinction |
| cube_bathymetry ADR-0007 addendum (MBES backscatter) | Yes | The sufficient-statistic backscatter bands (mean, confidence-scaled standard_error, sample_sd) extend Phase B.2 (Welford spill/restore); the Welford reconstruction formula (`n=(sample_sd/standard_error)^2`, M2, n=1 sentinel) is a new design decision that needs ADR coverage |

**Gap**: "ADR-0002 (store)" cited in the issue is unresolvable as written — no `0002-*.md` exists in `docs/decisions/`. This is likely the not-yet-authored canonical `docs/decisions/0007-mbes-backscatter-store.md` mentioned in the existing ADR-0007 addendum file, or a new addendum to ADR-0001. The plan phase must decide and document which files are created/updated.

### Consequences (from workspace map)

- New store-layer names (`survey` + `reference`) replace prior placeholder names (`cube` + `pre-existing`) throughout — ensure all code paths, docs, and scripts use the finalized names.
- The ops script `build_massabesic_store.sh` (unh_echoboats_project11 #352/PR#353) is an explicit acceptance criterion dependency; changes to the importer CLI (`--seed` flags or equivalent) must keep that script working.
- Downstream: unh_marine_autonomy#247 (sidescan) is parked behind this; no action needed here, but plan/review should note the interface this issue establishes is a dependency for that work.

### Actions
- [ ] Resolve the "ADR-0002 (store)" reference: decide whether to author `docs/decisions/0002-store-layer-design.md` (new) or add an addendum to ADR-0001 and the canonical ADR-0007, and state that decision in the plan.
- [ ] Add an explicit test for reference-layer seed precedence semantics: assert that a tile seeded from the `reference` layer does not contribute to accumulated sample counts (i.e., `seed_settled=false` path is not counted as measured data).
- [ ] Confirm that all identifiers, CLI flags, and store-path logic use the finalized layer names `survey` + `reference` (not the old `cube` + `pre-existing`).
- [ ] Verify uma#248 has landed before beginning implementation; gate the plan on this dependency.

## Plan Authored
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-96/plan.md` at `795b888`
**Branch**: feature/issue-96 at `795b888`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-07-01 11:54 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-96/plan.md` at `795b888`
**PR**: PR-less (`--issue 96`)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) File table omits three compile-breaking files that use removed `SourceLayer::Draft`/`Chart`: `src/cube_bathymetry_node.cpp`, `test/test_persistence.cpp`, `test/test_tile_eviction_rss.cpp` — `plan.md:104-116`
- [ ] (must-fix) Live-node `Draft`→`Survey` layer switch is an unstated behavioral consequence; confirm/document the live node's target layer — `plan.md:29-42`
- [ ] (suggestion) `welfordFromCell` collapses `n→1` when `sample_sd==0` for `n≥2` (identical samples); guard or document in ADR-0007 addendum — `plan.md:54-60`
- [ ] (suggestion) Explicitly resolve the issue's "ADR-0002 (store)" citation as addressed via ADR-0001/0007 addenda — `plan.md:129-134`

## Implementation
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Opus

**Branch**: feature/issue-96 (5 new commits: `ae5f68c`..`e022eb5`, on top of plan `6b92cb1`)

### What was implemented (all plan steps)

1. **uma#248 migration (compile fix).** `SourceLayer` Draft/Processed/Chart →
   Survey/Reference; `BathyCell` 2-band; `MbesCell` 3-band; per-cell
   `SourceRegistry` → store-level `StoreMetadata`. Touched `store_import.{h,cpp}`,
   `cube_bathymetry_node.cpp` (live node now writes the **`survey`** layer,
   operator-confirmed), `import_bag_main.cpp`, and all four affected tests.
2. **Backscatter 3-band write** (`geoGridToBackscatterCells`): n=1 sentinel +
   n≥2 formulas, `kBackscatterConfidenceScale = 1.96f` shared constant.
3. **Backscatter Welford reconstruction** (`welfordFromCell`): exact inverse;
   n≥2 identical-sample limitation documented in the ADR-0007 addendum (not
   code-guarded, as planned).
4. **Seed precedence** (`ImportAccumulator::seedNewTile` + `seeded_` set): survey
   warm-start (settled + backscatter Welford restore) > reference gate
   (predicted-only, not measured, no backscatter) > blank. `import_bag` gains
   `--reference-store` (replacing `--prior`); the upfront whole-sheet load is gone.
5. **Batch-regen**: `BatchRegen` (`batch_regen.{h,cpp}`, in the store_import
   library) + `batch_regen_bag` binary (`batch_regen_main.cpp`) + CMake target.
   Scatter to per-tile disk buckets → gather each tile in one unbounded pass →
   write once via new `ImportAccumulator::persistResidentTile`. Scratch dir cleaned
   up in `finalize`.
6. **Tests**: `test_batch_regen` (byte-exact vs single-pass unbounded, multi-tile
   + revisit + scratch cleanup); `test_import_eviction.ReferenceSeedDoesNotAddMeasuredData`;
   `test_store_import` Welford round-trips (`WelfordFromCellInvertsEncode`,
   `BackscatterWelfordRoundTripMultiSample`, n=1 sentinel in `BackscatterCellsMatchGridRecords`).
7. **Docs**: ADR-0001 addendum (two-rung seed precedence + `seed_settled` contract
   + batch-regen exact path + live-node→survey); ADR-0007 addendum (3-band
   write/reconstruct formulas + n=1 sentinel + n≥2 identical-sample limitation);
   README offline-import/rebuild + seed-precedence + backscatter-fidelity section.

### Build / test status — GREEN (built in-container)

The store libs (`marine_bathymetry_store`, `marine_mbes_backscatter_store` and deps)
were built from `layers/main/core_ws` source in-container, then `cube_bathymetry`
built and tested against them:

- `cube_bathymetry`: **build GREEN**, **tests GREEN** — 441 tests, 0 failures, 58
  skipped (skips are unrelated pre-existing); lint (uncrustify + cpplint) clean on
  all new/changed files. `batch_regen`'s 3 bit-exact-match tests pass.

### Deviations (also noted in plan.md § Deviations)

- Registry migration to `StoreMetadata` was required for compilation but unlisted
  in the plan; `finalize()` signature changed to `StoreMetadata *` pointers.
- Removed dead `timestamp_ns`/`source_index` params from the conversion functions
  and `ImportAccumulatorConfig` (the new cells don't carry them).
- Dropped `--source-id`/`--sensor-class` from `import_bag` (greenfield; no per-cell
  source id). `BatchRegen` added as a library class (not just a binary) so it is
  unit-testable.

### Host must finish

- **Out of scope (follow-up ops PR, different repo):** `build_massabesic_store.sh`
  in unh_echoboats_project11 must drop `--bathy-layer`/`--prior`/`--source-id`/
  `--sensor-class` and switch to `--reference-store`; it will otherwise error on the
  removed flags. Provenance now flows through `--platform`/`--sensor`/`--campaign`.
- Push + open PR (local-first; not pushed by this sub-agent).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-01 15:30 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-96 at `0a83ad0`
**Mode**: pre-push
**Depth**: Deep (reason: +2688/-354 across 18 files, 2 new ADRs, store-format migration + new binary)
**Must-fix**: 2 | **Suggestions**: 6
**Round**: 1 | **Ship**: continue — a real correctness gap in the headline "bit-exact" contract warrants a fix + re-review

### Findings
- [x] (must-fix) Batch-regen not bit-exact near seams: scatter routes each sounding to its ±1-cell window, but `GeoGrid::insert` deposits up to `max_radius=CONF_99PC·√(horizontal_error)` (multiple cells for realistic TPU); single-pass gets far-radius cross-sounding deposits the gather omits. Tests use he=0.1 (sub-cell) so they mask it — `cube_bathymetry/src/batch_regen.cpp:162`
- [x] (must-fix) `welfordFromCell` UB: unguarded `se=standard_error/kScale` division when `standard_error==0` & `sample_sd!=0` → `lround(inf)` + out-of-range uint32 cast; add `se==0→n=1` guard (cross-confirmed both lenses) — `cube_bathymetry/src/store_import.cpp:199`
- [x] (suggestion) Re-running batch-regen into a non-empty `-o` store silently blends (seedNewTile warm-starts output-store survey tiles) instead of exact rebuild; warn/guard/`--append` — `cube_bathymetry/src/batch_regen.cpp` finalize
- [x] (suggestion) `closeAllStreams` doesn't check ofstream close-time flush; a disk-full final flush silently truncates a bucket → silently-wrong tile — `cube_bathymetry/src/batch_regen.cpp:181`
- [x] (suggestion) `seedNewTile` swallows a survey-seed load failure then overwrites the tile from scratch, discarding existing coverage on a transient read error (asymmetric with reload path) — `cube_bathymetry/src/store_import.cpp:584`
- [x] (suggestion) `batch_regen_main` `std::stod`/`std::stoi` unguarded → `std::terminate` on a bad flag value (mirrors import_bag) — `cube_bathymetry/src/batch_regen_main.cpp:343`
- [x] (suggestion) `-r` resolution not validated `> 0` before driving GGGS level — `cube_bathymetry/src/batch_regen_main.cpp:343`
- [x] (suggestion) `batch_regen_main.cpp` duplicates ~800 lines of `import_bag_main.cpp` (BagReaders/speedAt/projection pipeline); extract shared helper — `cube_bathymetry/src/batch_regen_main.cpp` (deferred: extracting BagReaders + speedAt + the projection pipeline into a shared helper is a substantial structural refactor with its own review/test risk, out of proportion to this fix pass; tracked as a follow-up)
- [x] (watch, pre-existing/out-of-scope) `marine_tiled_raster_store::saveTile` writes direct to the final path (no temp-then-rename); the "atomic … never corrupts the surface" comments (from #92) overstate the durability #96's gather/eviction leans on — follow-up to make the write atomic or fix the comments (partially addressed: corrected the overstated cube-side comments in `store_import.{cpp,h}` and `cube_bathymetry_node.cpp` to say saveTile is a direct, flush/close-checked, NOT crash-atomic write; deferred: making the write atomic is a `marine_tiled_raster_store` change in another repo — out of scope for this worktree, tracked as a follow-up)

## Implementation
**Status**: complete
**When**: 2026-07-01 16:11 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-96 at `30136ff`   <!-- PR-less (`--issue 96`) -->
**Addressed**: Local Review (Pre-Push) — 2026-07-01 15:30 +00:00, branch feature/issue-96 at `0a83ad0`
**Commits**: `d68476f` (store_import), `83fc027` (batch_regen), `30136ff` (batch_regen_main)

### Actions
- [x] (must-fix) Batch-regen bit-exact near seams — `addBatch` now routes the WHOLE batch to every tile in the batch's one-cell-expanded bounds, mirroring `GeoMapSheet::addSoundings` exactly, so a near-seam sounding's far-radius (`max_radius=CONF_99PC·√(horizontal_error)`) cross-sounding deposits are no longer dropped; the gather's `GeoGrid::insert` trims the superset bucket to the exact deposit set. Adds `SeamCrossingExactMatch` (multi-cell radius across a real GGGS seam) — `cube_bathymetry/src/batch_regen.cpp`, `test/test_batch_regen.cpp`
- [x] (must-fix) `welfordFromCell` `se<=0`/non-finite guard → `n=1` sentinel, avoiding `lround(inf/NaN)` UB + out-of-range uint32 cast — `cube_bathymetry/src/store_import.cpp`
- [x] (suggestion) Batch-regen exact rebuild: gather sets `skip_survey_seed` so it never blends onto tiles it rebuilds, and warns when the output survey layer is non-empty — `cube_bathymetry/src/batch_regen.cpp`, `include/cube_bathymetry/store_import.h`, `src/store_import.cpp`
- [x] (suggestion) Bucket close-time flush now checked — `flushOpenStreams` before gather + a flush/check on LRU eviction; a disk-full truncation is a hard error, not a silently-wrong tile — `cube_bathymetry/src/batch_regen.cpp`
- [x] (suggestion) `seedNewTile` protective on a survey-seed READ error — returns false so the caller drops the tile like a failed reload, protecting the intact-but-unreadable on-disk surface — `cube_bathymetry/src/store_import.cpp`
- [x] (suggestion) `batch_regen_main` numeric parsing guarded — `std::stod`/`std::stoi` via helpers that catch and route to `usage()` (and reject trailing junk) instead of `std::terminate` — `cube_bathymetry/src/batch_regen_main.cpp`
- [x] (suggestion) `batch_regen_main` rejects non-positive `-r` resolution up front — `cube_bathymetry/src/batch_regen_main.cpp`
- [x] (suggestion) `batch_regen_main`/`import_bag_main` ~800-line duplication — `cube_bathymetry/src/batch_regen_main.cpp` (deferred: substantial structural refactor with its own review/test risk, out of proportion to this fix pass; tracked follow-up)
- [x] (watch) `saveTile` "atomic … never corrupts" comments overstate durability — corrected the cube-side comments to say the write is direct + flush/close-checked but NOT crash-atomic — `src/store_import.cpp`, `include/cube_bathymetry/store_import.h`, `src/cube_bathymetry_node.cpp` (deferred: making the write crash-atomic is a `marine_tiled_raster_store` change in another repo, out of scope for this worktree)

### Build / test — GREEN
Store libs (`marine_bathymetry_store`, `marine_mbes_backscatter_store`,
`marine_tiled_raster_store`) built from this worktree's `core_ws` source, then
`cube_bathymetry` built + tested against them:
- **build GREEN** (only pre-existing warnings: `tmpnam` in `test_angular_response_curve`,
  unused-var in `test_tile_eviction_rss`).
- **tests GREEN** — full suite **442 tests, 0 failures, 58 skipped** (skips pre-existing).
  `BatchRegen.SeamCrossingExactMatch` passes (asserts ≥2 tiles spanned and bit-exact
  vs single-pass at a ~3-cell deposit radius); `test_store_import` (Welford round-trips)
  and `test_import_eviction` green.
- **lint** — uncrustify + cpplint clean on all changed files.

### Deferred (follow-ups, not done here)
- Extract a shared `BagReaders`/`speedAt`/projection helper from
  `batch_regen_main.cpp` + `import_bag_main.cpp` (finding #8).
- Make `marine_tiled_raster_store::saveTile` crash-atomic (temp-then-rename) so the
  "atomic" durability the gather/eviction leans on is real (finding #9, other repo).

### Next step
Lifecycle: **Implementation → review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 96 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-01 16:25 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-96 at `16b0de1`
**Mode**: pre-push
**Depth**: Deep (reason: store-format migration + new binary + 2 ADR addenda, +3002/-354)
**Must-fix**: 1 | **Suggestions**: 3
**Round**: 2 | **Ship**: recommended — one mechanical read-back guard; must-fix fell 2→1, not a design question

### Findings
- [ ] (must-fix) Gather read-back swallows I/O errors, breaking the bit-exact contract on the error path: read-open failure on a known-created bucket is `continue`d and `in.read` can't tell `badbit` from EOF → silently missing/truncated tile; guard symmetric with the hard-throwing write path — `cube_bathymetry/src/batch_regen.cpp:274-281`
- [ ] (suggestion) `closeAllStreams` discards close-time errors, safe only because `flushOpenStreams` runs first; bind that ordering (assert/comment or check-and-throw) — `cube_bathymetry/src/batch_regen.cpp:224-228`
- [ ] (suggestion) `import_bag_main` still uses raw `std::stod`/`std::stoi`/`std::stoll` → `std::terminate` on bad CLI value; mirror the guarded parse helpers added to the sibling `batch_regen_main` — `cube_bathymetry/src/import_bag_main.cpp:351-382`
- [ ] (suggestion) `topic_info` loop var can be `const &` (cppcheck); folds into the tracked ~800-line dedup follow-up — `cube_bathymetry/src/batch_regen_main.cpp:183`

### Notes
- Round 1's two must-fixes (near-seam bit-exactness, `welfordFromCell` division UB) verified correctly fixed under adversarial tracing (Lens A + Lens B) with new regression tests.
- Static analysis: cpplint + uncrustify clean; cppcheck 2 trivial style notes (finding above + a `useStlAlgorithm` nit dropped as below threshold).
- Governance Watch: `build_massabesic_store.sh` (unh_echoboats_project11) flag migration is a real breaking consequence, documented as an out-of-scope follow-up ops PR.
- Base `origin/jazzy` reviewed from local ref (remote fetch failed on host-key verification — possibly stale).

### Next step
Lifecycle: **Local Review** → `address-findings` (verdict changes-requested) → re-run `review-code` → push / open PR → `triage-reviews`.
Ship advisory is **recommended** after the one mechanical must-fix lands; the host decides.
