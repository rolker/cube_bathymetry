# Plan: Build on ROS 2 Rolling: port cube_bathymetry and coverage_assessment, add a rolling branch

## Issue

https://github.com/rolker/cube_bathymetry/issues/182 (tracking issue rolker/unh_marine_autonomy#415, causes 1 to 16 in its comments; follow-up issue rolker/cube_bathymetry#184).

This revision refreshes the plan for **Part 2** (the `cube_bathymetry` package, then the plain-container Rolling CI job the owner chose). Part 1 is delivered and is recorded below in short.

## Part 1, delivered

Pull request 183, merged at `7d1d3cb` on `rolling`; backport to `jazzy` is pull request 185. It made the Jazzy `industrial_ci` job run for `rolling` (`ci.yml` triggers, identical on both branches), pinned upstream rolker/unh_marine_autonomy at `rolling` in `upstream.repos` (`rolling` only, never taken whole onto `jazzy`), restricted `ci-image.yml` publishing to `jazzy`, and ported `coverage_assessment`: cause 1, cause 2, cause 16 (`InertWaitable`), cause 14 extended (broadcasters from `*io_`), cause 14 (`src/make_transform_listener.hpp`: template on the listener type, `RequiredInterfaces` detection, `spin_thread = true` explicit, `static_assert` refusing a pointer; named test `NodeTest.TfListenerSubscribesThroughThisNodeOnItsOwnThreadNotAHiddenNode`, mutation-checked), cause 11 keys. Jazzy CI on its head: 53 + 15 CTest tests, 2143 gtests, 0 failures. `test_frame_chain` (coverage) stays red on Rolling until Part 2 installs `cube_bathymetry`.

## Context: Part 2 baseline (built and run 2026-11-23 on this machine)

Ubuntu 26.04, Rolling, GCC 15, GDAL 3.12; upstream from the hosted Rolling layers (`underlay`: `grid_map` 2.5.0 at `0ece1b5`, `nav2_msgs`, `geodesy`; `core`: rolker/unh_marine_autonomy `rolling` at `ec4fd5d7`). Everything the package needs is installed here.

**As the branch is** (`./build.sh`, branch at `d249536`): `cube_bathymetry` fails in CMake in 1.7 s, `CMakeLists.txt:248 Unknown CMake command "ament_target_dependencies"` (cause 1); `coverage_assessment` not processed (it orders after `cube_bathymetry` through its `test_depend`). One build pass shows only the first error, so the rest was found on a scratch copy outside the repository (deleted at the end) with `make -k`, fixing each layer to reach the next:

| # | Error (file:line) | Cause |
|---|---|---|
| E1 | `CMakeLists.txt:248` `ament_target_dependencies(cube_bathymetry_grid_projection grid_map_core)` | 1 |
| E2 | `tf2/buffer_core.h`, `tf2/time.h`, `tf2/exceptions.h`, `tf2/LinearMath/{Matrix3x3,Quaternion}.h` not found: `include/cube_bathymetry/detections_projector.h:35-36`, `src/detections_projector.cpp:31`, `src/import_bag_main.cpp:155`, `src/batch_regen_main.cpp:83`, `test/test_beam_geometry.cpp:32-34`, `test/test_detections_projector.cpp:29` (9 includes, 6 files) | 2 |
| E3 | `message_filters/subscriber.h` not found, `src/cube_bathymetry_node.hpp:62` (unused, as is `tf2_ros/message_filter.h` at :61). Deleting both takes `tf2_ros::Buffer` out of the header (`cube_bathymetry_node.hpp:521,1073` "'Buffer' is not a member of 'tf2_ros'"): it came in through `message_filter.h` | new to #415 (cause 2's kind, in `message_filters`) |
| E4 | `bag_to_geotiff`: `tf2_sensor_msgs/tf2_sensor_msgs.hpp:45: fatal error: Eigen/Eigen` (`bag_to_geotiff.cpp:38`; target at `CMakeLists.txt:389-404` links no Eigen carrier). On Rolling `${grid_map_ros_INCLUDE_DIRS}` (its `target_include_directories`) holds no Eigen path (printed); what carried it on Jazzy was not checked | new to #415 |
| E5 | `test/test_node_lifecycle.cpp:2166` `InertWaitable`: "invalid new-expression of abstract class type" | 16 |
| T1 | Builds once E1 to E5 are fixed; then **`NodeLifecycle.AllCallbacksShareTheDefaultGroup` fails** (`test_node_lifecycle.cpp:2142` `unspun_groups` 0, wanted >= 1; `:2154` `/tf` count 0, both clocks): `cube_bathymetry_node.hpp:522` `TransformListener(*tf_buffer_, this)` takes Rolling's `bool spin_thread` constructor and builds a hidden node | 14 |

