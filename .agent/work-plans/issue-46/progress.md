---
issue: 46
---

# Issue #46 — Two numeric bugs vs Calder reference: profile_err svp variance double-squared + max_variance_allowed precedence

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-20 10:51 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-46 at `3830646`
**Mode**: pre-push
**Depth**: Standard (reason: small diff but touches core error-model/grid estimation paths)
**Must-fix**: 0 | **Suggestions**: 1 (addressed inline)

Specialists: static analysis (ament_cpplint + ament_uncrustify, clean) · two disjoint-lens
Claude adversarial passes (both clean, fixes verified against Calder's `errmod_full.c` /
`cube_grid.c`). Both regression tests independently confirmed to FAIL with the bugs
reintroduced. 240 tests, 0 failures.

### Findings
- [x] (suggestion) test comment understated that net carries two quadratic svp terms (profile_err + ang_svp) — clarified — `cube_bathymetry/test/test_error_model.cpp:200`
