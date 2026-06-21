---
issue: 15
---

# Issue #15 — Slope correction is disabled (commented out) in Node::insert

## Issue Review
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #15
**Comment**: https://github.com/rolker/cube_bathymetry/issues/15#issuecomment-4761051413
**Scope verdict**: well-scoped

### Actions
- [ ] Verify sign convention of `sonar_relative_position.z` matches `predicted_depth_` before implementing offset (`predicted_depth_ - sonar_relative_position.z`); sonar frame z is "down" while depth convention is "negative below surface".
- [ ] Add test in `test_node.cpp` covering the slope-correction path (non-zero `sonar_relative_position.z` with valid `predicted_depth_`).
- [ ] Add comment near the re-enabled code block explaining why slope correction was originally commented out (missing `range` field at port time).
- [ ] If a new `range` field is added to `Sounding` instead of reusing `sonar_relative_position.z`, update all `Sounding` constructors and document the field's sign convention.

## Plan Authored
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-15/plan.md` at `e3129be`
**Branch**: feature/issue-15 at `e3129be`
**Phases**: single

### Open questions
- [ ] Grid integration follow-on: should a new issue be filed now to track hooking `setPredictedDepth` into Grid/GeoGrid after a prior-surface interpolation pass, or leave for discovery when that pipeline is built?