No other error. Warnings in a full clean build of the package with E1 to E5 fixed (63 compile units): `-Wcpp` 7 (`tf2_ros/buffer.h` x4, `transform_listener.h` x3; issue 184 item 1), `-Wunused-result` 6 (`bag_to_geotiff.cpp:650,651,708,712,753,757`), `-Wparentheses` 4 (`xy.h:50`, one header in four units), `-Wrange-loop-construct` 3 (`map_sheet.cpp:94,106`, `geo_map_sheet.cpp:215`), `-Wsign-compare` 3 (`grid.cpp:86` x2, `node.cpp:388`), `-Wunused-parameter` 2 (`error_model.cpp:137,245`), `-Wnarrowing` 1 (`node.cpp:268`), `-Wmissing-field-initializers` 1 (`test_derived_tiles.cpp:131`), `-Wunused-but-set-variable` 1 (`test_tile_eviction_rss.cpp:130`), linker `tmpnam` 1 (`test_angular_response_curve.cpp:42`); CMake: `CMP0167` (FindBoost) from `filters`, upstream. None is a #415 cause; all but `-Wcpp` are source warnings GCC 15 gives, not Rolling changes (whether GCC 13 gives the same is not checked). Not this port's: listed for issue 184.

Not present: cause 3 (`CPL_CVSID`: no use in either package); cause 12 (`GDALAllRegister()` only at `bag_to_geotiff.cpp:622`, in the offline tool's `main`, once; no `std::thread`, `std::async` or multi-threaded executor in either package's `src/` or `include/`; the tiles are written through `marine_bathymetry_store`'s `tile_io`, which delegates to `marine_tiled_raster_store`, which registers once per process since `775f8b62`; the store's per-operation calls are in its GeoTIFF/S-102 import code, which this package does not call); causes 4 to 10, 13 (no `setup.py`, no `logger.warn`, no Nav2 code, no `geodesy`, no `package.xml` order fault: `xmllint` passes).

**Tests on Rolling with E1 to E5 and cause 14 fixed** (scratch copy, `ROS_DOMAIN_ID` 163 to 165, localhost): `cube_bathymetry` 53 of 53 CTest tests pass (the 52 others through `ctest -j4` in 112 s, `test_node_lifecycle` run directly since its CTest environment pins domain 87: 67 tests, 5 s); `coverage_assessment`'s `test_frame_chain` passes once `cube_bathymetry` is installed. Before the cause-14 fix: 66 of 67 in `test_node_lifecycle`, the one failure being T1. Slowest: `test_import_bag_slope` 103 s, `test_reset_cube_nodes` 43 s, `test_import_eviction` 29 s, `test_mixed_level_import` 23 s, `test_level_plan` 16 s, `test_batch_regen` 13 s, the three other pytests 9 to 10 s; all else under 5 s. `test_real_bag_smoke` skips (its excerpt is on `/mnt/nadata`, not mounted here).
Repeats as separate processes: `test_node_lifecycle` 20 of 20; the 34 other fast tests 10 of 10 each; the nine slow ones 3 of 3 each (`ctest -j4 --repeat until-fail:3`, 5 min 11 s). No intermittent failure seen. Cold build of both packages here: 1 min 42 s (2 workers, `-j8`).

