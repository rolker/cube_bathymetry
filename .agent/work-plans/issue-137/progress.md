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
- [ ] (must-fix) README "Seed precedence" still states Chart is exact-level only with "no cross-level fallback for chart" — the sentence this PR falsifies — `README.md:96-99`; the adjacent "The fallback logs the reference level it used" now covers both layers — `README.md:104-106`
- [ ] (must-fix) `--reference-store` help text describes only the store's `reference` layer, so an operator pointing it at an ENC/chart store reads that the chart layer will not gate — `cube_bathymetry/src/import_bag_main.cpp:144-153`, `cube_bathymetry/src/batch_regen_main.cpp:91`
- [ ] (must-fix) A "hit" means a tile MATCHED, not that a cell was primed: `primeLayerForTile` returns true on `find`/fallback success while `primeFromTile` and `primeFromTileResample` skip every NaN cell and return void — a matched-but-no-data-over-this-tile prior permanently suppresses the new warning, and the header comment asserting "hits those that primed at least one cell" is false — `cube_bathymetry/src/store_import.cpp:850-877`, `cube_bathymetry/include/cube_bathymetry/store_import.h:501-506`
- [ ] (must-fix, triple-confirmed) A run whose windowed loads all THREW is reported as a coverage gap: `found_layers_out` is populated after `loadWindow` inside the same try, so the warning prints "No chart or reference tiles overlap the surveyed area" for an unreadable/corrupt/permission-denied store — `cube_bathymetry/src/store_import.cpp:1247`
- [ ] (must-fix) "the blunder gate was INACTIVE for this entire run" can be false: rung-1 survey warm-start primes the predicted surface (`seed_settled=true`) and returns before the counter increments, so a mixed re-import where most tiles warm-started reports the gate off everywhere — `cube_bathymetry/src/store_import.cpp:1245-1246`
- [ ] (must-fix) Plan Consequences row 2 (update the ImportAccumulator/seedNewTile Doxygen) only partially delivered: `finalize()` now emits an operator-facing warning that its Doxygen does not mention — `store_import.h:417-427`; `seedNewTile` rung 2 still reads "reference — else a `reference/` tile" with no Chart — `store_import.h:471-479`; `primeFromTileResample`'s doc still says "from a COARSER *reference* tile" — `store_import.cpp:735-745`
- [ ] (must-fix, triple-confirmed) The silent-no-op guard never fires on the authoritative rebuild path: `BatchRegen::gather` builds a fresh per-tile `ImportAccumulator` and calls `persistResidentTile`, never `finalize()` — yet `batch_regen_bag` takes the same `--reference-store` and prints the identical "Reference-prior seeding from …" banner — `cube_bathymetry/src/batch_regen.cpp:295,342-346`, `cube_bathymetry/src/batch_regen_main.cpp:835-837`
- [ ] (must-fix) The widened-gate false-reject trade-off is recorded in project ADR-0001 for Reference ("legitimate deeper-than-charted returns can be rejected; the margin is tunable via the `blunder_*` params") but is not extended to Chart, which this PR makes the dominant gating path — `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md:239-253`
- [ ] (suggestion) Phase A short-circuits Phase B: an exact-level tile with no data over this survey tile returns true and the cross-level fallback that does have data is never tried — `cube_bathymetry/src/store_import.cpp:855-859`
- [ ] (suggestion) `findCrossLevelPrior` takes only the finest containing tile; if that one is empty over the survey tile, coarser containing tiles with data are not tried — `cube_bathymetry/src/store_import.cpp:830-836`
- [ ] (suggestion) No ceiling on how coarse a Chart fallback may be (an L2 chart tile ≈ 232 m/cell under an L10 survey); consider bounding the level gap, scaling the blunder margin with the resample gap, or making cross-level Chart priming opt-in — `cube_bathymetry/src/store_import.cpp:823`
- [ ] (suggestion) Priors FINER than the survey level are still dropped entirely by `cand.level() >= index.level()` — `cube_bathymetry/src/store_import.cpp:823`
- [ ] (suggestion) The live node's `primeFromPriorLayers` is still exact-level-only, so the issue's own premise (an ENC product never has a survey-level tile) leaves the gate off afloat where it is safety-relevant — worth a follow-up issue — `cube_bathymetry/src/store_import.cpp:343-370`, `cube_bathymetry/src/cube_bathymetry_node.cpp:252`
- [ ] (suggestion) The cross-level audit line now fires per tile per layer and again on every eviction revisit; consider first-occurrence-per-(layer,level) or a finalize tally so it does not bury the "permanently dropping ~N sounding(s)" warning — `cube_bathymetry/src/store_import.cpp:869-874`
- [ ] (suggestion) "any of N tile(s)" mislabels the unit — `prior_prime_attempts_` counts prime calls and revisits double-count; report hits/attempts unconditionally — `cube_bathymetry/src/store_import.cpp:1245`
- [ ] (suggestion) `finalize()` has no try around the persist/metadata-save calls, so a throw skips the warning; the counters are never reset, so a second `finalize()` re-emits it — `cube_bathymetry/src/store_import.cpp:1204-1258`
- [ ] (suggestion) `BoundaryFlushCrossLevelChartRejectsDeepBlunder` asserts only `with.empty()`, unlike the Reference mirror it copies, which runs a no-prior baseline and asserts `baseline_is_deep` — `cube_bathymetry/test/test_import_eviction.cpp:1285-1360`
- [ ] (suggestion) Both `CaptureStderr()` calls are not exception-safe: a throw before `GetCapturedStderr()` leaves fd 2 redirected and the next capture trips gtest's single-capturer check, aborting the binary — `cube_bathymetry/test/test_import_eviction.cpp:1398,1437`
- [ ] (suggestion) Assert the `"chart … seeded via cross-level fallback"` audit line in `CoarseLevelChartSeedRejectsDeepBlunder` so the test pins the mechanism rather than an absence — `cube_bathymetry/test/test_import_eviction.cpp:1193`
- [ ] (suggestion) Plan step 5 says "L8 chart under L10"; the 8 m fixture is L7 (the code and Implementation Notes are right, the plan text is stale) — `.agent/work-plans/issue-137/plan.md` step 5
- [ ] (suggestion) Plan said assert the warning "appears exactly once"; the test asserts substring presence only, so a duplicate emission would pass — `cube_bathymetry/test/test_import_eviction.cpp:1400-1407`
- [ ] (suggestion) Plan's ADR table marks project ADR-0008 "Not triggered"; it IS triggered (it names the Chart/Reference prior seeding path) and the change complies — correct the verdict rather than the compliance — `.agent/work-plans/issue-137/plan.md` ADR Compliance
- [ ] (suggestion, instruction CANDIDATE — operator decides, do not auto-apply) A layer-keyed lookup added for one `SourceLayer` must be checked against every other layer sharing the code path; this is the third instance of the class (#115, #119, #137)

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
- [ ] Fix the three warning-correctness holes together (primed-cell counting, a separate read-failure tally, and scoping the "INACTIVE" claim to the prior store)
- [ ] Make the guard reachable from `batch_regen` — expose the tallies on `ImportAccumulator` and report from `BatchRegen::finalize`
- [ ] One documentation sweep: README seed-precedence bullets, both `--reference-store` help strings, the `finalize`/`seedNewTile`/`primeFromTileResample` doc comments, and ADR-0001's widened-gate trade-off extended to Chart
- [ ] Decide and record whether the cross-level Chart fallback needs a coarseness bound or an opt-out (suggestion, not a blocker) — or file it as a follow-up alongside the live-node exact-level gap
