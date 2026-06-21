# Plan: Extract detections->soundings projection into a node-free library (offline/deterministic replay)

## Issue

https://github.com/rolker/cube_bathymetry/issues/43

## Context

`DetectionsToPointCloud::detectionsCallback` contains all projection logic — beam ray
geometry from `ping_info.sound_speed` + tx/rx angles, roll/pitch from a TF lookup on
`level_frame`, heave from `tide_frame`, SOG from cached odometry, per-sounding TPU via
`cube::ErrorModel::compute()`, range-gate filtering — welded to a lifecycle node with
`tf2_ros::Buffer` (requires a ROS clock) and `rclcpp` subscriptions.

PR #53 (issue #52) merged `Sounding::intensity` (float, NaN-default); the intensity
passthrough from `SonarDetections.intensities` is already in `detectionsCallback` and
must survive the extraction.

`bag_to_geotiff` already feeds `/tf` + `/tf_static` into a standalone
`tf2_ros::Buffer` (not `tf2::BufferCore` — it passes an `rclcpp::Clock` but never
spins; the buffer is effectively used offline). `bag_filter()` currently admits only
`TFMessage`, `NavSatFix`, and `PointCloud2`.

The `cube_bathymetry` library target (`add_library(cube_bathymetry ...)`) already
links `marine_acoustic_msgs` via `${marine_acoustic_msgs_TARGETS}` and
`marine_autonomy::marine_autonomy`. It does NOT link `rclcpp`. The
`detections_to_pointcloud` executable links both `cube_bathymetry` and `rclcpp`
separately. Adding `DetectionsProjector` to the library preserves this boundary.

## Approach

1. **Close issue #31** — all three implementation steps (PR #33, #35, #37) are merged;
   the issue is stale-open. Close via `gh issue close 31 --repo rolker/cube_bathymetry`
   before first commit to clean the dependency graph.

2. **Add `DetectionsProjector` to the `cube_bathymetry` library** — add
   `include/cube_bathymetry/detections_projector.h` (rclcpp-free header) and
   `src/detections_projector.cpp` to the existing library target. No new CMake target,
   no new ament export changes needed. `marine_acoustic_msgs` is already a library-level
   dep — no CMakeLists.txt dependency change required.

3. **Define `ProjectorParams` struct** in `detections_projector.h`:
   frame names (`base_link_frame`, `level_frame`, `tide_frame`), range gates
   (`minimum_range`, `maximum_range`), `cube::Vessel`, `cube::Device`. All plain data,
   no ROS node, no rclcpp.

4. **Define `ProjectionResult` struct** in `detections_projector.h`:
   `std::vector<cube::Sounding> soundings` + a small diagnostic struct
   `ProjectionDiagnostics` (counts: `filtered_range`, `missing_attitude`,
   `missing_odom`) so the node can log without the projector touching rclcpp.

5. **Implement `DetectionsProjector::project()`** in `detections_projector.cpp`:

   ```cpp
   ProjectionResult project(
     const marine_acoustic_msgs::msg::SonarDetections & detections,
     const tf2::BufferCore & tf,
     float vessel_speed_mps    // NaN when caller has no recent odom
   ) const;
   ```

   - Calls `lookupAtOrLatest()` (extracted as a free function or private static) on
     the `tf2::BufferCore` (no `rclcpp::Time` — use `tf2::TimePoint` from the message
     stamp converted via `tf2_ros::fromMsg`).
   - Builds `cube::Platform`, calls `error_model_.compute()`, applies range gate.
   - Preserves `Sounding::intensity` passthrough (NaN when `detections.intensities`
     is absent or shorter than the detection index) — already handled by
     `Sounding(detections, i, depth)` constructor; no extra code needed.
   - Returns `ProjectionResult{soundings, diagnostics}`; logging is the caller's
     responsibility.

   **SOG supply**: the caller passes `vessel_speed_mps` directly. The node caches
   `last_vessel_speed_` from odom (unchanged) and passes it with a staleness check;
   `bag_to_geotiff` passes `std::nanf("")` (no odom in a detections-only bag — the
   error model accepts NaN vessel speed).

6. **Refactor `DetectionsToPointCloud`** to thin adapter:
   - Hold a `DetectionsProjector` member (constructed in `on_configure` from params).
   - `detectionsCallback` calls `projector_.project(...)` then packs the
     `ProjectionResult::soundings` into `PointCloud2` and publishes.
   - Node keeps all logging via `ProjectionDiagnostics` fields (RCLCPP_WARN_THROTTLE).
   - `lookupAtOrLatest` moves to the projector as an internal helper; the node's
     staleness check for odom stays in the node.

