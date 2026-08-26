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
- [ ] (must-fix, cross-lens confirmed) `reportPriorPrimeOutcome` returns early on `hits > 0`, so `read_failures` is only ever REPORTED in the zero-hit case: a prior store that became unreadable for 499 of 500 tiles but primed one emits no run-level line at all — the same silent-gate-off the issue exists to close, with a different cause. Report read failures independently of the `hits == 0` gate — `cube_bathymetry/src/store_import.cpp:1021`
- [ ] (must-fix, cross-lens confirmed) The cross-level audit line prints the literal claim "; reported once per prior level", but `audit_seen` lives in `ImportAccumulator::prior_tally_` and `BatchRegen::finalize` builds a FRESH accumulator per gathered tile — so on the authoritative rebuild path the dedup suppresses nothing, the line fires once per tile (exactly what the Round-1 fix set out to stop), and the printed promise is false. `merge()` unions `audit_seen` after the fact and never feeds it back. Same line is also hard-prefixed `import_bag:` on a `batch_regen` run, though `reportPriorPrimeOutcome` was given a `tool` parameter for precisely that reason — `cube_bathymetry/src/store_import.cpp:897-909`, `cube_bathymetry/src/batch_regen.cpp:341,353`
- [ ] (must-fix) The new public-header contract — "a caller asking 'is the blunder gate on for this tile?' must key on this count, never on the fact that a tile was found (#137)" — is violated by the two LIVE-NODE prior-prime sites: `primeFromPriorLayers` does `primeFromTile(...); ++primed;`, discarding the now-available count, and `cube_bathymetry_node.cpp` sets `primed = true` on a bare `find()`. Both drive operator-facing "Blunder gate active (#91)" / "Re-primed prior gate" messages that can be false on an all-NaN prior. This is NOT deferred item 3 (which is about exact-level-only priming) — it is a one-line fix at each site now the return value exists, or else scope the header/README/ADR contract text explicitly to the offline importer — `cube_bathymetry/src/store_import.cpp:366-368`, `cube_bathymetry/src/cube_bathymetry_node.cpp:1263-1279`
- [ ] (suggestion, cross-lens confirmed) Phase B stops at the FIRST candidate that primes >= 1 cell, so a one-cell sliver of data in the finest containing prior suppresses a coarser prior with full coverage — and the tally records a hit, so nothing warns. The comment says "actually holds data over this survey tile", which reads as coverage. Consider walking all candidates coarsest-first priming only still-unprimed cells, or gating the fall-through on a coverage fraction — `cube_bathymetry/src/store_import.cpp:887-908`
- [ ] (suggestion) The guard fires only when `hits == 0` for the WHOLE run: 1 primed tile out of 500 warns nothing though 499 ran ungated. Partial coverage is the more likely field failure than total non-coverage; consider always reporting "primed N of M attempt(s)" — `cube_bathymetry/src/store_import.cpp:1021`
- [ ] (suggestion) `layers_seen` is populated from every tile `loadWindow` returned, including the edge-adjacent coarse neighbours `findCrossLevelPriors` deliberately rejects — so a genuine no-coverage case on a boundary tile is reported to the operator as a level MISMATCH, which has a different remedy — `cube_bathymetry/src/store_import.cpp:964-971`
- [ ] (suggestion) "N further tile(s) warm-started ... and never reached the prior rung" implies disjointness from `attempts`, but a tile warm-started on first touch, evicted, then revisited is counted in both (`reloadEvictedTile` always attempts the prior). Also "the output store's survey layer": the bathymetry layer rung 1 actually reads is `Processed` — `cube_bathymetry/src/store_import.cpp:1036`, `:1165`
- [ ] (suggestion) Rung 1 still treats a MATCH as a prime — `primeFromTile(it->second, sheet_, true)`'s count is discarded and `survey_warm_starts` incremented unconditionally — so an empty `processed/` tile short-circuits the prior rung, and the new warning text positively vouches for those tiles. At minimum gate the increment on a non-zero prime — `cube_bathymetry/src/store_import.cpp:1140,1165`
- [ ] (suggestion) Both prime helpers filter `std::isnan` only, not `std::isfinite`. Now that the primed COUNT is the gating signal, a +/-inf depth counts as a hit: it short-circuits the walk to a coarser prior with real data and suppresses the run-level warning, while the blunder limit is meaningless on it — `cube_bathymetry/src/store_import.cpp:264`, `:787`
- [ ] (suggestion) `BatchRegen` has no `prior_outcome_reported_` equivalent and never clears `prior_tally_` (a second `finalize()` double-merges and re-emits); and unlike `ImportAccumulator::finalize` it structurally cannot emit the warning FIRST, so a gather-loop throw loses it. Inherent, but `batch_regen.h:107-110` implies parity — say so there — `cube_bathymetry/src/batch_regen.cpp:353-356`
- [ ] (suggestion) `PriorPrimeTally::merge` unions `audit_seen` but nothing reads it after a merge — dead state that actively disguises the dedup must-fix above — `cube_bathymetry/src/store_import.cpp:1008`
- [ ] (suggestion) Two of the Round-1 must-fix branches have no POSITIVE test: `read_failures` reporting ("FAILED to read the prior store") and the `survey_warm_starts` scoping clause. `NoUsablePriorEmitsWarning` asserts only their ABSENCE. Per the Quality Standard a bug fix carries its test — `cube_bathymetry/test/test_import_eviction.cpp:1541`
- [ ] (suggestion) README live-node section is stale after this branch: "Exact survey level only — unlike the offline `reference/` path" (the offline path is now chart AND reference), and "Evicted prior-primed tiles ... **not reloadable on revisit** ... (deferred, #118)" is false — `cube_bathymetry_node.cpp:1255-1284` implements the revisit re-prime — `README.md:155`, `:163-165`
- [ ] (suggestion) The README carries the shoal-bias note but not the false-reject trade-off or the UNBOUNDEDNESS of the chart fallback (an L2 tile is ~232 m/cell under an L10 survey) — that honest text lives only in ADR-0001. One sentence of it belongs where the operator will actually read it — `README.md:96-115`

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
- [ ] Un-gate the read-failure report from `hits == 0` so an intermittently unreadable prior store always produces a run-level line
- [ ] Make the audit-line dedup run-scoped (hoist the tally, or pass a `PriorPrimeTally *` into each gather accumulator) — or drop the "reported once per prior level" claim from the line; and plumb the tool name so a `batch_regen` run does not attribute its gate diagnostics to `import_bag`
- [ ] Either fix the two live-node match-is-a-prime call sites (one line each now the counts exist) or explicitly scope the new header/README/ADR contract text to the offline importer and record the live-node gap alongside deferred item 3
- [ ] Add positive tests for the read-failure and warm-start clauses of the warning (the Quality Standard's "fix it completely: add the test")
- [ ] Fix the two stale live-node README claims (offline path is now chart + reference; #118 revisit re-priming IS implemented)