### The TF listeners

| Where | Today | On Rolling | Thread meant | Port action |
|---|---|---|---|---|
| `src/cube_bathymetry_node.hpp:522` (live node, `CubeBathymetry : rclcpp_lifecycle::LifecycleNode`) | `make_shared<TransformListener>(*tf_buffer_, this)` | compiles; the pointer becomes `bool spin_thread`, a hidden `transform_listener_impl_*` node with its own thread holds `/tf`, `/tf_static` (T1 shows it) | its own thread, outside the executor: the node's comments say so (`cube_bathymetry_node.hpp:2470-2477`, "the tf2_ros::TransformListener's (/tf, /tf_static)" groups "run on their own threads"), and Jazzy's default was `spin_thread = true` | fix (commit 6) |
| `src/detections_to_pointcloud.cpp:115` (plain `rclcpp::Node`) | `make_shared<TransformListener>(*tf_buffer_)` | the self-node constructor, `(BufferCore &, bool spin_thread = true, bool static_only = false)`; Jazzy's is `(BufferCore &, bool spin_thread = true)` (read on geometry2 `jazzy`): a hidden node and own thread on **both** distros, as before | own thread (default) | none: not cause 14, nothing changes between distros; the hidden node (its own QoS overrides, no `use_sim_time`, the buffer uses this node's clock) is pre-existing; noted for issue 184 |
| `import_bag_main.cpp:2898`, `batch_regen_main.cpp:1150`, `bag_to_geotiff.cpp:382` (offline tools) | `tf2_ros::Buffer tfBuffer(clock, duration)`, filled from the bag | no listener; Rolling's `Buffer` default `RequiredInterfaces()` and Jazzy's default `NodeT()` both mean no node | none | none |
| broadcasters | none in `cube_bathymetry` (source or tests) | n/a | n/a | none |

Proof of the fix, as in Part 1: the mutation evidence is already in hand from the scratch copy. With the helper, `test_node_lifecycle` passes 67 of 67; with the helper's `spin_thread` set to `false` (relinked, new mtime), `AllCallbacksShareTheDefaultGroup` fails at `:2142` and `:2154`; with the raw `this` (the baseline) it fails at the same lines. The existing test catches both mutations but cannot tell them apart, and its message ("test setup: the TF listener's group was not found") does not name the cause. So commit 6 adds `NodeLifecycle.TfListenerSubscribesThroughThisNodeOnItsOwnThreadNotAHiddenNode`, the same body as Part 1's test (a vector of groups per topic, exactly one for `/tf` and `/tf_static`, not the default group, not auto-added, the same group for both), and repeats both mutations on the final tree (`ldd`/mtime shows the mutated binary ran), recording them in the commit message. Alternative: rely on the existing test (it fails for both); rejected for parity with Part 1 and a message that names cause 14.

**Where the helper lives: a copy per package**, `cube_bathymetry/src/make_transform_listener.hpp`, Part 1's text with namespace `cube_bathymetry` (built and passing in the scratch copy; `cpplint`, `uncrustify` pass). Alternatives: (a) `cube_bathymetry` installs it and `coverage_assessment` uses it: that makes `coverage_assessment` build-depend on `cube_bathymetry` (today a `test_depend` only), changes already merged and backported code, and adds an installed API; (b) one helper upstream in `marine_autonomy` (its `tf2_utils.h:50` already has a cause-14 `shared_ptr` helper): a cross-repository change. Both are bigger than a port should make. The duplicate (about 70 lines, private to each package's `src/`) goes to issue 184 with the suggestion to consolidate in `marine_autonomy` once the port is done.

## Approach: Part 2 commits (branch from `rolling`, one cause per commit, list order)

