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

## Checkpoint
**Status**: complete
**When**: 2026-11-23 10:11 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (the two questions that were the owner's, the Rolling CI job and the order of the parts, he answered, quoted below; the Plan Review's remaining findings each state their resolution and go to the implementer as binding corrections, which gives the same result as a plan revision without another plan and review round on a plan that took 52 minutes to write)
**After**: plan
**Decision**: proceed

Read: `plan.md` at `90b0eb7` and the Plan Review of it (needs-work; nine findings; the six points the host asked about confirmed, with gaps).

**The owner's decisions** (asked in one dialog, with the review's corrections to the plan's facts in front of him: that `cube_bathymetry` is in the navigation path through the tiles `bathymetry_layer` reads; that option (iii) would not work as described; that the Rolling job estimate is roughly 15 to 25 minutes cold with `grid_map` unmeasured; that neither order leaves `rolling` fully green after the first part):

- Rolling CI: "B: plain container job (Recommended)": a `ros:rolling-ros-core` container job like rolker/unh_marine_autonomy's, added after Part 2 as its own CI commit, its real run time reported to him from the first run.
- Order of the parts: "coverage_assessment first (Recommended)".

**Part 1 is implemented now** (CI layer (a) and `coverage_assessment`), with these corrections overriding the plan where they differ:

- F7: cause 11 for `coverage_assessment` also needs `lifecycle_msgs` as `exec_depend` (the installed launch file imports it; today it is a `test_depend` only) and `launch_testing` as `test_depend`.
- F8: the cause-14 helper passes `spin_thread` explicitly as `true` in both branches, because Jazzy's `TransformListener(buffer, this)` defaulted to `true` and this node's design depends on TF being on its own thread; the listener type is a template parameter of the detection, so both `if constexpr` branches are dependent; the commit says the fix keeps TF on a dedicated thread.
- F6: `upstream.repos` is the one file never taken whole from `rolling` onto `jazzy`; say so in the pull request, and widen or correct the `.github/ci/Dockerfile` comment that says it must match `upstream.repos`.
- F5: Part 1's first CI run is also the first run of this repository's Jazzy job since 2026-11-12, before upstream's manifest and CMake changes of 2026-11-20; a red in `setup_upstream_workspace`, `rosdep` or an upstream package's build is upstream's, a red in `cube_bathymetry` or `coverage_assessment` is this change's.
- F9: the unreproduced failure of three timing tests in `test_coverage_assessment_node` is settled by evidence before the pull request: the final binary 50 times under a deliberate CPU load with nothing else on its `ROS_DOMAIN_ID` (86); if any run fails, the same with the pre-fix binary; the result goes to the owner if it is not zero.

**For Part 2's plan revision** (recorded here so the next `plan-task` reads them): F2, the validation statement is wrong, `cube_bathymetry` feeds the Nav2 costmap through `marine_bathymetry_store` and `bathymetry_layer`, and a Jazzy/Rolling comparison of imported tiles on the same bag is owed before it runs on Rolling on a boat; F1, F3, F4, the corrected facts on the cut, option (iii) and the estimates; the Rolling job (owner's choice B) is added after Part 2 with an `actions/cache` step considered and its measured time reported.

## Implementation
**Status**: complete
**When**: 2026-11-23 11:38 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Branch**: feature/issue-182 at `73372b5`

