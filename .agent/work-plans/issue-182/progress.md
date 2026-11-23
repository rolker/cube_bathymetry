---
issue: 182
---

# Issue #182 — Build on ROS 2 Rolling: port cube_bathymetry and coverage_assessment, add a rolling branch

## Issue Review
**Status**: complete
**When**: 2026-11-23 09:20 -05:00
**By**: Claude Code Agent (claude-sonnet-5-5)

**Issue**: #182

### Scope Assessment

**Well-scoped?** Yes, as a port with parts. The goal and done-when are checkable. Both packages together are about 38.5k lines of non-test source and 33k of tests, but the packages do not depend on each other (`coverage_assessment/package.xml` has no `cube_bathymetry` depend), so they can be cut into separate pull requests.
**Right repo?** Yes. Every change is in this repository; CI and `upstream.repos` are here.
**Serves**: none. This project has no written goals or design document (`AGENTS.md` names none), so no goal is claimed; the port serves the tracking issue rolker/unh_marine_autonomy#415.
**Design picture?** No change. No design document exists here; the port makes no behavioural change.
**Dependencies**: rolker/unh_marine_autonomy#415 (the cause list). Build needs the seven ported `unh_marine_autonomy` packages (their `rolling` branch exists) and `grid_map` from source (ANYbotics/grid_map has a `rolling` branch; the local underlay uses it at 0ece1b5). `gz4d_geo.h` defects stay with rolker/unh_marine_autonomy#420.

#### What the cause list missed or got wrong (checked against the source at `f3ca1e3` and `/opt/ros/rolling`)

