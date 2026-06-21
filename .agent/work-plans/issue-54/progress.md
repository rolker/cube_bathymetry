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

## Plan Review
**Status**: complete
**When**: 2026-06-21 08:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-54/plan.md` at `6ae0394`
**PR**: PR-less (no draft PR yet on feature/issue-54)
**Verdict**: changes-requested

### Findings
- [x] (must-fix) ADR-0007 D3 deviation unacknowledged: plan stores Welford mean/M2/count but NOT per-beam `{raw intensity, grazing angle}` pairs that D3 explicitly mandates as sufficient stats. The plan's rationale (lines 55-60) asserts grazing angle is "not per-hypothesis-acceptance" and says slope from #15 will substitute at output, but this directly contradicts D3 ("The hypothesis carries, per contributing beam, the sufficient statistics needed to correct later — `{raw intensity, grazing angle}`"). Either (a) conform to D3 and store per-beam pairs, or (b) amend ADR-0007 D3 to ratify the slope-only approach before implementing. A silent deviation from the accepted ADR is not acceptable — the justification must be captured in the ADR itself, not only in a plan comment. — `plan.md:55-60`, `plan.md:149`
- [x] (must-fix) Initialization path missing intensity call: `Node::update()` has three paths — (1) no prior hypotheses (`!best`, lines 45-51 of node.cpp), (2) accepted by best (`best->update()` returns `true`), (3) intervention (`best->update()` returns `false`). The plan describes paths (2) and (3) but omits path (1). When the first beam arrives and `best` is nullptr, `addHypothesis()` seeds a new hypothesis and that beam's intensity must be accumulated on it. The plan must explicitly include `depth_hypotheses_.back()->updateIntensity(intensity)` in the no-prior-hypothesis branch. — `plan.md:63-87`
- [x] (must-fix) `extractNodeRecord()` omits `nominated_hypothesis_` path: `extractDepthAndUncertainty()` (node.cpp:168-186) checks `nominated_hypothesis_` before falling through to `chooseHypothesis()`. The plan's step 5 defines `extractNodeRecord()` based only on `chooseHypothesis()` without mentioning the nominated path. If the plan's implementation diverges from `extractDepthAndUncertainty()`'s logic here, the two output methods will give different answers for the same node state. The plan should explicitly specify how `extractNodeRecord()` handles `nominated_hypothesis_`. — `plan.md:104-115`
- [ ] (suggestion) `#pragma pack(push, 1)` on `DepthAndUncertainty` — adding `float intensity` extends a packed struct. The current struct is two `float`s (8 bytes); adding a third is trivially layout-safe, but the plan should note this is extending a packed struct (common.h:75-86) and verify no caller depends on `sizeof(DepthAndUncertainty) == 8`. — `plan.md:127`
- [ ] (suggestion) Welford variance: the plan defines `intensity_var = intensity_M2 / (intensity_count - 1)` (sample variance). ADR-0007 D4 says `intensity_uncertainty` is "variance of the estimate" (i.e. `intensity_M2 / (intensity_count * (intensity_count - 1))` = sample variance / n). The plan should confirm which formula it emits in `NodeRecord.intensity_var` and whether it matches D4's "estimate variance" semantics. A discrepancy here will mismatch the store's quality band interpretation. — `plan.md:96-98`
- [ ] (suggestion) `queueFlush()` intensity transport: the plan mentions "queueFlush() similarly passes intensity from the copied queue entries" (step 3) but `queueFlush()` copies to a `std::vector<DepthAndUncertainty>` (node.cpp:244) and then calls `update(q[ex_pt].depth, q[ex_pt].uncertainty, ...)` (line 261). The intensity field will be in `q[ex_pt].intensity` but the call must be updated to pass it. The plan should list `queueFlush()` explicitly in the Files to Change table (currently only node.cpp is listed, which covers it implicitly, but the specific call site should be named). — `plan.md:88`, `plan.md:131`
- [ ] (suggestion) Test for init-path intensity: the mandatory test for "exclusion on intervention" covers the `false` branch; a complementary test for the initialization path (first beam → new hypothesis created → intensity accumulated) should be added to `test_node.cpp` to ensure path (1) above is exercised. — `plan.md:133`
