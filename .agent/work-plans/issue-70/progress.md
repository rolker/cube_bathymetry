---
issue: 70
---

# Issue #70 — Tile eviction + incremental publish to bound long-duration growth

## Issue Review
**Status**: complete
**When**: 2026-06-26 14:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #70
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

The issue identifies two concrete unbounded-growth problems with a shared root
cause (PoC-era design carried forward through the #21 GGGS migration) and
proposes bounded, actionable fixes for each. The problems are real: the
monolithic publish caused a confirmed deployment incident (udp_bridge saturation,
2026-06-10). Bundling both problems in one issue is defensible — they share the
same structural cause and the fixes are complementary — though an implementer
may choose to split into separate PRs for reviewability.

The dependency on #69 (single-fused-grid store adaptation) is explicit in the
issue; #69's progress.md shows a "Local Review (Pre-Push): approved" entry dated
2026-06-26, so the dependency appears nearly ready but must be confirmed landed
before implementation of #70 begins.

**Right repo**: Yes — cube_bathymetry is a project repo; the worktree is
correctly scoped to `sensors_ws/src/cube_bathymetry`.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Only what's needed | OK | Fixes concrete operational failures (RAM growth, bandwidth saturation), not speculative |
| Test what breaks | OK | Issue explicitly calls for a bounded-RSS / bounded-tile-count test on a long synthetic track |
| A change includes its consequences | Watch | Retiring monolithic `GridMap` publish changes the Nav2 costmap consumer's input — the issue notes a "bounded live CA grid" but decoupling plan needs detailing in the plan |
| Capture decisions, not just implementations | Action needed | Eviction policy (LRU vs. distance-from-vessel, budget size) and per-tile output message format are non-trivial design choices that belong in an ADR |
| Improve incrementally | Watch | Two distinct changes bundled; consider whether eviction and incremental publish can be PRed separately while keeping the acceptance criterion testable |
| Safety First (project) | Watch | Nav2 CA grid will shift from whole-area to windowed view — plan must specify behavior at the boundary (e.g., unknown cells default to occupied or free) |
| Modularity and Decoupling (project) | OK | Per-tile incremental output naturally decouples survey persistence from live display and boat→CAMP transmission |
| Simulation-First Validation (project) | OK | Acceptance criterion calls for long-duration replay/sim |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0001 — Adopt ADRs | Yes | Eviction policy (budget, LRU vs. vessel-proximity), per-tile output schema, and CA-grid windowing strategy are design decisions that need an ADR or structured rationale |
| 0002 — Worktree isolation | Yes (satisfied) | Already in issue-cube_bathymetry-70 worktree |
| 0008 — ROS 2 conventions | Yes | Per-tile output (new message type or existing GridMap per tile) must follow ROS 2 message/topic conventions; topic namespace and QoS should be documented |

### Consequences

- Retiring monolithic `GridMap` publish → existing subscribers (Nav2, RViz,
  any downstream visualization) must migrate to the new per-tile stream or the
  windowed CA grid; migration notes needed in the PR
- New per-tile output format gates the boat→CAMP live coverage view
  (rolker/unh_marine_autonomy#86, #250) — the tile schema chosen here becomes
  a shared interface contract
- Eviction policy adds runtime parameters (budget, distance threshold) → parameter
  documentation and default values need to be specified
- `test_publish_equivalence.cpp` tests the existing monolithic path — it will need
  adaptation once the monolithic publish is retired

### Actions
- [ ] Confirm #69 is merged before implementation begins
- [ ] Record eviction policy and per-tile output schema in an ADR (or issue-level
  design note committed to the branch) before implementing
- [ ] Plan must specify Nav2 CA-grid windowing behavior (unknown-cell default) to
  preserve safety guarantees
- [ ] Address monolithic-publish consumer migration in the PR (Nav2 costmap,
  visualization) — or explicitly scope out-of-scope subscribers and note them
- [ ] Update `test_publish_equivalence.cpp` (or replace/extend it) to cover the
  new per-tile path as part of the same PR

## Plan Authored
**Status**: complete
**When**: 2026-06-26 15:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-70/plan.md` at `bf81a78`
**Branch**: feature/issue-70 at `bf81a78`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.