7. **Extend `bag_to_geotiff` for detections-only bags**:
   - Add a `-d /detections` CLI flag for the SonarDetections topic (default empty =
     disabled; existing `-t /soundings` path unchanged).
   - Extend `bag_filter()` to admit `marine_acoustic_msgs/msg/SonarDetections`.
   - In the main loop, when a SonarDetections message arrives on the detections topic,
     deserialize it, call `projector_.project(detections, tfBuffer, std::nanf(""))`,
     pack the resulting soundings into a transient `PointCloud2` (same layout as the
     existing live path), push onto `soundings_buffer`, set `check_buffer = true`.
   - Construct `DetectionsProjector` from CLI args for frame names / range gates /
     Vessel / Device params (new `-f base_link_frame` etc. flags, all optional with
     the same defaults as the node).

8. **Unit tests** in `test/test_detections_projector.cpp`:
   - **Geometry test**: synthetic `SonarDetections` with one beam (known tx/rx angles,
     known sound speed, known two_way_travel_time); prepopulate a `tf2::BufferCore`
     with identity transforms for all three frames; call `project()`; assert
     `sonar_relative_position` matches hand-computed beam coordinates and
     `vertical_error`/`horizontal_error` are finite and positive.
   - **Intensity passthrough**: detection with `intensities` populated → assert
     `sounding.intensity == expected`; detection without intensities → assert
     `std::isnan(sounding.intensity)`.
   - **Range gate**: detection outside `minimum_range`/`maximum_range` → filtered
     from result; one inside → survives.
   - **Missing attitude**: `BufferCore` with no transforms for `level_frame` → roll/
     pitch = NaN → `cube::ErrorModel::compute()` still produces a sounding (NaN
     uncertainty); diagnostics count `missing_attitude == 1`.
   - **Node regression check**: use the same `DetectionsProjector` configured with
     identity params; verify that the node code path (wrapping `project()`) produces
     the same `PointCloud2` fields as the projector result directly.

9. **CMakeLists.txt changes**:
   - Add `src/detections_projector.cpp` to the `cube_bathymetry` library source list.
   - Add `test_detections_projector` gtest target linked to `cube_bathymetry` (which
     already transitively provides `marine_acoustic_msgs`).
   - No new `find_package` calls needed — all deps already present.

10. **package.xml**: no changes needed — all deps already declared.

