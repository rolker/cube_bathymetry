---
issue: 91
---

# Issue #91 — Live chart-prior predicted-surface seeding for blunder gating

## Issue Review
**Status**: complete
**When**: 2026-08-18 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #91
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Clarify whether the live prime should use `SourceLayer::Chart` or `SourceLayer::Reference` — the issue body says `Chart`, the offline import path in `store_import.cpp:688` (the sibling from #89) currently uses `Reference`, and #119 ("import gate ignores chart layer since the migration") leaves the correct layer ambiguous. Document the choice explicitly in code and align with whatever #119 resolves.
- [ ] Confirm whether budget bounding (`trimResidentToBudget` after `loadIntoSheet`) should apply to the chart prior prime as it does to the draft warm-start prime (#70). The issue does not mention it; the draft prime trims to `max_resident_tiles` right after load to bound RAM — the chart prime loads the same store format and should be consistent.
- [ ] Explicitly scope out evict/revisit re-priming (#118) in the acceptance criteria or plan. The issue body acknowledges the once-at-configure limitation but the acceptance criteria are silent on it; making the deferral explicit prevents scope creep and false review expectations.
- [ ] Document the #59 slope-correction side-effect in the implementation. The issue flags it as a "Bonus" but the acceptance criteria omit it; it should appear in the code comment and PR description so reviewers and operators know the prime activates slope correction, not just the blunder gate.

## Plan Authored
**Status**: complete
**When**: 2026-08-18 10:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-91/plan.md` at `d2ffe75`
**Branch**: feature/issue-91 at `d2ffe75`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-08-18 05:26 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-91/plan.md` at `d2ffe75`
**PR**: PR-less (--issue mode)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Step 3 swaps `seedNewTile`'s exact-match Reference prime for a whole-window `primeFromPriorLayers`. `seedNewTile` loads a *windowed* store (`loadWindow`), which for multi-level reference stores (the #115 ENC L5/L7/L8-under-L10 case) contains coarse tiles keyed at their own coarse level. `primeFromPriorLayers`→`loadIntoSheet`→`primeFromTile` runs on *every* tile, and `primeFromTile` has no cross-level guard (it walks each tile's own `CellAreaIterator`). Coarse tiles would be primed with wrong geometry *in addition to* the `primeFromTileResample` fallback that still runs — so the plan's claim "preserves the #115 cross-level Reference behavior" does not hold. Fix: scope `primeFromPriorLayers`/`loadIntoSheet` to exact-level tiles, or keep the `find(index)` exact-match prime in `seedNewTile` and add only a Chart exact-match lookup for #119. — `plan.md:83`
- [ ] (suggestion) ADR table labels "ADR-0008 (store conventions)"; project ADR-0008 is "Predicted-surface touchdown interpolation geometry" — directly relevant since this prime seeds `predicted_depth_` and activates slope correction (#59). Fix the label and frame compliance as "seeds the surface ADR-0008's touchdown path consumes without altering that geometry." — `plan.md:231`
- [ ] (suggestion) Live node runs two sequential `trimResidentToBudget()` calls (new prior prime + existing draft prime at node.cpp:308); note which primed tiles survive the second trim, since predicted-only prior tiles are not reloadable on revisit (#118). — `plan.md:176`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-18 05:51 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-91 at `22c5f50`
**Mode**: pre-push
**Depth**: Standard (reason: medium change to live on_configure + shared safety-gate helper, two call sites)
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 1 | **Ship**: recommended — no must-fix; plan-review level-scoping fix implemented, both adversarial lenses clean (Lens B's 2 must-fix claims verified as false positives)

### Findings
- [ ] (suggestion) Whole prior store is loaded into RAM before trimResidentToBudget bounds it; note that a very large prior spikes RAM at configure and recommend a region-scoped prior_store_dir — `cube_bathymetry/src/cube_bathymetry_node.cpp:247`

### Notes
- Static analysis: cppcheck run (no new findings on diffed lines); cpplint unavailable on host.
- Local Adversarial skipped: Ollama not installed. Copilot off (default).
- Test suite NOT executed in this pre-push review (static + adversarial reads only); CI runs the GTests.
- Dismissed (false positives): (1) param re-declaration on reconfigure — matches the node's existing convention for ~15 params incl. sibling draft_dir; (2) "trim before draft_dir = data loss" — trimResidentToBudget drops only non-dirty cold tiles and never persists; at configure the sheet holds only clean prior-primed tiles (documented #118 gate-loss, not data loss).

## Integrated Review
**Status**: complete
**When**: 2026-08-18 02:08 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #127 at `dcc9478`
**Sources**: 3 (Copilot R1 @ `dcc9478`, Local Review (Pre-Push) @ `22c5f50`, CI rollup @ `dcc9478`)
**Cross-source confirmations**: 0
**CI**: all-pass (ROS 2 Jazzy industrial_ci success, copilot-pull-request-reviewer success)

### Findings
- [x] (medium, integrator) README "Seed precedence (`--reference-store`)" still documents only the two rungs survey -> reference; the #119 fix adds an exact-level `Chart`-layer rung inside rung 2 (no cross-level fallback for Chart), so the user-facing description of `--reference-store` behavior is now incomplete — `README.md:67-86`
- [x] (low, integrator) New live-node `prior_store_dir` parameter is undocumented outside the source comments; the README describes the offline prior-gate path in detail but not its live equivalent — `README.md`
- [ ] (low, Copilot) Doxygen `@ref primeFromTileResample` in the public header points to a symbol that is not declared in any header (it is a `namespace cube` definition local to `store_import.cpp`), so readers cannot resolve it from the public API surface; reword to describe the cross-level resample path without an `@ref` — `cube_bathymetry/include/cube_bathymetry/store_import.h:253`

### False positives
- (Copilot) "it's in an anonymous namespace in store_import.cpp" / "can produce broken docs / warnings" — the premise and the consequence are both wrong: `primeFromTileResample` is defined at `namespace cube` scope (the anonymous namespace in that file closes at line 336, well above the definition at line 597), and the repo has no Doxyfile and no docs job in `.github/workflows/`, so no doc build emits a warning. Only the readability half of the comment is actionable, kept as a low finding above.

### Addressed since prior round
- (suggestion, Local Review (Pre-Push) @ `22c5f50`) Whole prior store loaded into RAM before `trimResidentToBudget` — addressed by `dcc9478`, which documents the transient configure-time RAM peak and recommends a region-scoped `prior_store_dir` at `cube_bathymetry/src/cube_bathymetry_node.cpp:267-274`.
