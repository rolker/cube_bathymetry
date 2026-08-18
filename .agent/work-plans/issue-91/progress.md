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
