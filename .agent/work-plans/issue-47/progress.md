---
issue: 47
---

# Issue #47 — Error-model fidelity: datum-aware tide terms + parameterized device errors + divergences doc

## Issue Review
**Status**: complete
**When**: 2026-06-20 18:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #47
**Comment**: https://github.com/rolker/cube_bathymetry/issues/47#issuecomment-4758729271
**Scope verdict**: well-scoped

### Actions
- [ ] Confirm whether `ellipsoidal_referenced` flag needs to be exposed as a ROS parameter in `detections_to_pointcloud.cpp`/`node.cpp` (not just a `Vessel` struct field) for operator control without recompile.
- [ ] Verify that new default `range_error_percent=0.005` (0.5%) produces TPU values consistent with IHO Order 1a/Special Order limits at representative survey depths (2–20 m).
- [ ] Add inline comment at `error_model.cpp` constructor lines 32–35 explaining why tide terms are omitted (ellipsoid-referenced; references issue #47).
- [ ] Link divergences doc from package README and from issue #30.
- [ ] Confirm existing `test_error_model.cpp` round-trip tests still pass unchanged when defaults are preserved.

## Plan Authored
**Status**: complete
**When**: 2026-06-20 19:45 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-47/plan.md` at `8672188`
**Branch**: feature/issue-47 at `8672188`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-20 12:48 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context)) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-47/plan.md` at `8672188`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) Test 3 (`RangeErrorFloorDominatesAtShallowDepth`) calls `range_error(0.02)` directly, but `ErrorModel::range_error()` is a `private` method (`error_model.h:270`, under `private:` at line 217). The test fixture lives in `namespace cube` but still can't reach a private member. Either make `range_error()` public, add a `FRIEND_TEST`/friend declaration, or exercise the floor through the public `compute()` path. Plan does not mention this access change. — `plan.md:108-111`
- [ ] (must-fix) Test 4 (`DefaultsPreserveExistingBehavior`) is mislabeled and not bug-distinguishing. The range-error default deliberately changes from `0.05` (5%) to `0.005` (0.5%) — a 10x reduction — AND adds a 0.05 m floor that did not exist before. So default `range_error()` behavior is NOT preserved; only the *tide* path and the non-range error terms are. Re-scope this to "tide terms off by default / non-range error terms unchanged", and add an explicit assertion that the new default `range_error(depth)` equals `(max(0.005*|depth|, 0.05))^2` so the intended change is pinned rather than silently asserted as a no-op. — `plan.md:113-115,180`
- [ ] (suggestion) Step 1 wires the tide-variance contribution into `static_error_sources_.vertical_reduction` at construction time based on `ellipsoidal_referenced`. That is the right seam (keeps `swath_vertical()` untouched), but note `vertical_reduction` is pre-summed once in the constructor — confirm no other consumer reads the raw draft/ddraft/loading sum expecting it to exclude tide. (Verified: only `swath_vertical()` reads `vertical_reduction`, so the seam is safe — worth a one-line note in the plan.) — `plan.md:43-48`
- [ ] (suggestion) Step 4 documents the angle-error fallback as "no change". Worth recording in the divergences doc that the static fallback `device_.across_track_beamwidth / 12.0` (`error_model.cpp:214`) is consumed in **degrees** while the live per-ping path converts to radians first (`*M_PI/180.0`, line 216) — a pre-existing unit inconsistency in the fallback branch. Not in scope to fix, but the divergences doc is the natural place to flag it so it isn't mistaken for intended behavior. — `plan.md:91-93`
- [ ] (suggestion) Test 2 (`RangeErrorParameterized`) sets `floor=0.0`; with `std::max(|depth|*pct, 0.0)` that is fine, but state the depth explicitly so the percent term provably dominates the (zero) floor and the test stays robust if the floor default changes. — `plan.md:106-107`
- [ ] (suggestion) Plan "Files to Change" paths use the `cube_bathymetry/<...>` nested-package prefix (correct for this repo's `<repo>/cube_bathymetry/` layout); README link target in Step 7 should sit alongside the existing "Data flow & pose sourcing (design: #31)" section in the repo-root `README.md`, which already cross-references #30/#31 — a natural anchor. — `plan.md:140-143`

### Assessment
Scope, issue alignment, file targeting, principle alignment (ellipsoidal flag + range params as ROS parameters declared in `on_configure()` per ADR-0008; divergences doc per "capture decisions"), and ADR compliance are all sound. All five Issue-Review action items are addressed by the plan. Range/Device-placement decision (range error on `Device`, not `Vessel`) is correct — range error is a sonar spec. The two test must-fixes (private-method access; mislabeled defaults-preserved test) are the only blockers and are cheap to fix during implementation.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-20 14:43 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-47 at `bfa90fe`
**Mode**: pre-push
**Depth**: Standard (reason: medium change touching core error-model + a lifecycle node + new ROS params)
**Must-fix**: 0 | **Suggestions**: 2 (both addressed)

Specialists: static analysis (ament_cpplint + ament_uncrustify, clean) · two disjoint-lens
Claude adversarial passes (both no must-fix; all source/arithmetic/Calder-reference claims
verified). 245 tests, 0 failures.

### Findings
- [x] (suggestion) divergences doc said angle error is beamwidth/√12 but code uses /12 (self-contradictory) — corrected prose + added modeling note — `cube_bathymetry/docs/divergences_from_calder.md:73`
- [x] (suggestion) new range params unvalidated; negative percent/floor would silently corrupt TPU — added non-negative clamp + warn — `cube_bathymetry/src/detections_to_pointcloud.cpp:90`
