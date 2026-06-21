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
