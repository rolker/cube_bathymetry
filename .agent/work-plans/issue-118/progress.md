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

## Plan Review
**Status**: complete
**When**: 2026-08-18 06:45 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-118/plan.md` at `abb891d`
**PR**: PR-less (`--issue` mode; `gh` unauthenticated, so the issue body/comments were read via this file's `## Issue Review` entry, not live)
**Verdict**: changes-requested

<!-- Independence: Plan Authored By "Claude Code Agent (Claude Sonnet)"; this reviewer
     shares the workspace-wide agent name but is a separately-dispatched fresh Opus
     context (host-driven independent review per #490). The name-based self-review
     heuristic false-positives in a mono-name workspace, so no self-review annotation
     is applied — this review is independent. -->

### Findings
- [ ] (must-fix) Re-prime ordering inverts the established priority: plan applies reference **after** the survey restore at both sites, but `on_configure` primes reference first then lets survey/draft overwrite so the finer survey predicted depth wins (`cube_bathymetry_node.cpp:234`). `primeFromTile` writes `setPredictedDepthAt` for every finite cell (`store_import.cpp:250`), so on a mixed survey+reference tile the coarse reference clobbers the finer survey predicted surface — silently degrading blunder gate + #59 slope correction on revisited surveyed cells (regression vs current survey-only reload). Fix: reference-first-then-survey, or reference only when the survey tile is absent (mirror `seedNewTile`'s early-return exclusivity) — `plan.md:63`, `plan.md:74`
- [ ] (must-fix) `primeFromTileResample` is defined in an anonymous namespace at `store_import.cpp:597`, AFTER `ImportAccumulator::reloadEvictedTile` (line 545); calling it from there won't compile without hoisting/forward-declaring the helper. Plan's "accessible from the same TU" (`plan.md:70`) omits this reorder.
- [ ] (suggestion) Live-node fix is untested: the new test lands in `test_import_eviction.cpp` (offline `ImportAccumulator` only). `CubeBathymetryNode::reloadEvictedTile` — the afloat safety path, duplicated logic — gets no coverage; `test_node.cpp` is the low-level `Node`, not the ROS node. Note the gap / add a node-level revisit test if feasible — `plan.md:84`
- [ ] (suggestion) Live-node exact-level prime must scope to `scratch.tiles(layer).find(index)` for the single revisited tile; reusing `primeFromPriorLayers` over the window would prime edge-adjacent neighbors and inflate resident count against the eviction budget — `plan.md:74`
- [ ] (suggestion) Context prose says "both call sites carry deferred-#118 comments" but only the live node does (`cube_bathymetry_node.cpp:240`, `:269`); step 4 is correct. Line `:269` sits in a still-valid RAM-spike comment — update in place, don't wholesale-delete — `plan.md:21`, `plan.md:93`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-18 07:11 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-118 at `5f34bb5`
**Mode**: pre-push
**Depth**: Deep (reason: 374 changed code lines >=200 + safety-critical blunder gate on live path)
**Must-fix**: 2 | **Suggestions**: 3
**Round**: 1 | **Ship**: continue — two live-path must-fixes (gate re-primed too late; permanent gate loss on re-prime failure) are genuine safety/correctness concerns warranting another read.

Specialists: Static Analysis (clean on changed lines — cpplint clean; cppcheck errors are pre-existing false positives on untouched lines). Claude Adversarial 2 passes (Lens A + Lens B, cross-confirmed the ordering gap). Governance + Plan Drift by lead. Copilot off (default). Local skipped (Ollama not installed).

### Findings
- [x] (must-fix) Live node re-primes the gate one batch too late: `addSoundings` runs before the revisit-reload loop, so a false-deep blunder in the first revisit batch of an evicted prior-only tile is accepted ungated (Node::insert returns true when predicted_depth_ is NaN). Offline path reloads before addSoundings via gridIndicesForSoundings; mirror it. — `cube_bathymetry/src/cube_bathymetry_node.cpp:1467` (vs loop at :1489)
- [x] (must-fix) Prior re-prime failure permanently loses the gate (prior-only live node, draft_dir empty): loadWindow throw is caught then falls through to `return true`, caller erases the evicted marker, no re-eviction ever retries — tile stays ungated for the session; the "retried on its next revisit" comment is false. Return prior-re-prime success from the draft-empty branch. — `cube_bathymetry/src/cube_bathymetry_node.cpp:1209`
- [x] (suggestion) Offline variant of the above: primePriorLayersForTile returns "did I prime" not "did the read succeed", so a failed prior read + successful survey restore erases the evicted_ marker with no retry. — `cube_bathymetry/src/store_import.cpp:567`
- [x] (suggestion) Re-prime-failure observability: single 5s-throttled WARN collapses distinct failing tiles; a failed gate re-activation is operator-actionable and should be more prominent than the success INFO. — `cube_bathymetry/src/cube_bathymetry_node.cpp:1210`
- [x] (suggestion) No live-node test: the regression test covers only offline ImportAccumulator; the live CubeBathymetryNode::reloadEvictedTile path (where both must-fixes live) is untested. Add a live-node test that forces a prior read failure on revisit and asserts the tile stays in evicted_indices_. — `cube_bathymetry/test/test_import_eviction.cpp:568` (partly deferred: added the offline analog `PriorReadFailureOnRevisitKeepsTileEvicted` covering the shared reload logic; a direct `CubeBathymetryNode` test needs the class extracted from `cube_bathymetry_node.cpp` — behind `main()`, no header/library target — a structural refactor out of scope for this fix pass)

## Implementation
**Status**: complete
**When**: 2026-08-18 07:29 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-118 at `1c488b4`
**Addressed**: Local Review (Pre-Push) (2026-08-18 07:11, at `5f34bb5`) — all 5 findings (2 must-fix, 3 suggestions)
**Commits**: `6301463` (node), `fdb3816` (import), `1c488b4` (test)

### Actions
- [x] (must-fix) Reload evicted tiles BEFORE `addSoundings`, keyed off `gridIndicesForSoundings` (the same influence-radius-expanded window the add touches), so the first revisit batch of a prior-only tile is gated; failed reloads dropped after the add — `cube_bathymetry/src/cube_bathymetry_node.cpp` (`6301463`)
- [x] (must-fix) Track `prior_ok` in `reloadEvictedTile` and return it from both the draft-empty and survey-restore branches, so a thrown prior read keeps the tile evicted for retry instead of erasing the marker and running ungated for the session — `cube_bathymetry/src/cube_bathymetry_node.cpp` (`6301463`)
- [x] (suggestion) Re-prime-failure WARN now names the tile and throttles at 1 s (vs the 30 s success INFO), and states the real retry contract — `cube_bathymetry/src/cube_bathymetry_node.cpp` (`6301463`)
- [x] (suggestion) Offline: `primePriorLayersForTile` gains a `read_ok` out-param distinguishing "load threw" from "nothing to prime"; `ImportAccumulator::reloadEvictedTile` returns it so a failed prior read keeps the tile evicted for retry — `cube_bathymetry/src/store_import.cpp` (`fdb3816`)
- [x] (suggestion — partly deferred) Added offline regression test `PriorReadFailureOnRevisitKeepsTileEvicted` exercising the shared reload logic; a direct `CubeBathymetryNode` test is deferred (class not test-exposed — behind `main()`, no header/library target — needs a structural refactor out of scope here) — `cube_bathymetry/test/test_import_eviction.cpp` (`1c488b4`)

### Verification
- `ament_cpplint` clean on all three changed files.
- **Build/gtest NOT run in this environment**: the worktree's ROS underlay is unbuilt — all of `main/{underlay,core,platforms,site}_ws/install` are empty — and those trees are shared via symlink into `main/`, so building them from this worktree could disrupt concurrent worktree agents. The new `test_import_eviction` case (and the full suite) must be run by the re-review / CI, which build against a populated underlay. The changes were self-reviewed for compile-correctness (signatures, default-arg placement on the single forward declaration, header availability of `tileFilename`/`layerDirName`).

### Next step
review-code (re-review the fixes) via a fresh-context sub-agent:
`.agent/scripts/dispatch_subagent.sh --mode in-process --issue 118 --skill review-code`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-18 07:40 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-118 at `71cc1c3`
**Mode**: pre-push
**Depth**: Deep (reason: safety-critical live blunder-gate path + ~592 changed code lines >=200)
**Must-fix**: 0 | **Suggestions**: 0 (1 already-adjudicated design note, no action)
**Round**: 2 | **Ship**: recommended — Round-1 must-fixes addressed and independently re-verified; 0 new must-fix.

Specialists: Static Analysis (ament_cpplint clean on all 4 changed files; no >99 lines; no whitespace errors). Claude Adversarial 2 passes (Lens A logic — no findings; Lens B systemic/safety — its sole "must-fix" (save-timer race between addSoundings and dropTile) is a FALSE POSITIVE: SingleThreadedExecutor (node.cpp:1548) cannot preempt pingCallback, and the add->drop window is pre-existing (origin/jazzy add:1414/drop:1444) — this PR moved reload BEFORE the add, improving gating). Governance + Plan Drift by lead (matches plan @5f34bb5; live-node direct test deferral documented/accepted). Copilot off (default). Local skipped (Ollama not installed).

Note: build/gtest NOT run here (shared underlay unbuilt, symlinked into main/ — building risks concurrent worktree agents). Compile-correctness reviewed statically; CI must run the suite (incl. the 2 new test_import_eviction cases) against a populated underlay before merge.

### Findings
- [ ] (note, no action) Live-node revisit re-prime is exact-level-only (no cross-level fallback), unlike the offline path — accepted: matches the live node's own on_configure prime, introduces no gap first-touch didn't have; operator-adjudicated per plan open question — `cube_bathymetry/src/cube_bathymetry_node.cpp:1176`
