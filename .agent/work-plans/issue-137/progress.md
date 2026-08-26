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
