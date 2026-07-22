---
issue: 104
---

# Issue #104 — Live coverage tiles: neighbor tiles never update — grid selection under-margined vs sounding influence radius

## Plan Authored
**Status**: complete
**When**: 2026-07-22 10:29 -0400
**By**: Claude Code Agent (Claude Fable 5)

**Plan**: `.agent/work-plans/issue-104/plan.md` at `4b34b9a`
**Branch**: feature/issue-104 at `4b34b9a`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready. (Legacy planar
      `MapSheet` keeps its 2-cell buffer — unused code path, noted in plan.)

## Plan Review
**Status**: complete
**When**: 2026-07-22 10:33 -0400
**By**: Claude Code Agent (Claude Fable 5) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-104/plan.md` at `4b34b9a`
**PR**: PR-less
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) Doc-sync step misses repo ADR-0001 addendum ("one-cell-expanded
      window", line 257) and the revisit-reload comment at
      `cube_bathymetry_node.cpp:1138` — `plan.md:46`
- [ ] (suggestion) Add unit test: non-finite `horizontal_error` in a batch must
      not poison batch bounds (finite soundings still select their tiles) —
      `plan.md:49`
- [ ] (suggestion) Record in the ADR-0001 compliance row that the evicted-revisit
      reload is dirty-set-keyed (node.cpp:1149-1156) and therefore consistent
      with widened selection by construction — `plan.md:92`

## Implementation
**Status**: complete
**When**: 2026-07-22 11:23 -0400
**By**: Claude Code Agent (Claude Fable 5)

**Branch**: feature/issue-104 at `de7c75b` (+ plan sync after verification)
**Plan**: `.agent/work-plans/issue-104/plan.md` — kept in sync inline; all 3
plan-review findings folded in at `330ec08`

### Summary
- `Parameters::influenceRadius` extracted (shared by `GeoGrid::insert`,
  `Grid::insert`, `boundsForSoundings`); selection padded by per-sounding
  radius with one-cell floor and non-finite guard; `GeoGrid::insert` gates
  non-finite radii. Doc/comment sync incl. repo ADR-0001 addendum.
- Tests: 463/463 pass (5 new). Seam test uses depth −100 m because
  `Node::insert`'s capture gate (`max(0.05·|depth|, 0.5)` m) bounds deposit
  reach independent of influence radius — discovered during implementation.
- **Bag verification (discriminator)**: 07-21 sessions 17:50 + 14:09 UTC
  replayed pre/post at `-r 0.5`: identical tile sets, ZERO new cells, only
  3 / ~200 seam-cell value refinements. The under-margin fix is correct
  hardening (matters in deeper water) but does NOT explain the 2026-07-21
  live-coverage symptom — RCA continues downstream (publish/transport;
  `~/tiles` is BEST_EFFORT over udp_bridge).

### Open items
- [ ] Decide issue-close semantics: fix addresses the titled defect but not
      the field symptom — PR should reference, probably not auto-close, #104
- [ ] `/review-code` (pre-push) before opening the PR

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-22 11:39 -0400
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-104 at `6b49539`
**Mode**: pre-push
**Depth**: Deep (reason: 200+ changed lines, 14 files, touches project ADR-0001)
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 1 | **Ship**: recommended — no must-fix; clean static analysis, faithful refactor, strong tests, honestly-scoped

### Findings
- [x] (suggestion) `GeoGrid::insert` guards only non-finite `radius`; the new comment says the planar `Grid::insert` "gates the same inputs at its door," but Grid also rejects `vertical_error<=0` and non-finite position/depth. Pre-existing asymmetry, out of the margin fix's scope — consider a matching door-guard in `GeoGrid::insert` for defense-in-depth — `cube_bathymetry/src/geo_grid.cpp:47`
  → Addressed: full door-gate added (position/depth/errors finite, vertical_error>0, horizontal_error>=0) + door-gate test cases in `NonFiniteHorizontalErrorDoesNotPoisonBatchBounds`; 463/463 tests pass.
- [x] (governance/scope) Bag verification shows the fix does NOT reproduce the 2026-07-21 field symptom (identical tile sets, 0 new cells). PR should **reference** #104 without a `Closes #104` keyword so the RCA (publish/transport over BEST_EFFORT udp_bridge) stays open — `progress.md` open items
  → Addressed: user-confirmed — #104 stays open, PR body references without `Closes`.

## Integrated Review
**Status**: complete
**When**: 2026-07-22 12:35 -0400
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #105 at `02a02b0`
**Sources**: 3 (Copilot R1 @ `02a02b0`, Local Review (Pre-Push) @ `6b49539` — both prior findings addressed, CI rollup)
**Cross-source confirmations**: 0 (Copilot's finding is the selection-side residual of the local review's door-gate theme, not a duplicate)
**CI**: all-pass (ROS 2 Jazzy industrial_ci, copilot-pull-request-reviewer)

### Findings
- [x] (valid, Copilot) `influenceRadius` returns a FINITE radius for degenerate
      inputs the grids door-gate (`vertical_error<=0` → ratio ∞ → clamped to
      finite `max_radius`; non-finite depth likewise), so selection/reload
      windows widen and tiles get created + LRU-touched for soundings that can
      never deposit. Fix: NaN sentinel for the door-gate's non-positional input
      set — `cube_bathymetry/src/parameters.cpp:71`
  → Addressed: sentinel added covering depth/vertical_error/horizontal_error
    (finiteness + sign); 7-case sentinel test in test_parameters.cpp; 463/463.
- [x] (valid, Copilot) `influenceRadius` doc comment must document the widened
      sentinel set (degenerate depth / vertical_error, not only
      horizontal_error) — `cube_bathymetry/include/cube_bathymetry/parameters.h:104`
  → Addressed: doc rewritten to name the door-gate-parity sentinel set.

### False positives
- (none)
