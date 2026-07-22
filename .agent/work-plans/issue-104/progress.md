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
