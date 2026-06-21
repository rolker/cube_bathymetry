---
issue: 54
---

# Issue #54 — CUBE co-estimation of backscatter: intensity-in-Hypothesis (Welford) + deferred-settled node-output correction (ADR-0007)

## Issue Review
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #54
**Comment**: https://github.com/rolker/cube_bathymetry/issues/54#issuecomment-4761028304
**Scope verdict**: well-scoped

### Actions
- [ ] Implement Phase A (Welford accumulation + enriched node record) and Phase B (output-stage GeoCoder correction stub) as separate, named phases in the PR description; Phase A is unblocked, Phase B stubs the correction pending cube_bathymetry#15.
- [ ] Do NOT fold cube_bathymetry#15 into this issue — keep them independent.
- [ ] Stub the output-stage incidence correction explicitly (not silent omission): emit uncorrected intensity with a comment citing #15 and ADR-0007 D3.
- [ ] Coordinate `MbesCell` shape with `marine_mbes_backscatter_store` write-API seam before implementing (confirm `{ depth, depth_var, intensity, intensity_var, n_samples }` matches).
- [ ] "Exclusion on intervention" test is mandatory and must not be deferred.
- [ ] Handle NaN intensity gracefully in the Welford accumulator (skip NaN beams; verify in tests).
- [ ] Audit all callers/constructors of `Hypothesis` after the struct grows per-beam state vector.

## Plan Authored
**Status**: complete
**When**: 2026-06-21 00:30 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-54/plan.md` at `6ae0394`
**Branch**: feature/issue-54 at `6ae0394`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.