Part 1 only (CI layer (a) and `coverage_assessment`), with the checkpoint's corrections F5 to F9:
- `c323928` CI: `ci.yml` triggers `[jazzy, rolling]`; Dockerfile "MUST match" comment widened (F6), behaviour unchanged. Backport.
- `a171fca` CI: `upstream.repos` pins unh_marine_autonomy at `rolling`, with a comment that the file is never taken whole onto `jazzy`. Not backported.
- `3fac5ea` cause 1: `target_link_libraries(coverage_assessment_core grid_map_core::grid_map_core)`. Backport.
- `873c0fc` cause 2: `tf2/exceptions.hpp`, `tf2/time.hpp`. Backport.
- `0f7ad1a` new cause: `InertWaitable` overrides Rolling's pure `Waitable` members, `get_timers()` without `override`. Backport.
- `a9a5941` new cause: broadcasters built from `*io_`. Backport.
- `3819efa` cause 14: `src/make_transform_listener.hpp` (listener type as template parameter, `spin_thread` explicitly `true` in both branches), call site, test `NodeTest.TfListenerSubscribesThroughThisNodeOnItsOwnThreadNotAHiddenNode` (only this node's callback groups). Backport.
- `73372b5` cause 11: `rcl_interfaces` and `lifecycle_msgs` as `depend` (the latter was test-only; the installed launch file imports it), `launch_testing` and `rcutils` as `test_depend`. Backport.

Verified on Rolling (no Jazzy here; the CI job is the only Jazzy check):
- `./build.sh --packages-select coverage_assessment --packages-ignore cube_bathymetry` (cold): 1 package finished, 37 s; only the tf2_ros `.h` deprecation `#warning`s. Without `--packages-ignore cube_bathymetry` colcon refuses (cube_bathymetry's `package.sh` missing).
- `./test.sh` with the same selection: 93% passed, 14 of 15; `test_frame_chain` fails by design (`cube_bathymetry` not installed). In a scratch copy with Part 2's build fixes for `cube_bathymetry`: `test_frame_chain` passed (13 tests).
- Mutations (scratch copy, test executable relinked): raw `this` and `spin_thread = false` each fail the new test and `CallbackGroupsHoldNothingButTfAndClockOutsideTheExecutor`, 51 others pass.
- Repeats as separate processes: 4 core gtests 50 of 50 each; launch test 50 of 50; `test_coverage_assessment_node` 50 of 50 under 17 busy loops on 20 cores (load 17.1 to 17.7), domain 86 empty before every tenth run and holding only the test's own nodes during one, run time 62 to 64 s. The unreproduced failure did not recur; no pre-fix run was needed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-23 11:47 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: approved

**Branch**: feature/issue-182 at `4572289`
**Base**: origin/rolling
**Depth**: Standard (reason: CI workflow and upstream pin change plus one change to running code, the TF listener; about 170 changed lines outside the records)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 1 | **Ship**: recommended — no must-fix findings; remaining suggestions can be applied or tracked

Part 1 only (CI plumbing and `coverage_assessment`). Specialists: static analysis (cppcheck on the new header clean; ament linters pass in the package's test run; YAML parses), governance, plan drift (matches the plan as corrected by the Checkpoint: the Dockerfile comment is F6, `lifecycle_msgs`/`launch_testing` are F7, explicit `spin_thread` and templated detection are F8), Claude adversarial (no must-fix), Gemini and Codex (both completed).

Listener (cause 14): read line by line against `/opt/ros/rolling/include/tf2_ros/tf2_ros/transform_listener.hpp` and geometry2 `jazzy` `transform_listener.hpp`. Rolling takes the `RequiredInterfaces` constructor (the `(BufferCore&, bool, bool)` one is not viable for a node reference); Jazzy has no `RequiredInterfaces` and `(buffer, &node, true)` picks the `NodeT&&` template by exact match over the pointer-to-bool overload, which is the old call. Same node, `spin_thread = true`, same QoS and option defaults, `static_only = false`, on both. Both `if constexpr` branches depend on `ListenerT`. The new test fails for a hidden node (no `/tf` on this node) and for `spin_thread = false` (default group, auto-added), and reads only this node's callback groups.

CI: upstream rolker/unh_marine_autonomy `rolling` (ec4fd5d7) and `jazzy` (c4bc9ed7) have the same 18 manifests with no manifest, CMake or source difference, so the prune list and `ROSDEP_SKIP_KEYS` fit unchanged; upstream's own Jazzy job is green on both branches (2026-11-20, 2026-11-23). The `:jazzy` image (last built 2026-11-16) predates upstream's 2026-11-20 keys; job-time `rosdep install` covers them. Expected first run: green; if red, the step names whose it is (Checkpoint F5).

Dismissed: Gemini's "`tf2_ros/transform_listener.hpp` may not exist on Jazzy" (it does, geometry2 `jazzy`); Gemini's plain `target_link_libraries` signature, `lifecycle_msgs` as `depend`, and Dockerfile drift (Checkpoint F6/F7); the adversarial reviewer's `tf2_ros/*.h` includes (left for the follow-up issue by rule). Codex's Jazzy teardown hang (listener destroyed before its spin thread enters `spin()`: `cancel()` is then overwritten by `spinning.exchange(true)`) is real in Jazzy's tf2_ros/rclcpp but not introduced here: the old `TransformListener(*tf_buffer_, this)` had the same thread and destructor, and the test file already has nine configure/cleanup cycles; kept as a follow-up suggestion, not a must-fix.

Re-run here (Rolling): `./build.sh --packages-select coverage_assessment --packages-ignore cube_bathymetry` up to date, 1 package finished; `./test.sh` same selection: 14 of 15, `test_frame_chain` fails by design; the new test plus the two group-audit tests, 20 of 20 separate runs on domain 86 (checked empty with `ros2 node list --no-daemon`). Mutations not re-run (implementer's record). Nothing run on Jazzy.

### Findings
- [ ] (suggestion) The new test keeps one group per topic (`group_of[topic] = group`), so a second `/tf` or `/tf_static` subscription on this node would go unnoticed; collect a list per topic and assert exactly one — `coverage_assessment/test/test_coverage_assessment_node.cpp:1996`
- [ ] (suggestion) Guard the helper against a pointer argument: on Rolling a `Node *` lvalue would silently select `(BufferCore&, bool spin_thread, bool static_only)` (hidden node, `static_only = true`); add `static_assert(!std::is_pointer_v<NodeT>)` — `coverage_assessment/src/make_transform_listener.hpp:58`
- [ ] (suggestion) `ci-image.yml`'s `workflow_dispatch` publishes the `:jazzy` tag from whatever ref it is run on; run from `rolling` it bakes `rolling`'s upstream pins, contrary to the Dockerfile comment; restrict the job to `refs/heads/jazzy` (a CI commit, backported) — `.github/workflows/ci-image.yml:14`
- [ ] (suggestion) Record in the follow-up issue the pre-existing Jazzy race where a `TransformListener` destroyed before its thread enters `spin()` can hang in `join()` (Codex); not introduced by this branch — `coverage_assessment/src/coverage_assessment_node.hpp:548`

## Checkpoint
**Status**: complete
**When**: 2026-11-23 11:47 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (the pre-push review approved round 1 with no must-fix; Jazzy cannot be built on the development machine and a pull request into `rolling` has had no CI until this branch's trigger change, so the pushed branch's first run is the first Jazzy evidence and the first test of that change)
**After**: publish
**Decision**: publish

Read: the Local Review (Pre-Push) at `4572289`, verdict approved, ship recommended, four suggestions open: the new test keeps one callback group per topic, so a second `/tf` or `/tf_static` subscription would go unnoticed (`coverage_assessment/test/test_coverage_assessment_node.cpp:1996`); guard the helper against a pointer argument with a `static_assert` (`coverage_assessment/src/make_transform_listener.hpp:58`); `ci-image.yml`'s manual dispatch publishes the `:jazzy` image tag from whatever branch it is run on (`.github/workflows/ci-image.yml:14`); record in the follow-up issue a pre-existing Jazzy race where a `TransformListener` destroyed before its thread enters `spin()` can hang (`coverage_assessment_node.hpp:548`). They stay open and go through triage after publish, so the first CI run starts now. This is a project issue, so the host runs no full pre-commit pass. The pull request says `Part of #182`.

## Integrated Review
**Status**: complete
**When**: 2026-11-23 11:59 -05:00
**By**: Claude Code Agent (claude-opus-5-5)

**PR**: #183 at `0f68fb4`
**Sources**: 4 (Local Review (Pre-Push) @ `4572289` (covers `0f68fb4` via bookkeeping), Gemini and Codex via that review, GitHub reviews (none), CI rollup)
**Cross-source confirmations**: 0 at head (the ci-image finding was raised by Codex and the pre-push reviewer in the same round, and the pointer-guard finding by Gemini and the pre-push reviewer; both sources sit in that one entry, not two at this head)
**CI**: pending (run 45220509531, `ROS 2 Jazzy (industrial_ci)`, still in its `industrial_ci` step at 11:58 -05:00, about 11 minutes in; no result to record; the host re-checks before the merge checkpoint)

### Findings
- [x] (suggestion, Local Review @ `4572289` + Codex) `ci-image.yml`'s `workflow_dispatch` publishes `:jazzy` from any ref, so run from `rolling` it bakes `rolling`'s upstream pins into the Jazzy image. Fix: add `if: github.ref == 'refs/heads/jazzy'` to the `build-and-push` job. The file stays identical on both branches: on `jazzy` the schedule trigger (runs on the default branch, `jazzy`) and the push trigger (`branches: [jazzy]`) both have `github.ref == refs/heads/jazzy`, so they are unaffected; on `rolling` only a dispatch can fire it, and that is now skipped. A CI commit, backported by hand — `.github/workflows/ci-image.yml:25`
- [x] (suggestion, Local Review @ `4572289` + Gemini) Guard the helper against a pointer argument: with a `Node *` lvalue `NodeT` deduces as `Node *`, and on Rolling `make_shared<ListenerT>(buffer, node, true)` selects `(BufferCore &, bool spin_thread, bool static_only)` silently. Fix: `static_assert(!std::is_pointer_v<NodeT>, "pass the node by reference");` as the first line of `makeTransformListener`. It is on the caller's argument type only; the Jazzy branch's internal `&node` is `Node *` when `NodeT = Node`, so it is unaffected (a `Node *` argument would already fail to compile there as `Node **`). `<type_traits>` is already included. The only caller (`*this`) is fine today — `coverage_assessment/src/make_transform_listener.hpp:58`
- [x] (suggestion, Local Review @ `4572289`) The new test records one group per topic (`group_of[topic] = group`), so a second `/tf` or `/tf_static` subscription on this node goes unnoticed. Fix: collect `std::map<std::string, std::vector<rclcpp::CallbackGroup::SharedPtr>>`, `ASSERT_EQ(groups_of[topic].size(), 1u)`, then run the existing checks on `groups_of[topic].front()` — `coverage_assessment/test/test_coverage_assessment_node.cpp:1991`
- [x] (suggestion, Local Review @ `4572289` + Codex) Record in the follow-up issue (with the deprecated `tf2_ros/*.h` includes) the pre-existing Jazzy race: a `TransformListener` destroyed before its thread enters `spin()` can hang in `join()`, because Jazzy's `spin()` resets the cancel flag on entry. Not introduced by this branch (Jazzy's `TransformListener(buffer, this)` had the same thread); a note is the whole action, per the port rule of no big changes — `coverage_assessment/src/coverage_assessment_node.hpp:548` (deferred: not this pull request's: pre-existing on Jazzy; recorded for the follow-up issue the host drafts for the owner)

### False positives
- (Gemini, pre-push round) "`tf2_ros/transform_listener.hpp` does not exist on Jazzy" — Jazzy's tf2_ros ships the `.hpp` headers (the `.h` ones are the deprecated shims); this was already ruled on in the pre-push review, and the Jazzy CI run settles it either way

## Checkpoint
**Status**: complete
**When**: 2026-11-23 11:59 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (four valid suggestions are open and each has a small, named fix or is a note for the follow-up issue; merging past open findings is the owner's answer only)
**After**: findings
**Decision**: address

Read: the Integrated Review of pull request 183 at `0f68fb4`: four valid suggestions, one false positive, CI pending (the Jazzy job was still running when the triage ended; its result is read by the host and handed to the fix pass if it arrives in time, and CI on the fix push is the check either way). Addressed: (1) `ci-image.yml`: `if: github.ref == 'refs/heads/jazzy'` on the `build-and-push` job, so a manual run from `rolling` cannot publish the `:jazzy` image; (2) `make_transform_listener.hpp`: a `static_assert` refusing a pointer argument; (3) the listener test collects every callback group per topic and asserts exactly one. (4) the pre-existing Jazzy race (a `TransformListener` destroyed before its thread enters `spin()` can hang in `join()`, `coverage_assessment_node.hpp:548`) is not this pull request's to fix: it is recorded for the follow-up issue the host drafts for the owner, and its box is ticked as deferred there.

## Implementation
**Status**: complete
**When**: 2026-11-23 12:39 -05:00
**By**: Claude Code Agent (claude-opus-5-5)

**PR**: #183 at `f9524b5`
**Addressed**: Integrated Review at `0f68fb4` (2026-11-23 11:59 -05:00)
**Commits**: `f80d56c` (CI), `41e8476` (code), `6cbd24b` (test), `f9524b5` (box checks)

### Actions
- [x] `ci-image.yml`: `if: github.ref == 'refs/heads/jazzy'` on `build-and-push`, with a one-line reason; triggers unchanged, file identical for both branches — `.github/workflows/ci-image.yml:27`
- [x] `static_assert(!std::is_pointer_v<NodeT>, "pass the node by reference")` as the first statement of `makeTransformListener`. Builds on Rolling; on a scratch TU a `rclcpp::Node *` argument now fails with `make_transform_listener.hpp:62:23: error: static assertion failed: pass the node by reference`, the same TU against the previous header compiled silently, and a reference argument compiles — `coverage_assessment/src/make_transform_listener.hpp:62`
- [x] Listener test collects a vector of groups per topic and asserts exactly one for `/tf` and `/tf_static`, checks on `.front()`. Mutations (in the worktree, rebuilt with `build.sh` each time, restored with `git checkout`, tree clean after): (a) raw `TransformListener(*tf_buffer_, this)` fails at line 2005 "this node holds no subscription to /tf"; (b) helper with `spin_thread = false` fails at lines 2010/2013 "in the executor's default group" and "would be spun by the node's executor"; the distinct messages and a relinked executable (new mtime, size) show the mutated code ran; restored passes — `coverage_assessment/test/test_coverage_assessment_node.cpp:1991`
- [x] Pre-existing Jazzy listener-destruction race — `coverage_assessment/src/coverage_assessment_node.hpp:548` (deferred: not this pull request's: pre-existing on Jazzy; recorded for the follow-up issue the host drafts for the owner)

### Checks
- `./build.sh --packages-select coverage_assessment --packages-ignore cube_bathymetry`: 1 package finished (only the known tf2_ros `.h` deprecation warnings).
- `./test.sh` same selection: 93% tests passed, 1 tests failed out of 15 (`test_frame_chain`, by design until Part 2).
- Node test (domain 86 checked clear first) as 30 separate processes: pass=30 fail=0.
- CI on `0f68fb4` (run 45220509531, `ROS 2 Jazzy (industrial_ci)`): pass in 16m50s; 19 upstream packages built, cube_bathymetry 53/53 and coverage_assessment 15/15 tests passed on Jazzy. The commits of this pass have no Jazzy check until CI runs on the push.

## Local Review
**Status**: complete
**When**: 2026-11-23 12:55 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: approved
**Dispatch**: resumed (agent adb5d0cfb8e4238fd, resume 1 of 3)

**PR**: #183 at `f366b9f`
**Depth**: Standard (reason: round 2 narrow check per handoff: the round-1 pre-push findings and the fix diff `0f68fb4..f366b9f`; no cross-model reviewers, per the owner's rule after a round with no must-fix)
**Must-fix**: 0 | **Suggestions**: 0

Fix diff: `f80d56c` (ci-image.yml guard), `41e8476` (static_assert), `6cbd24b` (test), plus records. The only non-test code change is the `static_assert` and its two comment lines in `make_transform_listener.hpp`.

- `static_assert(!std::is_pointer_v<NodeT>)`: it applies to the caller's type before the `if constexpr`, so it covers both branches. Jazzy's internal `&node` is unaffected. It rejects only a raw pointer (including a reference to a pointer), which is the silent Rolling case. Probed on Rolling with the node test's compile flags: a `rclcpp::Node &` compiles; a `rclcpp::Node *` fails at `make_transform_listener.hpp:62` "static assertion failed: pass the node by reference". A `std::shared_ptr<rclcpp::Node> &` passes the assert but fails loudly in `NodeInterfaces` ("has no member named get_node_base_interface"). It does not compile silently: `shared_ptr`'s `operator bool` is explicit. No legitimate call is rejected.
- `if: github.ref == 'refs/heads/jazzy'`: the default branch is `jazzy` (checked), so the schedule trigger's ref is `refs/heads/jazzy`; push is limited to `[jazzy]`; `workflow_dispatch` gets the chosen ref, so only a dispatch from `jazzy` publishes. A job skipped by `if` reports success, so the run is not red. ci-image.yml has no pull_request trigger.
- Test: a vector of groups per topic, with exactly one asserted for each of `/tf` and `/tf_static`; the checks run on `.front()` after the size assert. The implementer's mutation record (raw `this`; `spin_thread = false`) still fails it.
- The fourth round-1 suggestion (the Jazzy listener destruction race) was deferred by the host to the follow-up issue; settled.

CI on `f366b9f` (run 45229921366, `ROS 2 Jazzy (industrial_ci)`): pass in 15m46s. Log lines: "Summary: 19 packages finished [3min 15s]" (upstream); "Summary: 2 packages finished [3min 55s]"; "100% tests passed, 0 tests failed out of 53"; "100% tests passed, 0 tests failed out of 15"; "Summary: 2143 tests, 0 errors, 0 failures, 158 skipped"; listener test on Jazzy: "[       OK ] NodeTest.TfListenerSubscribesThroughThisNodeOnItsOwnThreadNotAHiddenNode (9 ms)".

Re-run here (Rolling): build with `--packages-select coverage_assessment --packages-ignore cube_bathymetry`: 1 package finished. The listener test and the two group-audit tests: 20 of 20 separate runs on domain 86 (checked empty with `ros2 node list --no-daemon`). The static_assert probe is described above.

### Findings
- [ ] No issues found. LGTM.

## Integrated Review
**Status**: complete
**When**: 2026-11-23 12:57 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Dispatch**: resumed (agent a5ab5afa351fba0b8, resume 1 of 3)

**PR**: #183 at `f366b9f`
**Sources**: 3 (Local Review @ `f366b9f` (covers `c70f14e` via bookkeeping), GitHub reviews (none: no reviews, inline comments or conversation comments), CI rollup)
**Cross-source confirmations**: 0
**CI**: all-pass

CI: `ROS 2 Jazzy (industrial_ci)` on `f366b9f` (run 45229921366) passed in 15m46s. Log lines: "Summary: 19 packages finished [3min 15s]" (upstream); "Summary: 2 packages finished [3min 55s]"; "100% tests passed, 0 tests failed out of 53" (cube_bathymetry); "100% tests passed, 0 tests failed out of 15" (coverage_assessment); "Summary: 2143 tests, 0 errors, 0 failures, 158 skipped"; "[       OK ] NodeTest.TfListenerSubscribesThroughThisNodeOnItsOwnThreadNotAHiddenNode (9 ms)". The run left pending in the previous entry (45220509531 on `0f68fb4`) completed with success.

Previous round (Integrated Review at `0f68fb4`): I checked each fix against the diff `0f68fb4..f366b9f`. `f80d56c` adds `if: github.ref == 'refs/heads/jazzy'` to `build-and-push`, and the triggers are unchanged. `41e8476` adds `static_assert(!std::is_pointer_v<NodeT>, ...)` before the `if constexpr`, so the Jazzy branch's `&node` is untouched. `6cbd24b` collects a vector of groups per topic and asserts `size() == 1u` before the `.front()` checks. The Jazzy listener-destruction race was deferred by the host to the follow-up issue. The false positive from that round (Gemini's `.hpp` include claim) is settled: CI built `make_transform_listener.hpp` on Jazzy with the `tf2_ros/transform_listener.hpp` include, and the test passed. The `sources` helper lists the pre-push entry at `4572289` as stale, which is expected: its four findings were ruled on in that Integrated Review and are all closed now.

### Findings
- [x] No open findings. All four findings from the previous round are closed (three fixed, one deferred to the follow-up issue). The Local Review at `f366b9f` found nothing, there is no GitHub review, and CI passes on Jazzy.

### False positives
- (Gemini, pre-push round) "`tf2_ros/transform_listener.hpp` does not exist on Jazzy" — confirmed false by the Jazzy CI build at `f366b9f`

## Checkpoint
**Status**: complete
**When**: 2026-11-23 13:03 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: owner
**After**: merge
**Decision**: merge

The owner's answers, 2026-11-23: "Merge when CI is green (Recommended)" for pull request 183 (Part 1); "Backport right after (Recommended)" for the hand backport to `jazzy`; "Post it (Recommended)" for the comment on rolker/unh_marine_autonomy#415 (cause 14 extended to the broadcasters, cause 16 for `rclcpp::Waitable` subclasses); "Open it (Recommended)" for the follow-up issue in this repository. The host merges only after the CI run on the pushed records passes.

## Checkpoint
**Status**: complete
**When**: 2026-11-23 13:14 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (part 1 merged as pull request 183 at `7d1d3cb`; the plan was written before part 1's reviews and its part 2 section predates what part 1 found, so it is refreshed rather than implemented as it stands)
**After**: plan
**Decision**: revise

Part 1 (CI for `rolling`, `coverage_assessment`) is merged; its backport to `jazzy` is pull request 185. The plan is refreshed for part 2, the `cube_bathymetry` package, and then for the plain-container Rolling CI job (the owner's choice "B: plain container job", added after part 2 with its measured run time reported to him). To carry into the refreshed plan: part 1's helper and `static_assert` pattern for the TF listener (cause 14, `spin_thread = true`); causes 15 to 16 on rolker/unh_marine_autonomy#415; the unused `tf2_ros/message_filter.h` include goes in part 2; the validation statement must say that this package feeds the Nav2 costmap; a comparison of produced tiles before and after is owed; left-alone items go to rolker/cube_bathymetry#184 as a comment drafted for the owner.

## Plan Authored
**Status**: complete
**When**: 2026-11-23 13:39 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Plan**: `.agent/work-plans/issue-182/plan.md` at `33371b1`

Refreshed for Part 2 (`cube_bathymetry`) from a measured Rolling baseline: as the branch is, CMake fails at `CMakeLists.txt:248` (cause 1); past it, cause 2 (9 includes, 6 files), the unused `message_filters/subscriber.h` include (new; removing it needs `tf2_ros/buffer.hpp`), Eigen missing for `bag_to_geotiff` (new), cause 16 (`InertWaitable`), then cause 14 as one failing test (`AllCallbacksShareTheDefaultGroup`). With fixes in a scratch copy: 53 of 53 tests, `test_frame_chain` green, no intermittent failure in repeats (node test 20x, 34 fast tests 10x, 9 slow 3x). Plan: 7 code commits (all backported), the Part 1 listener helper copied per package with a named test and two mutations (both already shown to fail the existing audit test), and the plain-container Rolling CI job as the last commit (needs `grid_map` and, newly found, `nav2_msgs` from source; estimated 17 to 22 min cold). States that the package feeds the Nav2 costmap and proposes a Jazzy-before/Rolling-after tile comparison on synthetic bags (Rolling side here, Jazzy side on deadpool).

## Plan Review
**Status**: complete
**When**: 2026-11-23 13:47 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Verdict**: needs-work

**Issue**: #182 — Build on ROS 2 Rolling: port cube_bathymetry and coverage_assessment, add a rolling branch
**Plan**: `.agent/work-plans/issue-182/plan.md` at `33371b1`
**Branch**: `feature/issue-182`

Review of the refreshed plan's Part 2 (Part 1 is a delivered record). The earlier Plan Review in this file is of the first plan (`90b0eb7`). Its Part 2 points are carried: the navigation-path statement (its finding 2) is now in the plan, and its estimate basis (finding 4) is used. Reviewed by reading: the plan; `cube_bathymetry` and `coverage_assessment` CMake, manifests, sources and tests; the Rolling underlay sources (`grid_map` at `0ece1b5`, navigation2 at `9113449e`, geographic_info at `f70b81a`) and `/opt/ros/rolling` headers; upstream `jazzy` through the GitHub API (geometry2 `tf2/`, `tf2_ros/transform_listener.hpp` and `.cpp`, `tf2_sensor_msgs/CMakeLists.txt`; rosbag2 `bag_metadata.hpp`); `rosdep resolve` for the new keys; the model workflows (mru_transform and unh_marine_navigation `rolling`, rolker/unh_marine_autonomy `ros-base-docker.yml`). Nothing built or run in this review, and no timing was taken. The planner's 53/53 and 67/67 results are not re-run here.

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Seven code commits of one cause each, all backported, then the CI commit and the records. Left-alone items go to issue 184. |
| Issue alignment | Needs work | The code commits cover every error the baseline found (E1 to E5, T1), and I confirmed each by reading. Commit 8 builds `nav2_msgs` and `nav2_common` from source, which the owner has put on hold, and the plan does not list this as an open question (finding 1). **Serves**: "none: this repository has no written goals or design document (`AGENTS.md` names none); the port serves rolker/unh_marine_autonomy#415." Accepted: the project has no goals written down. |
| File targeting | Good | The file list matches the errors. The 9 `tf2/*.h` includes are at the lines named, and their `.hpp` names exist on geometry2 `jazzy`. |
| Consequences | Needs work | The validation statement says the package is on the navigation path and that a shifted grid is a grounding risk (plan lines 76 to 83). The tile comparison has no pass criterion set before it runs, and it does not say which side writes the bags (finding 2). Commit 6's parameter change is not named (finding 4). |
| Principle alignment | Good | Port-only. The cause-14 fix gets a named test with two mutations. Duplicating the helper is argued, and its consolidation goes to issue 184. |
| ADR compliance | Good | ADR-0013 is followed. Repository ADR 0001 is not triggered. |
| ROS conventions | Good | Commit 6 puts the listener back on the node's own interfaces with `spin_thread = true`, as on Jazzy. Manifest keys are rosdep keys and resolve (below). |

### Point checks

1. **Nav2 source in the Rolling job: the facts.** The dependency is real at `0ece1b5`. `grid_map_ros/package.xml:22` declares `<depend>nav2_msgs</depend>`, `CMakeLists.txt:12` calls `find_package(nav2_msgs REQUIRED)`, and `GridMapRosConverter.hpp:25,152,166` declares the costmap converters. It dates from the ROS 2 port of `grid_map_ros` (`07c8429`, #242, the only commit that touches that line), so no ROS 2 `grid_map` commit avoids it, Jazzy's 2.2 line included. `rosdep resolve --rosdistro rolling` has no rule for `nav2_msgs`, `nav2_common` or any `grid_map_*` package.
   - **What this repository needs from `grid_map_ros`**: only `GridMapRosConverter`. `cube_bathymetry` uses `toMessage` at `cube_bathymetry_node.hpp:1478,1755`, through the `cube_bathymetry_node_link` target (`CMakeLists.txt:322-335`, the node and `test_node_lifecycle`), and `bag_to_geotiff` takes its include directories (`:392`). `coverage_assessment` uses `fromMessage` and `toMessage` at `coverage_assessment_node.hpp:772,1451`. Both packages call `find_package(grid_map_ros REQUIRED)` at the top level, so every target, the offline importers included, waits on it. The libraries and importers need only `grid_map_core` and `grid_map_msgs`.
   - **What building the two costs**: `nav2_msgs` (69 interface files) depends on `nav2_common`, `backward_ros`, `geographic_msgs` and `unique_identifier_msgs`. The last three resolve to Rolling binaries (`ros-rolling-backward-ros` and `ros-rolling-geographic-msgs` are installed here). `nav2_common` needs `python3-typeshed` and `python3-osrf-pycommon`, which resolve. The planner measured about 1 min 5 s for `nav2_msgs` here; a runner is probably about twice that. The navigation2 working tree is 98 MB without `.git`, so a sparse or blob-filtered clone is worth having. The pin `9113449e` is on `main`. The local install was built on 11-19 from `dcffa939`, not from the pin, but the two commits do not differ in `nav2_msgs` or `nav2_common`.
   - **How Jazzy gets it**: from binaries. The Jazzy `upstream.repos` has no `grid_map` or navigation2 entry, so rosdep installs `ros-jazzy-grid-map-ros` and, through it, `ros-jazzy-nav2-msgs`.
   - **What the project does elsewhere**: both model Rolling jobs keep Nav2 out. Upstream's `ros-base-docker.yml:11-19` says its Rolling job "never asks for navigation2", and `bathymetry_layer` is checked on Rolling by hand against a source-built navigation2. unh_marine_navigation's Rolling job builds only its three packages that need no Nav2. Commit 8 would be the project's first CI job to build Nav2 packages from source.
2. **Cause 14, per listener**: confirmed with one correction. Rolling's `transform_listener.hpp` has `explicit (BufferCore &, bool spin_thread = true, bool static_only = false)` and `(BufferCore &, RequiredInterfaces, ...)`, so `this` binds to the `bool` overload. The Jazzy one-argument constructor and Rolling's both create `transform_listener_impl_*` with the same `NodeOptions` (geometry2 `transform_listener.cpp`, both branches), so `detections_to_pointcloud.cpp:115` behaves the same on both distros, and leaving it alone is right for a port. Correction: `DetectionsToPointCloud` is a `rclcpp_lifecycle::LifecycleNode` (`detections_to_pointcloud.cpp:37`), not a plain `rclcpp::Node`. Jazzy also has the three-argument `(BufferCore &, bool, bool static_only)` constructor. Neither point changes the conclusion.
3. **Can Part 2's diff change output?** Not in the tiles. Commits 1 to 5 and 7 change build lines, include names (the `tf2/*.h` files are deprecation wrappers around the `.hpp` files), test code and the manifest. Commit 1 touches `cube_bathymetry_grid_projection`, which only the node link target and `test_publish_equivalence` use, not the importers. The grid_map Eigen plugin definitions still come from `grid_map_core-extras.cmake` at directory scope. Commit 4 adds an include path and nothing else. Commit 6 changes which node owns the `/tf` and `/tf_static` subscriptions and which thread delivers them; it does not change what reaches the buffer. Both before and after, a dedicated thread fills the same `tf2_ros::Buffer`, which is built on the node's clock (`cube_bathymetry_node.hpp:521`). Global `--ros-args` remaps reach the hidden node as well, and `/tf` is absolute. A lookup can therefore differ only through timing, that is, whether a transform has arrived by the time a ping asks for it. The one other observable change: the listener's default `qos_overriding_options` (Rolling `transform_listener.hpp:79-83`) now declare `qos_overrides./tf...` parameters on the cube node, as on Jazzy, instead of on the hidden node.
4. **E4, Eigen for `bag_to_geotiff`**: holds on Jazzy. geometry2 `jazzy` `tf2_sensor_msgs/CMakeLists.txt:31-48` defines an INTERFACE target that links `Eigen3::Eigen`, and `:94-104` exports it. Rolling's export links `Eigen3::Eigen` too. What supplied the Eigen include path on Jazzy before (probably the old-style `${grid_map_ros_INCLUDE_DIRS}`) cannot be checked without a Jazzy install, and the fix does not depend on it.
5. **Cause 11 keys**: `libsqlite3-dev` → `libsqlite3-dev` and `zlib` → `zlib1g-dev` on both noble and resolute. The old `sqlite3` key resolves to `sqlite3`, the command-line tool, while CMake needs the headers (`find_package(SQLite3 REQUIRED)`, `CMakeLists.txt:38`), so the change is right. Each ROS key added has an include in `src/` or `include/` (checked one by one), and `launch` and `launch_ros` are imported by the three installed launch files.
6. **Each commit on Jazzy (by reading)**: every `tf2/*.hpp` and `tf2_ros/buffer.hpp` named exists on geometry2 `jazzy`. The node header uses only `tf2_ros::Buffer`, `TransformListener` and `tf2::` exception and transform names, which `buffer.hpp`, `tf2_geometry_msgs.hpp` and `tf2_sensor_msgs.hpp` supply. The helper and `InertWaitable` text passed Jazzy CI in Part 1. The `tf2_sensor_msgs` target is linked today. `cube_bathymetry/` is identical on `origin/jazzy` (`0b70b46`) and `rolling`.
7. **CI shape against the models**: it matches mru_transform's `rolling` job: `ros:rolling-ros-core`, the `rosdep` init and update, a symlinked workspace, a SHA fetch of geographic_info with `COLCON_IGNORE`, `rosdep install` without `-r` over the `colcon list --packages-up-to` paths (checked non-empty), and the `if:` expression. `colcon list` over the planned sources gives a closure of 20 packages, matching the plan: the `grid_map` five, `nav2_msgs`, `nav2_common`, `geodesy`, `geographic_msgs` and the upstream `marine_*` set. `mru_transform` is not in it, rightly. See finding 3 for the steps the plan leaves out. The 17 to 22 minute cold estimate is plausible from the cited runs: the upstream Rolling job, plus Part 1's Jazzy runner figures (2 min 53 s to build and 5 min 46 s to test `cube_bathymetry`), plus about 2 min for `grid_map` and `nav2_msgs`. It is unmeasured, as the plan says.

### Findings

1. **[Issue alignment — owner's hold on Nav2 in CI] (needs work, owner decides)**: Commit 8 builds `nav2_msgs` and `nav2_common` from navigation2 source. The owner said about building Nav2 in CI: "Let me think about it more before we do anything else about it." Open Questions 1 to 5 do not raise it, and Done when requires "Rolling job green". Point check 1 has the facts: the dependency is real on every ROS 2 `grid_map`; neither package has a Rolling release; this repository needs `grid_map_ros` only for `GridMapRosConverter` in the two live nodes, but `find_package(grid_map_ros REQUIRED)` gates every target; and both model Rolling jobs keep Nav2 out. The options and their costs are these. (A) As planned: two Nav2 packages from a sparse, pinned clone, about 2 min of runner build. This is the project's first CI job to build Nav2 source, and it needs the owner's yes. (B) Commits 1 to 7 merge on the Jazzy job and hand runs on Rolling, as upstream does for `bathymetry_layer`, and the Rolling job waits for the owner's decision or a Nav2 Rolling release. This costs no Rolling CI for this repository meanwhile. It is the plan's own agreed fallback, so it needs nothing new. (C) A Rolling job without `grid_map_ros`: a CMake option to skip the two nodes, or this repository's own `GridMap` message conversion. That is a code change bigger than a port (issue 184 material). (D) Patching `grid_map_ros` in CI to drop `nav2_msgs`: a private fork in a workflow, and fragile. No `grid_map` commit avoids the dependency, so a different pin is not an option. The facts support (A) or (B); (C) and (D) are out of a port's scope. Resolution: add the hold as an open question with these options; make commit 8 conditional on the owner's answer, with (B) as the default if he has not answered.
2. **[Consequences — tile comparison (a)] (needs work)**: The method is sound in outline. The input is deterministic: `write_plane_bag` is closed-form, with no randomness or wall clock (`test_import_bag_slope.py:183-206`). The store libraries are code-identical on the two upstream branches: `git diff origin/jazzy origin/rolling` (`c4bc9ed7`..`ec4fd5d7`) touches none of the `marine_*` store, index, backscatter or vertical-datum packages, and `marine_interfaces/msg` is identical. Any difference therefore comes from the platform (GCC 13 against 15, glibc 2.39 against 2.43 libm, GDAL 3.8 against 3.12, the tf2 and rosbag2 versions), not from the port, and the plan should say so. Four gaps remain. (i) There is no criterion fixed before running. "Equal value for value … maximum absolute difference reported if not" leaves the verdict to after the fact. State in advance what passes: equal file lists and NaN masks, and every depth cell equal or within a stated tolerance (for example, one float32 ulp, or a stated number of millimetres). Report the count of differing cells as well as the maximum, because CUBE's hypothesis choice is discontinuous and a last-bit input difference can move a cell by a hypothesis separation. (ii) Say which side writes the bags. Prefer Jazzy writing and Rolling reading, the old-writer, new-reader direction. Both sides write rosbag2 metadata version 9 (checked), so either direction probably reads. (iii) Say how the script runs on deadpool without this session: `write_plane_bag` sits in a pytest module that imports `test_derive_angular_response` by path. The script needs that source tree and the Jazzy workspace sourced, and its command lines should be copyable as written. (iv) The "before" ref `f3ca1e3` is fine (`cube_bathymetry/` is unchanged on `jazzy` since then), but name the `jazzy` head the owner will build (`0b70b46` today) so his build matches the record.
3. **[CI shape] (minor)**: The step list leaves out what the models and the ccache recommendation need. `colcon test` needs `--event-handlers console_direct+ --return-code-on-test-failure` (mru_transform and upstream), and `colcon test-result --verbose` needs `if: always()`. ccache needs `apt-get install ccache` in the container, `CCACHE_DIR` under `$GITHUB_WORKSPACE` so `actions/cache` can save it, and the C launcher as well as the C++ one. Resolution: list these in commit 8's description.
4. **[Consequences — commit 6] (minor)**: Commit 6 is behaviour-neutral for the tiles (point check 3), but it does change the cube node's parameter set on Rolling: `qos_overrides./tf.subscription.*` and `qos_overrides./tf_static.subscription.*` move from the hidden node onto `/cube_bathymetry`, as on Jazzy. Resolution: say so in the commit message and in the validation paragraph, so that "the one change to running code" is stated in full.
5. **[Validation — (b) live comparison] (minor)**: (b) compares two runs that are not deterministic, and it has no baseline for run-to-run noise. Resolution: if (b) is run, first play the same bag twice into the commit-6 node and report that spread; judge commit 5 against commit 6 only against it.
6. **[Accuracy] (minor)**: The plan has three small factual slips; no conclusion changes. `detections_to_pointcloud` is a `LifecycleNode` (point check 2), not a plain `rclcpp::Node`. Jazzy also has the `static_only` self-node constructor. `marine_autonomy/tf2_utils.h:50` is not a helper: it is `Transformations`' constructor calling `make_shared<TransformListener>(*tf_buffer_, node)` with a `shared_ptr`. Resolution: correct the TF-listener table and the issue-184 note.

### Summary

The code commits (1 to 7) are right. Every error they fix was confirmed by reading the sources and headers, each should build on Jazzy, and none can change importer output. Commit 8 builds Nav2 packages from source, which the owner has put on hold, and the plan must put that to him as a decision. The tile comparison needs a pass criterion set before it runs, a stated bag-writing side, and command lines the owner can run on deadpool as written.

### Recommended Actions

- [ ] Add the Nav2-source hold as an open question with options (A) to (D) and their costs; make commit 8 conditional on the owner, with (B) as the default (finding 1).
- [ ] Tile comparison (a): state the pass criterion and the cell-count report in advance; name the bag-writing side; give runnable deadpool command lines; name the `jazzy` head; say that a difference measures the platform, not the port (finding 2).
- [ ] Commit 8: add `--return-code-on-test-failure`, `if: always()` on the result step, and the ccache install, directory and C launcher (finding 3).
- [ ] Commit 6: name the `qos_overrides` parameter move (finding 4).
- [ ] (b), if run: measure the run-to-run spread first (finding 5).
- [ ] Correct the three factual slips (finding 6).

## Checkpoint
**Status**: complete
**When**: 2026-11-23 13:54 -05:00
**By**: Claude Code Agent (claude-fable-5-2)
**Decided-by**: host (the plan review's verdict is needs-work; the one finding that was the owner's has his answer, quoted below; the rest are corrections the planner makes)
**After**: plan
**Decision**: revise

Read: the Plan Review at `33371b1`, verdict needs-work, six findings. Finding 1 went to the owner: the Rolling CI job cannot be built without `nav2_msgs` and `nav2_common` from navigation2 source, and he had said about Nav2 in CI "Let me think about it more before we do anything else about it." Asked on 2026-11-23 with options A (build the two packages), B and C (change the code), **his answer: "B: no Rolling job yet (Recommended)"**: Part 2's code merges on the Jazzy job plus recorded hand runs on Rolling; the Rolling job waits for his Nav2-in-CI decision or a Nav2 Rolling release and is tracked in rolker/cube_bathymetry#184. So commit 8 (the Rolling job) and its "Rolling job green" in Done-when leave the plan, and the hand runs on Rolling that replace it are named. Findings 2 to 6 are taken as written: the tile comparison gets a pass rule fixed before the run (file lists, NaN masks, a stated tolerance, the count of differing cells as well as the maximum), names which side writes the bags (Jazzy writes, Rolling reads) and the `jazzy` head the owner builds (`0b70b46`), states that it measures the platform and not the port, and comes as a script the owner can run on his Jazzy machine without this session; commit 6's message and the validation paragraph say that the `qos_overrides./tf*` parameters move onto the cube node on Rolling, as on Jazzy; comparison (b) needs a run-to-run noise baseline if it is run at all; the three factual slips are corrected. Finding 3 (CI step details) falls away with commit 8 and is recorded for when the job is written.

## Plan Authored
**Status**: complete
**When**: 2026-11-23 13:56 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Dispatch**: resumed (agent a4b153bf5e594aea4, resume 1 of 3)
**Plan**: `.agent/work-plans/issue-182/plan.md` at `44605af`

Revised after the Plan Review at `33371b1` and the owner's answer "B: no Rolling job yet". Changes: the Rolling CI job (commit 8) is removed and kept as a deferred section for issue 184 (Nav2 reason, shape, pins, review finding 3's step details, estimate). Hand runs on Rolling replace it and are recorded in `progress.md`: a clean build, the full test run with result lines, the repeats, `rosdep install --simulate` without `-r` over both packages plus `rosdep resolve` on resolute and noble, and the two cause-14 mutations. The tile comparison now measures the platform, not the port. Jazzy at `0b70b46` writes the bags and Rolling reads them. Pass rule fixed in advance: equal file lists and NaN masks, every finite cell within 1 mm, with the counts and the largest difference always reported. `tile_compare.sh` is planned to run on deadpool on its own; the plan states what is claimed if the owner's side has not run. Commit 6 names the `qos_overrides./tf*` parameter move. Comparison (b) is dropped. The three factual slips are corrected. Seven code commits, all backported. No new build was run.

## Plan Review
**Status**: complete
**When**: 2026-11-23 14:00 -05:00
**By**: Claude Code Agent (claude-opus-5-5)
**Dispatch**: resumed (agent a99d83b4d294af6cc, resume 1 of 3)
**Verdict**: ready

**Issue**: #182 — Build on ROS 2 Rolling: port cube_bathymetry and coverage_assessment, add a rolling branch
**Plan**: `.agent/work-plans/issue-182/plan.md` at `44605af`
**Branch**: `feature/issue-182`

This re-review covers the six findings of the Plan Review at `33371b1` and the revision `git diff 33371b1 44605af`. It is not a full review of the plan again. I read the Checkpoint (the owner's answer "B: no Rolling job yet (Recommended)") and the new Plan Authored entry. I checked these sources: the `tile_io.cpp` of `marine_bathymetry_store` and of `marine_mbes_backscatter_store`, `test_import_bag_slope.py`, and the `add_executable` lines of `cube_bathymetry/CMakeLists.txt`. I ran one command here, hand run 4's `rosdep install --simulate` (with `env.sh` sourced, then without it), and ran `rosdep resolve` on the new ROS keys for rolling and for jazzy/noble. Nothing was built.

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Seven code commits plus records. The Rolling job is a "Deferred" section for issue 184. |
| Issue alignment | Good | The plan follows the owner's answer B. Nothing in the plan still requires or implies a Rolling job: Done when, Open Questions, the principle table, Files to Change and the estimated scope were all checked. **Serves**: "none: this repository has no written goals or design document (`AGENTS.md` names none); the port serves rolker/unh_marine_autonomy#415." Accepted. |
| File targeting | Good | `ci.yml` is out of the plan. `tile_compare.sh` goes in the records. |
| Consequences | Good | The navigation-path statement is kept. The tile comparison now has a pass rule set in advance, a direction for the bags, a script the owner can run on deadpool, and a fallback claim (findings 1 to 3 below are refinements). Commit 6's `qos_overrides` change is named. |
| Principle alignment | Good | The hand runs replace the job and are recorded with their result lines. Comparison (b) is dropped with its reason. |
| ADR compliance | Good | ADR-0013 is followed. No other ADR is triggered. |
| ROS conventions | Good | Unchanged from the last review. |

### Earlier findings

1. **Nav2 hold**: settled by the owner (B). No part of the plan still assumes a Rolling job: "Rolling job green" is gone from Done when, the Open Questions no longer ask about one, commit 7's reasoning no longer depends on commit 8, and the pull request text the plan prescribes promises no Rolling job. Hand runs 1 to 5 cover what the job would have checked: a clean build, both packages' tests, repeats, keys and mutations. On keys, see finding 4: hand run 4 is a real check of the system keys on resolute. Noble is covered by the Jazzy job: `industrial_ci` installs the `rolling` branch's manifest for real on noble with no `-r`, its skip keys being only `marine_nav_*` and `marine_ais_msgs`. What the job would have added and the hand runs do not: a clean container, so no undeclared dependency hidden by this machine's installs, and a check on every push.
2. **Tile comparison**: taken. The 1 mm rule is sound for depth and uncertainty. Float64 rounding differences are around 1e-12 m, and even a long run of CUBE updates keeps them many orders of magnitude below 1 mm. A hypothesis switch moves a cell by the distance between hypotheses, which is centimetres or more. Jazzy writes the bags and Rolling reads them, both at metadata version 9. `0b70b46` is named. The fallback claim is honest: it says the platform question is "not shown" and owed before Rolling runs on a boat with `bathymetry_layer`. Refinements are in findings 1 to 3.
3. **CI step details**: kept in the deferred section, every item included, the C launcher too.
4. **`qos_overrides`**: in commit 6's message and in the validation paragraph.
5. **(b)**: dropped. The reason holds: without a noise baseline it could only ever show "within the noise", and the named test and its two mutations are the stronger evidence for commit 6.
6. **Factual slips**: all three corrected. `DetectionsToPointCloud` is a `LifecycleNode` (`:37`), Jazzy's `static_only` constructor is now listed, and `tf2_utils.h:50` is described as `Transformations`' constructor.

### Findings

1. **[Consequences — tile comparison: what is compared] (minor)**: "Tiles are written by `marine_bathymetry_store` as Float64" is true of the depth store only: 2 bands, depth and uncertainty (`marine_bathymetry_store/src/tile_io.cpp`, `saveTile<double>`). The runs taken from `test_import_bag_slope.py` also write the beam and slope backscatter stores. Those are 3-band Float32 tiles in dB (`marine_mbes_backscatter_store/src/tile_io.cpp`, `float` nodata), so a rule written in metres does not apply to them. Also, `batch_regen_bag` appears in that test only in refusal cases (`:637-640`, `:846-852`), so "the same `import_bag` and `batch_regen_bag` commands" produces no regenerated tiles. The depth layers the costmap reads are still covered, because `import_bag` writes the store through `store_import`. Resolution: limit the pass rule to the depth store, or state a tolerance in dB for the backscatter stores (which are not on the navigation path, so they could be reported only). Either define a `batch_regen_bag` run that writes tiles or leave the tool out.
2. **[Consequences — reading a difference beyond 1 mm] (minor)**: The plan hands any cell beyond 1 mm to the owner, "who decides whether it is a hypothesis switch to accept". A result between the two extremes need not be a switch. Many cells off by millimetres to centimetres, varying smoothly across the survey, would mean a computation that differs: a tf2 interpolation, a beam projection, or the uncertainty model. That must be explained, not accepted as a switch. Resolution: have the report show the pattern as well as the counts (how many tiles are affected, a histogram of the differences, and whether the cells beyond 1 mm are isolated or contiguous), and say in the plan that a widespread or smooth difference is "a different computation, to be explained".
3. **[Consequences — fallback record] (minor)**: The fallback claim is honest, but the owed item lives only in the records and in `progress.md`. Resolution: put "Rolling platform tile comparison owed before Rolling runs on a boat with `bathymetry_layer`" into the issue-184 comment the plan drafts, so it outlives the pull request. Also make the dry run on Rolling run twice and compare the `sha256sum` lists, so that "byte-deterministic run to run" is measured, not assumed.
4. **[Hand run 4 — what `--simulate` checks] (minor)**: I ran the command here with `env.sh` sourced: it prints nothing (nothing to install) and exits 0. `--ignore-src` counts every package on `AMENT_PREFIX_PATH` as present, `/opt/ros/rolling` included. So the check resolves the system keys (`libsqlite3-dev`, `zlib`, GDAL and the others) on resolute, but for the ROS-package keys it shows only that a package of that name is installed, not that a rosdep rule exists. Without `env.sh` it fails on the source-only keys (`marine_survey_index`, `marine_interfaces`), as expected. With `rosdep resolve --rosdistro rolling`, the new ROS keys `tf2_msgs`, `rcl_interfaces`, `std_msgs`, `builtin_interfaces`, `rcutils`, `launch` and `launch_ros` all resolve to `ros-rolling-*`, and all resolve for jazzy/noble. `grid_map_msgs` has no Rolling rule, which is expected: it is built from source. Resolution: add that `rosdep resolve` of the new ROS keys to hand run 4, naming `grid_map_msgs` as source-only on Rolling.

### Summary

The revision handles all six findings. The owner's answer B runs through the whole plan, finding 3 is kept for the deferred job, and the tile comparison is now a real test with a rule set in advance and an honest fallback. Four small refinements remain: which stores the rule covers and what `batch_regen_bag` contributes, how to read a difference beyond 1 mm that is not a switch, a durable record of the owed comparison, and a `rosdep resolve` of the new ROS keys. None blocks implementation.

### Recommended Actions

- [ ] Limit the 1 mm rule to the depth store, or give the backscatter stores their own tolerance in dB; define a `batch_regen_bag` run that writes tiles, or drop the tool (finding 1).
- [ ] Have the report show the pattern of differences beyond 1 mm, and read a widespread or smooth difference as a computation to explain (finding 2).
- [ ] Carry the owed comparison into the issue-184 comment; run the Rolling dry run twice and compare checksums (finding 3).
- [ ] Hand run 4: add `rosdep resolve --rosdistro rolling` of the new ROS keys (finding 4).