B = cherry-picked to `jazzy` by hand after merge; NB = not. `cube_bathymetry/` is identical on `jazzy` and `rolling` today (`git diff origin/jazzy origin/rolling -- cube_bathymetry` is empty), so every B commit should cherry-pick clean. Each message names its #415 cause or "new".

1. **cause 1** (B): `cube_bathymetry/CMakeLists.txt:245-248` becomes `target_link_libraries(cube_bathymetry_grid_projection grid_map_core::grid_map_core)` with Part 1's comment (definitions from `grid_map_core-extras.cmake`, include path from the target; the target exists in grid_map 2.2 and 2.5).
2. **cause 2** (B): the 9 includes of E2 to `.hpp` (exist on Jazzy, read on geometry2 `jazzy` in Part 1).
3. **new, unused includes** (B): delete `cube_bathymetry_node.hpp:61-62`, add `#include "tf2_ros/buffer.hpp"` beside line 45 (the `.hpp` name, so it adds no deprecation warning; exists on Jazzy). The other `tf2_ros/*.h` includes stay (issue 184).
4. **new, Eigen for `bag_to_geotiff`** (B): add `tf2_sensor_msgs::tf2_sensor_msgs` to its `target_link_libraries` (the header it includes is that package's; the target is already linked by `cube_bathymetry_node_link`, so it exists on Jazzy). Alternative `Eigen3::Eigen`: names the symptom, not the package the include belongs to.
5. **cause 16** (B): `test_node_lifecycle.cpp:2166`, Part 1's overrides and comment.
6. **cause 14** (B): the helper copy, `#include "make_transform_listener.hpp"` in the node header, `tf_listener_ = cube_bathymetry::makeTransformListener(*tf_buffer_, *this);`, the named test and the mutation record. Keeps TF on a dedicated thread; does not move it onto the executor. The comment at `cube_bathymetry_node.hpp:516-520` (#164, reset before replacing the buffer) stays true.
7. **cause 11** (B): `cube_bathymetry/package.xml`: `sqlite3` (the command-line tool) becomes `libsqlite3-dev` (and its comment); `<depend>zlib</depend>` (`find_package(ZLIB)`, `<zlib.h>` in `import_bag_main.cpp`); `<depend>` for `tf2_msgs` (moved from `exec_depend`), `grid_map_msgs`, `rcl_interfaces`, `std_msgs`, `builtin_interfaces` (included in `src/`, `include/`), `<test_depend>rcutils</test_depend>`, `<exec_depend>launch</exec_depend>` and `launch_ros` (installed launch files import them), as Part 1 did for `coverage_assessment`. System keys resolve on noble and resolute (`debian` rules); the rest are ROS packages. `.github/ci/Dockerfile` copies the manifest and needs no text change. Alternative: only the two system keys, the ROS-package ones to issue 184; rejected because the Rolling job's `rosdep` (commit 8) only installs what is declared, and Part 1 set the precedent. `kdl` stays (key differs by distro, issue 184 item 6).
8. **CI: the Rolling job** (B; separate commit, no code). See "Rolling CI job".
9. **Records** (NB): `.agent/work-plans/issue-182/`, carried in the pull request.

Verification on Rolling (here): `./build.sh` whole repository, 0 errors, warnings as the baseline list minus none added; `./test.sh`: 53 + 15 CTest tests pass, `test_frame_chain` green; repeats as "Done when". Jazzy: the CI job on the pull request only.

## Rolling CI job (owner's choice B: plain container)

**Where: the last commit of Part 2's pull request**, not a separate pull request. Reasons: the Part 2 pull request then gets an automated Rolling check of its own claims (otherwise only hand runs here), and the job's `rosdep install` without `-r` tests commit 7's keys in the same review; one review round instead of two. Risk: a red caused by the job itself (a moving upstream, a key) would hold Part 2's merge; fallback, agreed in advance: drop the commit from the pull request and land it as its own small pull request, Part 2 merging on the Jazzy job and the hand runs. Alternative (its own pull request after Part 2): cleaner review, but Part 2 merges with no Rolling CI.

**Shape**: a second job in `.github/workflows/ci.yml`, `ROS 2 Rolling (container)`, `if: (github.base_ref || github.ref_name) != 'jazzy'` (upstream's form), so the file stays identical on both branches as Part 1 left it and is backported as is (inert on `jazzy`). Steps after upstream's `rolling` job (`ros-base-docker.yml` on rolker/unh_marine_autonomy `rolling`): `ros:rolling-ros-core`; `ros-dev-tools git`; `rosdep init`/`update`; `../ws/src` with this checkout linked; source clones, depth 1, at pinned commits (what the local underlay builds; branches are the alternative): rolker/unh_marine_autonomy `rolling`, ANYbotics/grid_map at `0ece1b5` (no Rolling release), ros-navigation/navigation2 at `9113449e` for `nav2_msgs` and `nav2_common` only (**new fact**: `grid_map_ros` 2.5.0 `<depend>nav2_msgs</depend>`, and Nav2 has no Rolling release; `COLCON_IGNORE` on its other packages or a sparse checkout), geographic_info at `f70b81a` with `COLCON_IGNORE` in `geographic_msgs`, `geographic_info` (for `geodesy`, which `marine_survey_index` needs); `rosdep install --ignore-src -y` without `-r` over `colcon list --paths-only --packages-up-to cube_bathymetry coverage_assessment` (checked non-empty); `colcon build --packages-up-to cube_bathymetry coverage_assessment`; `colcon test --packages-select cube_bathymetry coverage_assessment`; `colcon test-result --verbose`. ccache through `actions/cache` (key `ccache-rolling-`) and `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`: recommended in the same commit, so the first run gives the cold time and a re-run the warm one. No image, no `ci-image.yml` or Dockerfile change, no `upstream.repos` use (the Jazzy job keeps that file).

**Estimate: about 17 to 22 minutes cold, perhaps 12 to 15 with a warm ccache; not measured on a runner.** Rough split: container, apt and `ros-dev-tools` 1 min; `rosdep install` 2 to 3 min (more than upstream's 62 s: GDAL, OpenCV for `grid_map_cv`, rosbag2); build 8 to 10 min (`grid_map`, `nav2_msgs` about 2.5 min, the upstream `marine_*` closure about 2 min, this repository 4 to 5 min); tests 6 to 7 min. Rests on: upstream's Rolling job (run 44807007698, 4-vCPU runner): 5 min 45 s in all, `rosdep install` 62 s, cold build of 16 packages 2 min 21 s, tests 1 min 22 s; this machine, cold, `-j4`, 4 workers: `grid_map` closure plus `nav2_msgs`/`nav2_common` 1 min 18 s (`nav2_msgs` alone 1 min 5 s, its message generation), `cube_bathymetry` and `coverage_assessment` 2 min 1 s (1 min 21 s and 40 s); tests serial per package about 5 min here (`cube_bathymetry` CTest sum about 4.5 min, the two packages in parallel). Runner cores are slower than this machine's, and that factor is the main unknown; the closest runner figures for this repository are the Jazzy job's on pull request 183 (run 45229921366, with ccache): build "2 packages finished [3min 55s]" (`cube_bathymetry` 2 min 53 s, about twice this machine's 1 min 21 s) and tests "[7min 4s]" (`cube_bathymetry` 5 min 46 s). The measured first-run time goes to the owner.

Does not check: repeat counts (one run per test); `bathymetry_layer` on Rolling (not in this repository); the real-bag smoke test (skips without its excerpt; the job prints it as skipped through `colcon test-result`).

## What this code is for, and the validation owed

`cube_bathymetry` is on the **navigation path**: the live node and the offline importers write the `draft/` and `processed/` depth tiles of `marine_bathymetry_store`, and rolker/unh_marine_autonomy's `bathymetry_layer` is a Nav2 costmap layer that reads those tiles so the planner routes around shoals; the node's own comment (`cube_bathymetry_node.hpp:2487-2489`, and `:1441`) notes that unknown cells are lethal under `unsurveyed_is_lethal`. A wrong, shifted or missing grid from a Rolling build is a grounding risk, not a display fault. `coverage_assessment` is operator display only.

Behaviour-neutral by kind: commits 1 to 5 and 7 change build lines, include names, test code and manifests, not the code that computes depths; the same tests passing is their evidence. Commit 6 is the one change to running code, in the live node only: it puts the TF subscriptions back on the node (as on Jazzy) with their own thread; on Rolling before it, TF still reached the same buffer through a hidden node, so depths were not expected to differ, but that is an argument, not a measurement.

**Tile comparison (owed to the owner).** Input data: no recorded bag is in the repository. The tests make deterministic synthetic bags (`test_import_bag_slope.py` `write_plane_bag`: a sloping plane surveyed on four headings; `test_derive_angular_response.py`: a two-mode mcap bag), and the real 3,000-ping excerpt of the 2026-06-09 M3 bag is at `/mnt/nadata/...` (`test_real_bag_smoke.cpp:67`), not mounted here. "Before" cannot be the pre-port tree on Rolling (it does not build); the importers' sources do not change in Part 2, so a Rolling-only before/after of them is the same code. What is meaningful and achievable:
- (a) **Cross-distro, the real check**: before = `jazzy` at `f3ca1e3` built on Jazzy (deadpool, the owner's 24.04 machine), after = `rolling` with Part 2 on Rolling (here). The same bag files (generated once, copied, so the input bytes are equal; the excerpt too where mounted), the same `import_bag` and `batch_regen_bag` commands (`test_import_bag_slope.py`'s arguments), then: the tile file lists equal; each band equal value for value (GDAL Python), with the NaN masks equal and the maximum absolute difference reported if not; bytes compared and reported but not required (GDAL 3.8 and 3.12 may write the same values differently). Plus `jazzy` after the backport against `jazzy` at `f3ca1e3`, both on Jazzy: bytes expected identical (the importers are already byte-deterministic: `test_a_second_run_with_the_same_bags_gives_identical_tiles`). A small script and the command lines go into `.agent/work-plans/issue-182/` (records, NB) so both sides run the identical commands; the Rolling side runs here in Part 2, the Jazzy side needs deadpool, so the comparison completes when the owner runs it there.
- (b) **The live node on Rolling**, the one changed code path: before = the Part 2 tree at commit 5 (hidden listener), after = commit 6; the same synthetic bag played with `ros2 bag play` (`use_sim_time`) into the node, draft tiles compared cell by cell. Not exact by construction (pings are best-effort depth 5, playback timing varies), so the result is the share of cells equal and the largest difference with ping counts; optional, recommended if the owner wants evidence for commit 6 beyond the audit test.
It can show that the port did not change importer output for these inputs, and with (a), that GCC 15, GDAL 3.12 and Rolling's libraries do not either. It cannot show: the live node with a real sonar and real TF timing; `bathymetry_layer` reading these tiles in a Nav2 costmap on Rolling; inputs unlike the synthetic geometry; anything at sea. No simulation runs here. Owed before Rolling runs on a boat with `bathymetry_layer`: (a) on the real excerpt, and a costmap run.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/CMakeLists.txt` | commits 1, 4 |
| `cube_bathymetry/include/cube_bathymetry/detections_projector.h`, `src/detections_projector.cpp`, `src/import_bag_main.cpp`, `src/batch_regen_main.cpp`, `test/test_beam_geometry.cpp`, `test/test_detections_projector.cpp` | commit 2 |
| `cube_bathymetry/src/cube_bathymetry_node.hpp` | commits 3, 6 |
| `cube_bathymetry/test/test_node_lifecycle.cpp` | commits 5, 6 |
| `cube_bathymetry/src/make_transform_listener.hpp` (new) | commit 6 |
| `cube_bathymetry/package.xml` | commit 7 |
| `.github/workflows/ci.yml` | commit 8 |
| `.agent/work-plans/issue-182/` | plan, progress, tile-comparison script and results |

## Left alone, for issue 184 (a comment drafted for the owner when Part 2 lands)

The GCC warnings listed in the baseline; the six remaining `tf2_ros/*.h` includes in `cube_bathymetry` (already item 1); `detections_to_pointcloud`'s self-node listener (same on both distros); the two copies of `make_transform_listener.hpp` (consolidate in `marine_autonomy`, whose `tf2_utils.h:50` has the `shared_ptr` form); `libgdal-dev` declared only as `build_depend` while `bag_to_geotiff` links GDAL at run time; `find_package(lifecycle_msgs)` without `REQUIRED` (`CMakeLists.txt:28`); `test_node_lifecycle`'s fixed `ROS_DOMAIN_ID=87` lets two concurrent runs on one machine cross-talk (the CMake comment also says ids above 101 overlap the ephemeral port range; these runs used 163 to 165 without trouble); `bag_to_geotiff.cpp:622-623` (already item 3).

## Prior Art

Part 1 (the helper, the test, the `InertWaitable` text, the manifest pattern), taken unchanged. Upstream's `rolling` container job (`ros-base-docker.yml`), taken for the CI shape. Rejected: a shared helper (above), an `industrial_ci` Rolling leg (the owner chose B), a CI image for Rolling.

## Serves

none: this repository has no written goals or design document (`AGENTS.md` names none); the port serves rolker/unh_marine_autonomy#415.

## Design Picture

No change to the design picture; no design document exists here. Commit 6 restores the listener placement the node's comments describe.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Smallest change per error; warnings, the duplicate helper and pre-existing listener design go to issue 184. |
| A change includes its consequences | Manifest keys, the CI job that installs them, and the tile evidence in the same pull request. |
| Test what you claim | Named cause-14 test with two mutations; repeat counts; tile comparison stated with what it cannot show. |
| Keep one current design | `ci.yml` stays identical on both branches; `upstream.repos` untouched. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0013 (progress timeline) | Yes | This plan and its `## Plan Authored` entry. |
| Repository ADR 0001 (tile eviction) | No | Eviction code untouched. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `package.xml` keys | `.github/ci/Dockerfile` bake | Yes: it copies the manifest; no text change; `ci-image.yml` rebuilds on the `jazzy` push |
| `ci.yml` (new job) | `ci-image.yml`, Dockerfile | No change needed: the Rolling job uses no image |
| `upstream.repos` | | Not touched |

## Open Questions

1. Rolling job as the last commit of Part 2 (recommended) or its own pull request.
2. Pins in the Rolling job: commits matching the local underlay (recommended) or branches (`grid_map` `rolling`, navigation2 `main`).
3. Tile comparison (a) needs a Jazzy build on deadpool by the owner: when; and whether (b) is wanted.
4. ccache in the Rolling job from the first commit (recommended) or after a measured cold run.
5. The ROS-package keys in commit 7 (recommended, as Part 1) or system keys only.

## Done when (numbers)

- The whole repository builds on Rolling; `cube_bathymetry` 53 of 53 and `coverage_assessment` 15 of 15 CTest tests pass; `test_frame_chain` green.
- Repeats as separate processes, own `ROS_DOMAIN_ID`: `test_node_lifecycle` 50 of 50; every other `cube_bathymetry` gtest 50 of 50, except the nine over 9 s (named above), 20 of 20.
- Both cause-14 mutations fail the named test on the final tree.
- Jazzy job green on the pull request; Rolling job green, its time reported.
- Tile comparison (a), Rolling side run and recorded; Jazzy side by the owner.
- Any intermittent failure recorded with name and count for the owner.

## Estimated Scope

One pull request into `rolling`, 8 commits plus records; backport of commits 1 to 8 to `jazzy` by hand.