- Cause 1 counts 4 `ament_target_dependencies` calls. There are 2: `cube_bathymetry/CMakeLists.txt:248` and `coverage_assessment/CMakeLists.txt:54`, both `grid_map_core`. The other two matches are comment lines (246, 52).
- Cause 2 is incomplete and partly wrong. In Rolling `tf2/*.h` is gone for `buffer_core`, `time`, `exceptions` and `LinearMath/*` (only `.hpp` remains). The full list of removed includes is: `include/cube_bathymetry/detections_projector.h:35-36`; `src/detections_projector.cpp:31`; `src/import_bag_main.cpp:155`; `src/batch_regen_main.cpp:83`; `coverage_assessment/src/coverage_assessment_node.hpp:51-52`; tests `test_beam_geometry.cpp:32-34` and `test_detections_projector.cpp:29` (cube_bathymetry). The issue's "two test files" are both in `cube_bathymetry`.
- Missed: `message_filters/subscriber.h` (`src/cube_bathymetry_node.hpp:62`) does not exist in Rolling (only `subscriber.hpp`), so it is a build failure, not a "to check". But the issue's guess about the message-filter subscription is moot: both `tf2_ros/message_filter.h` (line 61) and `message_filters/subscriber.h` are includes only. Nothing in either package uses `MessageFilter`, `message_filters::Subscriber`, or `rmw_qos_profile_t` subscribe. The fix is to drop the two includes (or move them to `.hpp`), which also makes the C++20 concept question moot for message_filters. Related: `message_filters` is not declared in `cube_bathymetry/package.xml` (cause 11) and would not need to be once the includes go.
- Missed: Rolling's `.hpp` replacement for the `tf2_ros/*.h` headers exists and all of `tf2_ros/buffer.h`, `transform_listener.h`, `message_filter.h`, `static_transform_broadcaster.h`, `transform_broadcaster.h` now `#warning`. There are 11 `tf2_ros` includes (`import_bag_main.cpp:156`, `batch_regen_main.cpp:84`, `bag_to_geotiff.cpp:31`, `detections_to_pointcloud.cpp:34-35`, `cube_bathymetry_node.hpp:45,61`, `coverage_assessment_node.hpp:54-55`, `test_coverage_assessment_node.cpp:67-68`), which is the "about 11" the issue sets aside. Of the eleven, `message_filter.h` goes anyway (unused), so the follow-up issue covers the other ten.
- C++ standard: both set 17 only when `CMAKE_CXX_STANDARD` is unset (`cube_bathymetry/CMakeLists.txt:14`, `coverage_assessment/CMakeLists.txt:13`), same as the ported `marine_backscatter`. Whether anything else in Rolling's headers needs C++20 is not established until a build; with the message_filters includes gone, nothing here includes the headers that use `concept`/`requires` directly.
- Cause 12: `GDALAllRegister()` has one call site, `bag_to_geotiff.cpp:622`, in the offline tool's `main` path, once per run after the import loop. No library or tool code starts a thread (`std::thread` appears only in `test_real_bag_smoke.cpp` and `test_node_lifecycle.cpp`, neither touches GDAL), so the race does not arise here. It is not a change for this port; the stores' own GDAL handling (`marine_bathymetry_store` still registers per operation) is out of this repository. `libgdal-dev` is declared.
- Cause 11: `package.xml` declares `<depend>sqlite3</depend>`, which is the key for the command-line tool. The code includes `<sqlite3.h>` (`include/cube_bathymetry/survey_index_query.h:25`) and uses `find_package(SQLite3)`, so it needs `libsqlite3-dev`, as `marine_survey_index/package.xml:32` has. Confirmed missing key.
- Causes 4 and 5 (Python): neither applies. Both packages are `ament_cmake`; there is no `setup.py`, so no `tests_require`, and no `logger.warn` call exists in the Python (`grep` over `*.py` finds none). The Python that runs is `scripts/derive_angular_response.py`, three launch files, `coverage_assessment/test/test_launch_lifecycle.py` and four pytests, run through `ament_add_pytest_test`/`add_launch_test`; they only fail on Rolling by running them.
- Nav2 (causes 6, 7, 13): not used by either package (no `nav2_*` dependency or include); the only mention is prose in `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md:133`. These causes do not apply, and no Nav2 is needed for CI.
- The issue says `gz4d_geo.h` is included by 19 files; there are 16 source and test files (the other three hits are old work-plan notes).
- `original_cube/` is a third-party tree, in neither `CMakeLists.txt`; the port does not touch it.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Only what's needed | OK | The port is limited to what Rolling needs; deprecations are tracked as separate issues, per the owner's rule quoted in the issue. |
| Keep one current design | OK | No design document in this repository; no change in how the parts fit. |
| A change includes its consequences | Watch | The package manifests, `.github/ci/Dockerfile`, `ci.yml` and `ci-image.yml` must stay in sync (`AGENTS.md`); a `libsqlite3-dev` key or Rolling job touches several. |
| Test what you claim | Watch | The issue asks for repeated runs of the tests; "Done when" should state a count so "pass" is checkable. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0013 (progress timeline) | Yes | This entry and the later phases go to `.agent/work-plans/issue-182/progress.md`. |
| Repository ADR 0001 (tile eviction, `cube_bathymetry/docs/decisions`) | No | No behavioural change. |

### Consequences

- `.github/ci/Dockerfile`, `ci-image.yml` and `ci.yml` branch filters (`branches: [jazzy]`) must change together if a Rolling job is added; `ci.yml` and `ci-image.yml` trigger only on `jazzy` today, so a `rolling` pull request runs no CI at all until the filters change.
- The jazzy image installs `grid_map` through rosdep from apt; on Rolling the key does not resolve (no Rolling release), so a source build is needed.

### Rolling CI: facts for the owner's decision (not a recommendation)

