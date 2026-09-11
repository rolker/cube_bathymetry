---
issue: 137
---

# Issue #137 — Blunder gate cannot use an ENC chart layer: Chart priming is exact-level only, so a multi-level chart prior silently gates nothing

## Issue Review
**Status**: complete
**When**: 2026-08-25 23:41 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #137
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] No actions — issue is plan-task-ready.

## Plan Authored
**Status**: complete
**When**: 2026-08-25 23:45 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-137/plan.md` at `5e8cdf6`
**Branch**: feature/issue-137 at `5e8cdf6`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-08-25 23:49 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-137/plan.md` at `5e8cdf6`
**PR**: PR-less
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) `testing::internal::CaptureStderr()`/`GetCapturedStderr()` is not used anywhere else in `cube_bathymetry/test/*.cpp` today — the plan frames `NoUsablePriorEmitsWarning` as mirroring existing precedent, but the stderr-capture mechanism itself is new to this test file (it is a standard, well-supported gtest facility and `ament_add_gtest`-linked binaries already pull in gtest internals, so this is low-risk — just note it's not literally precedented like the other three tests). — `plan.md` step 5, `NoUsablePriorEmitsWarning`
- [ ] (suggestion) The forward declaration/comment block for `primePriorLayersForTile` at `store_import.cpp:633-636` (documenting the `read_ok` out-param contract) is a second comment site describing the function's public contract, distinct from the Doxygen block the Files-to-Change table calls out for `store_import.h`. If the new `found_layers_out`/counters change the signature, this forward-declaration comment should be updated in the same commit too. — `plan.md` "Files to Change" table

### Verified independently
- Confirmed by reading `cube_bathymetry/src/store_import.cpp` directly: `primeFromPriorLayers` (line 343, called from `cube_bathymetry_node.cpp`) is the whole-store bulk prime with the exact-level-only comment quoted verbatim in the plan; `primePriorLayersForTile` (line 796) is the per-tile function called from `seedNewTile`/`reloadEvictedTile` and is the one this issue's fix targets. `test/test_store_import.cpp`'s four `PrimeFromPriorLayers*` tests (lines 527-684) call the former; `test/test_import_eviction.cpp`'s `ChartLayerSeedRejectsDeepBlunder` (485), `CoarseLevelReferenceSeedRejectsDeepBlunder` (754), and `BoundaryFlushCrossLevelReferenceRejectsDeepBlunder` (862) are the real precedent for the latter. The plan's correction of the review-issue comment is correct.
- `primeFromTileResample` (store_import.cpp:739) is purely geometric over `BathymetryTile`/`GridIndex` with no `SourceLayer`-specific logic, so it transfers to Chart without modification, as the plan assumes.
- `ImportAccumulator::finalize()` (store_import.cpp:1150) is called exactly once per run, from `import_bag_main.cpp` only, and currently emits no diagnostics — a natural, whole-run-scoped site for the proposed "no usable prior" warning.
- The three cited eviction tests share one construction pattern (`BathymetryStore::fromCellSize` with the relevant layer's writable flag, `importTiles`, `save`, then `ImportAccumulator`/`GeoMapSheet`/`finalize()`/`loadBathyCells` comparison) that the two new Chart tests can follow directly.
- All principle names in the plan's self-check table match `docs/PRINCIPLES.md` headings exactly.

### Summary
The plan is well-scoped, technically accurate against the current source (including its correction of the prior review-issue comment, which checks out), and its test plan follows established patterns closely. No must-fix findings. The two suggestions above are minor completeness notes for the implementer, not blockers.

### Recommended Actions
- [ ] During implementation, double-check the `store_import.cpp:633-636` forward-declaration comment if the `primePriorLayersForTile` signature changes.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-26 00:15 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-137 at `747f31a`
**Mode**: pre-push
**Depth**: Deep (reason: 687 changed lines / 233 in store_import.cpp — over the 200-line Deep threshold; plan.md is also a project-repo override trigger)
**Must-fix**: 8 | **Suggestions**: 15
**Round**: 1 | **Ship**: continue — the Chart cross-level fix itself is correct and verified, but the issue's *second* defect (the silent-no-op warning) has three correctness holes of its own and four operator-facing documents still describe the old exact-level-only Chart behaviour

### Findings
- [x] (must-fix) README "Seed precedence" still states Chart is exact-level only with "no cross-level fallback for chart" — the sentence this PR falsifies — `README.md:96-99`; the adjacent "The fallback logs the reference level it used" now covers both layers — `README.md:104-106`
- [x] (must-fix) `--reference-store` help text describes only the store's `reference` layer, so an operator pointing it at an ENC/chart store reads that the chart layer will not gate — `cube_bathymetry/src/import_bag_main.cpp:144-153`, `cube_bathymetry/src/batch_regen_main.cpp:91`
- [x] (must-fix) A "hit" means a tile MATCHED, not that a cell was primed: `primeLayerForTile` returns true on `find`/fallback success while `primeFromTile` and `primeFromTileResample` skip every NaN cell and return void — a matched-but-no-data-over-this-tile prior permanently suppresses the new warning, and the header comment asserting "hits those that primed at least one cell" is false — `cube_bathymetry/src/store_import.cpp:850-877`, `cube_bathymetry/include/cube_bathymetry/store_import.h:501-506`
- [x] (must-fix, triple-confirmed) A run whose windowed loads all THREW is reported as a coverage gap: `found_layers_out` is populated after `loadWindow` inside the same try, so the warning prints "No chart or reference tiles overlap the surveyed area" for an unreadable/corrupt/permission-denied store — `cube_bathymetry/src/store_import.cpp:1247`
- [x] (must-fix) "the blunder gate was INACTIVE for this entire run" can be false: rung-1 survey warm-start primes the predicted surface (`seed_settled=true`) and returns before the counter increments, so a mixed re-import where most tiles warm-started reports the gate off everywhere — `cube_bathymetry/src/store_import.cpp:1245-1246`
- [x] (must-fix) Plan Consequences row 2 (update the ImportAccumulator/seedNewTile Doxygen) only partially delivered: `finalize()` now emits an operator-facing warning that its Doxygen does not mention — `store_import.h:417-427`; `seedNewTile` rung 2 still reads "reference — else a `reference/` tile" with no Chart — `store_import.h:471-479`; `primeFromTileResample`'s doc still says "from a COARSER *reference* tile" — `store_import.cpp:735-745`
- [x] (must-fix, triple-confirmed) The silent-no-op guard never fires on the authoritative rebuild path: `BatchRegen::gather` builds a fresh per-tile `ImportAccumulator` and calls `persistResidentTile`, never `finalize()` — yet `batch_regen_bag` takes the same `--reference-store` and prints the identical "Reference-prior seeding from …" banner — `cube_bathymetry/src/batch_regen.cpp:295,342-346`, `cube_bathymetry/src/batch_regen_main.cpp:835-837`
- [x] (must-fix) The widened-gate false-reject trade-off is recorded in project ADR-0001 for Reference ("legitimate deeper-than-charted returns can be rejected; the margin is tunable via the `blunder_*` params") but is not extended to Chart, which this PR makes the dominant gating path — `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md:239-253`
- [x] (suggestion) Phase A short-circuits Phase B: an exact-level tile with no data over this survey tile returns true and the cross-level fallback that does have data is never tried — `cube_bathymetry/src/store_import.cpp:855-859`
- [x] (suggestion) `findCrossLevelPrior` takes only the finest containing tile; if that one is empty over the survey tile, coarser containing tiles with data are not tried — `cube_bathymetry/src/store_import.cpp:830-836`
- [x] (suggestion) No ceiling on how coarse a Chart fallback may be (an L2 chart tile ≈ 232 m/cell under an L10 survey); consider bounding the level gap, scaling the blunder margin with the resample gap, or making cross-level Chart priming opt-in — `cube_bathymetry/src/store_import.cpp:823` (deferred: design decision explicitly held for the operator at the publish checkpoint — recorded as an open question in ADR-0001 and in plan.md "Deferred" item 1, not implemented)
- [x] (suggestion) Priors FINER than the survey level are still dropped entirely by `cand.level() >= index.level()` — `cube_bathymetry/src/store_import.cpp:823` (deferred: needs an aggregation rule — shoalest-wins vs mean — which is the same conservatism decision as the coarseness bound above; plan.md "Deferred" item 2)
- [x] (suggestion) The live node's `primeFromPriorLayers` is still exact-level-only, so the issue's own premise (an ENC product never has a survey-level tile) leaves the gate off afloat where it is safety-relevant — worth a follow-up issue — `cube_bathymetry/src/store_import.cpp:343-370`, `cube_bathymetry/src/cube_bathymetry_node.cpp:252` (deferred: recorded in plan.md "Deferred" item 3; the follow-up issue is NOT filed — AGENTS.md requires asking the operator before opening one and a dispatched sub-agent cannot ask, so it is surfaced at the publish checkpoint)
- [x] (suggestion) The cross-level audit line now fires per tile per layer and again on every eviction revisit; consider first-occurrence-per-(layer,level) or a finalize tally so it does not bury the "permanently dropping ~N sounding(s)" warning — `cube_bathymetry/src/store_import.cpp:869-874`
- [x] (suggestion) "any of N tile(s)" mislabels the unit — `prior_prime_attempts_` counts prime calls and revisits double-count; report hits/attempts unconditionally — `cube_bathymetry/src/store_import.cpp:1245`
- [x] (suggestion) `finalize()` has no try around the persist/metadata-save calls, so a throw skips the warning; the counters are never reset, so a second `finalize()` re-emits it — `cube_bathymetry/src/store_import.cpp:1204-1258`
- [x] (suggestion) `BoundaryFlushCrossLevelChartRejectsDeepBlunder` asserts only `with.empty()`, unlike the Reference mirror it copies, which runs a no-prior baseline and asserts `baseline_is_deep` — `cube_bathymetry/test/test_import_eviction.cpp:1285-1360`
- [x] (suggestion) Both `CaptureStderr()` calls are not exception-safe: a throw before `GetCapturedStderr()` leaves fd 2 redirected and the next capture trips gtest's single-capturer check, aborting the binary — `cube_bathymetry/test/test_import_eviction.cpp:1398,1437`
- [x] (suggestion) Assert the `"chart … seeded via cross-level fallback"` audit line in `CoarseLevelChartSeedRejectsDeepBlunder` so the test pins the mechanism rather than an absence — `cube_bathymetry/test/test_import_eviction.cpp:1193`
- [x] (suggestion) Plan step 5 says "L8 chart under L10"; the 8 m fixture is L7 (the code and Implementation Notes are right, the plan text is stale) — `.agent/work-plans/issue-137/plan.md` step 5
- [x] (suggestion) Plan said assert the warning "appears exactly once"; the test asserts substring presence only, so a duplicate emission would pass — `cube_bathymetry/test/test_import_eviction.cpp:1400-1407`
- [x] (suggestion) Plan's ADR table marks project ADR-0008 "Not triggered"; it IS triggered (it names the Chart/Reference prior seeding path) and the change complies — correct the verdict rather than the compliance — `.agent/work-plans/issue-137/plan.md` ADR Compliance
- [x] (suggestion, instruction CANDIDATE — operator decides, do not auto-apply) A layer-keyed lookup added for one `SourceLayer` must be checked against every other layer sharing the code path; this is the third instance of the class (#115, #119, #137) (deferred: instruction candidate — the finding itself says the operator decides; proposed in plan.md "Deferred" item 4, not written to .agent/knowledge/)

### Verified independently
- Static analysis clean: `ament_cpplint` and `ament_uncrustify` report "No problems found" on all three changed C++ files.
- Built the package incrementally and ran the gate tests: `ChartLayerSeedRejectsDeepBlunder`, `CoarseLevelReferenceSeedRejectsDeepBlunder`, `BoundaryFlushCrossLevelReferenceRejectsDeepBlunder`, `CoarseLevelChartSeedRejectsDeepBlunder`, `BoundaryFlushCrossLevelChartRejectsDeepBlunder`, `NoUsablePriorEmitsWarning` — 6/6 pass, and the chart audit line `chart blunder gate … (chart level 7 -> survey level 10)` is emitted as claimed.
- The containment walk in `findCrossLevelPrior` is a genuine verbatim extraction (same centre computation, same `>=` skip, same `Level(cand.level()).gridIndex(centre) != cand` rejection, same finest-wins tiebreak); only `tiles`/`index` became parameters. Returned pointer lifetime is safe — `ref` outlives its use.
- Chart-then-Reference overwrite priority is preserved across all four Phase A/B combinations. No concurrency exposure: zero `std::thread`/`async`/callback-group hits in `src/` and `include/`; both consumers are single-threaded CLIs.
- Per-tile cost of the new per-layer walk is negligible — `loadWindow` is called with one survey tile's bbox, so the scanned map holds only the few overlapping tiles.
- Commit identity on all 5 branch commits is `Claude Code Agent <roland+claude-code@ccom.unh.edu>`; atomic-commit discipline clean; working tree clean.
- Repo has no `.agents/README.md` and no `.pre-commit-config.yaml` — both pre-existing gaps acknowledged in the repo's own `AGENTS.md`, not introduced here. CI exists (`.github/workflows/ci.yml`, industrial_ci).

### Summary
The core fix is right and the tests bite: Chart now gets the same containment-checked cross-level walk as Reference, the extraction is verbatim, the layer-priority invariant survives, and the two new Chart tests plus the boundary-flush sibling pass and were shown to fail with Phase B disabled. The weakness is the issue's *second* defect — the silent-no-op warning counts tile matches rather than primed cells, cannot distinguish "no coverage" from "could not read the store", overstates its claim when most tiles warm-started from the survey layer, and is unreachable from `batch_regen`. Alongside that, four operator-facing documents (README, both `--help` strings, the header Doxygen, and ADR-0001's recorded false-reject trade-off) still describe the exact-level-only Chart behaviour this PR reverses.

### Recommended Actions
- [x] Fix the three warning-correctness holes together (primed-cell counting, a separate read-failure tally, and scoping the "INACTIVE" claim to the prior store)
- [x] Make the guard reachable from `batch_regen` — expose the tallies on `ImportAccumulator` and report from `BatchRegen::finalize`
- [x] One documentation sweep: README seed-precedence bullets, both `--reference-store` help strings, the `finalize`/`seedNewTile`/`primeFromTileResample` doc comments, and ADR-0001's widened-gate trade-off extended to Chart
- [x] Decide and record whether the cross-level Chart fallback needs a coarseness bound or an opt-out (suggestion, not a blocker) — or file it as a follow-up alongside the live-node exact-level gap (recorded in ADR-0001 and plan.md; the decision itself is deferred to the operator, and the live-node follow-up issue is surfaced rather than filed)

## Implementation
**Status**: complete
**When**: 2026-08-26 00:45 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-137 at `427363b`
**Addressed**: `## Local Review (Pre-Push)` (2026-08-26 00:15 -04:00, verdict changes-requested, branch at `747f31a`) — Round 1, 8 must-fix + 15 suggestions
**Commits**: `981319a`, `6529605`, `fc0d199`, `91f90b0`, `427363b`

The core Chart cross-level fix reviewed clean and was not touched. All work is in
the issue's *second* defect (the silent-no-op warning) and the documentation
sweep, fixed as the two coherent groups the review's Recommended Actions called
for rather than finding-by-finding.

**Verification**: `./sensors_ws/build.sh cube_bathymetry` clean (only the
pre-existing `test_tile_eviction_rss.cpp` unused-variable warning);
`./sensors_ws/test.sh cube_bathymetry` = **563 tests, 0 failures, 68 skipped**
(was 560/0 — the three new tests), `ament_cpplint` / `ament_uncrustify` clean.
Each new test was verified to FAIL against the pre-fix behaviour: reverting the
primed-cell semantics fails both `ImportEviction` additions, and removing the
`batch_regen` report fails the third.

### Actions
- [x] README seed precedence rewritten — the level-walk is now stated once for BOTH layers, with the matched-but-no-data case and the end-of-run warning — `README.md:88-116`
- [x] Both `--reference-store` help strings name chart + reference and the shared fallback; both startup banners now say outright that the banner is not evidence the gate engaged — `cube_bathymetry/src/import_bag_main.cpp`, `cube_bathymetry/src/batch_regen_main.cpp`
- [x] A hit is a primed CELL, not a matched tile: the three prime helpers return cell counts and every phase keys on the count — `cube_bathymetry/src/store_import.cpp`, `include/cube_bathymetry/store_import.h`
- [x] Read failures tallied separately (`PriorPrimeTally::read_failures`), so an unreadable store is never reported as "no tiles overlap the surveyed area" — `cube_bathymetry/src/store_import.cpp`
- [x] "INACTIVE for this entire run" → "for every tile that reached the prior rung", with rung-1 warm-starts counted and named separately — `cube_bathymetry/src/store_import.cpp`
- [x] `finalize` / `seedNewTile` / `primeFromTileResample` Doxygen corrected — `include/cube_bathymetry/store_import.h`, `cube_bathymetry/src/store_import.cpp`
- [x] The guard is reachable from `batch_regen`: per-tile tallies merged, `reportPriorPrimeOutcome` called once after the gather — `cube_bathymetry/src/batch_regen.cpp:353-357`, `include/cube_bathymetry/batch_regen.h`
- [x] ADR-0001's widened-gate false-reject trade-off extended to Chart, where it is now the dominant gating path — `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md`
- [x] Phase A no longer short-circuits Phase B — a hollow exact-level tile falls through — `cube_bathymetry/src/store_import.cpp`
- [x] `findCrossLevelPriors` returns every containing coarser tile finest-first, so an empty finest prior falls through to the next-coarser one — `cube_bathymetry/src/store_import.cpp`
- [x] Coarseness ceiling for the Chart fallback — **NOT implemented** (deferred: operator decision at the publish checkpoint; recorded as an open question in ADR-0001 and plan.md "Deferred" item 1)
- [x] Finer-than-survey priors still dropped (deferred: needs a shoalest-vs-mean aggregation rule — the same conservatism decision; plan.md "Deferred" item 2)
- [x] Live node's exact-level-only bulk prime (deferred: recorded in plan.md "Deferred" item 3; the follow-up issue is surfaced, NOT filed — AGENTS.md requires asking the operator first and a dispatched sub-agent cannot ask)
- [x] Cross-level audit line de-duplicated to once per (layer, prior level) via `PriorPrimeTally::audit_seen` — `cube_bathymetry/src/store_import.cpp`
- [x] The warning's unit is now "prior-seed attempt(s)", with the revisit double-count stated inline — `cube_bathymetry/src/store_import.cpp`
- [x] Warning emitted FIRST in `finalize()` (the persists have no `try` around them) and guarded so a second `finalize()` cannot repeat it — `cube_bathymetry/src/store_import.cpp`, `include/cube_bathymetry/store_import.h`
- [x] `BoundaryFlushCrossLevelChartRejectsDeepBlunder` gained the no-prior baseline its Reference mirror runs — `cube_bathymetry/test/test_import_eviction.cpp`
- [x] `StderrCapture` RAII wrapper in both test files; no raw `CaptureStderr`/`GetCapturedStderr` pair is left unbalanced on a throw — `cube_bathymetry/test/test_import_eviction.cpp`, `cube_bathymetry/test/test_batch_regen.cpp`
- [x] The cross-level audit line is asserted in `CoarseLevelChartSeedRejectsDeepBlunder`, so the test pins the mechanism — `cube_bathymetry/test/test_import_eviction.cpp`
- [x] Plan step 5 corrected: the 8 m fixture is L7, not L8 — `.agent/work-plans/issue-137/plan.md`
- [x] `NoUsablePriorEmitsWarning` asserts the warning appears EXACTLY once, and that a second `finalize()` does not repeat it — `cube_bathymetry/test/test_import_eviction.cpp`
- [x] Plan's ADR table: project ADR-0008 IS triggered and the change complies (the verdict was wrong, not the compliance) — `.agent/work-plans/issue-137/plan.md`
- [x] Instruction candidate — layer-keyed lookups must be checked against every layer sharing the path (deferred: the finding itself says the operator decides; proposed in plan.md "Deferred" item 4, not written to `.agent/knowledge/`)

### New tests
- `ImportEviction.MatchedButEmptyChartPriorStillWarns` — a coarse chart tile that *contains* the survey tile but is no-data over it must still trip the warning, and the ungated deep sounding really settles.
- `ImportEviction.EmptyExactLevelChartPriorFallsThroughToCoarsePrior` — a hollow exact-level chart tile must not short-circuit the coarser prior that has data.
- `BatchRegen.PriorThatPrimesNothingWarnsFromGather` — the warning fires from the authoritative rebuild, prefixed `batch_regen`, with a companion no-false-positive run.

### For the operator at the publish checkpoint
Four items are recorded and deliberately not implemented — the coarseness bound
on a Chart prior (an L2 tile is ~232 m/cell under an L10 survey), finer-than-
survey priors, a follow-up issue for the live node's exact-level-only bulk prime
(the gate is off *afloat* for an ENC prior, where it is safety-relevant), and the
layer-keyed-lookup instruction candidate. All four are written up in plan.md
under "Deferred — operator decides, NOT implemented".

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes)

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 137 --skill review-code --repo-slug cube_bathymetry

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-26 01:47 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-137 at `2e6b67e`
**Mode**: pre-push
**Depth**: Deep (reason: 1591 added / 125 deleted across 12 files; 355 lines in `store_import.cpp` alone, well over the 200-line Deep threshold, and the fix pass reworked the core helper signatures)
**Must-fix**: 3 | **Suggestions**: 11
**Round**: 2 | **Ship**: continue — Round 1's eight must-fixes are all genuinely discharged and verified, but the fix pass opened three new holes of the same silent-inactive-gate class it was closing: the read-failure tally is unreachable whenever any tile primed, the audit-line dedup is dead on the `batch_regen` path while the line still prints "reported once per prior level", and the new public-header "a match is not a prime" contract is violated by the two live-node call sites the branch left alone

### Findings
- [x] (must-fix, cross-lens confirmed) `reportPriorPrimeOutcome` returns early on `hits > 0`, so `read_failures` is only ever REPORTED in the zero-hit case: a prior store that became unreadable for 499 of 500 tiles but primed one emits no run-level line at all — the same silent-gate-off the issue exists to close, with a different cause. Report read failures independently of the `hits == 0` gate — `cube_bathymetry/src/store_import.cpp:1021`
- [x] (must-fix, cross-lens confirmed) The cross-level audit line prints the literal claim "; reported once per prior level", but `audit_seen` lives in `ImportAccumulator::prior_tally_` and `BatchRegen::finalize` builds a FRESH accumulator per gathered tile — so on the authoritative rebuild path the dedup suppresses nothing, the line fires once per tile (exactly what the Round-1 fix set out to stop), and the printed promise is false. `merge()` unions `audit_seen` after the fact and never feeds it back. Same line is also hard-prefixed `import_bag:` on a `batch_regen` run, though `reportPriorPrimeOutcome` was given a `tool` parameter for precisely that reason — `cube_bathymetry/src/store_import.cpp:897-909`, `cube_bathymetry/src/batch_regen.cpp:341,353`
- [x] (must-fix) The new public-header contract — "a caller asking 'is the blunder gate on for this tile?' must key on this count, never on the fact that a tile was found (#137)" — is violated by the two LIVE-NODE prior-prime sites: `primeFromPriorLayers` does `primeFromTile(...); ++primed;`, discarding the now-available count, and `cube_bathymetry_node.cpp` sets `primed = true` on a bare `find()`. Both drive operator-facing "Blunder gate active (#91)" / "Re-primed prior gate" messages that can be false on an all-NaN prior. This is NOT deferred item 3 (which is about exact-level-only priming) — it is a one-line fix at each site now the return value exists, or else scope the header/README/ADR contract text explicitly to the offline importer — `cube_bathymetry/src/store_import.cpp:366-368`, `cube_bathymetry/src/cube_bathymetry_node.cpp:1263-1279`
- [x] (suggestion, cross-lens confirmed) Phase B stops at the FIRST candidate that primes >= 1 cell, so a one-cell sliver of data in the finest containing prior suppresses a coarser prior with full coverage — and the tally records a hit, so nothing warns. The comment says "actually holds data over this survey tile", which reads as coverage. Consider walking all candidates coarsest-first priming only still-unprimed cells, or gating the fall-through on a coverage fraction — `cube_bathymetry/src/store_import.cpp:887-908`
- [x] (suggestion) The guard fires only when `hits == 0` for the WHOLE run: 1 primed tile out of 500 warns nothing though 499 ran ungated. Partial coverage is the more likely field failure than total non-coverage; consider always reporting "primed N of M attempt(s)" — `cube_bathymetry/src/store_import.cpp:1021`
- [x] (suggestion) `layers_seen` is populated from every tile `loadWindow` returned, including the edge-adjacent coarse neighbours `findCrossLevelPriors` deliberately rejects — so a genuine no-coverage case on a boundary tile is reported to the operator as a level MISMATCH, which has a different remedy — `cube_bathymetry/src/store_import.cpp:964-971`
- [x] (suggestion) "N further tile(s) warm-started ... and never reached the prior rung" implies disjointness from `attempts`, but a tile warm-started on first touch, evicted, then revisited is counted in both (`reloadEvictedTile` always attempts the prior). Also "the output store's survey layer": the bathymetry layer rung 1 actually reads is `Processed` — `cube_bathymetry/src/store_import.cpp:1036`, `:1165`
- [x] (suggestion) Rung 1 still treats a MATCH as a prime — `primeFromTile(it->second, sheet_, true)`'s count is discarded and `survey_warm_starts` incremented unconditionally — so an empty `processed/` tile short-circuits the prior rung, and the new warning text positively vouches for those tiles. At minimum gate the increment on a non-zero prime — `cube_bathymetry/src/store_import.cpp:1140,1165`
- [x] (suggestion) Both prime helpers filter `std::isnan` only, not `std::isfinite`. Now that the primed COUNT is the gating signal, a +/-inf depth counts as a hit: it short-circuits the walk to a coarser prior with real data and suppresses the run-level warning, while the blunder limit is meaningless on it — `cube_bathymetry/src/store_import.cpp:264`, `:787`
- [x] (suggestion) `BatchRegen` has no `prior_outcome_reported_` equivalent and never clears `prior_tally_` (a second `finalize()` double-merges and re-emits); and unlike `ImportAccumulator::finalize` it structurally cannot emit the warning FIRST, so a gather-loop throw loses it. Inherent, but `batch_regen.h:107-110` implies parity — say so there — `cube_bathymetry/src/batch_regen.cpp:353-356`
- [x] (suggestion) `PriorPrimeTally::merge` unions `audit_seen` but nothing reads it after a merge — dead state that actively disguises the dedup must-fix above — `cube_bathymetry/src/store_import.cpp:1008`
- [x] (suggestion) Two of the Round-1 must-fix branches have no POSITIVE test: `read_failures` reporting ("FAILED to read the prior store") and the `survey_warm_starts` scoping clause. `NoUsablePriorEmitsWarning` asserts only their ABSENCE. Per the Quality Standard a bug fix carries its test — `cube_bathymetry/test/test_import_eviction.cpp:1541`
- [x] (suggestion) README live-node section is stale after this branch: "Exact survey level only — unlike the offline `reference/` path" (the offline path is now chart AND reference), and "Evicted prior-primed tiles ... **not reloadable on revisit** ... (deferred, #118)" is false — `cube_bathymetry_node.cpp:1255-1284` implements the revisit re-prime — `README.md:155`, `:163-165`
- [x] (suggestion) The README carries the shoal-bias note but not the false-reject trade-off or the UNBOUNDEDNESS of the chart fallback (an L2 tile is ~232 m/cell under an L10 survey) — that honest text lives only in ADR-0001. One sentence of it belongs where the operator will actually read it — `README.md:96-115`

### Verified independently
- **Every fix-pass verification claim checked and holds.** `./sensors_ws/build.sh cube_bathymetry` clean (zero compiler warnings); `./sensors_ws/test.sh cube_bathymetry` = **563 tests, 0 errors, 0 failures, 68 skipped**, exactly as claimed. `ament_cpplint` and `ament_uncrustify` report "No problems found" over all eight changed C++ files.
- **The two new `ImportEviction` tests were empirically shown to bite**, not taken on trust: temporarily restoring the Phase-A short-circuit (`primeFromTile(...) > 0` -> unconditional) makes `EmptyExactLevelChartPriorFallsThroughToCoarsePrior` FAIL on both assertions, proving the hollow exact-level tile really is persisted and loaded (the test is not vacuous); temporarily restoring matched-tile-is-a-hit semantics in Phase B makes `MatchedButEmptyChartPriorStillWarns` FAIL. Source restored and the working tree verified clean afterwards.
- The three Round-1 correctness claims hold as written: an unreadable prior store is no longer reported as "no tiles overlap" (`read_failures >= attempts` gets its own sentence), a matched-but-all-NaN prior tile still warns, and `BatchRegen.PriorThatPrimesNothingWarnsFromGather` covers the batch_regen path and asserts the `batch_regen:` prefix.
- Header hygiene is clean: `<algorithm>` (for the new `std::sort`), `<vector>`, `<set>`, `<utility>` are all present in `store_import.cpp`; `batch_regen.h` includes `store_import.h`, so `PriorPrimeTally` is a complete type there. (`<cstddef>` is still transitive for `std::size_t`, now on two public signatures — pre-existing.)
- The `void` -> `std::size_t` return-type change is source-compatible at every call site; all consumers are in-package and the two that ignore the value are the pre-existing node/settled-restore paths.
- The containment walk in `findCrossLevelPriors` is a faithful extraction of the #115 logic (same centre computation, same `>=` skip, same `Level(cand.level()).gridIndex(centre) != cand` rejection); the returned pointers alias tiles owned by the `ref` store, which outlives their use. The Chart-primes-first / Reference-overwrites priority invariant survives the `kLayers` refactor.
- `survey_warm_starts` is incremented only on the successful rung-1 return inside `seedNewTile`, and `reloadEvictedTile` correctly never increments it.
- Documentation sweep from Round 1 is genuinely delivered: README seed-precedence, both `--reference-store` help strings, both startup banners, the `finalize` / `seedNewTile` / `primeFromTileResample` Doxygen, and ADR-0001 rung 2 including the widened-gate false-reject trade-off extended to Chart and the coarseness bound recorded as an open question.
- The four DEFERRED items are correctly recorded in `plan.md` under "Deferred — operator decides, NOT implemented" and were treated as out of scope by this review. Finding 3 above is deliberately NOT one of them.
- Commit identity on all 12 branch commits is `Claude Code Agent <roland+claude-code@ccom.unh.edu>`; atomic-commit discipline clean; working tree clean at review time. Repo has no `.agents/README.md` / `review-context.yaml` (pre-existing gap acknowledged in the repo's own `AGENTS.md`), so the review ran on source plus `AGENTS.md`/`README.md`/ADR-0001.

### Summary
The Round-1 fix pass is substantial and honest: all eight must-fixes are discharged, the documentation sweep is complete and accurate, the three new tests are real (two of them proven to fail against pre-fix behaviour by direct experiment here), and the suite/lint numbers are exactly as reported. The problem is that the rework opened three new instances of the very class of defect #137 exists to close — a diagnostic that reports "fine" when it is not. The read-failure tally cannot be reached whenever a single tile primed; the audit-line dedup is dead on `batch_regen` while the line still tells the operator it is deduplicated; and the branch asserted a package-wide "a match is not a prime" contract in a public header while leaving the two LIVE-node call sites — the safety-relevant ones — still counting matches.

### Recommended Actions
- [x] Un-gate the read-failure report from `hits == 0` so an intermittently unreadable prior store always produces a run-level line
- [x] Make the audit-line dedup run-scoped (hoist the tally, or pass a `PriorPrimeTally *` into each gather accumulator) — or drop the "reported once per prior level" claim from the line; and plumb the tool name so a `batch_regen` run does not attribute its gate diagnostics to `import_bag`
- [x] Either fix the two live-node match-is-a-prime call sites (one line each now the counts exist) or explicitly scope the new header/README/ADR contract text to the offline importer and record the live-node gap alongside deferred item 3
- [x] Add positive tests for the read-failure and warm-start clauses of the warning (the Quality Standard's "fix it completely: add the test")
- [x] Fix the two stale live-node README claims (offline path is now chart + reference; #118 revisit re-priming IS implemented)

## Implementation
**Status**: complete
**When**: 2026-08-26 01:33 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-137 at `bd14d45`
**Addressed**: `## Local Review (Pre-Push)` (2026-08-26 01:47 -04:00, verdict changes-requested, branch at `2e6b67e`) — Round 2, 3 must-fix + 11 suggestions
**Commits**: `c9b9f8d`, `8209271`, `dee129f`, `fd2bd7e`, `5be5bf4`, `c1ae9bc`, `c464748`, `8513794`, `6284543`, `a29feb1`, `7e0a2a4`, `bd14d45`

All three must-fixes were the same defect the issue exists to close — a diagnostic
that reports "fine" when it is not — reintroduced by the Round-1 fix pass. All
fourteen findings were actioned; **nothing was deferred**. The four items under
plan.md's "Deferred — operator decides" are untouched and remain deferred; the
live-node must-fix below is deliberately NOT deferred item 3 (the live node is
still exact-level-only).

One reviewer-offered choice was decided explicitly: for the third must-fix the
reviewer allowed *either* fixing the two live-node call sites *or* scoping the new
header/README/ADR contract text to the offline importer. **The call sites were
fixed**, because the falsehood is in an operator-facing message on the *afloat*
path, the counts the contract asks for already existed, and narrowing the contract
would have left the live node quietly claiming a blunder gate it does not have.

**Verification**: `./sensors_ws/build.sh cube_bathymetry` clean (only the
pre-existing `test_tile_eviction_rss.cpp:130` unused-variable warning, in a file
this branch does not touch); `./sensors_ws/test.sh cube_bathymetry` =
**571 tests, 0 errors, 0 failures, 68 skipped** (was 563/0/68 — the eight new tests);
`ament_cpplint` and `ament_uncrustify` report no problems over every changed file.

**Every new test was run against the pre-fix behaviour and observed to FAIL**, by
restoring in turn: the `hits > 0` early return, the per-accumulator tally and
`import_bag` prefix, the unconditional `++primed`, the record-every-window-tile
tally, the first-hit short-circuit, the unconditional warm start, and the `isnan`
filter — then restoring the fix and re-running.
`WarmStartedTilesAreScopedOutOfThePriorWarning` fails pre-fix on the corrected
`processed/` wording only; its value is as the positive coverage that clause never
had, which the review asked for.

### Actions
- [x] (must-fix) Un-gate the read-failure report from `hits == 0` — `store_import.cpp` `reportPriorPrimeOutcome` — `c9b9f8d`
- [x] (must-fix) Make the audit-line dedup run-scoped and plumb the tool name — `usePriorTally` + `ImportAccumulatorConfig::tool`; `PriorPrimeTally::merge` deleted with its write-only `audit_seen` union — `8209271`
- [x] (must-fix) Fix the two live-node match-is-a-prime call sites (chosen over scoping the contract text) — `store_import.cpp:primeFromPriorLayers`, `cube_bathymetry_node.cpp` revisit re-prime; `PriorLayerPrimeResult::empty_tiles` added — `dee129f`
- [x] (suggestion) Phase B first-hit short-circuit → per-cell UNION of the usable priors, coarsest first, exact level last — `5be5bf4`, ADR-0001 `c1ae9bc`
- [x] (suggestion) Report partial coverage ("primed only M of N attempt(s)") — `c9b9f8d`
- [x] (suggestion) `layers_seen` split from `unusable_seen` so a boundary coverage gap is not reported as a level mismatch — `fd2bd7e`
- [x] (suggestion) Warm-start clause: names the `processed/` layer, and states the two counts are not disjoint — `c9b9f8d`
- [x] (suggestion) Rung 1 no longer treats a MATCH as a prime — an empty `processed/` tile falls through to the prior rung — `c464748`
- [x] (suggestion) Both prime helpers filter `isfinite`, not `isnan` — `8513794`
- [x] (suggestion) `BatchRegen` once-guard added; `batch_regen.h` says outright it cannot emit the warning first — `8209271`
- [x] (suggestion) `PriorPrimeTally::merge`'s dead `audit_seen` union removed with `merge()` itself — `8209271`
- [x] (suggestion) Positive tests for the read-failure and warm-start clauses — `c9b9f8d`
- [x] (suggestion) Two stale live-node README claims corrected — `6284543`
- [x] (suggestion) README carries the false-reject trade-off and the unbounded chart coarseness — `6284543`

### Known gaps (recorded, not fixed)
- The node's evicted-tile revisit re-prime has **no test harness** in this package
  — there are no `prior_store_dir` node tests at all — so that one-line change is
  covered by inspection plus the shared `primeFromTile` contract test
  (`StoreImport.PrimeFromPriorLayersDoesNotCountAMatchedButEmptyTile`).
  Pre-existing gap, not introduced here.
- `ImportAccumulatorConfig::tool` prefixes the **prior-gate** diagnostics only;
  the accumulator's other `import_bag:` lines (spill/persist/abort) are unchanged,
  matching the finding's scope.
- Pre-existing unused-variable compiler warning at
  `test/test_tile_eviction_rss.cpp:130`, in a file this branch does not modify.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the Round-2 fixes)

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 137 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-26 01:46 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-137 at `bb40be0`
**Mode**: pre-push
**Depth**: Deep (reason: 2637 added / 156 deleted across 14 files; 391 added lines in `store_import.cpp` alone, and the Round-2 pass reworked the tally ownership model and the cross-level walk semantics)
**Must-fix**: 2 | **Suggestions**: 14
**Round**: 3 | **Ship**: recommended — every Round-2 must-fix is genuinely discharged and independently verified, and for the first time on this branch NO finding is a runtime defect: nothing found can cause a wrong gating decision or hide a gate that is off. Both remaining must-fixes are one-clause prose corrections to text this branch's own last commits falsified

### Findings
- [ ] (must-fix, cross-lens confirmed) Three sites still describe the single-tile fallback that commit `5be5bf4` deleted — "otherwise fall back to resampling the finest CONTAINING coarser tile that holds data". The walk now runs unconditionally and primes the per-cell UNION of every containing coarser tile, exact level last. Both `--reference-store` help strings are the operator-facing text for the exact flag this issue is about, and they understate the very exposure the operator is being asked to bound at the publish checkpoint (deferred item 1) — `cube_bathymetry/include/cube_bathymetry/store_import.h:629`, `cube_bathymetry/src/import_bag_main.cpp:150`, `cube_bathymetry/src/batch_regen_main.cpp:97`
- [ ] (must-fix, cross-lens confirmed) Four sites still describe `batch_regen` reporting "from the merged per-tile tallies" / "batch-regen merges it after the fact", but `PriorPrimeTally::merge` was DELETED in `8209271` and every gather accumulator now writes one shared tally by pointer. At `store_import.h:661` and `store_import.cpp:1439` the deleted mechanism is the stated RATIONALE for the once-guard, so a future reader who acts on it reintroduces the duplicate warning — `cube_bathymetry/include/cube_bathymetry/store_import.h:541`, `:661`, `cube_bathymetry/src/store_import.cpp:1439`, `cube_bathymetry/test/test_batch_regen.cpp:483`
- [ ] (suggestion, cross-lens confirmed — BOTH adversarial lenses rated this must-fix) `unusable_seen` collects every window tile that is neither the exact index nor a containing coarser fallback, which includes SAME-LEVEL edge-adjacent neighbours (`loadWindow`'s overlap test is inclusive — the branch's own `EdgeAdjacentCoarseNeighborIsNotReportedAsALevelMismatch` test proves such neighbours are returned). The message then prints "cannot gate them (finer than the survey level, or coarse tiles that do not contain them): chart@L10 ... vs survey level 10" — a level matching neither named cause. Down-rated from must-fix only because the preceding sentence already gives the correct remedy ("a coverage gap, not a level mismatch") — `cube_bathymetry/src/store_import.cpp:920-929`, `:1089-1098`
- [ ] (suggestion, cross-lens confirmed) Stray unmatched `)` closing the unusable-tiles sentence: the operator reads "... vs survey level 10.)" though no parenthesis was opened — `cube_bathymetry/src/store_import.cpp:1097`
- [ ] (suggestion) The partial-coverage line attributes read failures to a coverage gap: `attempts` is incremented BEFORE the try, so a throwing attempt lands in both `attempts` and `read_failures`, and the line then says the gate was inactive for "the N attempt(s) the prior did not cover" when the prior may cover them and the store was unreadable. Mitigated by the independent read-failure line, but it is the same conflation Round 2 separated. Subtract `read_failures`, or say "did not cover or could not be read" — `cube_bathymetry/src/store_import.cpp:1002`, `:1153-1156`
- [ ] (suggestion) `hits` counts a tile that primed >= 1 cell out of ~921,600 as fully gated, so a prior covering one cell of every survey tile yields `hits == attempts` and `reportPriorPrimeOutcome` emits NOTHING. The honesty signal is tile-granular while the fix it reports on is cell-granular. The cell counts already flow through both prime helpers and are discarded; summing primed cells against gateable cells would close it. Best follow-up-issue candidate on this branch — `cube_bathymetry/src/store_import.cpp:1045`, `:1136-1160`
- [ ] (suggestion) `attempts` does not mean what the warning says on the `batch_regen` path: `BatchRegen::finalize` builds a FRESH accumulator (hence a fresh `seeded_`) per bucket and `addBatch` seeds every tile in the influence-expanded window, so one tile is prior-primed once per bucket that touches it — while the line explains the repeats as "first touch + evicted-tile revisits" and `batch_regen` sets `max_resident_tiles = 0` and never evicts — `cube_bathymetry/src/store_import.cpp:1002`, `:1139`, `cube_bathymetry/src/batch_regen.cpp:346-351`
- [ ] (suggestion, cross-lens confirmed) Performance/RAM of the union walk: `primeFromTileResample` walks all ~921,600 survey cells with a geodetic `Level::cellIndex` per cell, and now runs for EVERY containing coarser tile of BOTH layers, on first touch and on every evicted-tile revisit — where pre-branch it ran at most once. On the documented ENC ladder (L2/L6/L7/L8 under L10) that is ~8x the lookups per tile touch, and near-full-density `Node` materialization becomes the normal case for exactly the chart-prior workload this issue targets, against `ImportAccumulator`'s bounded-RAM purpose (#92). Worth measuring on a real ENC store — `cube_bathymetry/src/store_import.cpp:941-969`
- [ ] (suggestion, cross-lens confirmed) Layer priority is applied OUTSIDE the level walk, so it outranks level fidelity: a Reference L2 tile (232 m/cell) overwrites a Chart L10 gate (0.9 m/cell) on every cell both cover, and the Reference coarse walk now runs even when Reference HAS an exact-level tile, filling its holes over a finer Chart gate. Either interleave the layers by level or state the precedence choice outright — `cube_bathymetry/src/store_import.cpp:964-968`, `:1024-1035`
- [ ] (suggestion, cross-lens confirmed) The de-duplicated audit line names one specific `survey tile <index>` while being emitted once per (layer, level) for the whole run, so the named tile is arbitrary and the operator cannot tell WHICH tiles a very coarse prior gated — the tiles most exposed to the false-reject mode. Name the pair without a tile index, or carry a per-(layer,level) primed-cell counter into the run-level warning — `cube_bathymetry/src/store_import.cpp:955-963`
- [ ] (suggestion) `layers_seen` / `unusable_seen` are run-scoped, so once ONE tile in the run had a usable prior the "coverage gap, not a level mismatch" explanation is unreachable for every other tile, and the partial-coverage warning offers nothing about WHY the uncovered attempts were uncovered — the commonest real case (a prior that stops at a survey edge) — `cube_bathymetry/src/store_import.cpp:1081-1102`
- [ ] (suggestion) The backscatter Welford reconstruction runs BEFORE the new `warm_started > 0` check, so with `c464748`'s fall-through an all-no-data `processed/` depth tile whose sibling backscatter tile holds data seeds settled intensity Welfords on cells that then get only a predicted-only prior prime. Requires an inconsistent store, but moving the block inside the `warm_started > 0` branch keeps the two halves of the warm start together — `cube_bathymetry/src/store_import.cpp:1247-1264`
- [ ] (suggestion) A first-touch prior read failure permanently disables the gate for that tile: `seedNewTile` passes `read_ok = nullptr` and inserts into `seeded_` unconditionally, so `addBatch` never retries — the opposite of `reloadEvictedTile`'s drop-and-retry contract. Documented as deliberate and now surfaced by the run-level read-failure line, but "the gate silently stayed off" is this issue's own failure mode; not inserting into `seeded_` when the prior read threw costs one retry per later batch — `cube_bathymetry/src/store_import.cpp:1320-1326`
- [ ] (suggestion) `cfg_.tool` is half applied: `persistBathyTile`'s clear-overlapped-draft warning hard-codes `import_bag:` and IS reachable from `batch_regen` via `persistResidentTile`, so a rebuild log can still interleave both prefixes. (`seedNewTile` / `reloadEvictedTile` / `addBatch`'s hard-coded lines are unreachable from batch_regen only by configuration, not by invariant.) Recorded as scoped in the Implementation entry — `cube_bathymetry/src/store_import.cpp:568`, `:718`, `:731`, `:1288`, `:1305`, `:1416`
- [ ] (suggestion) The live-node WARN's remedy is wrong when `empty_tiles > 0`: it reports "%zu matched the survey level but held no data over it" and then still advises "Check ... that the store has reference/ or chart/ tiles at the survey level" — which it demonstrably does. Split the remedy on `empty_tiles` — `cube_bathymetry/src/cube_bathymetry_node.cpp:253-260`
- [ ] (suggestion) Two "no false positive" companion assertions check only that "primed NOTHING" is absent, never that "primed only " is absent, so the quiet claim is untested against the new partial-coverage line; both also survey at the raw tile corner `(43.0, -70.0)` that a sibling test deliberately avoids — `cube_bathymetry/test/test_import_eviction.cpp:1578`, `cube_bathymetry/test/test_batch_regen.cpp:519`
- [ ] (suggestion) The `isnan` -> `isfinite` conversion is pinned by test only for `primeFromTileSkippingMask`'s DEPTH band. The MASK-band conversion is unpinned and is a behaviour change on a live path unrelated to the prior gate (a +/-inf `Processed` cell no longer shadows its `Draft` cell in `loadDraftSkippingProcessed`), as is the `primeFromTileResample` conversion — `cube_bathymetry/src/store_import.cpp:257`, `:797`, `cube_bathymetry/test/test_store_import.cpp:842`
- [ ] (suggestion) The startup banner goes to stdout while every prior warning goes to stderr, so an operator capturing `> import.log` keeps the reassuring banner and loses the countervailing WARNING — and the banner now explicitly promises "A run that primes nothing WARNS at the end" — `cube_bathymetry/src/import_bag_main.cpp:742-747`, `cube_bathymetry/src/batch_regen_main.cpp:840-845`
- [ ] (suggestion) `plan.md`'s "Files to Change" table still lists 3 files; the branch touches 12. The Round-1/Round-2 narrative sections cover the rest, but the table itself is the drift a PR reviewer flags — `.agent/work-plans/issue-137/plan.md:103-111`

### Verified independently
- **Every fix-pass verification claim checked and holds.** `./sensors_ws/test.sh cube_bathymetry` = **571 tests, 0 errors, 0 failures, 68 skipped**, exactly as claimed (was 563 — the 8 new tests are all present and named as reported). `ament_cpplint` and `ament_uncrustify` report "No problems found" over all ten changed C++ files.
- **Compiler warnings**: forced a full recompile of every changed translation unit. Three warnings, ALL pre-existing in files this branch does not touch (`xy.h:50` `-Wparentheses`, `test_angular_response_curve.cpp:37` `tmpnam`, `test_tile_eviction_rss.cpp:130` unused-but-set). No new warning from this branch. The Implementation entry named only the third; the other two are equally pre-existing.
- **The riskiest new behaviour was proven to bite, not taken on trust**: temporarily restoring the pre-fix first-hit short-circuit in `primeLayerForTile` (forward walk + `break`) makes `SliverInTheFinestPriorDoesNotSuppressAFullCoverageCoarserPrior` FAIL on its coverage assertion, with the log showing the L7 sliver gating alone. Source restored, rebuilt, test re-passes, working tree verified clean.
- The three Round-2 must-fixes are genuinely discharged: `read_failures` now has its own line independent of `hits`; the audit dedup is run-scoped by pointer (`CrossLevelAuditLineIsRunScopedAndNamesBatchRegen` asserts exactly one occurrence across two gathered tiles, with a control proving the fallback really fired, plus `import_bag:` absence); and both live-node sites key on the primed cell count, with the `|| primed` ordering at `cube_bathymetry_node.cpp:1279` correctly avoiding a short-circuited second prime.
- `usePriorTally` lifetime and aliasing are sound: `BatchRegen::prior_tally_` is a member outliving every per-bucket accumulator, `priorTally()` redirects both const and non-const overloads, the whole path is single-threaded (`SingleThreadedExecutor`), and no double-report path exists because the gather never calls `ImportAccumulator::finalize`. (Latent trap, not a finding: that once-guard is per-accumulator while the tally may be shared, so a future driver that both shares a tally AND calls `finalize()` per accumulator would emit the run-level warning once per accumulator.)
- `PriorPrimeTally::merge` is gone and nothing references it; the only residue is the stale prose in must-fix 2.
- `findCrossLevelPriors`' containment test, the finest-first sort, the coarsest-first reverse walk and its per-cell overwrite semantics are correct; `primed_cells`' double counting is harmless (compared `> 0` only); the `std::find` over `fallbacks` compares stable addresses into the same `std::map`.
- README.md and ADR-0001 describe the union walk, the false-reject trade-off and the 232 m/L2-under-L10 figure accurately — they are the doc sites that WERE updated; the two must-fixes are the ones that were not.
- The four DEFERRED items in `plan.md` were treated as out of scope. Adversarial Lens B independently re-derived deferred item 1 with new evidence worth handing the operator — see Summary.
- Commit identity on all 26 branch commits is `Claude Code Agent <roland+claude-code@ccom.unh.edu>`; atomic-commit discipline clean; working tree clean at review time. Repo still has no `.agents/README.md` / `review-context.yaml` (pre-existing, acknowledged in its own `AGENTS.md`).

### Summary
Round 3 is the first round on this branch where the fix pass did not open a new instance of the defect class the issue exists to close. Two independent adversarial lenses plus this reviewer found no runtime defect: nothing can cause a wrong gating decision, and no operator warning states something false about whether the gate engaged. What remains is prose that this branch's own last commits falsified (the two must-fixes), one wording gap in a warning whose leading sentence already gives the right remedy, and a dozen follow-up-grade improvements. Recommend correcting the two must-fixes — seven one-clause edits, no behaviour change — and shipping.

**For the operator's deferred decision (item 1, the Chart coarseness bound), Lens B added evidence the plan does not yet carry**: `Node::insert` takes the MINIMUM of the three blunder terms, and `primeFromTileResample` floors the seeded variance at `kPrimeVarianceFloor = 1e-4` (sigma = 1 cm) for a chart tile with no uncertainty band — so the `blunder_scalar * sqrt(var)` term can never be the minimum and a 232 m/cell L2 prior gates exactly as hard as an exact-level one. Concretely: one L2 cell spanning a 3 m shoal and a 25 m channel gates every survey cell under it at ~13 m, rejecting the channel's real seafloor. That makes "scale the blunder margin with the resample gap" — already one of the three options recorded in `plan.md` — a one-line change in `primeFromTileResample` rather than a design project, and it is the option that preserves the widened gate's benefit. Worth putting in front of the operator with the other three deferred items.

### Recommended Actions
- [ ] Correct the "finest CONTAINING coarser tile" text at the three sites (header Doxygen + both `--reference-store` help strings) to describe the unconditional per-cell union walk
- [ ] Correct the four "merged per-tile tallies" sites to describe the shared-tally-by-pointer design, especially the two that give it as the rationale for the once-guard
- [ ] Consider, before push: the same-level entry in `unusable_seen` (both lenses rated must-fix), and the stray `)` — both one-clause edits in the same function
- [ ] File as follow-ups rather than fixing here: cell-granular coverage reporting (the strongest remaining honesty gap), the union walk's RAM/CPU cost on a real ENC store, and the read-failure attribution in the partial-coverage line
