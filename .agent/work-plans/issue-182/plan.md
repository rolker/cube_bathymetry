# Plan: Build on ROS 2 Rolling: port cube_bathymetry and coverage_assessment, add a rolling branch

## Issue

https://github.com/rolker/cube_bathymetry/issues/182 (tracking issue rolker/unh_marine_autonomy#415, causes 1 to 16 in its comments; follow-up issue rolker/cube_bathymetry#184).

This revision refreshes the plan for **Part 2** (the `cube_bathymetry` package). Part 1 is delivered and is recorded below in short.

**Revised 2026-11-23 (after the Plan Review at `33371b1` and the owner's answer)**: the Rolling CI job is out of Part 2, owner's words "B: no Rolling job yet (Recommended)", because it needs `nav2_msgs` and `nav2_common` from navigation2 source and he has Nav2 in CI on hold; Part 2 merges on the Jazzy job plus the recorded hand runs on Rolling named below, and the job is kept as a deferred section for issue 184. Also revised: the tile comparison has a pass rule fixed in advance and a script the owner runs on deadpool; commit 6 names the `qos_overrides` parameter move; comparison (b) dropped; three factual slips corrected. Text the first refresh (`33371b1`) said differently is marked "(revised 2026-11-23)".

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
| `src/detections_to_pointcloud.cpp:115` (`DetectionsToPointCloud : rclcpp_lifecycle::LifecycleNode`, `:37`; revised 2026-11-23, first said a plain `rclcpp::Node`) | `make_shared<TransformListener>(*tf_buffer_)` | the self-node constructor, `(BufferCore &, bool spin_thread = true, bool static_only = false)`; Jazzy's are `(BufferCore &, bool spin_thread = true)` and `(BufferCore &, bool spin_thread, bool static_only)` (read on geometry2 `jazzy`; revised 2026-11-23, the second was first left out), both creating `transform_listener_impl_*` with the same `NodeOptions` as Rolling's: a hidden node and own thread on **both** distros, as before | own thread (default) | none: not cause 14, nothing changes between distros; the hidden node (its own QoS overrides, no `use_sim_time`, the buffer uses this node's clock) is pre-existing; noted for issue 184 |
| `import_bag_main.cpp:2898`, `batch_regen_main.cpp:1150`, `bag_to_geotiff.cpp:382` (offline tools) | `tf2_ros::Buffer tfBuffer(clock, duration)`, filled from the bag | no listener; Rolling's `Buffer` default `RequiredInterfaces()` and Jazzy's default `NodeT()` both mean no node | none | none |
| broadcasters | none in `cube_bathymetry` (source or tests) | n/a | n/a | none |

Proof of the fix, as in Part 1: the mutation evidence is already in hand from the scratch copy. With the helper, `test_node_lifecycle` passes 67 of 67; with the helper's `spin_thread` set to `false` (relinked, new mtime), `AllCallbacksShareTheDefaultGroup` fails at `:2142` and `:2154`; with the raw `this` (the baseline) it fails at the same lines. The existing test catches both mutations but cannot tell them apart, and its message ("test setup: the TF listener's group was not found") does not name the cause. So commit 6 adds `NodeLifecycle.TfListenerSubscribesThroughThisNodeOnItsOwnThreadNotAHiddenNode`, the same body as Part 1's test (a vector of groups per topic, exactly one for `/tf` and `/tf_static`, not the default group, not auto-added, the same group for both), and repeats both mutations on the final tree (`ldd`/mtime shows the mutated binary ran), recording them in the commit message. Alternative: rely on the existing test (it fails for both); rejected for parity with Part 1 and a message that names cause 14.

**Where the helper lives: a copy per package**, `cube_bathymetry/src/make_transform_listener.hpp`, Part 1's text with namespace `cube_bathymetry` (built and passing in the scratch copy; `cpplint`, `uncrustify` pass). Alternatives: (a) `cube_bathymetry` installs it and `coverage_assessment` uses it: that makes `coverage_assessment` build-depend on `cube_bathymetry` (today a `test_depend` only), changes already merged and backported code, and adds an installed API; (b) one helper upstream in `marine_autonomy` (whose `tf2_utils.h:50`, `Transformations`' constructor, calls `make_shared<TransformListener>(*tf_buffer_, node)` with a `shared_ptr`, a cause-14 form that fails to compile on Rolling once instantiated; revised 2026-11-23, first called a helper): a cross-repository change. Both are bigger than a port should make. The duplicate (about 70 lines, private to each package's `src/`) goes to issue 184 with the suggestion to consolidate in `marine_autonomy` once the port is done.

## Approach: Part 2 commits (branch from `rolling`, one cause per commit, list order)

B = cherry-picked to `jazzy` by hand after merge; NB = not. (Revised 2026-11-23: the first refresh's commit 8, the Rolling CI job, is removed by the owner's answer B; see "Deferred: the Rolling CI job".) `cube_bathymetry/` is identical on `jazzy` and `rolling` today (`git diff origin/jazzy origin/rolling -- cube_bathymetry` is empty), so every B commit should cherry-pick clean. Each message names its #415 cause or "new".

1. **cause 1** (B): `cube_bathymetry/CMakeLists.txt:245-248` becomes `target_link_libraries(cube_bathymetry_grid_projection grid_map_core::grid_map_core)` with Part 1's comment (definitions from `grid_map_core-extras.cmake`, include path from the target; the target exists in grid_map 2.2 and 2.5).
2. **cause 2** (B): the 9 includes of E2 to `.hpp` (exist on Jazzy, read on geometry2 `jazzy` in Part 1).
3. **new, unused includes** (B): delete `cube_bathymetry_node.hpp:61-62`, add `#include "tf2_ros/buffer.hpp"` beside line 45 (the `.hpp` name, so it adds no deprecation warning; exists on Jazzy). The other `tf2_ros/*.h` includes stay (issue 184).
4. **new, Eigen for `bag_to_geotiff`** (B): add `tf2_sensor_msgs::tf2_sensor_msgs` to its `target_link_libraries` (the header it includes is that package's; the target is already linked by `cube_bathymetry_node_link`, so it exists on Jazzy). Alternative `Eigen3::Eigen`: names the symptom, not the package the include belongs to.
5. **cause 16** (B): `test_node_lifecycle.cpp:2166`, Part 1's overrides and comment.
6. **cause 14** (B): the helper copy, `#include "make_transform_listener.hpp"` in the node header, `tf_listener_ = cube_bathymetry::makeTransformListener(*tf_buffer_, *this);`, the named test and the mutation record. Keeps TF on a dedicated thread; does not move it onto the executor. The comment at `cube_bathymetry_node.hpp:516-520` (#164, reset before replacing the buffer) stays true. The message also says (revised 2026-11-23, Plan Review finding 4) that on Rolling the listener's default QoS-overriding options now declare `qos_overrides./tf.subscription.*` and `qos_overrides./tf_static.subscription.*` on the cube node instead of on the hidden node, as on Jazzy; a launch file or parameter file that sets them now reaches the subscription.
7. **cause 11** (B): `cube_bathymetry/package.xml`: `sqlite3` (the command-line tool) becomes `libsqlite3-dev` (and its comment); `<depend>zlib</depend>` (`find_package(ZLIB)`, `<zlib.h>` in `import_bag_main.cpp`); `<depend>` for `tf2_msgs` (moved from `exec_depend`), `grid_map_msgs`, `rcl_interfaces`, `std_msgs`, `builtin_interfaces` (included in `src/`, `include/`), `<test_depend>rcutils</test_depend>`, `<exec_depend>launch</exec_depend>` and `launch_ros` (installed launch files import them), as Part 1 did for `coverage_assessment`. System keys resolve on noble and resolute (`debian` rules); the rest are ROS packages. `.github/ci/Dockerfile` copies the manifest and needs no text change. Alternative: only the two system keys, the ROS-package ones to issue 184; rejected because Part 1 set the precedent and a future Rolling job's `rosdep` installs only what is declared (revised 2026-11-23: first argued from commit 8). `kdl` stays (key differs by distro, issue 184 item 6).
8. **Records** (NB): `.agent/work-plans/issue-182/`, carried in the pull request.

Jazzy: the CI job on the pull request only.

### Hand runs on Rolling that replace a Rolling CI job (revised 2026-11-23)

No CI checks Rolling for this repository until the deferred job exists, so these runs, on this machine on the final tree of the pull request, are the Rolling evidence. Each is recorded in the `## Implementation` entry of `progress.md` with its command, the tree's sha and the result lines quoted (logs themselves are not committed); a run on an earlier tree is rerun after any later code commit.

1. **Clean build, both packages**: `rm -rf sensors_ws/build sensors_ws/install sensors_ws/log`, then `MAKEFLAGS=-j8 ./build.sh --parallel-workers 2`. Record: the colcon summary line ("2 packages finished"), 0 errors, and the warning count per class against the baseline list above (no class new or larger).
2. **Full test run, both packages**: domains 86 and 87 checked empty first (`ROS_DOMAIN_ID=86 ros2 node list --no-daemon`, same for 87), then `./test.sh --parallel-workers 2`. Record: each package's CTest line ("100% tests passed, 0 tests failed out of 53" and "... out of 15"), the `colcon test-result` summary line, the `test_frame_chain` line, and `test_real_bag_smoke`'s skip stated as a skip.
3. **Repeats** as "Done when", as separate processes, `test_node_lifecycle` run directly with `ROS_DOMAIN_ID` 160 to 170 (its CTest environment pins 87); record pass and fail counts per test name.
4. **Cause 11 keys on resolute without `-r`**: with `env.sh` sourced, `rosdep install --simulate --ignore-src -y --rosdistro rolling --from-paths cube_bathymetry coverage_assessment` (from the repository root). Pass: no "Cannot locate rosdep definition" and no "ERROR"; every key it would install is listed; the keys satisfied only by the hosted source layers (`grid_map_*`, `nav2_msgs` through `grid_map_ros`, the `marine_*` packages, `geodesy`) are named and checked present in `rolling/underlay` or `rolling/core`. Plus `rosdep resolve libsqlite3-dev zlib --os=ubuntu:resolute` and `--os=ubuntu:noble`, each giving `libsqlite3-dev` and `zlib1g-dev`. The Jazzy job's `rosdep` checks the keys on noble.
5. **The two cause-14 mutations** on the final tree (commit 6), with the mutated binary shown to be the one that ran.
6. **Tile comparison (a), Rolling side** (below).

## Deferred: the Rolling CI job (revised 2026-11-23, for issue 184)

The first refresh planned it as commit 8. The owner's answer was "B: no Rolling job yet (Recommended)": the job waits for his decision on Nav2 in CI or a Nav2 Rolling release, and is tracked in rolker/cube_bathymetry#184. What it needs, kept so it can be written then:

- **Why Nav2**: `grid_map_ros` declares `<depend>nav2_msgs</depend>` on every ROS 2 `grid_map` (since `07c8429`), neither `grid_map` nor navigation2 has a Rolling release, and both packages here call `find_package(grid_map_ros REQUIRED)` at top level, though only the two live nodes use it (`GridMapRosConverter`). Options the review listed: (A) build `nav2_msgs` and `nav2_common` from a sparse pinned navigation2 clone (about 2 min of runner build; the project's first CI build of Nav2 source); (C) a CMake option or this repository's own `GridMap` conversion so the job needs no `grid_map_ros` (a code change, not a port's); (D) patching `grid_map_ros` in CI (rejected, fragile).
- **Shape (as planned)**: a second job in `ci.yml` with `if: (github.base_ref || github.ref_name) != 'jazzy'`, `ros:rolling-ros-core`, `ros-dev-tools git`, `rosdep` init and update, this checkout linked into `../ws/src`; clones at pins: rolker/unh_marine_autonomy `rolling`, ANYbotics/grid_map `0ece1b5`, navigation2 `9113449e` (`nav2_msgs`, `nav2_common` only, sparse or `--filter=blob:none`), geographic_info `f70b81a` with `COLCON_IGNORE` in `geographic_msgs` and `geographic_info`; `rosdep install --ignore-src -y` without `-r` over the non-empty `colcon list --paths-only --packages-up-to cube_bathymetry coverage_assessment` (20 packages); build `--packages-up-to`; test `--packages-select cube_bathymetry coverage_assessment`.
- **Review finding 3, for when it is written**: `colcon test --event-handlers console_direct+ --return-code-on-test-failure`; `colcon test-result --verbose` with `if: always()`; ccache needs `apt-get install ccache` in the container, `CCACHE_DIR` under `$GITHUB_WORKSPACE` for `actions/cache` (key `ccache-rolling-`), and both `-DCMAKE_C_COMPILER_LAUNCHER=ccache` and `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
- **Estimate**: about 17 to 22 min cold, 12 to 15 warm, unmeasured on a runner; the basis (upstream's Rolling job, run 44807007698; this machine at `-j4`: `grid_map` with `nav2_msgs` 1 min 18 s, this repository 2 min 1 s; the Jazzy job on pull request 183: `cube_bathymetry` build 2 min 53 s, tests 5 min 46 s) is in the first refresh at `33371b1`.

## What this code is for, and the validation owed

`cube_bathymetry` is on the **navigation path**: the live node and the offline importers write the `draft/` and `processed/` depth tiles of `marine_bathymetry_store`, and rolker/unh_marine_autonomy's `bathymetry_layer` is a Nav2 costmap layer that reads those tiles so the planner routes around shoals; the node's own comment (`cube_bathymetry_node.hpp:2487-2489`, and `:1441`) notes that unknown cells are lethal under `unsurveyed_is_lethal`. A wrong, shifted or missing grid from a Rolling build is a grounding risk, not a display fault. `coverage_assessment` is operator display only.

Behaviour-neutral by kind: commits 1 to 5 and 7 change build lines, include names, test code and manifests, not the code that computes depths; the same tests passing is their evidence. Commit 6 is the one change to running code, in the live node only: it puts the TF subscriptions back on the node (as on Jazzy) with their own thread; on Rolling before it, TF still reached the same buffer through a hidden node, so depths were not expected to differ (a lookup can differ only by timing), but that is an argument, not a measurement. Its one other observable effect on Rolling (revised 2026-11-23, Plan Review finding 4): the `qos_overrides./tf.subscription.*` and `qos_overrides./tf_static.subscription.*` parameters are declared on the cube node instead of on the hidden `transform_listener_impl_*` node, as on Jazzy, so QoS overrides set for the cube node govern its TF subscriptions again.

**Tile comparison (owed to the owner; revised 2026-11-23).** Input data: no recorded bag is in the repository. The tests make deterministic synthetic bags in closed form, with no randomness or wall clock (`test_import_bag_slope.py` `write_plane_bag`: a sloping plane surveyed on four headings), and the real 3,000-ping excerpt of the 2026-06-09 M3 bag is at `/mnt/nadata/...` (`test_real_bag_smoke.cpp:67`), not mounted here.

**What it measures.** Part 2 does not change the importers' sources (commits 1 to 5 and 7 change build lines, include names, tests and the manifest; commit 6 touches only the live node), and the upstream store, index and backscatter packages are code-identical on upstream `jazzy` and `rolling`. So the comparison measures the **platform**, not the port: GCC 13 against 15, glibc's libm, GDAL 3.8 against 3.12, and the tf2 and rosbag2 versions. That is still the question that matters for the costmap: does a Rolling build turn the same soundings into the same depths. "Before" is `jazzy` at **`0b70b46`** (Part 1's backport merged; `cube_bathymetry/` unchanged from `f3ca1e3`) on Jazzy; "after" is the Part 2 tree on Rolling.

**Direction**: Jazzy writes the bags, Rolling reads them (old writer, new reader, the direction a real survey archive takes); both write rosbag2 metadata version 9.

**Pass rule, fixed before any run.** Tiles are written by `marine_bathymetry_store` as Float64 with NaN as nodata (`tile_io.cpp:56-60,370`).
1. The same set of tile files (relative paths) in each store.
2. For each tile and band, the same NaN mask.
3. Every finite cell equal within **1 mm absolute** (0.001 in the band's units, metres for depth and uncertainty); an integer band, if any, exactly equal.
4. Reported in every case, pass or fail: per band, the number of cells that differ at all, the number beyond 1 mm, the largest absolute difference and the tile and cell where it is; the bytes of each tile file compared and reported, not required (GDAL 3.8 and 3.12 may encode equal values differently); the manifests compared with their write times and build fingerprints removed, reported, not required.

Why 1 mm: platform differences enter as last-bit differences in libm and in the order of floating-point operations, around 1e-12 m at survey depths in Float64, far below 1 mm. CUBE's hypothesis choice is discontinuous, so if such a difference tips a cell to another hypothesis the cell moves by a hypothesis separation, centimetres or more, far above 1 mm. 1 mm therefore separates "the same answer" from "a different answer" with orders of magnitude on each side, and it is far below anything the costmap acts on. A cell beyond 1 mm fails the rule; the count and size go to the owner, who decides whether it is a hypothesis switch to accept.

**The script**, written during implementation as `.agent/work-plans/issue-182/tile_compare.sh` (with its Python in the same file or beside it), records only, not backported. It must run on deadpool without this session:
- `tile_compare.sh jazzy --src <cube_bathymetry checkout> --out <dir>`: with the Jazzy workspace sourced, generates the bags with `write_plane_bag` (imported by path from `<src>/cube_bathymetry/test/`, as the pytests do), runs `import_bag` and `batch_regen_bag` with `test_import_bag_slope.py`'s arguments, writes `versions.txt` (`gcc --version`, `gdal-config --version`, `ldd --version`, `dpkg-query -W` for the ROS tf2 and rosbag2 packages, `git -C <src> rev-parse HEAD`), `sha256sum` lists of bags and tiles, and packs everything into `<dir>/tile_compare_jazzy.tar.gz`. If the real excerpt is reachable (`CUBE_REAL_BAG_EXCERPT` or the default path), it is imported too.
- `tile_compare.sh rolling --from tile_compare_jazzy.tar.gz --src <checkout> --out <dir>`: with the Rolling workspace sourced, imports the Jazzy-written bags with the same commands and applies the pass rule, printing a report and `PASS`/`FAIL`.
- The owner's command lines, given in the script's header and in the pull request: `git -C <ws>/src/cube_bathymetry fetch && git -C <ws>/src/cube_bathymetry checkout 0b70b46`; build `cube_bathymetry` there (`colcon build --packages-up-to cube_bathymetry --parallel-workers 4`); `source <ws>/install/setup.bash`; fetch the script with `git -C <ws>/src/cube_bathymetry show origin/feature/issue-182:.agent/work-plans/issue-182/tile_compare.sh > ~/tile_compare.sh`; `bash ~/tile_compare.sh jazzy --src <ws>/src/cube_bathymetry --out ~/tile_compare`. He sends back `~/tile_compare/tile_compare_jazzy.tar.gz` (bags, tiles, checksums, `versions.txt`, logs; a few MB for the synthetic bags).
- During Part 2 the Rolling side is run here in a dry mode (Rolling writes and reads its own bags) to prove the script and record Rolling's checksums; the comparison proper runs when his tarball arrives.

**If his side has not run by the merge checkpoint**, Part 2 claims only: the importers' sources are unchanged by the port (shown by the diff); the Rolling tool output on the synthetic bags is recorded and byte-deterministic run to run; whether Rolling's platform gives the same depths as Jazzy's is **not shown**, and is owed before Rolling runs on a boat with `bathymetry_layer`.

**(b) The live node, commit 5 against commit 6: dropped** (revised 2026-11-23; the first refresh made it optional). With best-effort pings and playback timing, two runs of the same commit already differ, so it would first need a run-to-run spread (Plan Review finding 5) and could only show "within the noise". Commit 6 changes which node owns the TF subscriptions, not the buffer or what the pings are matched against; the named test and its two mutations are the stronger evidence. Kept as an option if the owner wants it.

It can show (with his side run) that Rolling's platform gives the same importer depths as Jazzy's for these inputs. It cannot show: the live node with a real sonar and real TF timing; `bathymetry_layer` reading these tiles in a Nav2 costmap on Rolling; inputs unlike the synthetic geometry (unless the real excerpt is reachable on his side); anything at sea. No simulation runs here. Owed before Rolling runs on a boat with `bathymetry_layer`: the comparison on the real excerpt, and a costmap run.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/CMakeLists.txt` | commits 1, 4 |
| `cube_bathymetry/include/cube_bathymetry/detections_projector.h`, `src/detections_projector.cpp`, `src/import_bag_main.cpp`, `src/batch_regen_main.cpp`, `test/test_beam_geometry.cpp`, `test/test_detections_projector.cpp` | commit 2 |
| `cube_bathymetry/src/cube_bathymetry_node.hpp` | commits 3, 6 |
| `cube_bathymetry/test/test_node_lifecycle.cpp` | commits 5, 6 |
| `cube_bathymetry/src/make_transform_listener.hpp` (new) | commit 6 |
| `cube_bathymetry/package.xml` | commit 7 |
| `.agent/work-plans/issue-182/` | plan, progress, `tile_compare.sh` and its results |

## Left alone, for issue 184 (a comment drafted for the owner when Part 2 lands)

The GCC warnings listed in the baseline; the six remaining `tf2_ros/*.h` includes in `cube_bathymetry` (already item 1); `detections_to_pointcloud`'s self-node listener (same on both distros); the two copies of `make_transform_listener.hpp` (consolidate in `marine_autonomy`, whose `tf2_utils.h:50`, `Transformations`' constructor, passes a `shared_ptr`; revised 2026-11-23); the deferred Rolling CI job (section above); `libgdal-dev` declared only as `build_depend` while `bag_to_geotiff` links GDAL at run time; `find_package(lifecycle_msgs)` without `REQUIRED` (`CMakeLists.txt:28`); `test_node_lifecycle`'s fixed `ROS_DOMAIN_ID=87` lets two concurrent runs on one machine cross-talk (the CMake comment also says ids above 101 overlap the ephemeral port range; these runs used 163 to 165 without trouble); `bag_to_geotiff.cpp:622-623` (already item 3).

## Prior Art

Part 1 (the helper, the test, the `InertWaitable` text, the manifest pattern), taken unchanged. Upstream's `rolling` container job (`ros-base-docker.yml`), taken for the deferred CI shape. Rejected: a shared helper (above), an `industrial_ci` Rolling leg (the owner chose B), a CI image for Rolling.

## Serves

none: this repository has no written goals or design document (`AGENTS.md` names none); the port serves rolker/unh_marine_autonomy#415.

## Design Picture

No change to the design picture; no design document exists here. Commit 6 restores the listener placement the node's comments describe.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Smallest change per error; warnings, the duplicate helper and pre-existing listener design go to issue 184. |
| A change includes its consequences | Manifest keys with a `rosdep` check on resolute by hand (no Rolling CI), and the tile evidence in the same pull request. |
| Test what you claim | Named cause-14 test with two mutations; repeat counts; tile comparison stated with what it cannot show. |
| Keep one current design | No CI file changes in Part 2; `upstream.repos` untouched. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0013 (progress timeline) | Yes | This plan and its `## Plan Authored` entry. |
| Repository ADR 0001 (tile eviction) | No | Eviction code untouched. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `package.xml` keys | `.github/ci/Dockerfile` bake | Yes: it copies the manifest; no text change; `ci-image.yml` rebuilds on the `jazzy` push |
| `upstream.repos` | | Not touched |

## Open Questions

(Revised 2026-11-23: the first refresh's questions on the Rolling job's placement, pins and ccache are settled by the owner's answer B and move to the deferred section.)
1. When the owner can run the Jazzy side of the tile comparison on deadpool, and whether the real excerpt is reachable there.
2. The ROS-package keys in commit 7 (recommended, as Part 1) or system keys only.

## Done when (numbers)

- The whole repository builds on Rolling; `cube_bathymetry` 53 of 53 and `coverage_assessment` 15 of 15 CTest tests pass; `test_frame_chain` green.
- Repeats as separate processes, own `ROS_DOMAIN_ID`: `test_node_lifecycle` 50 of 50; every other `cube_bathymetry` gtest 50 of 50, except the nine over 9 s (named above), 20 of 20.
- Both cause-14 mutations fail the named test on the final tree.
- Jazzy job green on the pull request. (Revised 2026-11-23: "Rolling job green" removed with the job.)
- The hand runs on Rolling 1 to 5 recorded in `progress.md` on the final tree.
- `tile_compare.sh` written; its Rolling dry run recorded; the comparison run and its report recorded if the owner's tarball has arrived, otherwise the claim stated as above.
- Any intermittent failure recorded with name and count for the owner.

## Estimated Scope

One pull request into `rolling`, 7 code commits plus records; backport of commits 1 to 7 to `jazzy` by hand.
