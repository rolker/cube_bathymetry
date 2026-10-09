---
issue: 164
---

# Issue #164 — coverage_assessment node (area vs survey standard, per-source credit) + fresh CUBE sheet without a restart

## Issue Review
**Status**: complete
**When**: 2026-10-09 12:56 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #164
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-more-detail

### Findings
- The cleanup description is accurate as far as it goes. `on_cleanup` (cube_bathymetry_node.cpp:810) calls `saveDirtyTiles()`, cancels timers and drops the sonar-info subscription; it never touches `geo_map_sheet_`. `on_configure` builds a new sheet (line 107) and clears `evicted_indices_`, the disk-serve queue, `refresh_tracker_`, the saving ledger and `catalog_builder_` (lines 146-170). With `draft_dir` set, the prime block (lines 371-430) reloads saved Processed and Draft tiles into that fresh sheet. With `draft_dir` and `prior_store_dir` empty, nothing is reloaded and the sheet starts empty.
- But the no-persistence path is probably not reachable yet: `on_configure` calls `declare_parameter` for about 18 parameters with no `has_parameter` guard and nothing is undeclared in `on_cleanup` (grep for undeclare and has_parameter: no hits). rclcpp throws `ParameterAlreadyDeclaredException` on a second declare, and rclcpp_lifecycle swallows exceptions from transition callbacks (the file's own comment at line 713 says so). So a second configure after cleanup most likely fails or half-configures. Found by reading source; not run. The first work item should confirm it by running the transitions.
- No existing test configures the node. `test/test_node.cpp` tests the estimator, not the lifecycle node, and the node class lives in `cube_bathymetry_node.cpp` next to `main()`, so it cannot be linked into a gtest. A cleanup/configure check needs either extracting the class into a library target or a launch_testing/lifecycle-service test (neither is in package.xml or CMakeLists today).
- Design problem for item 1: the `grid` topic is not the whole sheet. `publishCaGrid` (line 1084) publishes only tiles within `ca_window_radius_m` (default 200 m) of the vessel, at the ~5 s publish cadence, with NaN for unknown cells. A node that judges area from `grid` would see coverage vanish as the boat moves away, and its area numbers would fall. It needs to accumulate a latched union per source, or read from `coverage_tiles` / the store. This changes what "tests on synthetic grids" must cover (moving-window input, tile eviction).
- The `elevation` layer carries `c.depth` unmodified (grid_projection.cpp:149) and the README never states its sign. The standard's "depth below the water surface" needs a verified sign convention, and the layer name suggests the opposite of positive-down.
- `upstream.repos` pins unh_marine_autonomy at `jazzy`, and `ci_local.sh` / hosted CI build against it. Any code that includes the new messages will not build in CI until unh_marine_autonomy#406 merges to jazzy.
- The repo has no `.agents/README.md` (only `AGENTS.md` and `.agents/ci_local_extra.sh`), so the issue's last work item creates it rather than updating it. The parameter table lives in the top-level README.md, not under `cube_bathymetry/`.

### Build and test order before the #406 messages merge
1. Cleanup-then-configure check and the declare-parameter fix (or a `clear` service): no dependency on the messages.
2. Coverage core as a plain C++ library with no ROS message types: polygon rasterisation, union of sources, `allowed(depth)=sqrt(a^2+(b*depth)^2)`, per-source and unique areas, NaN/missing uncertainty. Tests on synthetic `grid_map::GridMap` objects. Define the standard as a local struct that the ROS wrapper later fills from `SurveyStandard`.
3. The ROS node wrapper, subscriptions and publishers: develop against the #406 branch (override the `upstream.repos` version locally), then merge only after #406 is on jazzy. Do not commit a branch pin to `upstream.repos`.

### Actions
- [ ] Reproduce a cleanup then configure cycle on the live node (lifecycle services, `draft_dir` empty) and record whether the second configure succeeds; if `ParameterAlreadyDeclaredException` fires, guard the declares (`has_parameter`) or undeclare in `on_cleanup`, with a regression test.
- [ ] Decide the coverage node's input: latched union over time from `grid` (200 m vessel window) versus `coverage_tiles` / the store; state it in the issue before planning.
- [ ] Verify and document the sign convention of the `elevation` layer before the depth-dependent threshold is written.
- [ ] Define `clear` and `draft_dir` semantics now if `clear` is built (delete saved tiles, move to a new session directory, or clear RAM only), including the catalog, eviction markers, disk-serve queue and refresh tracker, and clearing in a state where a save timer may fire.
- [ ] Put the lifecycle test seam in scope: the node class must be testable (extract to a library or add a launch_testing test).
- [ ] Order the work core-first so it builds without the #406 messages; do not pin `upstream.repos` to a feature branch in the merged PR.
- [ ] Create `.agents/README.md` from the workspace template (the repo has none) and add the new node's parameters to the README parameter table in the same PR.
- [ ] Consider two PRs: (A) reconfigure fix / `clear` with tests, (B) `coverage_assessment`; they share no code.

## Plan Authored
**Status**: complete
**When**: 2026-10-09 13:20 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-164/plan.md` at `6705d4f`
**Branch**: feature/issue-164 at `6705d4f`
**Phases**: 2 PRs: (a) fresh sheet, (b) coverage_assessment

### Open questions
- [ ] .agents/README.md: issue lists it, workspace AGENTS.md says dedicated task; default is not in these PRs
- [ ] Depth reference: plan uses ellipsoid height of the map_tide origin (via earth) because elevation is ellipsoid height, not map z; confirm
- [ ] Overlapping areas with different standards: one coverage_grid, cell is 1 only if it meets every containing area; acceptable?

## Plan Review
**Status**: complete
**When**: 2026-10-09 13:42 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-164/plan.md` at `6705d4f`
**PR**: PR-less
**Verdict**: changes-requested

Dispatched fresh-context sub-agent (handoff header present); not the plan author. Read as data: issue #164, unh_marine_autonomy#406, the review-issue comments on both. Settled items (two PRs, no `clear` before the event, per-source latched union from the 200 m window, #406 messages and rules, overlap rule, `.agents/README.md` out, host answers) were not reopened.

### Answers to the four review points

**1. Elevation reading: correct, with one correction to the surface-height step.**
- Verified in source. The live node transforms each sounding to ECEF via `earth <- sensor` and converts with `GeoPointLatLongDegrees(ecef)` (`cube_bathymetry_node.cpp` ~2079-2088). `GeoSounding` takes `point[2]` as depth (`geo_sounding.h`). The ECEF to geodetic conversion in `gz4d_geo.h` (`ToGeodetic`, `ret[CF::Height] = U*(1-b^2/(aV))`) returns height above the WGS84 ellipsoid. `grid_projection.cpp:149` copies it into `elevation` unchanged, no negation anywhere. So `elevation` is WGS84 ellipsoid height, positive up, and depth below the surface is `h_surface - elevation`. Independent corroboration the plan did not cite: `ssp_ray_tracer.h` says "Sounding::depth ... is an elevation, positive up", and the top-level README (~line 170) already says stored depths are WGS84 ellipsoidal heights (uma ADR-0002 section D4). The sim confirms it: `asv_sim/config/bizzyboat.yaml` pins the water surface at 48.88 m ellipsoidal.
- Correction: the plan says the tangent-plane curvature term "never enters". It does. mru_transform defines `map` as an ENU tangent plane at the first fix with altitude forced to 0 (`mru_transform.cpp:61-63`, `map_frame.cpp`), so the origin height is 0 and the `map` z axis is the ellipsoid normal there. `earth <- map_tide` has its origin directly above the map origin, so its geodetic altitude is just the tide z. But that z is the average of the **boat's** odometry z (`sea_surface_estimator.hpp:370`), and odometry z is a tangent-plane coordinate at the boat's position: map z = h - d^2/(2R), d being the boat's distance from its map origin. So the plan's `h_surface` is low by d^2/2R: about 8 cm at 1 km, 0.3 m at 2 km, 2 m at 5 km. Fine for a pond-scale sim near the first fix; wrong as a stated property. It also means the detour through `earth` buys nothing over `map <- map_tide` z plus the origin height (0). Either document the bound (and log or refuse beyond a distance) or add d^2/2R using the boat's horizontal distance from the map origin.
- Spatial uniformity of `h_surface`: acceptable for the event and for areas of a few km. Water-surface ellipsoid height follows the geoid, which varies by centimetres per km in this region (not measured here; the sim holds it constant at 48.88). State the assumption and an extent bound in the README.

**2. Pull-style resampling: necessary. NaN-never-erases: right for coverage, wrong for the uncertainty field.**
- Necessary. `geoGridsToGridMap` sizes the lattice from the min/max of the projected cell centres (`grid_projection.cpp` ~125-138), centres it there, and bins each cell with `getIndex` (nearest). So the lattice phase and extent change with every message (the window is whole GGGS tiles within 200 m, so extent jumps at tile boundaries), and CUBE cells are not aligned to the lattice, so one message already has duplicates and gaps. A push-style union would alias.
- Residual risk: pull resampling does not repair a pinhole already in one message. A stationary boat keeps the same extent and phase, so a pinhole can persist and area_covered is under-counted. Measure it on real CUBE grids (NaN share inside a surveyed swath) in the early spike.
- NaN-never-erases is right for coverage. But the plan overwrites the (elevation, uncertainty) pair on a finite elevation (line 81): a finite elevation with a NaN uncertainty (the sim before TF settles produces NaN uncertainty, per the node's own comment) would wipe a good uncertainty and flip a meeting cell to not meeting. Apply never-erases per field.

**3. Scope.**
- PR (a), keep: steps 1-4, 6, 7, 8 (sign test and README), 10, 11; step 5 as written (releasing the tf listener before the buffer also closes a real window in which the old listener can touch a destroyed buffer, `cube_bathymetry_node.cpp` 500-501). Defer or shrink: step 9's stub-`ros2` shell test. The script is the operator tool, keep it, but the test can be cut to three cases (consumer ordering, refuse on `draft_dir`, failing transition) and the "already inactive" and four-node permutations dropped; live verification (step 11) covers the rest. Do not let the script block PR (a): if it slips, ship (a) with the README command sequence and add the script in a follow-up. Put the node header in `src/` rather than `include/` so it is not installed as public API.
- PR (b), cut: the custom "reject set while ACTIVE" parameter guard (use `read_only` descriptors as the CUBE node does for read-at-configure values, one fewer callback and test); the `earth_frame` and `survey_area_queue_depth` parameters (hard-code the first, constant for the second); the B7 matrix down to one happy path, frame handling, stale tide, and reset-empties-union. Defer: lower-priority registry edge tests beyond replace-by-id and stale stamp.
- Library-first: B1-B5 build without #406 only on a branch that stops at B5. The PR head includes B6, and CI builds the head against `upstream.repos` at `jazzy`, so PR (b) as one PR is red until #406 is on `jazzy`. #406 is four small `.msg` files and is also needed by unh_marine_simulation#88: get it merged first, now, and the order becomes only a commit order. If it cannot be, split (b) into a core-library PR and a wrapper PR.

**4. Consequences missed**: see findings (Dockerfile rosdep lines, `ros2lifecycle`/`ros2param` exec_depends, issue checklist, cross-repo sim launch).

### Findings
- [ ] (must-fix, high) Source grids are not in one shared frame today: the sim launches CUBE with `map_frame = <ns>/map` (`sim_robot_launch.py` ~388-392, `bizzyboat_sim_core_launch.py` 172) and each boat's mru_transform sets its own map origin at its first fix, so four boats give four frames and four origins; the plan's rule "a grid in another frame is rejected" (line 91-92) would drop three of four sources. Decide before B3/B6: let the accumulator resample each message through `earth` (lattice centre to earth to source frame via TF, a natural extension of the pull design), or make the sim give every CUBE one common frame; update the #406 `header.frame_id` wording to match whichever — `plan.md:89-92`
- [ ] (must-fix, medium) "The tangent-plane curvature term never enters" is false: `map_tide.z` is the boat's tangent-plane z, so `h_surface` from `earth <- map_tide` is low by d^2/2R (8 cm at 1 km, 0.3 m at 2 km, 2 m at 5 km, d = tide-publishing boat's distance from its map origin); fix the Context text, document the bound, and either correct for it or refuse beyond a distance — `plan.md:26-28`
- [ ] (must-fix, medium) Move the datum and frame check from B9 to a first spike before any wrapper code: a throwaway node or script on a running sim that prints `h_surface - elevation` per source and the `header.frame_id`s; B9 is the last step and is where both the frame problem above and any sign error would otherwise first show — `plan.md:118,127`
- [ ] (must-fix, medium) Clamping negative depth to 0 hides a wrong surface height (sign, datum, stale tide) as a plausible verdict; count cells with depth below about -1 m and log a throttled WARN with the count and the surface height used — `plan.md:86`
- [ ] (must-fix, medium) PR (b) cannot be green in CI as one PR before #406 is on `jazzy` (the head includes B6); say so, and make "merge #406 first" the plan (or split (b)) rather than "B1-B5 can be reviewed earlier" — `plan.md:116,190`
- [ ] (suggestion, medium) Apply never-erases per field: a finite elevation with NaN uncertainty must not overwrite a finite uncertainty already held — `plan.md:81`
- [ ] (suggestion, medium) Event config: `bizzyboat.yaml` sets `draft_dir: /home/field/data/world/depths` for `/**/cube_bathymetry`, and `bizzyboat_sim_core_launch.py` layers that file; if the event sim loads it, a lifecycle cycle reloads the old tiles and "fresh sheet" is false. The script refuses loudly, but confirm now which launch the event uses and that `draft_dir` is empty there; record it in the README reset section — `plan.md:60-61`
- [ ] (suggestion, medium) `coverage_grid` at up to 4,000,000 cells is a 16 MB float message republished whenever any grid changed (about every 5 s per boat); lower the default `max_raster_cells` (1,000,000) or add a minimum publish period — `plan.md:93-95,114`
- [ ] (suggestion, low) Measure the pinhole share in single CUBE grids in the spike (a stationary boat repeats the same phase, so pull resampling cannot fill it) and compare `area_covered` against a direct count — `plan.md:80-83`
- [ ] (suggestion, low) CI plumbing: the Dockerfile's `rosdep check` and `rosdep install` run `--from-paths src cube_bathymetry` (`.github/ci/Dockerfile` 45-52), so adding `coverage_assessment` needs that argument too, not only the COPY and the `ci-image.yml` path — `plan.md:119,141`
- [ ] (suggestion, low) PR (a) package.xml: the script calls `ros2 lifecycle` and `ros2 param`; add `exec_depend` for `ros2lifecycle` and `ros2param`. PR (b): list `grid_map_ros`, `grid_map_msgs`, `rclcpp_lifecycle`, `tf2_eigen` explicitly — `plan.md:57,119`
- [ ] (suggestion, low) The plan says the README "says nothing" about the elevation convention; it already states ellipsoidal heights for stored depths (~line 170, uma ADR-0002 D4). Cite that and `ssp_ray_tracer.h` in the new README text instead of presenting it as new — `plan.md:23`
- [ ] (suggestion, low) `water_surface_frame` is one frame, but there is one `<ns>/map_tide` per boat; say which boat's tide is used and what happens when that boat's estimator withholds `map_tide` (plausibility bound), since the assessor then silently stops scoring everyone — `plan.md:92,111`
- [ ] (suggestion, low) Cross-repo consequences are not in the table: unh_marine_simulation#88 launch (assessor, empty `draft_dir`, frames), and the #164 checklist still lists `.agents/README.md`; tick or reword it before "Closes #164" so closing is honest — `plan.md:165-171`
- [ ] (suggestion, low) The `coverage_assessment` and `coverage_grid` QoS is not stated; a scoreboard that starts late needs transient_local on both (the assessment per area) — `plan.md:93`
- [ ] (suggestion, low) Test environment: use a per-test `ROS_DOMAIN_ID` and `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST` (jazzy; `ROS_LOCALHOST_ONLY` is deprecated) so parallel colcon tests and the CI container do not cross-talk — `plan.md:40-41`

### Evaluation
| Dimension | Verdict |
|---|---|
| Scope | Needs work (PR (a) fine; PR (b) trim, see point 3) |
| Issue alignment | Good |
| File targeting | Needs work (Dockerfile arguments, exec_depends) |
| Consequences | Needs work (cross-repo sim launch, issue checklist) |
| Documentation and instruction impact | Good |
| Principle alignment | Needs work (silent clamp, documented-from-assumption curvature claim) |
| ADR compliance | Good (project 0001 respected; workspace 0018 gate noted) |
| ROS conventions | Needs work (frames per namespace, QoS unstated) |

## Plan Authored
**Status**: complete
**When**: 2026-10-09 14:30 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-164/plan.md` at `19eb599`
**Branch**: feature/issue-164 at `19eb599`
**Phases**: 2 PRs: (a) fresh sheet, (b) coverage_assessment (after unh_marine_autonomy#406 is on jazzy)

Revision after the plan review at `8fbc067` (changes-requested). All five must-fix items and every suggestion are applied or listed under "Review disposition" in the plan.

### Changes
- Frames: assessor raster has its own `map_frame`; each source grid is transformed into it per message via TF (through `earth`); a source with no transform is skipped with a throttled WARN, never silently dropped.
- Surface height: `h_surface` is low by d^2/2R (d = tide boat's distance from its map origin); documented, and scoring is refused (warn, skip) beyond `water_surface_max_boat_distance`; which boat's `map_tide`, and the withheld-tide behaviour, stated. Spatially varying surface is named later work.
- Datum/frame/pinhole spike is B1, before any package code. Negative depths counted below about -1 m with a throttled WARN, not clamped silently.
- #406 first; (b) stays one PR; wrapper developed against an uncommitted `upstream.repos` override.
- Cuts: node header under `src/`; stub-ros2 test shrunk to three cases; step 5 kept (listener released before buffer); (b) custom set-guard replaced by `read_only` descriptors, `earth_frame` and `survey_area_queue_depth` dropped, B7 test matrix shrunk.
- Suggestions folded in: per-field never-erases; `max_raster_cells` default 1,000,000; transient_local on both outputs; per-test `ROS_DOMAIN_ID` + `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`; Dockerfile `--from-paths`; exec_depends for `ros2lifecycle`/`ros2param`; explicit (b) deps; README cites existing ellipsoidal-heights statement and `ssp_ray_tracer.h`; cross-repo consequences (sim#88 demo launch must leave `draft_dir` empty) and the issue-checklist rewording before "Closes #164".

### Open questions
- [ ] .agents/README.md stays out of these PRs; the issue checklist item needs rewording (owner's yes) before (b) closes #164
- [ ] Overlapping areas with different standards: one coverage_grid, a cell is 1 only if it meets every containing area (owner settled; revisit only if the scorer needs per-area rasters)

## Implementation
**Status**: complete (PR (a) only; PR (b) not started; not pushed, no PR opened)
**When**: 2026-10-09 14:30 -04:00
**By**: Claude Sonnet

**Plan**: `.agent/work-plans/issue-164/plan.md` at `bc7ae4c`
**Scope**: PR (a), the fresh-sheet fix (the issue body must say "Part of #164", not close it)

### Commits
- `db74bf8` move the node class to a private header (pure refactor; `src/cube_bathymetry_node.hpp`, `main()` stays in the `.cpp`, INTERFACE link target)
- `dc9fdca` test seam: `sheet()`, `catalogSize()`, defaulted `NodeOptions` constructor argument
- `dbf1927` `declareOnce` on every `declare_parameter` in `on_configure`, state resets, `on_cleanup` release order, `test_node_lifecycle`
- `af5c179` `ElevationIsEllipsoidHeightPositiveUp` in `test_publish_equivalence`
- `c782c9a` `scripts/reset_cube_nodes.sh`, its three-case stub test, `exec_depend` on `ros2lifecycle` and `ros2param`
- `5c8b1c0` README: elevation convention, "Starting a fresh sheet without restarting"
- `269dd90` cpplint/uncrustify fixes for the moved header and the new test
- `bc7ae4c` plan kept in sync with what was built

### Reproduction (red test)
Run on `dc9fdca`, i.e. the pure refactor plus the seam, before the guard: **all four `test_node_lifecycle` cases failed**. The log line is `Caught exception in callback for transition 10 ... Original error: parameter 'map_frame' has already been declared`, then `Callback returned ERROR during the transition: configure`, and the node stays UNCONFIGURED. This confirms the reading in the issue review: rclcpp_lifecycle swallowed `ParameterAlreadyDeclaredException`, so a second configure never succeeded and "cleanup then configure starts empty" was unreachable. After `dbf1927` all four pass. The stop condition did not trigger: the empty-start assertions hold with the guard alone.

### Test results (worktree, clean rebuild of the package after the header refactor, no incremental-build segfaults seen)
- `./sensors_ws/build.sh cube_bathymetry` then `./sensors_ws/test.sh cube_bathymetry`: **805 tests, 0 errors, 0 failures, 90 skipped** (the 90 are the cppcheck cases, cppcheck is not run here). ctest level: 39 tests, all passed, including `test_node_lifecycle` (4 cases), `test_reset_cube_nodes` (3 cases) and `test_publish_equivalence` (4 cases, one new).
- Mutation checks: negating the layer in `grid_projection.cpp` makes `ElevationIsEllipsoidHeightPositiveUp` fail; removing the quiesce loop from the script fails the consumer-ordering case.
- Lint: the first full run flagged cpplint/uncrustify on the moved header and the new test; fixed in `269dd90`, the second run is clean.

### Live verification (plan step 11)
Two real `cube_bathymetry_node` processes (`/boat1`, `/boat2`) on a private `ROS_DOMAIN_ID` with localhost-only discovery, a throwaway publisher (not committed, in the workspace scratchpad) feeding `soundings` plus static `earth`/`map`/`sensor` TF.
- The script ran from unconfigured, from active and from inactive states, with a `--consumer`, and exited 0 each time with all nodes `active`. With `draft_dir` set it refused (exit 1) and changed nothing.
- Sheet freshness: a 30 m patch was fed and a `grid` of 450 finite cells at x about 14 m was received; after the script ran, a patch 120 m east produced a `grid` of 444 cells centred at x about 134 m, 30 m wide, i.e. none of the first patch's cells survived.
- Sign check on the real node: a sounding 30 m below an ellipsoid-height-0 point gave `elevation` = -30.0, confirming positive-up ellipsoid height end to end. (A spurious +30 seen midway was a stale install left by the mutation check, not the code.)
- Observed: each `ros2` CLI call took several seconds with no discovery daemon, so a four-node reset takes on the order of a minute or more; the README does not promise a time.

### Deviations from the plan (all recorded in the plan)
- Seam is two accessors plus a `NodeOptions` constructor argument, not one accessor (catalog emptiness and pre-configure parameter overrides need them).
- `on_cleanup` also resets the ping and tile-request subscriptions and the four publishers, not only the tf listener and buffer. Side effect to know: the transient_local catalog publisher disappears during a reset, so a late joiner during the reset window gets no stale catalog.
- The script configures a never-configured node first, because `draft_dir` is only declared at configure.
- The stub test's first case uses four nodes with one already inactive (the case list is still three).

### Not done / left
- Full-scope `ci_local.sh` (owed before push, per ADR-0018); nothing pushed.
- PR (b) entirely: starts with the B1 datum/frame/pinhole spike. Facts checked now that (b) will rely on: `bizzyboat_project11/config/bizzyboat.yaml` sets `draft_dir: /home/field/data/world/depths` under `/**/cube_bathymetry` (a CUBE node launched with that file will not be fresh after a reset); the README text for the elevation sign and the d^2/2R bound is in place.
- The issue's `.agents/README.md` checklist item still needs rewording (owner's yes) before (b) closes #164.
- A `kill` of cube_bathymetry processes during live testing may have hit processes of another worktree if any were running then (a `pkill -f`/comm match, not narrowed to my domain); nodes from the sim#88 worktree were running again afterwards.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-09 14:35 -04:00
**By**: Claude Code Agent (Claude Sonnet)
**Verdict**: changes-requested

**Branch**: feature/issue-164 at `dad7c7b`
**Mode**: pre-push (PR (a) only, diff vs origin/jazzy)
**Depth**: Standard (reason: lifecycle/real-boat node code, 11 files +3394/-2166, new CMake targets and an operator script)
**Must-fix**: 4 | **Suggestions**: 12
**Round**: 1 | **Ship**: continue — four correctness findings (a TF use-after-free window on configure retry, a data-loss path on a failed flush, a script that can leave boats' CUBE nodes down after a typo, a documentation claim that is false for lake surveys); each fix is small and precise, so round 2 should be a quick confirmation.

Specialists: static (shellcheck clean, bash -n clean, ament lint inside colcon test clean, no warnings from changed files), governance, plan drift, Claude Adversarial Lens A + Lens B (sonnet), Cross-Model gemini + codex (both EXIT=0). Local model not run (--no-local). Clean rebuild of the package (build and install dirs deleted first): build OK; `colcon test`: 805 tests, 0 errors, 0 failures, 90 skipped (cppcheck cases); ctest 39 of 39 passed including test_node_lifecycle (4) and test_reset_cube_nodes. Moved class diffed against jazzy: only the intended edits. A throwaway test (removed again) showed shutdown from unconfigured/inactive/active, destruction while active and repeated cleanup do not crash, and that a configure that throws lands in UNCONFIGURED (not finalized) without on_cleanup and can be retried.

### Findings
- [x] (must-fix) on_configure assigns `tf_buffer_` (then `tf_listener_`) without first releasing a listener left over from a configure that threw after line 522 (e.g. invalid `sonar_info_topic`; node returns to UNCONFIGURED without on_cleanup, confirmed by a throwaway test). The old listener's own spin thread still references the destroyed buffer: reset `tf_listener_` before assigning `tf_buffer_` in on_configure, and add a failed-configure-then-retry test — `cube_bathymetry/src/cube_bathymetry_node.hpp:522`
- [x] (must-fix) on_cleanup ignores a failed final `saveDirtyTiles()` (it logs, keeps the dirty set, returns); cleanup still succeeds and the next configure replaces the sheet, destroying unsaved tiles. Reachable only now that reconfigure works, and also via `--allow-persistence`. Return FAILURE from on_cleanup (node stays INACTIVE) when `draft_dir_` is set and dirty grids remain; add a write-failure test (ADR-0001: never lose survey data) — `cube_bathymetry/src/cube_bathymetry_node.hpp:857`
- [x] (must-fix) reset_cube_nodes.sh checks only parameters before it starts quiescing: a mistyped `--consumer`, or a finalized/unreadable later node, is found after earlier CUBE nodes are already unconfigured (they stop publishing `grid`), and `die` prints no states or recovery hint. Preflight every node and consumer for a walkable state (active/inactive/unconfigured) before changing anything, and print states plus "re-run to continue" on any exit after step 1 — `cube_bathymetry/scripts/reset_cube_nodes.sh:135`
- [x] (must-fix) Documentation claims seabed elevation is negative; false whenever the water surface is above the ellipsoid (Lake Massabesic full pool is +48.88 m ellipsoidal, so a 10 m deep seabed is about +38.9 m). Say "below the water surface's ellipsoid height", and add a positive-height case to the test — `README.md:12`, `README.md:44`, `cube_bathymetry/test/test_publish_equivalence.cpp:335`
- [x] (suggestion) on_cleanup does not cancel `refresh_timer_` (the other timers have a guard) and its callback dereferences `sonar_tile_publisher_` unguarded; unreachable today (deactivate cancels it) but this change nulls the publisher. Cancel it in cleanup and null-check publishers in drainRefreshQueue/publishCatalog/drainDiskServeQueue — `cube_bathymetry/src/cube_bathymetry_node.hpp:857`
- [x] (suggestion) reset script configures an `unconfigured` node before the draft_dir check (loads the whole draft store, leaves the node INACTIVE) and then says "Nothing was reset"; the multi-node refusal is not atomic. Try `ros2 param get` first, configure only if unreadable, and word the message accurately — `cube_bathymetry/scripts/reset_cube_nodes.sh:136`
- [x] (suggestion) `ros2 lifecycle get/set` calls have no deadline (`timeout`), so an unresponsive node hangs the script after boats were already quiesced; `2>&1` also lets a stderr warning corrupt state parsing — `cube_bathymetry/scripts/reset_cube_nodes.sh:67`
- [x] (suggestion) `--consumer` swallows the next argument even when it is a flag (`--consumer --allow-persistence`); reject values starting with `-` — `cube_bathymetry/scripts/reset_cube_nodes.sh:51`
- [x] (suggestion) README: state that a reset with `draft_dir` empty permanently discards the in-RAM survey (nothing is saved), that it stops `grid` for the named boats between steps 1 and 3 (not for use underway), and that a bad `read_only` value (capture_spacing_scale, publish_dirty_subwindow) cannot be corrected without a restart — `README.md:84`
- [x] (suggestion) Tests: EmptyStartWithoutPersistence only has teeth in the pointer comparison; the publish-cursor/cache resets (`hpp:159-168`) are asserted nowhere; no failed-configure-retry case; read_only overrides across a cycle untested; the last loop of ElevationIsEllipsoidHeightPositiveUp restates the previous assertion and the comment overstates (it bypasses pingCallback) — `cube_bathymetry/test/test_node_lifecycle.cpp:163`, `cube_bathymetry/test/test_publish_equivalence.cpp:384`
- [x] (suggestion) Stale references to `cube_bathymetry_node.cpp` for code now in the header, one saying the class has "no test harness" — `cube_bathymetry/src/store_import.cpp:248`, `cube_bathymetry/test/test_anti_entropy_disk_serve.cpp:31`, `cube_bathymetry/test/test_import_eviction.cpp:642`
- [x] (suggestion) `ROS_DOMAIN_ID=164` is above the usual 101 ceiling (UDP port range overlap) and shared by any concurrent run of this test from another worktree; pick a value at or below 101 — `cube_bathymetry/CMakeLists.txt:396`
- [x] (suggestion) on_cleanup keeps the old sheet (up to `max_resident_tiles` tiles) and `catalog_builder_` in RAM until the next configure; reset them after the final flush — `cube_bathymetry/src/cube_bathymetry_node.hpp:857`

### Answers to the six review points
1. Pure refactor: yes. Diff of the moved class shows only the header guard, `<memory>`, the NodeOptions constructor, the two accessors, declareOnce sites, the added resets/releases, and `main()` removed. `last_grid_publish_time_ = rclcpp::Time()` equals the member's default, so no clock-type mismatch.
2. Publisher reset in cleanup: not harmful, mildly beneficial. The old behaviour (publishers replaced only at the next configure) would have kept a stale transient_local catalog alive through the unconfigured window, and the script's ordering resets a consumer exactly then; a consumer reset in that window would have re-received the old catalog. Now a late joiner (CAMP, renderer) in the window sees no catalog, and the fresh one arrives one `catalog_interval` (5 s) after activate, the same as a first start. Keep the reset; recreating on configure as before is what happens anyway.
3. Cleanup ordering and transitions: listener before buffer is correct (TransformListener default spin thread is joined in its destructor). Shutdown from unconfigured/inactive/active, destruction while active and repeated cleanup were exercised and do not crash. Holes: the configure-retry window (must-fix 1) and the unchecked flush (must-fix 2). No on_shutdown override exists (shutdown from ACTIVE skips the flush; pre-existing).
4. Script: ordering is right (all CUBE quiesced, consumer cycled, CUBE back); parsing matches Jazzy output and it verifies by re-reading state. Weak points: no preflight of states/consumers (must-fix 3), configure-first before the draft_dir refusal (also for a never-configured node), no timeouts, no recovery hint. Refusal on a set `draft_dir` works for configured nodes (stub test) and for never-configured ones after the configure side effect.
5. declareOnce: first-configure behaviour is identical (types int64_t where int was deduced, overrides applied at declaration, read_only descriptors and ranges kept). New behaviours, documented in the code and README: a runtime-set value carries into the next configure; a read_only parameter with a bad value (capture_spacing_scale) can no longer be fixed without restart (it already could not configure before).
6. Incremental build: deleted `sensors_ws/build/cube_bathymetry` and `install/cube_bathymetry`, rebuilt from scratch: success, 805/0/0/90, no segfault. The diff touches no library header or struct layout; the moved class is only in the executable and the test.

### Plan adherence
Matches plan.md PR (a) and its sync commit; only trivial drift (header guard `CUBE_BATHYMETRY_NODE_HPP_` rather than the plan's name). Process note: the Implementation entry says cube node processes were killed by name during live testing; other worktrees' nodes may have been hit.

## Implementation
**Status**: complete
**When**: 2026-10-09 15:15 -04:00
**By**: Claude Sonnet

**Branch**: feature/issue-164 at `85ecd88`
**Addressed**: Local Review (Pre-Push), Round 1, at `bbed503` (4 must-fix, 9 suggestions; all 13 addressed, none deferred)
**Commits**: ef848a4 8e5b2cc 3077280 1aa899d e8438bc b87796e 022fc85 49066dd 9860641 3125ee3 159002a 85ecd88 a0c418d (plan sync)

### Actions
- [x] (must-fix) `tf_listener_` released before `tf_buffer_` is replaced in `on_configure`; `FailedConfigureCanBeRetried` (invalid `sonar_info_topic`, retry twice, fix the parameter, reach ACTIVE) — `cube_bathymetry_node.hpp` (ef848a4)
- [x] (must-fix) `on_cleanup` returns FAILURE before releasing anything when `draft_dir` is set and dirty grids remain after the flush; `CleanupFailsWhenTheFinalFlushFails` (a regular file where the `draft/` dir belongs, so it also fails as root; retry after removing it writes the tiles and reloads them) — (8e5b2cc)
- [x] (must-fix) `reset_cube_nodes.sh` preflights every node and consumer for a walkable state before any transition, prints states plus a re-run hint on any failure after the first change (and on SIGINT/SIGTERM) — (9860641)
- [x] (must-fix) README and `ElevationIsEllipsoidHeightPositiveUp` say "below the water surface's ellipsoid height"; positive case added (surface +48.88 m, 10 m deep, seabed about +38.88 m, depth recovered as `h_surface - elevation`) — (b87796e)
- [x] (suggestion) `refresh_timer_` cancelled in cleanup; `drainRefreshQueue`, `publishCatalog`, `drainDiskServeQueue` return early on a missing publisher or sheet. Defensive and unreachable through the transitions, so no behavioural test — (3077280)
- [x] (suggestion) Refusal made atomic: `draft_dir` read first; a never-configured node is configured, read and put back only after every other check passes; message no longer claims a reset it did not make — (9860641)
- [x] (suggestion) every `ros2` call under `timeout` (`RESET_CUBE_GET_TIMEOUT` 30 s, `RESET_CUBE_SET_TIMEOUT` 300 s); parsed output is stdout only — (9860641)
- [x] (suggestion) `--consumer` rejects a missing value or one starting with `-` (exit 2) — (9860641)
- [x] (suggestion) README caveats: empty-`draft_dir` reset discards the in-RAM survey; no `grid` between quiesce and bring-up, not underway; bad `read_only` value needs a restart. Also documents the preflight, timeouts, the probe, and the refused cleanup — (3125ee3)
- [x] (suggestion) `test_node_lifecycle` strengthened: `friend struct CubeBathymetryTestAccess` dirties 11 session fields and the catalog, `ReconfigureResetsTheSessionState` checks each is fresh; `ReadOnlyOverridesSurviveACycle`; failed-configure retry; elevation test's last loop replaced and its comment corrected — (e8438bc, b87796e)
- [x] (suggestion) stale `cube_bathymetry_node.cpp` references updated in `store_import.cpp`, `test_anti_entropy_disk_serve.cpp`, `test_import_eviction.cpp`; the "no test harness" claim now says what is and is not covered — (022fc85, 159002a, 85ecd88)
- [x] (suggestion) test `ROS_DOMAIN_ID` 164 -> 87 (other tests here use 89 and 101) — `CMakeLists.txt` (49066dd)
- [x] (suggestion) `on_cleanup` releases the sheet and `catalog_builder_` after the final flush; `CleanupReleasesTheSheetAndCatalog` (weak_ptr expiry, saved tiles still reload) — (1aa899d)

### Test results
- Clean rebuild of the package (build and install dirs deleted) at `3125ee3`, then `./sensors_ws/test.sh cube_bathymetry`: first full run **810 tests, 0 errors, 2 failures, 90 skipped**; the two failures were one cpplint line_length (a comment I had rewrapped wrongly in `test_import_eviction.cpp:646`), fixed in 159002a and 85ecd88. Final run on `85ecd88` (incremental build after comment-only edits): **810 tests, 0 errors, 0 failures, 90 skipped** (the 90 are cppcheck cases). ctest level: 39 of 39, including `test_node_lifecycle` (9 cases, was 4) and `test_reset_cube_nodes` (8 cases in the stub script, was 3). `ament_cpplint` and `ament_uncrustify` clean.
- Mutation checks: removing the `have_publish_tf_` reset fails `ReconfigureResetsTheSessionState`; removing the sheet release fails `CleanupReleasesTheSheetAndCatalog`; the previous `reset_cube_nodes.sh` fails 12 assertions of the new stub script (cases 2 to 8). Not mutation-checked: the `tf_listener_` ordering (`FailedConfigureCanBeRetried` also passed on the unfixed code, as the review's throwaway test did; it pins retry works, the use-after-free window itself needs a sanitizer run) and the cleanup refusal (not mutated, the test exercises it directly).
- Live (private `ROS_DOMAIN_ID=93`, localhost discovery, real `cube_bathymetry_node` as `/boat1`): mistyped `--consumer` exits 1 in about 6 s with "nothing was changed"; from never-configured the script probes (configure, cleanup) then resets (configure, activate) and ends `active` in about 48 s; with `draft_dir` set at launch the probe finds it, puts the node back to `unconfigured` and refuses; `--allow-persistence` then proceeds to `active`.

### Notes
- A first live attempt left two of my own node processes running (killing the `ros2 run` wrapper does not stop its child), which made one run query two nodes of the same name. I confirmed by `/proc/<pid>/environ` that both carried `ROS_DOMAIN_ID=93`, killed exactly those two PIDs, and re-ran with the node binary started directly. No process outside domain 93 was touched.
- Still owed before push: full-scope `ci_local.sh` (ADR-0018); round 2 of `review-code`. Nothing pushed.
