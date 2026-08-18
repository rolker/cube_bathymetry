---
issue: 59
---

# Issue #59 — Port predicted-surface producer (cube_grid_initialise + touchdown interpolation)

## Issue Review
**Status**: complete
**When**: 2026-08-18 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #59
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #59 ports the two pieces of the predicted-surface producer that #15 explicitly deferred:
(1) external-prior load (`cube_grid_initialise` analog — seed each node's `predicted_depth_`/variance via `setPredictedDepth` from a prior bathymetry array, plus a base null hypothesis); and
(2) per-sounding touchdown interpolation (`cube_grid_interpolate` analog — bilinear blend of 4 surrounding nodes' `predicted_depth_` at each sounding's `(de, dn)` offset, writing `Sounding::predicted_depth_at_touchdown`, returning the no-correction sentinel if any corner is no-data).

Both pieces land in one PR because neither delivers value without the other. Once merged, slope correction is active end-to-end; until then deployed behaviour remains uncorrected (offset 0).

The issue is in the right repo (`cube_bathymetry`), in the right worktree (`feature/issue-59`). The dependency on #15 is already satisfied — `setPredictedDepth`, `predicted_depth_at_touchdown`, and the corrected `Node::insert` offset are all in the codebase (`jazzy`, merged via PR #15).

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Capture decisions, not just implementations | Watch | The Grid-vs-GeoGrid interpolation geometry choice (regular bilinear vs GGGS-cell analog) is flagged in the issue as a decision that "belongs here" but is not resolved. This is exactly the kind of non-obvious design choice ADR-0001 targets — plan-task should capture it in an ADR before implementation. |
| A change includes its consequences | Watch | `Grid` currently lacks a `setPredictedDepthAt` method (only `GeoGrid` has one). Adding it is a public API change; plan should call this out. Tests for both interpolation paths (bilinear accuracy, no-data corner handling, sentinel return) must land in the same PR as the implementation. |
| Test what breaks | Watch | The interpolation math is correctness-sensitive (drives slope correction on every sounding). Issue doesn't specify test strategy. Plan should include: bilinear blend at interior offsets, edge/corner behavior, and the `INVALID_DATA`-sentinel path when any corner node carries no prior. |
| Human control and transparency | Action needed | The prior bathymetry load mechanism is unspecified: "load a prior bathymetry array" says nothing about the data source (file? ROS topic? service call?) or format. In a ROS 2 node context this is a meaningful design decision. If file-based (like the original C code), what format (GeoTIFF? binary?)? Needs clarification before plan finalization — otherwise the plan will make assumptions that may not match operator intent. |
| Improve incrementally | OK | Scope is tight: exactly the two pieces #15 deferred. No creep evident. |
| Only what's needed | OK | Minimal, well-bounded change. |
| Workspace vs. project separation | OK | Change is entirely inside the `cube_bathymetry` project repo. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0001 — Adopt ADRs | Yes | The Grid-vs-GeoGrid interpolation geometry decision is a non-obvious design choice with lasting consequences; should be documented as an ADR before implementation. |
| 0002 — Worktree isolation | Met | Already in `feature/issue-59` worktree. |
| 0008 — ROS 2 conventions | Yes | New C++ public API on `Grid` and `GeoGrid`; follow ROS 2 naming and style conventions. No anticipated concern, but plan should note it. |
| 0013 — progress.md vocabulary | Met | This entry uses `## Issue Review`. |

### Consequences

- `Grid` gains a new public method (peer to `GeoGrid::setPredictedDepthAt`) — downstream callers (`cube_bathymetry_node`, `batch_regen`) should be checked.
- `Grid::insert` and `GeoGrid::insert` will set `Sounding::predicted_depth_at_touchdown` before calling `Node::insert` — existing tests for `insert` remain valid but new path-specific tests are required.
- `cube_bathymetry` README / API docs should document the new prior-load interface and the data-source requirement.

### Actions
- [ ] Clarify prior bathymetry load mechanism (data source and format) before plan finalization — this drives the interface design.
- [ ] Capture the Grid-vs-GeoGrid interpolation geometry decision in an ADR before implementation begins.
- [ ] Plan tests for the new interpolation paths: bilinear blend accuracy, no-data corner sentinel, `GeoGrid` cell-geometry handling.

## Plan Authored
**Status**: complete
**When**: 2026-08-18 12:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-59/plan.md` at `604d4b5`
**Branch**: feature/issue-59 at `604d4b5`
**Phases**: single

### Open questions
- [ ] Prior-load mechanism: plan proposes `prior_bathymetry_dir` (existing tile-store format). Operator to confirm or redirect to GeoTIFF/ENC import.
- [ ] GeoGrid GGGS cell-corner convention: confirm fractional-position convention within a GGGS cell for the bilinear blend. Pinned during implementation.
- [ ] Variance convention for prior load: fixed-metres vs. percentage-of-depth — expose one or both as a node parameter?

## Plan Review
**Status**: complete
**When**: 2026-08-18 01:59 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent: fresh-context sub-agent, model differs from the Sonnet plan author; no shared context. The `## Plan Authored` `**By**` agent-name matches only because "Claude Code Agent" is the framework identity shared by all Claude agents here, not because this is a self-review. -->

**Plan**: `.agent/work-plans/issue-59/plan.md` at `604d4b5`
**PR**: PR-less (`--issue` mode)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Step 6 reinvents the existing `Reference`-layer prime — `loadIntoSheet(store, SourceLayer::Reference, sheet, /*seed_settled=*/false)` → `primeFromTile` (cube#89/#96, `store_import.cpp:230-272`) already loads an external prior and seeds `predicted_depth_` via `setPredictedDepthAt` without settling; the batch importer already primes from a coarse Reference prior (`store_import.cpp:604`). Reuse it rather than build a parallel `prior_bathymetry_dir` + `initializePredictedDepthAt` loop — `plan.md:114-128`
- [ ] (must-fix) Step 3's `initializePredictedDepthAt` adds a base hypothesis (`setPredictedDepth` + `addHypothesis`), contradicting cube#89's deliberate "coarse prior gates, does not fill" decision (`store_import.h:200-204`, `store_import.cpp:250-262`). Reconcile: justify the divergence or drop the hypothesis seed for the GGGS/Reference path — `plan.md:62-70`
- [ ] (should-fix) Context root-cause is inaccurate: `predicted_depth_` is NOT always `INVALID_DATA` in production — `setPredictedDepthAt` seeds it on the draft/reference prime paths. The real gap is the missing `interpolatePredictedDepth` + insert wiring (Piece 2), which is what leaves `predicted_depth_at_touchdown` at its sentinel → offset 0 — `plan.md:9-13`
- [ ] (should-fix) GeoGrid bilinear stencil (Step 5) must select the 4-cell neighbourhood by the touchdown's quadrant relative to the cell center (±1 per axis, mirroring `cube_grid_interpolate`'s `floor` lower-left pick), not a fixed `(row+1, col+1)`. Add a GGGS test exercising an off-center quadrant, not just a shared interior point — `plan.md:99-103, 147-149`
- [ ] (suggestion) Record that the port threads only interpolated depth (not the original's `var_pred`) into `predicted_depth_at_touchdown`, consistent with `Node::insert`'s depth-delta offset (`node.cpp:201-204`) — divergence worth noting — `plan.md:76-90`

### Notes
- Core interpolation (Steps 4-5) + insert sounding-copy wiring are correctly designed and faithful to `cube_grid.c:2360-2403`. The `INVALID_DATA` sentinel (vs. the original's `0.0f`) is verified correct against `node.cpp:201-204` and is better than the original.
- ADR-0008 slot is free; existing project ADRs are 0001/0002/0003/0007. ADR-first geometry decision is the right call.
- Must-fix findings are scoped to the prior-load half (Steps 3, 6); the interpolation core may proceed. Verified against source in the `feature/issue-59` worktree — existing API (`setPredictedDepth`, `addHypothesis`, `Sounding::predicted_depth_at_touchdown`, `GeoGrid`/`GeoMapSheet::setPredictedDepthAt`, `setSettledDepthAt`) all present.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-18 02:52 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-59 at `94b5345`
**Mode**: pre-push
**Depth**: Deep (reason: ADR add + correctness-sensitive interpolation math on the insert hot path)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 1 | **Ship**: recommended — no must-fix findings; both adversarial lenses and static analysis clean, 11/11 new tests pass.

### Findings
- [ ] (suggestion) Add the no-`var_pred` divergence to the canonical registry (recorded in ADR-0008, missing from the doc) — `cube_bathymetry/docs/divergences_from_calder.md`
- [ ] (suggestion) `ScatterRecord::predicted_depth_at_touchdown` is now recomputed on replay insert — redundant serialized payload; document or drop (pre-existing, follow-up) — `cube_bathymetry/src/batch_regen.cpp:63`

### Notes
- Static analysis: ament_cpplint clean; cppcheck clean on changed lines (remaining hits are pre-existing untouched lines).
- Claude Adversarial Lens A (logic) and Lens B (systemic): no must-fix. Bilinear corner-order/axis mapping, boundary gates, signed/unsigned promotion, and sentinel/NaN paths independently verified correct; per-sounding copy hoisted above the node loop (O(1), hot path respected); gates-not-fills invariant upheld.
- Local Adversarial skipped: ollama not installed. Copilot off (default).
- Tests: 6 GridPredictedSurfaceTest + 5 GeoGridPredictedSurfaceTest all pass (log/test_2026-08-17_22-42-00).

## Integrated Review
**Status**: complete
**When**: 2026-08-17 23:42 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #122 at `b7bb846`
**Sources**: 3 (Copilot R1 @ `b7bb846`, Local Review (Pre-Push) @ `94b5345`, CI rollup @ `b7bb846`)
**Cross-source confirmations**: 0
**CI**: all-pass (ROS 2 Jazzy industrial_ci success; copilot-pull-request-reviewer success)

### Findings
- [x] (low, Copilot) `Grid::interpolatePredictedDepth` casts `std::floor(rx/ry)` to `int32_t` before any range check; the public contract promises `INVALID_DATA` for out-of-range input, so an extreme finite coordinate passed directly (tests already call the method out-of-range) is UB rather than a clean sentinel. Fix: floor into `double`, range-check against `int32_t` limits (and grid bounds), then cast — `cube_bathymetry/src/grid.cpp:137`
- [x] (low, Copilot) Same pre-cast pattern in `GeoGrid::interpolatePredictedDepth`; here `insert()` has no out-of-tile rejection ahead of the call, so an absurd-but-finite latitude/longitude reaches the cast. Fix: floor into `double`, validate range, then cast — `cube_bathymetry/src/geo_grid.cpp:150`
- [ ] (suggestion, Local Review @ `94b5345`) `ScatterRecord::predicted_depth_at_touchdown` is now recomputed on replay insert — redundant serialized payload; document or drop (pre-existing, follow-up issue) — `cube_bathymetry/src/batch_regen.cpp:63`

### Addressed since prior round
- (Local Review @ `94b5345`) no-`var_pred` divergence missing from the canonical registry — added in `b7bb846` (`cube_bathymetry/docs/divergences_from_calder.md`).

### False positives
- None. Both Copilot findings describe a real contract/implementation gap: `Grid::insert` and `GeoGrid::insert` gate non-finite inputs at the door (so NaN/inf cannot reach the cast through the insert path), and `Grid::insert`'s effect-box overlap gate additionally bounds `rx`/`ry`, but both interpolators are public API documented to return `INVALID_DATA` for out-of-range input, and `GeoGrid`'s has no bounding gate upstream. Not dismissible under the Quality Standard.

### Notes
- No human reviewer comments and no conversation comments on the PR.
- Copilot's review is against the current head SHA — not stale.
- Related pre-existing pattern worth covering with the same fix: `Grid::insert`'s effect-box `int32_t` casts at `cube_bathymetry/src/grid.cpp:74-77`.
