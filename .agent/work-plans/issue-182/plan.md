# Plan: Build on ROS 2 Rolling: port cube_bathymetry and coverage_assessment, add a rolling branch

## Issue

https://github.com/rolker/cube_bathymetry/issues/182 (tracking issue: rolker/unh_marine_autonomy#415, causes 1 to 13 in its comments; cause 14 below was sent by the host during planning and is not yet on #415).

## Context

Planned from a build, not from text search. On this machine (Ubuntu 26.04, GCC 15, GDAL 3.12, Rolling underlay with `grid_map` 2.5.0 at 0ece1b5 and `geodesy`, hosted core install built from rolker/unh_marine_autonomy `rolling`), everything the two packages need is installed; nothing is missing. All figures below are from a scratch copy outside the repository with the planned fixes applied (deleted at the end of planning).

Baseline at `f3ca1e3` (`rm -rf build install log; ./build.sh --continue-on-error`): 2.4 s, 0 of 2 packages built.
- `cube_bathymetry` fails in CMake: `CMakeLists.txt:248 (ament_target_dependencies): Unknown CMake command "ament_target_dependencies"` (cause 1).
- `coverage_assessment` fails the same way at `CMakeLists.txt:54` when built alone (`--packages-select`); in the whole-repo build it is "not processed", because it has `<test_depend>cube_bathymetry</test_depend>` (`package.xml:40`) so colcon orders it after cube and skips it when cube fails.

