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

## Checkpoint
**Status**: complete
**When**: 2026-11-23 09:05 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (the Issue Review found the issue well-scoped as a port delivered in parts; its actions correct the cause list and narrow it, and the one CI point it raises is inside what the issue already asks, that the existing Jazzy job keeps checking both branches)
**After**: issue-actions
**Decision**: proceed

Read: the Issue Review of 182 (well-scoped, right repository, no design change). Its actions go to the planner as inputs:

- The corrected cause list: 2 real `ament_target_dependencies` calls (`cube_bathymetry/CMakeLists.txt:248`, `coverage_assessment/CMakeLists.txt:54`); the removed tf2 headers as listed; `message_filters/subscriber.h` and `tf2_ros/message_filter.h` in `cube_bathymetry_node.hpp` are unused includes, the first of which does not exist on Rolling; `sqlite3` should be `libsqlite3-dev`; causes 4, 5, 6, 7, 12 and 13 do not apply (confirm 12 against the build).
- The cut into three parts (`coverage_assessment`; the `cube_bathymetry` build fixes; the `cube_bathymetry` tests and repeated runs) is the plan's to settle from a baseline build.
- CI: `ci.yml` and `ci-image.yml` trigger only on `jazzy`, so a pull request into `rolling` gets no CI at all today. Making the existing Jazzy job run for `rolling` is inside the issue ("the existing job ... must keep passing on both branches") and is planned as a CI-only commit. Whether and how to add a Rolling job stays the owner's decision at the plan checkpoint; the plan sets out the options with what each costs.
- A stated repeat count for the launch and lifecycle tests goes into the plan's done-when.

## Plan Authored
**Status**: complete
**When**: 2026-11-23 09:58 -05:00
**By**: Claude Code Agent (claude-sonnet-5-5)
**Plan**: `.agent/work-plans/issue-182/plan.md` at `90b0eb7`

Planned from a Rolling build and test run on a scratch copy (deleted). Baseline: both packages fail at `ament_target_dependencies` (2.4 s); with the fixes both build cold in 1 min 33 s and the suite runs in 5 min 34 s. The build also showed three causes not on #415 (`bag_to_geotiff` needs `tf2_sensor_msgs` linked for Eigen; `InertWaitable` in two tests is abstract on Rolling; `StaticTransformBroadcaster(shared_ptr)` no longer compiles). Cause 14 (`TransformListener(buffer, this)` silently makes a hidden `transform_listener_impl_*` node on Rolling) was sent by the host during planning; it is confirmed here in both packages by a failing audit test and a probe, and the plan gives a helper that builds on Jazzy (whose constructor needs a pointer, so `*this` alone does not). Cut: Part 1 CI plumbing plus `coverage_assessment`, Part 2 `cube_bathymetry`, Part 3 repeat runs and the Rolling job if chosen. Repeat runs: 0 failures in 52 test names (50x, 20x for the node and launch tests, 10x for nine heavy ones); one unreproduced load-related failure of three timing-sensitive coverage node tests is recorded. Open for the owner: cut order, Rolling job option (recommendation: plain-container job), upstream pins on `rolling`.

## Plan Review
**Status**: complete
**When**: 2026-11-23 10:03 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: needs-work

**Issue**: #182 — Build on ROS 2 Rolling: port cube_bathymetry and coverage_assessment, add a rolling branch
**Plan**: `.agent/work-plans/issue-182/plan.md` at `90b0eb7`
**Branch**: `feature/issue-182`

Reviewed by reading: the plan, the issue review, `ci.yml`, `ci-image.yml`, `.github/ci/Dockerfile`, `upstream.repos`, `coverage_assessment` sources, tests and CMake; `/opt/ros/rolling` headers (`tf2_ros/transform_listener.hpp`, `static_transform_broadcaster.hpp`, `rclcpp/waitable.hpp`); upstream `jazzy` branches through the GitHub API (geometry2 `transform_listener.hpp`, `static_transform_broadcaster.hpp`, `tf2_sensor_msgs/CMakeLists.txt`; rclcpp `waitable.hpp`, `resource/get_interface.hpp.em`); the read-only rolker/unh_marine_autonomy clone (`origin/jazzy` and `origin/rolling` fetched today) and that repository's CI runs. Nothing built or run here; no timing taken.

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Three parts into `rolling`, Part 1 small (one package, CI plumbing). The cut is sound; one fact behind the order choice is wrong (finding 1). |
| Issue alignment | Good | Covers the issue and every Issue Review action (corrected cause list, `libsqlite3-dev`, repeat counts in Done when, CI triggers). The review asked for `ci-image.yml` triggers too; the plan argues for leaving it and the argument holds (one tag, deps-only image, job-time `rosdep install`). **Serves**: "none: this repository has no written goals or design document (`AGENTS.md` names none); the port serves rolker/unh_marine_autonomy#415." Accepted: the project has no goals written down. |
| File targeting | Good | Part 1 files are the right ones; `coverage_assessment/package.xml` misses two keys (finding 7). |
| Consequences | Needs work | The validation statement says neither package is in the navigation path; `cube_bathymetry`'s tiles feed the Nav2 bathymetry costmap (finding 2). Dockerfile comment and sync trap (finding 6). |
| Principle alignment | Good | Port-only, deferrals listed for one follow-up issue, cause 14 gets a named test with a mutation check. |
| ADR compliance | Good | ADR-0013 entries in place; repository ADR 0001 not triggered by Part 1. |
| ROS conventions | Good | Cause 14 fix restores the listener on the node's own interfaces with `spin_thread = true` on both distros (checked); manifests use rosdep keys. |