11. **API documentation**: `detections_projector.h` comment on `project()` documents:
    - `tf` must have been populated with `/tf` + `/tf_static` transforms covering the
      ping timestamp (or nearby — `lookupAtOrLatest` falls back to latest).
    - `vessel_speed_mps` is NaN when SOG is unavailable; the error model accepts NaN.
    - `Sounding::intensity` is NaN when `detections.intensities` is absent or the index
      is out of range — consumers must not interpret NaN as zero backscatter.

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/detections_projector.h` | **NEW** — `ProjectorParams`, `ProjectionDiagnostics`, `ProjectionResult`, `DetectionsProjector` class declaration |
| `src/detections_projector.cpp` | **NEW** — `DetectionsProjector::project()` implementation, `lookupAtOrLatest()` helper |
| `src/detections_to_pointcloud.cpp` | Refactor: add `DetectionsProjector` member; delegate from `detectionsCallback`; keep all node-level logging; remove inline projection math |
| `src/bag_to_geotiff.cpp` | Extend `bag_filter()` for `SonarDetections`; add `-d` flag; add detections deserialization + `project()` call + PointCloud2 pack in main loop; add `DetectionsProjector` construction from CLI args |
| `CMakeLists.txt` | Add `src/detections_projector.cpp` to library; add `test_detections_projector` gtest; add `${marine_acoustic_msgs_TARGETS}` and `tf2_ros::tf2_ros` link to the library target (tf2_ros already a find_package dep; confirm it's not needed on the library — currently only on the executable) |
| `test/test_detections_projector.cpp` | **NEW** — unit tests: geometry, intensity passthrough, range gate, missing-attitude, node regression |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | `DetectionsProjector` API is minimal: one `project()` call, one params struct, one result type. No extra abstraction layers. |
| Improve incrementally | Single PR: library class + node adapter + unit tests + offline consumer. The existing `bag_to_geotiff` soundings path (PointCloud2) is unchanged; the detections path is additive. |
| Test what breaks | First time the projection math gets unit tests. Synthetic ping + prepopulated `BufferCore` is deterministic and doesn't require a bag. |
| A change includes its consequences | `bag_to_geotiff` offline consumer is in-scope. `CMakeLists.txt` and library source list updated. On-boat behavior explicitly unchanged. |
| Capture decisions | This plan documents the `project()` signature rationale (caller-supplied SOG, diagnostics struct). The intensity NaN contract is in the API comment. |
| Human control | Lifecycle node interface (parameters, publishers, configure/activate/deactivate) unchanged. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 — Worktree isolation | Yes | Work is in `feature/issue-43` worktree already. |
| ADR-0008 — ROS 2 conventions | Yes | New header at `include/cube_bathymetry/detections_projector.h`; class named `DetectionsProjector`; test named `test_detections_projector`. No rclcpp in library headers. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `detections_to_pointcloud.cpp` (projection math removed) | `test_detections_projector.cpp` regression check verifies same output | Yes |
| `cube_bathymetry` library gains `detections_projector.cpp` | CMakeLists adds source; no new exported targets or API surface changes for existing consumers | Yes |
| `bag_to_geotiff` gains detections path | `bag_filter()` extended; new CLI flag `-d`; projector construction from args | Yes |
| `tf2_ros` usage in library | Currently `tf2_ros::Buffer` is only in executables; `DetectionsProjector` uses only `tf2::BufferCore` (from tf2, not tf2_ros) and `tf2_ros::fromMsg` for timestamp conversion — check if `tf2_ros` must be added to library link or if `tf2` alone suffices | Yes — step 9 notes to confirm |

## Open Questions

- **`tf2::BufferCore` vs `tf2_ros::Buffer` in the projector**: `lookupAtOrLatest` takes
  `tf2::BufferCore&` (the base class); `tf2_ros::Buffer` IS-A `tf2::BufferCore`, so the
  node can pass its `tf_buffer_` unchanged and `bag_to_geotiff` can pass its
  `tf2_ros::Buffer`. Confirm that `tf2_ros::fromMsg` (for stamp conversion) is accessible
  from the library without pulling in `rclcpp` — it lives in `tf2_ros/time.h` which should
  not depend on `rclcpp`. If it does, use `tf2::TimePoint` constructed manually from the
  stamp nanoseconds instead.
- **`bag_to_geotiff` projector params from CLI**: how many `-f` flags are needed (frame
  names + range gates + Vessel + Device = ~10 params)? Consider a YAML/JSON config file
  flag instead of individual CLI args to avoid flag explosion. This is a usability
  question, not a correctness one — the default values match the node defaults, so most
  users won't need to override.

## Implementation Deviations

These are the deviations from the plan as written, folded in during implementation:

1. **`tf2_geometry_msgs` added to the library** (in addition to the planned
   `tf2_ros::tf2_ros`). `tf2::getEulerYPR` (from `tf2/utils.hpp`) resolves the
   quaternion via `tf2::fromMsg(geometry_msgs::Quaternion, ...)`, whose
   definition lives in `tf2_geometry_msgs` (header-only). Without it the library
   link failed with an undefined reference. `detections_projector.cpp` includes
   `tf2_geometry_msgs/tf2_geometry_msgs.hpp` (before `tf2/utils.hpp`) so the
   symbol is emitted in that TU. `tf2_geometry_msgs` does NOT pull rclcpp, so the
   rclcpp-free guarantee holds. Added to `find_package`, the library
   `target_link_libraries`, and `package.xml`.

2. **`ProjectionDiagnostics::total` and `missing_heave` added** (plan listed
   `filtered_range`, `missing_attitude`, `missing_odom`). `total` carries the
   pre-filter sounding count so the node's existing "N of M filtered by range"
   debug log is reproducible from the result. `missing_heave` records the heave
   TF miss. `missing_odom` was dropped — SOG staleness stays entirely in the node
   (it owns the odom cache and the `notTooOld` gate and passes NaN), so the
   projector has no odom to miss.

3. **`-d` projector frame/range overrides** in `bag_to_geotiff` use long flags
   (`--base-link-frame`, `--level-frame`, `--tide-frame`, `--minimum-range`,
   `--maximum-range`) rather than a single `-f` flag or a config file. Defaults
   match the node defaults, so a detections-only bag projects identically to the
   live node with no flags.

4. **rclcpp-free verification result**: `detections_projector.h` includes zero
   rclcpp/rclcpp_lifecycle/tf2_ros headers; `detections_projector.cpp.o` has
   zero UNDEFINED rclcpp symbols (the only `rclcpp::` symbols present are two
   local-linkage constexpr constants pulled transitively through a message
   header — not a link dependency). The library target links no rclcpp.

## Estimated Scope

Single PR.
