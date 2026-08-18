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
