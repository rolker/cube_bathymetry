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

## Plan Review
**Status**: complete
**When**: 2026-06-20 20:30 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-43/plan.md` at `b6425e7`
**PR**: PR-less (--issue / file-path mode)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) Resolve the CMakeLists contradiction: plan prose (Context, steps 9 & 121) says "no CMakeLists dep change / no new find_package," but the Files-to-Change table says add `${marine_acoustic_msgs_TARGETS}` and `tf2_ros::tf2_ros` to the library. Ground truth: the `cube_bathymetry` library already links `${marine_acoustic_msgs_TARGETS}` (CMakeLists:47) so that add is redundant; but it does NOT currently link tf2/tf2_ros, and the projector will call `tf2::BufferCore::lookupTransform` + `tf2::getEulerYPR` (tf2/utils.hpp) — both currently reach the node only transitively via the executable's `tf2_ros::tf2_ros` link. So a tf2-providing link to the library target IS required. Decide and commit to one target: `tf2_ros::tf2_ros` (also covers `fromMsg`) is the safe choice; drop the redundant marine_acoustic_msgs add. — `plan.md:138`
- [ ] (must-fix) The roll/pitch extraction (`tf2::getEulerYPR` from `tf2/utils.hpp`, node lines 207-212) and heave extraction (tide translation.z) must move into the projector along with `lookupAtOrLatest`. The plan describes moving `lookupAtOrLatest` but does not explicitly list the attitude/heave TF reads. The node's BufferCore is fed `rclcpp::Time`; `BufferCore::lookupTransform` takes `tf2::TimePoint`, so the projector's `lookupAtOrLatest` signature must change to `tf2::TimePoint` and the stamp conversion happens once at the top of `project()` — confirm this is the plan's intent. — `plan.md:82-84`
- [ ] (suggestion) `tf2_ros::fromMsg` rclcpp concern is real but already de-risked: `bag_to_geotiff.cpp:329,333` already calls `tf2_ros::fromMsg(header.stamp)` in an executable. For the LIBRARY, prefer the plan's stated fallback unconditionally — build `tf2::TimePoint` directly from `builtin_interfaces::msg::Time` sec/nanosec (no `tf2_ros/time.h` include at all). That keeps the library's tf2 surface to pure `tf2`/`tf2_ros` headers with zero rclcpp risk, and makes the "library must not link rclcpp" guarantee inspection-obvious rather than dependent on a header's internals. — `plan.md:170-176`
- [ ] (suggestion) Behavior-parity check: the plan keeps SOG caller-supplied (`vessel_speed_mps`) and the node passes its `last_vessel_speed_` with the staleness gate (`notTooOld`) staying in the node — this is correct and preserves on-boat semantics (NaN-on-stale, same throttled WARN). Make the regression test assert this explicitly: node-path with a stale odom stamp must pass NaN into `project()` and produce identical soundings to a direct `project(..., NaN)` call. As written, the "node regression check" (plan step 8e) only verifies identity-param round-trip; add the stale-SOG case so the seam's contract is pinned. — `plan.md:111-113`
- [ ] (suggestion) Range-gate placement: currently the node does range filtering AFTER `compute()` (node lines 247-263). The plan moves it into `project()`. Confirm the projector returns ALREADY-filtered soundings and that `ProjectionDiagnostics.filtered_range` carries the dropped count so the node's existing `RCLCPP_DEBUG_STREAM_THROTTLE` message is reproducible. The plan implies this but the diagnostics field name (`filtered_range`) should map 1:1 to the node's current debug log. — `plan.md:50,80`
- [ ] (suggestion) package.xml "no change" is defensible (tf2_ros is already `<depend>`, tf2 arrives transitively), but note the pre-existing latent smell: the node already uses `tf2::` symbols directly without a direct `tf2` rosdep. Not introduced by this PR; do not expand scope, but the projector inherits the same transitive reliance. Leave as-is unless CI rosdep validation flags it. — `plan.md:121`

