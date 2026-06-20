---
issue: 43
---

# Issue #43 — Extract detections->soundings projection into a node-free library (offline/deterministic replay)

## Issue Review
**Status**: complete
**When**: 2026-06-20 15:30 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #43
**Comment**: https://github.com/rolker/cube_bathymetry/issues/43#issuecomment-4760206630
**Scope verdict**: well-scoped

### Actions
- [ ] Close issue #31 (all three steps merged — PR #33, #35, #37 — issue body stale-open; close before or at start of #43 implementation to avoid misleading dependency signal).
- [ ] Decide library split up-front: adding `DetectionsProjector` to the existing `cube_bathymetry` library vs. a new target; verify `marine_acoustic_msgs` is available as a library dep (currently only on the `detections_to_pointcloud` executable) — check `CMakeLists.txt` + `package.xml` before writing the plan.
- [ ] Extend `bag_to_geotiff`'s `bag_filter()` to admit `marine_acoustic_msgs/msg/SonarDetections` (currently only passes `TFMessage`, `NavSatFix`, `PointCloud2`) for the detections-only-bag offline path.
- [ ] Ensure `DetectionsProjector` headers do not pull in `rclcpp` (the existing `cube_bathymetry` library target does not link rclcpp — keep that boundary clean).
- [ ] Document intensity NaN contract in `DetectionsProjector` API comments (`intensity` is NaN when source omits it — do not interpret as zero).
- [ ] Use synthetic `SonarDetections` + prepopulated `BufferCore` for regression test (more reliable than bag-replay comparison).

## Plan Authored
**Status**: complete
**When**: 2026-06-20 19:15 -04:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-43/plan.md` at `b6425e7`
**Branch**: feature/issue-43 at `b6425e7`
**Phases**: single

### Open questions
- [ ] `tf2::BufferCore` vs `tf2_ros::Buffer` in projector: confirm `tf2_ros::fromMsg` (for stamp conversion) does not pull in rclcpp through the library — if it does, convert stamp manually from nanoseconds.
- [ ] `bag_to_geotiff` projector params from CLI: individual `-f` flags for ~10 params may be unwieldy — consider a YAML/JSON config flag instead (usability only, correctness unaffected by default values).
