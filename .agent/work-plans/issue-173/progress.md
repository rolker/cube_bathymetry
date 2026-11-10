---
issue: 173
---

# Issue #173 — Slope-aware incidence correction for M3 backscatter via the shared seabed-geometry module (marine_backscatter)

## Issue Review
**Status**: complete
**When**: 2026-11-10 12:30 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Issue**: #173
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped (one issue, two PRs at the derive-tool boundary is the safer cut; see Actions)

### Actions
- [ ] Action needed: the issue body predates two decisions. Body says the CUBE adapter samples "the pass's own in-memory grid" and computes incidence in `GeoGrid::insert`; the operator comment (2026-10-30) makes the OFFLINE import two-pass against the finished own-survey surface. The plan must treat the body as superseded for `import_bag` and keep the in-memory/"surface so far" idea only as the live-node open question.
- [ ] Action needed: the issue never says how `derive_angular_response.py` gets the same angle as the import. It reads raw bags and bins by `rx_angle`; an incidence-fitted table applied by the import needs the same number per beam. The plan must name one source of truth for the angle and the table/fingerprint basis marker.
- [ ] Action needed: the dependency "uma geometry module" is not available. The work exists only as local, unpushed, un-code-reviewed branch `feature/issue-304` (10 commits, 318 tests green, plan review done, `/review-code` pre-push still owed). cube#173 cannot start implementation until it is reviewed, pushed and merged.
- [ ] Action needed: `batch_regen_bag` (own `ScatterRecord` spill, no TF, no SonarInfo) is not mentioned. Decide refuse-with-message (like the table) versus support.
- [ ] Watch: "reuse the depth-adaptive recon structure" is right for the spill and replay (`ReconCollector::forEachSpilled`) but `--depth-adaptive` refuses `--bs-store` today (single-level backscatter store), so the two-pass slope path is fixed-level only. Say so.
- [ ] Watch: dependency "cube#172 part B must land first" is satisfied (#172 and #176 are on jazzy); the issue text can be refreshed.
- [ ] Watch: issue and design note use the name `DepthSampler`; the implemented API is `marine_backscatter::seabed::SeabedHeightSampler` (up-positive heights, `SourceId`, per-axis spans, `FallbackReason`). The plan uses the real names.
- [ ] Watch: no ADR; record the design as an addendum to the ADR-0007 addendum set (the design is still evolving).

## Plan Authored
**Status**: complete
**When**: 2026-11-10 12:29 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Plan**: `.agent/work-plans/issue-173/plan.md` at `823fb4e`
**Branch**: feature/issue-173 at `823fb4e` (local only, not pushed, no PR)
**Phases**: single issue; two PRs recommended (commit groups A-D, then E-F)

### Open questions
- [ ] uma#304 must be reviewed, pushed and merged before groups E-F (local branch feature/issue-304, 10 commits, no /review-code yet)
- [ ] Derive tool angle source: C++ beam ledger versus Python re-implementation
- [ ] Fallback definition: earth-frame angle from vertical versus sonar-frame |rx_angle|
- [ ] Live node v1: flat-normal earth-frame (L1) versus staying on beam angle; running-surface slope deferred
- [ ] batch_regen_bag with incidence: refuse versus support
- [ ] Include --slope-surface (skip pass one) now; stencil floor default; one PR or two; default basis stays beam until Appledore comparison

## Plan Authored
**Status**: complete
**When**: 2026-11-10 14:20 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Plan**: `.agent/work-plans/issue-173/plan.md` updated with the operator decisions (a "Decisions" section) at commit 6213722

### Decisions (2026-11-10, operator)
1. uma#304 geometry module first: done (merged, unh_marine_autonomy PR #413, jazzy dd737cd7).
2. The derive tool gets its angle from the import's per-beam output (the ledger), not a Python re-implementation.
3. Fallback angle where there is no surface: earth-frame angle from vertical, not the sonar-frame |rx_angle|.
4. Live node, first version: earth-frame angle with a flat normal, no_surface counted; a running own surface is deferred.
5. batch_regen_bag refuses incidence-angle runs.
6. Include --slope-surface <store> (reuse a depth store, skip pass one).
7. Slope stencil: 3 cells with a minimum baseline in metres (default 4 m).
8. Two PRs, split after commit group D.
9. Existing beam-angle tables stay valid for beam runs and are refused for incidence runs, and the reverse.
10. Default angle basis stays beam until an Appledore upslope/downslope comparison shows the correction helps.
11. The 4 m default is provisional; measure the pass-one depth uncertainty on real data first. Kept as a parameter, marked provisional in README and plan.

### Plan sync with the merged module
- Geometry-module section rewritten to the merged API (heights ellipsoidal up-positive, flat fallback with reasons, caller counts, tile-seam behaviour, thread-safety, cache sizing).
- Angle selection moved to Node::update's door, so the median queue layout is unchanged; beam_index moved to PR 2 (ledger).
- Implementation instruction for PR 1 lists the module-dependent content (sampler adapter, two-pass import, --slope-surface) as PR 1 content; the plan records PR 1 = groups A to E, PR 2 = group F. This differs from the literal "split after D" of decision 8 and is flagged for the operator.
