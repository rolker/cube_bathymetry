---
issue: 118
---

# Issue #118 — Evict → Revisit Loses Blunder Gate on Reference-Only Tiles

## Issue Review
**Status**: complete
**When**: 2026-08-18 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #118
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

The bug is precisely characterized in the issue and confirmed against current source:

- **`ImportAccumulator` (offline, `store_import.cpp`)**: `seeded_` marks a tile once on first touch; `evicted_` marks it on eviction. On revisit the `evicted_` branch calls `reloadEvictedTile()` (survey layer only) and erases from `evicted_`, but `seeded_` is never cleared, so `seedNewTile()` can never fire again. A reference-only tile has no survey layer to reload, so it comes back with no settled hypotheses and no predicted surface — blunder gate and slope correction both silently inactive.

- **Live node (`cube_bathymetry_node.cpp`)**: `trimResidentToBudget()` evicts prior-primed tiles (clean/on-disk, so evictable). `reloadEvictedTile()` (line 1160) only restores `SourceLayer::Survey` from the draft store. No mechanism re-reads `prior_store_dir_` on revisit. The gap is explicitly marked with a comment at line 240: "deferred as cube#118". The prior store object is discarded after `on_configure`.

**Orchestrator-noted context (current):**
- PR #122 (#59, merged): a lost predicted surface now also silently disables SLOPE CORRECTION on revisited tiles — not just the blunder gate. Stakes are higher than the original issue body describes.
- PR #127 (#91 + #119, merged): `primeFromPriorLayers` now primes Chart first / Reference overwrites, exact-level only. The reload re-prime must mirror the same layered semantics at both call sites. Both PR #127 call sites already carry "deferred as #118" comments as placeholders.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Safety First (project) | Action needed | Blunder gate + slope correction silently inactive on revisited unsurveyed tiles — exactly the failure mode that matters in nearshore survey (Lewes/Shoals pattern). Fix is correctness-critical. |
| Human control and transparency | Watch | Gate loss is fully silent (no log, no error). Fix should emit a WARN/INFO when re-priming on reload to make the state observable. |
| Enforcement over documentation | OK | Code comments document the gap; fix closes it mechanically. |
| Capture decisions, not just implementations | Watch | The live node re-prime design choice (retain store handle vs. per-tile cache vs. re-read from disk on demand) is non-trivial. If retaining a handle: RAM impact vs. #70 budget discipline. If re-reading: latency per revisit. The chosen approach should be documented in the plan/ADR. |
| A change includes its consequences | Action needed | Issue requests a regression test; without it the fix is incomplete per this principle. Test shape: reference-only tile → evict → revisit → assert deep-outlier rejected (blunder gate active). |
| Only what's needed | OK | Narrowly scoped to the evict/revisit path; no new abstraction required. |
| Improve incrementally | OK | Single PR, two call sites in one repo. Well-contained. |
| Test what breaks | Action needed | Regression test for evict/revisit gate loss is the primary safety signal for this class of bug (#110, #91, #118 cluster). |
| Simulation-First Validation (project) | Watch | Offline importer path is testable in unit tests; live node path is harder to simulate end-to-end but the prior-prime + evict + revisit sequence should be coverable with a unit test of the node class. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 — Adopt ADRs | Watch | The live-node re-prime design (store handle lifecycle) is a design decision worth recording, especially if a new handle-retention pattern is introduced. Code already references ADR-0001 in comments for the settled-state CUBE hypothesis semantics. |
| ADR-0002 — Worktree isolation | OK | Already in worktree `issue-cube_bathymetry-118`. |
| ADR-0008 — ROS 2 conventions | OK | Node changes should follow ROS 2 lifecycle patterns; prior already loaded in `on_configure`. |
| ADR-0013 — progress.md vocabulary | OK | This entry. |

### Consequences

Per consequences map:
- **Package parameters/topics**: `prior_store_dir_` is already a declared parameter; no interface changes expected.
- **Reusable pattern/pitfall**: The `seeded_` + `evicted_` invariant (seeded_ never cleared on eviction) is a subtle correctness constraint. If fixed by clearing `seeded_` on eviction, downstream readers of `seeded_` must be audited. If fixed by re-priming inside `reloadEvictedTile`, `seeded_` semantics stay narrower. Worth noting in plan.
- The fix must be consistent with PR #127's `primeFromPriorLayers` semantics: Chart first, Reference overwrites, exact-level match for bulk prime, cross-level resample for Reference-only tiles via `primeFromTileResample`.

### Actions
- [ ] Implement fix at both call sites: `ImportAccumulator::reloadEvictedTile` (offline) and `CubeBathymetryNode::reloadEvictedTile` (live node), mirroring PR #127's layered `primeFromPriorLayers` semantics.
- [ ] Live node design decision: choose and document how `prior_store_dir_` is re-accessed on revisit (retain store handle, reload per-tile window, or per-tile cache); weigh against #70 RAM budget discipline.
- [ ] Add regression test: reference-only tile → evict → revisit → assert deep-outlier sounding is still rejected (blunder gate active).
- [ ] Add WARN/INFO log when re-priming on reload so gate activation is observable (transparency principle).
- [ ] Remove "deferred as #118" comments at the two PR #127 call sites once fixed.

## Plan Authored
**Status**: complete
**When**: 2026-08-18 12:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-118/plan.md` at `abb891d`
**Branch**: feature/issue-118 at `abb891d`
**Phases**: single

### Open questions
- [ ] Live node re-prime mechanism: plan proposes per-tile `loadWindow` (Option A, zero RAM cost); operator adjudicates if retained-handle (Option B) is preferred.
- [ ] Cross-level fallback for live node revisit path: plan aligns to exact-level only (matches on_configure); operator adjudicates if cross-level resample is wanted here too.
