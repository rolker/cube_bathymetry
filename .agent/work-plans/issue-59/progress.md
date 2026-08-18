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