What the scratch copy showed once past each first diagnostic (one build pass shows only the first error of a package, so these were found by `make -k`):
1. cause 1, two calls (`cube_bathymetry/CMakeLists.txt:248`, `coverage_assessment/CMakeLists.txt:54`): `target_link_libraries(<t> grid_map_core::grid_map_core)`. The target is exported by `grid_map_core`; the Eigen plugin definitions come from the package's `grid_map_core-extras.cmake` as directory-scope `add_definitions` at `find_package` time, so nothing else is needed. 
2. cause 2, 11 files (list in Part 2 and Part 1): `tf2/*.h` to `tf2/*.hpp`.
3. `message_filters/subscriber.h` and `tf2_ros/message_filter.h` (`cube_bathymetry_node.hpp:61-62`): nothing uses them. Removing `message_filter.h` takes `tf2_ros::Buffer` out of the node header (it came in through it), so the build then fails with `'Buffer' is not a member of 'tf2_ros'` at `cube_bathymetry_node.hpp:519`; fix: `#include "tf2_ros/buffer.hpp"` at the place line 45 sits. Nothing else needs the removed includes.
4. NEW (A), `bag_to_geotiff`: `tf2_sensor_msgs/tf2_sensor_msgs.hpp:45: fatal error: Eigen/Eigen: No such file or directory`. The target includes `tf2_sensor_msgs.hpp` (`bag_to_geotiff.cpp:38`) but links nothing that carries Eigen; it only built before through `ament_target_dependencies`-era transitive include dirs. Fix: add `tf2_sensor_msgs::tf2_sensor_msgs` to its `target_link_libraries` (`CMakeLists.txt:395-404`); the same target is already linked by `cube_bathymetry_node_link` (line 342), so it exists on Jazzy.
5. NEW (B), both `InertWaitable` test classes (`test_node_lifecycle.cpp:2166`, `test_coverage_assessment_node.cpp:1921`): `invalid new-expression of abstract class type 'InertWaitable'`. Rolling's `rclcpp::Waitable` makes pure `add_to_wait_set(rcl_wait_set_t&)`, `is_ready(const rcl_wait_set_t&)`, `take_data_by_entity_id`, `set_on_ready_callback`, `clear_on_ready_callback` and a new `get_timers() const`. Fix: override the five with empty bodies (`override` is valid on Jazzy: its `waitable.hpp` has all five as virtual with defaults, read on the `jazzy` branch) and add `get_timers()` without `override` (no base on Jazzy).
6. NEW (C), `coverage_assessment` test: `StaticTransformBroadcaster(io_)` and `TransformBroadcaster(io_)` with `io_` a `shared_ptr<Node>` no longer compile (`has no member named get_node_parameters_interface`). `*io_` builds on Rolling; on Jazzy the constructor is `template<class NodeT> ...(NodeT && node)` calling `get_node_parameters_interface(node)`, which accepts a node reference (rclcpp's `test_does_not_compile` set covers only the const forms), read on the `jazzy` branch. Not compiled on Jazzy here.
7. CAUSE 14, found by the first test run (below), see its own section.
C++ standard: C++17 is enough. `CMAKE_CXX_STANDARD` stays 17 as the default; targets that link `rclcpp` compile as `gnu++20` anyway (72 of 75 compile commands in `compile_commands.json`: Rolling's `rclcpp` exports `cxx_std_20`), the 3 that do not link it stay `gnu++17`. It builds and passes with that mix, so no change of standard; on Jazzy the same CMake gives C++17 throughout (Jazzy's `rclcpp` exports no standard), unchanged from today. Worth a line in the follow-up issue (mixed standards in one process), not a change here.

Cold full build on this machine (20 cores): `cube_bathymetry` + `coverage_assessment`, tests compiled: 1 min 33 s (cube alone 39 s; `coverage_assessment` alone 46 s). First full `colcon test`: 5 min 34 s (53 + 15 CTest tests; CTest wall 256 s and 78 s; slowest: `test_import_bag_slope` 90 s, `test_coverage_assessment_node` 62 s, `test_reset_cube_nodes` 38 s, `test_import_eviction` 26 s, `test_mixed_level_import` 21 s, the launch test 13 s). 2138 tests, 150 skipped (the real-bag smoke test skips without its excerpt, by design, as `.agents/ci_local_extra.sh` says).

### Cause 14 (from the host): `TransformListener(buffer, this)` silently makes its own node on Rolling

Rolling's constructors are `(BufferCore&, bool spin_thread = true, bool static_only = false)` and `(BufferCore&, RequiredInterfaces, bool spin_thread = true, ...)` (`transform_listener.hpp:97-130`). Both call sites here pass a raw `this` (`cube_bathymetry_node.hpp:522`, `coverage_assessment_node.hpp:548`), which converts to `bool`. Observed:
- First test run: `NodeLifecycle.AllCallbacksShareTheDefaultGroup` and `NodeTest.CallbackGroupsHoldNothingButTfAndClockOutsideTheExecutor` failed (`audit.unspun_topics.count("/tf")` 0, wanted 1; a debug print showed `unspun_groups` 0 with a wall clock): the node holds no `/tf` subscription at all. These two tests are what catches it today.
- A probe (`TransformListener(buf, node.get())` against `(buf, *node)`, then `get_node_names()`): the pointer form adds `/transform_listener_impl_5c155cb7bb40`; the reference form adds nothing. So on Rolling the node ran with an extra node and an extra spin thread delivering TF, outside the node's callback groups; the node's `qos_overrides./tf.*` parameters did not govern the TF subscription. TF data still flowed (the buffer was the node's), so no wrong result was seen, but the node's design (the audit tests assert exactly this) was broken.
- `use_sim_time`: the `Buffer` takes this node's clock either way, so replayed bags were not affected in what the buffer stored; the listener node's own `use_sim_time` was unset. The bag-replay tools here (`import_bag`, `batch_regen`, `bag_to_geotiff`) do not construct a listener (they feed `tf2::BufferCore`/`Buffer` from the bag), and `detections_to_pointcloud.cpp:115` calls `TransformListener(*tf_buffer_)` deliberately with the self-node form; none is affected.
- Jazzy: `*this` does NOT compile there (Jazzy's template dereferences with `node->get_node_base_interface()`, read on the `jazzy` branch), so the fix needs a switch. Planned helper, one small header per package (`src/make_transform_listener.hpp`, identical text, in each package, because the two packages do not depend on each other):
  `template<class T, class = void> struct takes_node_interfaces : std::false_type {};` specialised on `std::void_t<typename T::RequiredInterfaces>`; `makeTransformListener(tf2::BufferCore&, NodeT& node)` returns `make_shared<TransformListener>(buffer, node)` under `if constexpr (takes_node_interfaces<tf2_ros::TransformListener>::value)` and `(buffer, &node)` otherwise. Built and run on Rolling in a probe: no hidden node. Jazzy's `TransformListener` has no `RequiredInterfaces` (read), so it takes the pointer branch, which is today's call.
- The same kind of change in the broadcasters: the cube node has none; `coverage_assessment`'s test uses both and is item 6 above (reference form on both distros, no helper needed).
- Test for each package: one test that the node's own callback groups hold the `/tf` and `/tf_static` subscriptions after `configure()` (the existing `auditGroups` helper, `test_node_lifecycle.cpp:2087`, and the coverage twin), named for the cause so a regression says what happened. Mutation check, both done by running: `.get()`/`this` makes the audit fail (that is the baseline failure above); the planned commit repeats it once more on the final tree and records the result in the commit message.

## Approach

### The cut into three parts (each `Part of #182`, into `rolling`, backported to `jazzy` by hand after merge; the loop raises a checkpoint after each)

The review's cut stands, with one fact from the build that the owner should know: `coverage_assessment/CMakeLists.txt:142-168` builds `test_frame_chain` only if `cube_bathymetry` is installed, and otherwise registers it as a test that always fails (`cmake -E false`, by design, "never skip silently"). So after Part 1 the package builds and 14 of 15 CTest tests pass on Rolling alone (measured: build 46 s, test run 2 min 5 s, 230 gtests, 20 skipped); `test_frame_chain` goes green with Part 2. The Jazzy CI job, which builds both, stays green throughout. The other order (`cube_bathymetry` first) leaves `coverage_assessment` not built after the part that is the larger review; I recommend the review's order: Part 1 is small and proves the CI wiring and the cause-14 pattern before the big one. If the owner prefers a whole-repo-green `rolling` after Part 1, swap Parts 1 and 2 (Part 2's list does not depend on Part 1 except the CI commits, which would move to the first part).

- **Part 1: CI plumbing and `coverage_assessment`.** Detailed below.
- **Part 2: `cube_bathymetry` build and test-compile fixes, cause 14 for the node, cause 11 manifest.** Outline below; settled when it starts.
- **Part 3: repeated runs and what they find, the records, and the Rolling CI job if the owner picks one.** Likely small (see "Repeat runs"); fold into Part 2 if nothing is found and no Rolling job is chosen.

### Part 1 in detail (branch from `rolling`; commit order = list order)

Every commit message names its #415 cause (or "new cause" with the diagnostic). B = for the hand backport to `jazzy`, in this order; NB = not backported.

1. **CI: run the existing Jazzy job for `rolling`** (B, cherry-picks clean: the file is identical afterwards on both branches). `.github/workflows/ci.yml:3-7`: `branches: [jazzy]` becomes `branches: [jazzy, rolling]` for both `push` and `pull_request`, and a comment says the job is the Jazzy build and runs on both branches. Nothing else changes: job name `ROS 2 Jazzy (industrial_ci)` stays (a rename would orphan a required-check name), `DOCKER_IMAGE` stays `...cube_bathymetry-ci:jazzy`, ccache keys stay (`ccache-jazzy-${{ github.sha }}`, restore `ccache-jazzy-`; GitHub scopes caches to the branch and its base, and ccache is content-keyed, so a shared prefix is safe). `ci-image.yml` is not touched: see "CI layer (a)".
2. **CI: `upstream.repos` on `rolling` pins the upstream `rolling` branches** (NB, `rolling` only). `upstream.repos:7,12`: `unh_marine_autonomy` `version: rolling`. `mru_transform` stays `jazzy` (neither package depends on it, and its `rolling` is identical to `jazzy` today: `compare` shows 0 ahead, 0 behind; flag as an open question). `geographic_info` stays `ros2` (no `rolling` branch; head contains f70b81a, the floor `geodesy` needs).
3. **cause 1: `coverage_assessment`** (B). `coverage_assessment/CMakeLists.txt:52-54`: `ament_target_dependencies(coverage_assessment_core grid_map_core)` becomes `target_link_libraries(coverage_assessment_core grid_map_core::grid_map_core)`; the two comment lines above it (52-53) say the Eigen plugin definitions come from `grid_map_core`'s extras file at `find_package` time and the target carries the include path. Check: `ament_export_dependencies(grid_map_core)` near the end of the file stays.
4. **cause 2: `coverage_assessment`** (B). `src/coverage_assessment_node.hpp:51-52`: `tf2/exceptions.h` and `tf2/time.h` become `.hpp` (both exist on Jazzy, read in `ros2/geometry2` `jazzy`). The `tf2_ros/buffer.h` and `transform_listener.h` includes at 54-55 stay (follow-up issue).
5. **new cause B: `InertWaitable`** (B). `test/test_coverage_assessment_node.cpp:1921-1925`: add the five overrides and `get_timers()` as in the context section (comment names why).
6. **new cause C: broadcasters** (B). `test/test_coverage_assessment_node.cpp:254-255`: `(io_)` becomes `(*io_)`.
7. **cause 14: `coverage_assessment`** (B). New `src/make_transform_listener.hpp` (helper above); `coverage_assessment_node.hpp:548` calls `makeTransformListener(*tf_buffer_, *this)` and includes it; add the named test next to `CallbackGroupsHoldNothingButTfAndClockOutsideTheExecutor`; `CMakeLists.txt` needs nothing (header in `src/`, already on the node target's include path).
8. **cause 11: `coverage_assessment/package.xml`** (B). Add `<depend>rcl_interfaces</depend>` (`coverage_assessment_node.hpp` includes `rcl_interfaces/msg/parameter_descriptor.hpp`) and `<test_depend>rcutils</test_depend>` (`test_coverage_assessment_node.cpp` includes `rcutils/logging.h`). Both are ROS packages, so they resolve on Jazzy and Rolling (`rosdep resolve` on `ubuntu:resolute`: `ros-rolling-rcl-interfaces`, `ros-rolling-rcutils`). `.github/ci/Dockerfile` copies this manifest only to bake keys; these two are already in the image through `rclcpp`, so no Dockerfile edit and no image rebuild is needed for correctness (the push to `jazzy` after backport rebuilds it anyway: `ci-image.yml` watches `coverage_assessment/package.xml`).
9. **Records** (NB): `.agent/work-plans/issue-182/` is carried in the pull request to `rolling` and not backported (settled).

Verification for Part 1 (all on Rolling here): `./build.sh --packages-select coverage_assessment` builds with 0 errors; `./test.sh` for it: 14 of 15 pass, `test_frame_chain` fails by design; `test_coverage_assessment_node` 50 times and the launch test 20 times (counts under "Done when"). Jazzy: the CI job on the pull request is the only Jazzy check there is; it cannot be run here.

### Part 2 outline (to be settled when it starts)

`cube_bathymetry` only. Commits, one cause each:
- cause 1: `cube_bathymetry/CMakeLists.txt:245-248` (same replacement and comment).
- cause 2: `include/cube_bathymetry/detections_projector.h:35-36` (`tf2/buffer_core.h`, `tf2/time.h`); `src/detections_projector.cpp:31` (`tf2/exceptions.h`); `src/import_bag_main.cpp:155` and `src/batch_regen_main.cpp:83` (`tf2/time.h`); tests `test_beam_geometry.cpp:32-34` (`Matrix3x3`, `Quaternion`, `buffer_core`) and `test_detections_projector.cpp:29` (`buffer_core`). All to `.hpp`.
- dead includes: delete `cube_bathymetry_node.hpp:61-62` and add `tf2_ros/buffer.hpp` (see context item 3). The build confirmed nothing else uses them.
- new cause A: `bag_to_geotiff` links `tf2_sensor_msgs::tf2_sensor_msgs`.
- new cause B: `test_node_lifecycle.cpp:2166` `InertWaitable`.
- cause 14: helper header in `cube_bathymetry/src/` and `cube_bathymetry_node.hpp:522`, with the named test.
- cause 11, `cube_bathymetry/package.xml` (table below).
- Verification: whole-repo `./build.sh` clean; `test_frame_chain` goes green; the whole test run.

### Part 3 outline

Repeat runs per "Done when" and their records in `progress.md`; a fix commit for any intermittent failure the owner decides is this port's; the Rolling CI job if chosen (option below); the draft of the follow-up issue.

## Files to Change

| File | Change |
|------|--------|
| `.github/workflows/ci.yml` | Part 1: triggers `[jazzy, rolling]` |
| `upstream.repos` | Part 1, `rolling` only: `unh_marine_autonomy` at `rolling` |
| `coverage_assessment/CMakeLists.txt`, `src/coverage_assessment_node.hpp`, `test/test_coverage_assessment_node.cpp`, `package.xml`, new `src/make_transform_listener.hpp` | Part 1 as listed |
| `cube_bathymetry/CMakeLists.txt`, `package.xml`, 8 source/test files for causes 2 and 14, `test_node_lifecycle.cpp`, new `src/make_transform_listener.hpp` | Part 2 |
| `.github/ci/Dockerfile`, `ci-image.yml` | not touched in Parts 1 and 2 (see CI layer (a)); touched only by a Rolling option (iii) |

## CI layer (a): the existing Jazzy job on `rolling` (planned, Part 1)

- Triggers: item 1. Today a `rolling` pull request gets no CI at all.
- Image tag: the job keeps pulling `cube_bathymetry-ci:jazzy`. The image bakes system deps from the manifests and `upstream.repos` of the branch that built it (`Dockerfile` `COPY`s them); `industrial_ci` still runs `rosdep install` at job time and clones and builds the upstream sources every run, so a key added on `rolling` (here `libsqlite3-dev`, `zlib`, in Part 2) is installed at job time and a stale image costs a few apt seconds, not a failure (the Dockerfile header says this is the design). A second tag (`jazzy-on-rolling`) would need `ci-image.yml` to build from `rolling` too, and the weekly schedule runs only from the default branch (`jazzy`), so that image would go stale and the first pull request would find no image at all (the image is built on a push, and the pull request that adds the tag is the first run). Two branches pushing one tag would flap. So: one tag, `ci-image.yml` unchanged, and the weekly build does not need to know about `rolling`.
- `upstream.repos` (commit 2): which upstream is right for a Jazzy job on `rolling`? Upstream `jazzy` branches check "the rolling branch against what Jazzy users run today"; upstream `rolling` checks "the pair the owner will run on Rolling, built with Jazzy's compilers and Jazzy's ROS". The second is the question the job exists to answer (does the Rolling-first code still build on Jazzy, before it is backported), and upstream `rolling` is written to build on Jazzy and its own CI proves it (its `jazzy` job runs from the `rolling` branch). The first would fail the day `rolling` of this repository uses something only upstream `rolling` has, with no fault in this repository. The risk of the second: upstream `rolling` is 113 commits ahead of and 40 behind upstream `jazzy`, and a red job could come from upstream; the first red run is the check, and the fallback is pinning `jazzy` (a one-line change). Not verifiable here (no Docker): `COLCON_IGNORE` list in `ci.yml` (`mission_manager`, `mission_manager_interfaces`, `marine_autonomy_integration_tests`, `marine_web_view`) names directories that still exist in upstream `rolling` (checked in the local clone), `ROSDEP_SKIP_KEYS` unchanged.
- The `upstream.repos` commit is the one `rolling`-only file difference; `ci.yml` and `ci-image.yml` are identical on both branches. A later commit touching `upstream.repos` will conflict on a cherry-pick; resolve by keeping `jazzy`'s pins.
- Measured, not planned: the current Jazzy job takes 10 to 17 min per run (last 12 runs, `gh run list`); the image build about 4 min.

## CI layer (b): Rolling job options (owner decision, not chosen here)

Measured locally (20 cores): `grid_map` closure not timed; the seven upstream `marine_*` packages the repository needs build in about 60 s of summed package time; this repository cold 1 min 33 s; tests 5 min 34 s (mostly serial, so a 4-vCPU runner is about as slow on tests and 4 to 5 times slower on compiling). Runner figures below are estimates from those.

| | (i) none | (ii) plain-container job, as in unh_marine_autonomy | (iii) `industrial_ci` Rolling leg |
|---|---|---|---|
| Builds | nothing in CI; Rolling checked by hand here and on deadpool; follow-up issue | `ros:rolling-ros-core` container; clones `grid_map` (`rolling`), `geographic_info` at f70b81a with `COLCON_IGNORE` on `geographic_msgs`/`geographic_info`, upstream `unh_marine_autonomy` `rolling` (and `marine_survey_index` needs `geodesy`); `rosdep install` without `-r` over `colcon list --packages-up-to` this repository | `industrial_ci` with `ROS_DISTRO: rolling` (industrial_ci master supports it and defaults to `resolute`, read in `src/ros.sh`), its own `UPSTREAM_WORKSPACE` file and its own dependency image |
| New files | none | one job in `ci.yml` (or `ci-rolling.yml`); no image | `upstream.rolling.repos` (grid_map, geographic_info, UMA `rolling`), `.github/ci/Dockerfile.rolling` from `ros:rolling-ros-base` (or `ubuntu:resolute`), a second image job and tag `rolling`, weekly cron entry (default-branch file only) |
| Minutes per run | 0 | estimated 22 to 28 cold (apt 2 to 3, grid_map 3 to 5, UMA 5 to 6, this repository 7 to 9, tests 6); 12 to 16 with ccache | estimated 18 to 24 cold without the image, 10 to 14 with the image and ccache; image build about 5 |
| Checks | nothing automated | the Rolling build and the test suite on every pull request; key declarations (`rosdep` without `-r` over only the closure shows cause-11 gaps, the check #415 recommends) | the same, with `industrial_ci`'s pipeline and its ament linters; a false "green" risk is smaller on keys only if the image is not over-baked |
| Does not check | everything on Rolling | the repeat-run counts (a pull-request job runs each test once); `grid_map` and geodesy are cloned branches or commits, so a moving `grid_map` `rolling` can turn it red | same, plus industrial_ci on Rolling is unproven here (not run) |
| Fit with `industrial_ci` | untouched | independent of the `industrial_ci` job, which stays the Jazzy check; no shared files | extends the existing setup; two images to keep in sync with manifests (AGENTS.md: workflow, image workflow, Dockerfile must move together) |

**Recommendation (mine, not a decision):** (ii). It is the form that already ran green in other repositories of this port (unh_marine_autonomy, marine_ais), needs no image or weekly job to keep in sync, and its `rosdep` without `-r` is the check that finds undeclared keys, which this repository has (cause 11). Add it in Part 3 or as its own commit once Part 2 has merged, so it has something to build. (i) is acceptable if the owner wants this issue to end at the port; (iii) is the most work for a check (ii) already gives.

## Cause 11: undeclared or wrong dependencies (read from sources, CMake and tests against `package.xml`)

| Package | Finding | Fix | Resolves on noble / resolute |
|---|---|---|---|
| `cube_bathymetry` | `<depend>sqlite3</depend>` (`package.xml:32`) is the command-line tool; code includes `<sqlite3.h>` (`include/cube_bathymetry/survey_index_query.h:25`), CMake uses `find_package(SQLite3)` | `libsqlite3-dev` (and fix the comment above it) | `libsqlite3-dev` / `libsqlite3-dev` (rosdep `debian` rule) |
| `cube_bathymetry` | `find_package(ZLIB REQUIRED)` (line 22), `<zlib.h>` in `import_bag_main.cpp`, no key | add `<depend>zlib</depend>` | `zlib1g-dev` / `zlib1g-dev` |
| `cube_bathymetry` | `launch/*.py` import `launch` and `launch_ros`, installed to `share/`, no `exec_depend` | `<exec_depend>launch</exec_depend>`, `<exec_depend>launch_ros</exec_depend>` | ROS packages, both distros |
| `cube_bathymetry` | sources include `tf2_msgs`, `grid_map_msgs`, `rcl_interfaces`, `std_msgs`, `builtin_interfaces` (public header `quantize_tile.h`), tests include `rcutils`; `tf2_msgs` declared only as `exec_depend`, the others not at all | `<depend>` for the first five (`tf2_msgs` moves from `exec_depend`), `<test_depend>rcutils</test_depend>`; manifest only (they arrive through `rclcpp`, `tf2_ros`, `grid_map_ros` today). `grid_map_msgs` has no rosdep rule on Rolling (source build), as `grid_map_core` and `grid_map_ros` already | ROS packages; `grid_map_msgs` source-built on Rolling |
| `cube_bathymetry` | `kdl/frames.hpp` (`ping_georeference.cpp:27`) comes through `tf2_geometry_msgs`; the key differs by distro (`orocos_kdl_vendor` on Jazzy, `liborocos-kdl-dev` on Rolling, read in each `tf2_geometry_msgs/package.xml`) | leave, as the direct key is not the same on the two distros; note in the follow-up issue | n/a |
| `cube_bathymetry` | `message_filters` included but undeclared | goes away with the dead includes | n/a |
| `cube_bathymetry` | OpenSSL, nlohmann_json, GDAL, Eigen, `python3-gdal`, numpy, pytest | already declared (`libssl-dev`, `nlohmann-json-dev` resolving to `nlohmann-json3-dev`, `libgdal-dev` as `build_depend` only, `eigen`, ...) | yes |
| `coverage_assessment` | `rcl_interfaces` (src and test), `rcutils` (test) undeclared | Part 1 commit 8 | ROS packages |

`.github/ci/Dockerfile` copies both manifests and bakes the resolvable keys, so a new system key (`libsqlite3-dev`, `zlib`) is baked at the next image build (after the backport reaches `jazzy`; `ci-image.yml` watches both `package.xml` files) and installed by CI-time `rosdep install` meanwhile. No Dockerfile text change is needed; the "must stay in sync" rule is met because the Dockerfile has no copy of the key list. `rosdep check --from-paths cube_bathymetry coverage_assessment --ignore-src --rosdistro rolling` on this machine reports every system dependency satisfied (it cannot see the undeclared ones; that is what the table is for).

## Left alone and tracked (one follow-up issue, drafted by the host)

- `tf2_ros/*.h` deprecation warnings (26 `#warning` lines per full build): `bag_to_geotiff.cpp:31`, `detections_to_pointcloud.cpp:34-35`, `cube_bathymetry_node.hpp:45`, `import_bag_main.cpp:156`, `batch_regen_main.cpp:84`, `coverage_assessment_node.hpp:54-55`, `test_coverage_assessment_node.cpp:67-68` (10 includes after the dead `message_filter.h` goes).
- `GDALAllRegister()`: one call, `bag_to_geotiff.cpp:622`, in `main` of the offline tool, once per run, single-threaded. Confirmed: no thread is started anywhere in `src/` or `include/` of either package (`std::thread` only in two tests that do not touch GDAL). Not this port's. Also there: the driver pointer `GetDriverByName("GTiff")` is not null-checked (line 623).
- Mixed C++ standard (`gnu++17` and `gnu++20` targets in one build), above.
- Build warnings that are not new causes and not mine to fix in a port: `-Wunused-result` x6 in `bag_to_geotiff.cpp:650-757`, `xy.h:50` `-Wparentheses`, `-Wrange-loop-construct` (`map_sheet.cpp:94,106`, `geo_map_sheet.cpp:215`), `-Wsign-compare` (`grid.cpp:86`, `node.cpp:388`), `-Wnarrowing` (`node.cpp:268`), `-Wunused-parameter` (`error_model.cpp:137,245`), `tmpnam` in tests (`test_angular_response_curve.cpp:42`), `test_derived_tiles.cpp:131`, `test_tile_eviction_rss.cpp:130`. Whether Jazzy's GCC 13 prints the same is not checked.
- `ament_cppcheck` skips every file on Rolling (as in #415, "cppcheck 2.19.0 performance issues"), so cppcheck results there are not checks; CMake 4 deprecation notes (`cmake_minimum_required` 3.14.4 here is fine); `CMP0167` FindBoost warning comes from `grid_map_cv`/`filters`, not this repository.
- `original_cube/` is a third-party tree in neither CMake file; untouched.
- Unused upstream pin: `mru_transform` is in `upstream.repos` but neither package depends on it.

## What this code is for, and what validation is owed

`cube_bathymetry` is the CUBE bathymetric estimator (Calder's algorithm) with the live node (`cube_bathymetry_node`) and offline importers (`import_bag`, `batch_regen`, `bag_to_geotiff`); `coverage_assessment` scores survey coverage from sonar grids for the operator. At sea they produce survey coverage and bathymetry products: the live grid, tiles for the operator display and the persisted draft/epoch tiles that feed products. Neither is in the vehicle's control or navigation path: they subscribe to sonar, odometry and TF and publish grids and tiles; nothing here commands motion (`AGENTS.md`: the live tile store is "a derived product for the live view, not the record"; the raw bags and offline processing are).

Behaviour-neutral argument by kind: include renames (`.h` to `.hpp`) and `ament_target_dependencies` to `target_link_libraries` change no code, and the build with the same sources producing the same tests is the evidence; the dead includes are unused by construction (the build is the proof); the `InertWaitable` and broadcaster changes are in tests only; the manifest changes change no build output. Cause 14 is the one change that alters what runs: it restores what the Jazzy source already did (listener on the node); behaviour on Rolling before it was the unintended one, and the audit tests prove the after. The estimator, the unit conventions and the eviction logic are untouched.

Validation the port does not provide: no run on a vessel or in simulation; no run of `cube_bathymetry_node` in a live graph with a real sonar driver; no comparison of CUBE output between Jazzy and Rolling on a real survey bag (the real-bag smoke test skips without its excerpt); no check on GDAL 3.8 (Jazzy) of anything beyond CI; no Jazzy run here at all (Ubuntu 26.04, no Docker): every Jazzy statement is from reading upstream `jazzy` branches. Owed before a Rolling deployment: a real-bag import on Rolling compared with the Jazzy product (the real-bag smoke excerpt, run where the archive is mounted, with `.agents/ci_local_extra.sh`).

## Jazzy facts read (no Jazzy is installed here)

On `ros2/geometry2` `jazzy`: `tf2/include/tf2/{buffer_core,time,exceptions,convert,utils}.hpp` and `LinearMath/{Matrix3x3,Quaternion,...}.hpp` exist; `tf2_ros/include/tf2_ros/{buffer,transform_listener,static_transform_broadcaster,transform_broadcaster}.hpp` exist; `TransformListener` template constructor takes `NodeT && node` and calls `node->get_node_*_interface()` (pointer-like only), no `RequiredInterfaces`; the broadcasters take `NodeT && node` through `rclcpp::node_interfaces::get_node_parameters_interface(node)`; `tf2_sensor_msgs` exports `tf2_sensor_msgs::tf2_sensor_msgs` linking `Eigen3::Eigen`. On `ros2/rclcpp` `jazzy`: `Waitable` has the five signatures as non-pure virtuals and no `get_timers`. On `ANYbotics/grid_map` `jazzy` (2.2.2) and `rolling` (2.5.0): `ament_export_targets(export_grid_map_core HAS_LIBRARY_TARGET)` and the same `grid_map_core-extras.cmake`, so `grid_map_core::grid_map_core` exists on both. In `ros/rosdistro`: `libsqlite3-dev`, `zlib`, `libssl-dev`, `nlohmann-json-dev`, `libboost-dev` are `debian` rules (apply to noble and resolute; checked with `rosdep resolve` on `ubuntu:resolute` and `ubuntu:noble`); `jazzy/distribution.yaml` lists `grid_map` and `geographic_info`. `ros-industrial/industrial_ci` master: `rolling` supported, default OS `resolute`.

## Backport (Part 1 to `jazzy`, by cherry-pick, in this order)

Commits 1, 3, 4, 5, 6, 7, 8 (not 2, not 9). Each is written to build on Jazzy; none has been compiled on Jazzy. Expected conflicts: none in the sources (the files are identical on `jazzy` and `rolling` at `f3ca1e3`). Commit 1 is identical text on both. A later `upstream.repos` edit conflicts (above). `Waitable` overrides (5) and the helper (7) are the two with real Jazzy risk; the `jazzy` CI run on the pull request into `rolling` tests them first.

## Done when (numbers)

- Part 1: `coverage_assessment` builds on Rolling with no error; its gtests pass 50 of 50 runs each as separate processes (`ctest --repeat until-fail:50`), `test_coverage_assessment_node` 50 of 50 and the launch test (`test_launch_lifecycle.py`) 20 of 20; the Jazzy job is green on the pull request; `test_frame_chain` is the one expected failure.
- Part 2: the whole repository builds; every `cube_bathymetry` gtest passes 50 of 50 as separate processes, except the nine whose single run takes over 15 s (`test_import_bag_slope`, `test_reset_cube_nodes`, `test_import_eviction`, `test_mixed_level_import`, `test_level_plan`, `test_batch_regen`, the three pytest-driven ones), which need 20 of 20; `test_node_lifecycle` 50 of 50; `test_frame_chain` green; Jazzy job green.
- Any intermittent failure found is recorded with name and count and the owner decides; it is not hidden and not fixed silently.

## Repeat runs (scratch copy with the planned fixes, this machine, 4 parallel `ctest --repeat until-fail:N`)

Result: no intermittent failure in the final tree. 52 CTest test names run as separate processes with `ctest --repeat until-fail:N`, 4 names in parallel, 0 failures in all of them:
- 50 of 50 each: 38 `cube_bathymetry` gtests (including `test_node_lifecycle`, 261 s) and 5 `coverage_assessment` gtests (including `test_frame_chain`).
- 20 of 20: `test_coverage_assessment_node` (62 s a run) and the launch test `test_launch_lifecycle.py` (13 s a run); plus 4 of 4 earlier standalone runs of the node test after the cause-14 fix.
- 10 of 10 only (affordable here, each run 9 to 97 s): `test_import_bag_slope` (974 s for the ten), `test_reset_cube_nodes`, `test_import_eviction`, `test_mixed_level_import`, `test_level_plan`, `test_batch_regen`, `test_import_bag_mode_table`, `test_derive_angular_response`, `test_batch_regen_gain`. The 20 of 20 the done-when asks for needs a second pass (about 25 min more); not run.
- Not repeated: the ament lint tests.
One unreproduced failure to report, not hide: the first full run of `test_coverage_assessment_node` after the cause-14 change failed 3 tests by their own 20 to 30 s waits (`StaleTideBoatTransformSkipsScoringToo`, `MapFrameMovingAfterTheCaptureWithholdsUntilReconfigured`, `MaxAreasRefusesANewIdButStillUpdatesAHeldOne`), while another build and a second agent's jobs were using the machine; the same test passed alone at once (234 ms), and passed 24 of 24 afterwards. Not shown to be this port's, not shown to be pre-existing (the pre-fix binary was not repeated); the three tests wait on TF and area messages with timeouts, so they are load sensitive. The owner decides; this plan leaves it open and records that Part 1's 50-run count is the check.
Not found, though #415 found two like it in another repository on Rolling (GDAL 3.12 and GCC 15): nothing here races on `GDALAllRegister`, and no test failed on the temp-file-on-interrupt kind of defect.

## Prior Art

rolker/unh_marine_autonomy `rolling` (`marine_sidescan_mosaic`, `marine_survey_index`: `target_link_libraries` with exported targets, `target_include_directories` plus `${geodesy_LIBRARIES}` for `geodesy`, `libsqlite3-dev`), its `docker-ros-core` workflow (Jazzy job plus Rolling job in one file, branch-conditional), and the marine_ais workflow for `geodesy` at f70b81a: taken, except the repository's own `industrial_ci` job is kept for Jazzy. Rejected: a second image tag (flapping and staleness above), bumping `CMAKE_CXX_STANDARD` (not needed).

## Serves

none: this repository has no written goals or design document (`AGENTS.md` names none); the port serves rolker/unh_marine_autonomy#415.

## Design Picture

No change to the design picture; no design document exists here, and the port changes no behaviour (cause 14 restores the intended one).

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Port only; deprecations, warnings and the standard are listed for one follow-up issue. Cause 11 additions are manifest-only. |
| A change includes its consequences | Manifests, CI and records move in the same part; the Dockerfile needs no text change and the plan says why. |
| Test what you claim | Counts in "Done when"; cause 14 gets a named test with a mutation check; the Jazzy claim is "written by reading, checked by the job". |
| Keep one current design | Workflow files identical on both branches; the only branch-specific file content is `upstream.repos`. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0013 (progress timeline) | Yes | This plan and its `## Plan Authored` entry. |
| Repository ADR 0001 (tile eviction) | No | No behavioural change to eviction. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `package.xml` keys | `.github/ci/Dockerfile` bake | Yes: reads manifests, no text change |
| CI triggers | image workflow | Yes: decided unchanged, reasons above |
| `upstream.repos` on `rolling` | Dockerfile `COPY upstream.repos` | Yes: image stays built from `jazzy` pins, CI time `vcs import` uses the branch's file |

## Open Questions

1. Cut order (coverage first, with `test_frame_chain` red alone) or `cube_bathymetry` first. Recommendation: coverage first.
2. Rolling job: (i), (ii) or (iii). Recommendation: (ii), after Part 2.
3. Upstream pins on `rolling` for the Jazzy job: upstream `rolling` (recommended) or `jazzy`.
4. `mru_transform` is unused here: drop it from `upstream.repos` (a follow-up) or keep.
5. Whether any intermittent failure found in the repeat runs is this port's concern (see Repeat runs).
6. Cause 14 on `#415`: the host adds it.

## Estimated Scope

Three pull requests into `rolling`: Part 1 about 8 commits, Part 2 about 10, Part 3 small or folded. Each backported by hand.