### Point checks (the host's six)

1. **CI layer (a)**: works as described, with facts the plan does not state. Upstream `rolling` and `jazzy` of rolker/unh_marine_autonomy are code-identical today (40/113 commits apart, but `git diff origin/jazzy origin/rolling` touches 7 files, none a manifest, CMake file or source), so the pin choice changes nothing today. The package set is the same on both (18 manifests), so the `COLCON_IGNORE` list and `ROSDEP_SKIP_KEYS` still fit; Nav2 for `bathymetry_layer` resolves from apt on Jazzy as before. The new upstream keys (`libboost-dev`, `python3-gdal`, `pkg-config`, `libsqlite3-dev`, all landed on upstream `jazzy` 2026-11-20) are not in the image (last built 2026-11-16) and resolve on noble, so job-time `rosdep install` covers them. Part 1's own new keys (`rcl_interfaces`, `rcutils`) are in `ros:jazzy-ros-base`. See finding 5.
2. **Cause 14**: confirmed. Jazzy `transform_listener.hpp:109-131`: `template<class NodeT, ...> TransformListener(BufferCore&, NodeT && node, bool spin_thread = true, ...)`, calling `node->get_node_base_interface()` (pointer-like only), no `RequiredInterfaces`; so `TransformListener(*tf_buffer_, this)` on Jazzy ran with `spin_thread = true`. Rolling `transform_listener.hpp:117-130`: `explicit (BufferCore&, bool spin_thread = true, bool static_only = false)` and `(BufferCore&, RequiredInterfaces, bool spin_thread = true, ...)`; a raw pointer cannot bind to `NodeInterfaces`' `NodeT &` constructor, so `this` takes the `bool` overload. The plan's Rolling branch (`(buffer, node)`) gets `spin_thread = true` by default, matching Jazzy. Both `if constexpr` branches are dependent through `node`, so the helper works as written. The planned test inspects the node's own callback groups, not the domain graph, so the domain-wide false failure does not apply. Shutdown crash: the hidden-node form already had a dedicated spin thread, and the fixed form keeps one (on the node's own group), so the fix adds no thread; 20 of 20 and 24 of 24 node-test runs without a crash are consistent with that. See finding 8.
3. **New causes A, B, C**: confirmed by reading. A: Jazzy `tf2_sensor_msgs` exports `tf2_sensor_msgs::tf2_sensor_msgs` (INTERFACE, links `Eigen3::Eigen`); `bag_to_geotiff` (`cube_bathymetry/CMakeLists.txt:395-404`) links no Eigen carrier. B: Rolling `waitable.hpp` has the five plus `get_timers() const` pure; Jazzy has the five as non-pure virtuals and no `get_timers`, so `override` on the five and none on `get_timers` compiles on Jazzy; GCC 13's `-Woverloaded-virtual=1` (in `-Wall`) does not warn when one overload is overridden, and the existing `execute` override already follows that pattern; no `-Werror`. C: Jazzy broadcaster calls `get_node_parameters_interface(node)`, whose non-pointer overload takes `&node` (`get_interface.hpp.em:68-79`), so `*io_` compiles on Jazzy; Rolling takes `RequiredInterfaces`, which a `shared_ptr` does not convert to.
4. **Unreproduced failure**: the "load sensitive" reading is not the only one, and the cause-14 fix did not move TF onto the node's executor. See finding 9.
5. **Cause 11 / Dockerfile**: `rcl_interfaces` and `rcutils` confirmed; two more undeclared keys (finding 7). No Dockerfile text change needed: confirmed (it bakes from the manifests it copies; ROS keys are in the base image).
6. **Commit order and backport**: confirmed. Both branches are at `f3ca1e3`, so 1, 3 to 8 cherry-pick clean, and each builds on Jazzy alone by the reads above (3: `grid_map_core::grid_map_core` on Jazzy per the plan's read; 4: `.hpp` names exist; 5, 6, 7: points 2 and 3). Commit 1 on `jazzy`: the `rolling` entry is inert there (a push or pull request uses the workflow file of its own ref), and the file stays identical on both branches.

### Findings

1. **[Scope — cut order] (needs work)**: The plan says "If the owner prefers a whole-repo-green `rolling` after Part 1, swap Parts 1 and 2". That is not what the swap gives. With `cube_bathymetry` first, `coverage_assessment` still fails at configure (`coverage_assessment/CMakeLists.txt:54`, cause 1) until its own part lands, so the whole repository is red on Rolling after the first part in either order. Coverage-first: `cube_bathymetry` fails, `coverage_assessment` builds only with `--packages-select` (colcon skips it in a whole-repo build because of the `test_depend`) and 14 of 15 tests pass. Cube-first: `cube_bathymetry` builds and its tests run, `coverage_assessment` does not configure. "By design" holds: `CMakeLists.txt:155-160` registers `cmake -E false` when `cube_bathymetry` is not found. No CI sees the red test in either order (the Jazzy job builds both; the Rolling job, if chosen, comes after Part 2). Resolution: correct the sentence so the owner decides between those two states; a whole-repo-green `rolling` after the first part would need that part to carry `coverage_assessment`'s cause-1 fix as well.
2. **[Consequences — validation] (concern)**: "Neither is in the vehicle's control or navigation path" is wrong for `cube_bathymetry`. The node and the importers write `draft/` and `processed/` tiles into `marine_bathymetry_store` (`cube_bathymetry_node.hpp:420-423`, `store_import.cpp`, `batch_regen.cpp`), and rolker/unh_marine_autonomy's `bathymetry_layer` is a Nav2 costmap layer that reads exactly those layers ("so the planner routes around shoals", `bathymetry_layer/README.md:1-12`, `bathymetry_layer.hpp:30-31`). So a wrong depth from a Rolling build reaches the global costmap. `coverage_assessment` is outside that path (operator display). Resolution: correct the paragraph, and tie the owed validation to it: before `cube_bathymetry` runs on Rolling on a boat with `bathymetry_layer` enabled, a Jazzy/Rolling comparison of the imported tiles on the same bag (the real-bag excerpt, or a synthetic bag if the archive is not mounted). This is Part 2's validation; Part 1 is unaffected.
3. **[CI layer (b) — option (iii)] (needs work)**: Option (iii) would not work as described. `industrial_ci` builds the whole upstream monorepo minus the four pruned directories, which includes `bathymetry_layer` (`<depend>nav2_costmap_2d</depend>`, `<test_depend>nav2_util</test_depend>`); Nav2 has no Rolling release (rolker/unh_marine_autonomy `ros-base-docker.yml:11-19`, which leaves it out of its Rolling job for that reason). A Rolling `industrial_ci` leg would also need `bathymetry_layer` pruned and the `nav2_*` keys skipped, or a source-built navigation2. Resolution: add that to option (iii)'s "New files"/"Does not check" rows so the owner compares like with like.
4. **[CI layer (b) — estimates] (minor)**: The basis for option (ii)'s minutes is thinner than the table shows. The working example measures lower for upstream: rolker/unh_marine_autonomy's Rolling container job (runs 44807007698, 44806974373, 44803274495, 44791489172) takes 5 min 45 s to 8 min 5 s in total, with `rosdep install` 62 s, a cold build of about 16 packages in 2 min 21 s and tests in 1 min 22 s. That is less than the plan's "apt 2 to 3, UMA 5 to 6" for seven packages. Tests "6" is probably low: about 5.5 min locally, mostly serial and CPU-bound (`test_import_bag_slope` 90 s), on slower runner cores. The `grid_map` closure is untimed. The "12 to 16 with ccache" figure assumes a cache step the example job does not have. Resolution: give (ii) as a range anchored on the measured upstream job plus this repository's build and tests (roughly 15 to 25 min cold, with `grid_map` the unknown). Say that the ccache figure needs an `actions/cache` step added to the container job.
5. **[CI layer (a)] (minor)**: Three facts for the plan's CI text. (a) Pinning upstream `rolling` or `jazzy` gives the same tree today (point check 1), so open question 3 has no effect until the two diverge. (b) This repository's Jazzy job last ran on 2026-11-12. That was before upstream's 2026-11-20 manifest and CMake changes (on `jazzy` and `rolling` alike), and the image (2026-11-16) predates them too. So Part 1's first run is also the first run against those changes on either pin, and a red there may be upstream's: say which log line tells them apart (`setup_upstream_workspace`/`rosdep` or an upstream package's build, against `cube_bathymetry`/`coverage_assessment`). (c) Neither `jazzy` nor `rolling` has branch protection, and the only ruleset (default branch) has no status-check rule, so "a rename would orphan a required-check name" is not a reason today. Keeping the name is still fine.
6. **[Consequences — backport] (minor)**: After commit 2, `.github/ci/Dockerfile`'s comment "this MUST match cube's CI upstream.repos" is untrue on `rolling`: the image is built from `jazzy`'s pins and `rolling`'s file differs. That is harmless for a deps-only image while the trees match, but the plan should say so (or the comment be widened in commit 1, which is backported). The trap is not a cherry-pick of a later commit, which the plan covers. It is a whole-file or merge sync from `rolling` to `jazzy`, which is upstream's own practice ("jazzy takes rolling's copy whole", `ros-base-docker.yml:1-3`): that would carry the `rolling` pin onto `jazzy`. Resolution: name `upstream.repos` in the backport section as the one file never taken whole from `rolling`.
7. **[Cause 11 — coverage_assessment] (minor)**: The cause-11 table misses two keys. The installed launch file `coverage_assessment/launch/coverage_assessment_launch.py:42` imports `lifecycle_msgs`, which `package.xml:46` declares only as `test_depend`, so it needs `exec_depend` (or `depend`). `test/test_launch_lifecycle.py:41-42` imports `launch_testing`, which is not declared (it arrives through `launch_testing_ament_cmake`). Resolution: add both to commit 8 (ROS packages, so no image change).
8. **[Cause 14 helper] (minor)**: The helper is correct as planned (point check 2). Two changes make the intent survive edits. Pass `spin_thread` explicitly as `true` in both branches: Jazzy's `TransformListener(buffer, this)` defaulted to `true`, and this node's design depends on it (`coverage_assessment_node.hpp:879-885`: TF on its own thread, outside the executor). Make the listener type a template parameter of the trait test, so neither branch is checked against a concrete type that only one distro has. Note in the commit that the fix keeps TF on a dedicated thread; it does not move TF onto the node's executor.
9. **[Validation — unreproduced failure] (needs work, owner decides)**: Load is not the only plausible cause, and the plan's account should say so. First, a 20 to 30 s wait (`spinUntil(..., 20.0/30.0, each)`, `test_coverage_assessment_node.cpp:897-915, 995-1023`) is for events that take well under a second when idle; CPU load alone stretching that 20 to 100 times is possible but not the likely reading. Second, the test pins `ROS_DOMAIN_ID=86` with localhost discovery (`CMakeLists.txt:120-129`), and its TF frames are global (`earth`, `map`, `base_link` on `/tf` and latched `/tf_static`, not under the per-test namespace). Any other run of the same executable on this machine at the same time (the scratch copy and this worktree, or another agent's copy of this repository) would cross-feed TF, and two of the three failing tests are about TF going stale or `map` moving. Third, the cause-14 fix did not change the TF threading (finding 8), so the fix does not explain a timing change. To settle it: run the final binary 50 times under a deliberate CPU load (a parallel build) with nothing else on domain 86 (check with `ros2 node list` on 86, or run the binary directly with an unused `ROS_DOMAIN_ID`). Zero failures in 50 bounds the rate below about 6% at 95%. If any fails, repeat with the pre-fix binary (it builds on Rolling with the `this` call) under the same load. Whether a load-only flake belongs to this port is the owner's call.