- `upstream.repos` pins `unh_marine_autonomy` (`jazzy`), `mru_transform` (`jazzy`) and `geographic_info` (`ros2`). `git ls-remote`: `unh_marine_autonomy` and `mru_transform` have `rolling` branches; `geographic_info` has `jazzy`, `ros2`, `master` and no `rolling`; the local underlay uses its `ros2` branch (f70b81a). ANYbotics/grid_map has `rolling`.
- The image is `ros:jazzy-ros-base` plus ccache, git, colcon, rosdep, vcstool, with the rosdep keys of both packages and the upstream workspace baked, built weekly and on changes to the recipe or `package.xml` files.
- A Rolling leg would need what the Jazzy one does not: a Rolling base image (or none) in place of the `ros:jazzy-ros-base` one; `grid_map` cloned from `rolling` into the upstream workspace (its rosdep key does not resolve on Rolling, as the apt `grid_map` is Jazzy's) and its keys skipped; `upstream.repos` at the `rolling` branches, which cannot be the same file as Jazzy's, and `geographic_info` at `ros2`; `ROS_DISTRO: rolling`, its own image tag and ccache key; and `ROSDEP_SKIP_KEYS` rechecked. No Nav2 is involved, unlike `marine_ais`. How industrial_ci handles a Rolling runner and container was not checked here; the `unh_marine_autonomy` Rolling job (#415, part 1) is the working example to compare.

### Recommendations

- Cut the work into three parts, in this order: (1) `coverage_assessment` (about 3.4k source lines, independent of `cube_bathymetry`): its `ament_target_dependencies` call, the `tf2/*.h` includes in `coverage_assessment_node.hpp`, and its launch test; it needs only `grid_map` and `marine_*` and is the smallest first pass over the `grid_map` build; (2) `cube_bathymetry` build fixes (causes 1, 2, the dead message_filters includes, the `libsqlite3-dev` key); (3) `cube_bathymetry` tests, repeated runs, and anything they find, plus the Rolling CI commit if one is added. The split is for the plan to decide.
- Correct the cause table in the issue body: 2 `ament_target_dependencies` calls, not 4; `message_filters/subscriber.h` is an unused include that fails on Rolling; the two tests with `tf2/*.h` includes are `cube_bathymetry` ones; Nav2 (causes 6, 7, 13), Python (4, 5) and the GDAL race (12) do not apply here.
- Add `libsqlite3-dev` to `cube_bathymetry/package.xml` (cause 11) in the first build-fixing commit, and look for other keys the development machine hides (`libssl-dev` and `nlohmann-json-dev` are declared).
- Add the `rolling` branch to the `ci.yml` and `ci-image.yml` triggers in a CI-only commit, whatever the owner chooses for the Rolling job, since a `rolling` pull request gets no check today.
- State a repeat count in "Done when" for the launch and lifecycle tests (the issue says "a few hundred times"); #415 found intermittent failures only at that scale.

### Actions
- [ ] Cut the work into three parts, in this order: (1) `coverage_assessment` (about 3.4k source lines, independent of `cube_bathymetry`): its `ament_target_dependencies` call, the `tf2/*.h` includes in `coverage_assessment_node.hpp`, and its launch test; it needs only `grid_map` and `marine_*` and is the smallest first pass over the `grid_map` build; (2) `cube_bathymetry` build fixes (causes 1, 2, the dead message_filters includes, the `libsqlite3-dev` key); (3) `cube_bathymetry` tests, repeated runs, and anything they find, plus the Rolling CI commit if one is added. The split is for the plan to decide.
- [ ] Correct the cause table in the issue body: 2 `ament_target_dependencies` calls, not 4; `message_filters/subscriber.h` is an unused include that fails on Rolling; the two tests with `tf2/*.h` includes are `cube_bathymetry` ones; Nav2 (causes 6, 7, 13), Python (4, 5) and the GDAL race (12) do not apply here.
- [ ] Add `libsqlite3-dev` to `cube_bathymetry/package.xml` (cause 11) in the first build-fixing commit, and look for other keys the development machine hides (`libssl-dev` and `nlohmann-json-dev` are declared).
- [ ] Add the `rolling` branch to the `ci.yml` and `ci-image.yml` triggers in a CI-only commit, whatever the owner chooses for the Rolling job, since a `rolling` pull request gets no check today.
- [ ] State a repeat count in "Done when" for the launch and lifecycle tests (the issue says "a few hundred times"); #415 found intermittent failures only at that scale.
