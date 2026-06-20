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