### Summary

Part 1's content is right: the Jazzy and Rolling header facts behind causes 14, A, B and C hold, and each backported commit should build on Jazzy alone. The CI layer (a) design works. The plan needs corrections before the owner decides from it: the swap does not give a green `rolling` (1), `cube_bathymetry` does feed the navigation costmap (2), option (iii) omits the Nav2 pruning it would need (3), and the unreproduced failure has a cross-talk explanation as plausible as load (9).

### Recommended Actions

- [ ] Correct the cut-order text: neither order gives a whole-repo-green `rolling` after the first part; state what each leaves (finding 1).
- [ ] Correct the validation paragraph: `cube_bathymetry` tiles feed `bathymetry_layer` (Nav2 costmap); name the owed Jazzy/Rolling tile comparison before Rolling use with that layer (finding 2).
- [ ] Option (iii): add the `bathymetry_layer` prune and `nav2_*` skip keys it needs (finding 3); re-anchor option (ii)'s minutes on the measured upstream Rolling job and state the ccache step (finding 4).
- [ ] CI text: pins identical today; first run is also the first against upstream's 2026-11-20 changes, and how to tell an upstream red; drop the required-check reason (finding 5).
- [ ] Backport section: `upstream.repos` is never taken whole from `rolling`; fix or widen the Dockerfile "MUST match" comment (finding 6).
- [ ] Commit 8: add `lifecycle_msgs` as `exec_depend` and `launch_testing` as `test_depend` (finding 7).
- [ ] Helper: pass `spin_thread = true` explicitly in both branches; listener type as a template parameter (finding 8).
- [ ] Unreproduced failure: record the domain-86 cross-talk alternative and the settling run (50 under load, domain checked clear; pre-fix binary only if one fails) (finding 9).
