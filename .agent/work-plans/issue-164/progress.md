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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-09 15:32 -04:00
**By**: Claude Code Agent (Claude Sonnet)
**Verdict**: changes-requested

**Branch**: feature/issue-164 at `472a725`
**Mode**: pre-push (PR (a) only, diff vs origin/jazzy; round 2 of the fixes since `bbed503`)
**Depth**: Standard (reason: lifecycle node code on real boats plus an operator script; same signals as round 1)
**Must-fix**: 1 | **Suggestions**: 7
**Round**: 2 | **Ship**: recommended — all 13 round-1 findings verified fixed in the committed code; one must-fix remains (down from 4) and it is a documentation/message correction, not a design question; apply it and the cheap suggestions, then ship.

Specialists: static (shellcheck at warning severity clean on both scripts; two SC2116 style notes in the stub test; ament lint inside colcon test clean), governance, plan drift, Claude Adversarial Lens A + Lens B (sonnet), Cross-Model gemini + codex (both EXIT=0; codex: no findings; gemini: 6 findings, 1 refuted, see below). Local model not run. Clean rebuild (build and install dirs deleted first): build OK in 1 min 52 s; `./sensors_ws/test.sh cube_bathymetry`: **810 tests, 0 errors, 0 failures, 90 skipped** (cppcheck cases), including `test_node_lifecycle` (9 cases) and `test_reset_cube_nodes` (8 stub cases).

ASan: ran. A separate ASan build of a copy of the package (scratchpad, `-fsanitize=address`, about 3 min) passes all 9 `test_node_lifecycle` cases cleanly. Mutation: deleting the `tf_listener_.reset()` added in `on_configure` does NOT fail the existing `FailedConfigureCanBeRetried`, even under ASan, because no /tf message arrives while the stale listener lives. A throwaway variant that publishes /tf at about 5 kHz from a second node during 300 failed-configure retries makes ASan report a double free in `tf2::BufferCore` (buffer replaced while the old listener's thread still fed it) on the mutant and passes 3 of 3 runs with the fix. So the fix is correct; the committed test does not pin it (suggestion below).

### Round-1 verification (all 13 fixed in the committed code)
- TF listener released before the buffer is replaced in `on_configure`: present (hpp:534); ASan-verified above. Test added but not mutation-sensitive.
- `on_cleanup` returns FAILURE before releasing anything when dirty grids remain: present (hpp:861); `CleanupFailsWhenTheFinalFlushFails` passes in the regular and ASan runs, so the node does land in INACTIVE (gemini's claim that FAILURE from cleanup finalizes the node is refuted by the test and by the earlier live run). A successful save always clears the dirty set, so there is no false refusal; priming does not mark tiles dirty, so the script's probe configure/cleanup writes nothing.
- Script preflight, timeouts, stdout-only parsing, `--consumer` validation, refusal-before-reset: present and exercised by 8 stub cases; bash 5.2 `set -u` with empty arrays is fine.
- README elevation sign wording and the positive-surface test case, `refresh_timer_` cancel and the publisher/sheet guards, `ReconfigureResetsTheSessionState` (11 fields, matches the member list), `ReadOnlyOverridesSurviveACycle`, stale `.cpp` references, domain id 87, sheet/catalog release: all present.
- New `friend struct CubeBathymetryTestAccess`: forward-declared in the header, defined only in the test; no ODR or interface widening. Single-threaded executor serializes every callback with the transitions, so no callback can see a half-reset node; `refresh_parameters_handle_` stays registered after cleanup but only touches `refresh_tracker_` and two scalars (safe).

### Findings
- [x] (must-fix) The advice to clear `draft_dir` with `ros2 param set <node> draft_dir ''` (README "What is not cleared", and the `refuse_draft` message) omits its cost and is partly wrong: the running node keeps using the `draft_dir_` it configured with, so cleanup still flushes (and can still refuse) against the OLD directory, and after the next configure the node runs with persistence off — no draft saves, and `evictColdTiles` never evicts (RAM unbounded on a long survey, warning only) — until the parameter is restored or the node restarted. State this, and prefer advising a NEW empty directory (fresh sheet, persistence kept) — `README.md:131`, `cube_bathymetry/scripts/reset_cube_nodes.sh:156`
- [x] (suggestion) `timeout` puts `ros2` in its own process group, so Ctrl-C never reaches it and bash defers the INT/TERM trap until the call returns (up to 30 s on a read, 300 s on a transition); reproduced by two reviewers. Use `timeout --foreground` in `ros2_get` and `transition` — `cube_bathymetry/scripts/reset_cube_nodes.sh:107`
- [x] (suggestion) SIGHUP/SIGPIPE are not handled: an ssh drop mid-reset kills the script between the quiesce and the bring-up, leaving boats without `grid` and nothing printed. `trap '' HUP PIPE` and a README line about running it under tmux/nohup — `cube_bathymetry/scripts/reset_cube_nodes.sh:117`
- [x] (suggestion) `changed` is set by the probe's own `transition` calls, so a failing probe prints "the reset did not finish ... Re-run" although no CUBE node was reset; conversely a re-run refused in the preflight prints "nothing was changed" while boats from the earlier partial run are still unconfigured. Save and restore `changed` around the probe, and have the preflight warn for each CUBE node that is not `active` — `cube_bathymetry/scripts/reset_cube_nodes.sh:129`
- [x] (suggestion) `FailedConfigureCanBeRetried` passes without the `tf_listener_.reset()` fix (ASan-confirmed). Add a variant that publishes /tf from a helper node while retrying a failed configure (the throwaway used 300 retries at 2 ms with a 200 µs publisher; it fails on the mutant under ASan and is quiet without ASan, so it only has teeth in a sanitizer build — say so in the comment) — `cube_bathymetry/test/test_node_lifecycle.cpp:270`
- [x] (suggestion) After a refused cleanup the save timer is cancelled and nothing retries the save; there is also no `on_shutdown`, so shutdown or SIGINT from INACTIVE or ACTIVE drops the unsaved tiles, and the comment "not lost on shutdown" overstates what cleanup does (pre-existing, now more visible). Reword the comment and add one README sentence (tiles stay in RAM until cleanup is requested again; the last resort loses them), or add `on_shutdown` calling `saveDirtyTiles()` — `cube_bathymetry/src/cube_bathymetry_node.hpp:849`
- [x] (suggestion) Test and build hygiene: case 6b puts the `draft_dir` on boat1 and the never-configured node on boat4, so an inline-probing script would also pass — swap them; SC2116 `$(echo ...)` at lines 96 and 165; the CMake comment still says the stub covers "consumer ordering, refusal, fail-fast" (it has 8 cases) and the test environment does not blank `ROS_DISCOVERY_SERVER`, `ROS_STATIC_PEERS`, `FASTRTPS_DEFAULT_PROFILES_FILE`, `CYCLONEDDS_URI` as the mru_transform test does — `cube_bathymetry/test/test_reset_cube_nodes.sh:175`, `cube_bathymetry/CMakeLists.txt:400`

### Dismissed after checking
- gemini "cleanup FAILURE finalizes the node": refuted — `CleanupFailsWhenTheFinalFlushFails` asserts INACTIVE and passes (regular and ASan); jazzy's default state machine returns a failed cleanup to inactive.
- gemini "no null guard in `drainDiskServeQueue`": already added in 3077280. "`refresh_parameters_handle_` leaks past cleanup": safe, see above. "ros2 stdout banners break `string_param`": speculative (warnings go to stderr, and the script reads stdout only). "Initial catalog on activate": same behaviour as a first start. "Reset activates nodes that were inactive": documented behaviour ("configures and activates every CUBE node"). "Underway interlock": the README says not to run it underway; a flag is a design addition, not a defect.

### Plan adherence and governance
Plan and code agree; the plan's sync commit `a0c418d` records every round-1 change. ADR-0001 (never lose survey data) is honoured by the refusal path; the remaining gap is the unflushed shutdown above, pre-existing. Principles: no concerns beyond the must-fix (documentation consequence of an advised action). Doc impact: no new parameters; README updated. Gap noted: the repo has no `.agents/README.md` (the issue's own checklist item, deferred to PR (b)). Still owed before push: full-scope `ci_local.sh` (ADR-0018). Nothing pushed.


## Implementation
**Status**: complete
**When**: 2026-10-09 16:20 -04:00
**By**: Claude Sonnet

**Branch**: feature/issue-164 at `eb9f515`
**Addressed**: Local Review (Pre-Push), Round 2, 2026-10-09 15:32 -04:00 at `472a725` (1 must-fix, 7 suggestions; all 8 addressed, none deferred)
**Commits**: 0ee9eb7 cfc7566 b67d3c0 1210351 3d447cd d6e8d21 (plan sync 8f9f4ae, box ticks eb9f515; `.agents` ci_local hook, see Notes)

### Actions
- [x] (must-fix) README and the `refuse_draft` message no longer advise `draft_dir ''`; they say an empty `draft_dir` means no persistence and no eviction (RAM grows on a long survey) and that cleanup flushes against the directory the node was configured with, and advise a NEW empty directory plus `--allow-persistence` (the preflight reads the new value, so the flag is still needed; the old sheet flushes to the old directory, the new one reloads nothing) — `README.md`, `reset_cube_nodes.sh` (0ee9eb7)
- [x] (suggestion) `timeout --foreground` in `ros2_get` and `transition`; a stub `timeout` on PATH records every call and the test requires `--foreground` on all 72 — `reset_cube_nodes.sh` (cfc7566)
- [x] (suggestion) `trap '' HUP PIPE` plus a README line about tmux/nohup; tests send SIGHUP mid-reset and close the stdout pipe, both must still end all four nodes `active`; the final `print_states` returns 0 so a closed stdout does not become the exit status (found by the test: exit 1 before) — (cfc7566)
- [x] (suggestion) a failing probe is no longer reported as a failed reset (a `probing` flag instead of `changed`; the message says no CUBE node was reset and names the probed node, which may be left `inactive`); the preflight warns for every CUBE node that is not `active` — (cfc7566)
- [x] (suggestion) `FailedConfigureCanBeRetriedWhileTfIsPublished`: 300 failed configures at 2 ms with a helper node publishing `/tf` every 200 us; the comment says it only has teeth under ASan — `test_node_lifecycle.cpp` (3d447cd)
- [x] (suggestion) did the `on_shutdown` flush (small, testable): `on_shutdown` calls `saveDirtyTiles()`, logs how many tiles it could not write and still completes (a shutdown cannot be refused); the "not lost on shutdown" comment now says what cleanup does and does not cover; README says tiles stay in RAM while a cleanup is refused and that a killed process loses what the periodic save has not written; `ShutdownFromActiveFlushesUnsavedTiles`, `ShutdownCompletesWhenTheFlushFails` — `cube_bathymetry_node.hpp`, `README.md`, `test_node_lifecycle.cpp` (1210351)
- [x] (suggestion) case 6b now has the never-configured node FIRST and the `draft_dir` on the LAST node (an inline-probing script fails it); SC2116 fixed at both lines; the CMake comment lists the real case groups; both tests blank `ROS_DISCOVERY_SERVER`, `ROS_STATIC_PEERS`, `FASTRTPS_DEFAULT_PROFILES_FILE`, `CYCLONEDDS_URI` (one shared `CUBE_ISOLATED_TEST_ENV`, kept under the 140-column cmake lint) — `test_reset_cube_nodes.sh` (cfc7566), `CMakeLists.txt` (b67d3c0, d6e8d21)

### Test results
- Clean rebuild (build and install dirs for the package deleted), then `./sensors_ws/test.sh cube_bathymetry` at `d6e8d21`: **813 tests, 0 errors, 0 failures, 90 skipped** (the 90 are cppcheck cases); ctest 39 of 39. `test_node_lifecycle` has 12 cases (was 9), `test_reset_cube_nodes` 10 case groups (was 8). An earlier run with the CMake lines at 150 columns failed `lint_cmake` (2 failures); fixed in d6e8d21. shellcheck clean on both scripts.
- Mutation checks: dropping `--foreground` fails case 9a; dropping `trap '' HUP PIPE` fails 9b and 9c (exit 129 and 141). ASan build of a copy of the package (scratchpad), `test_node_lifecycle`: all 12 cases pass with the fix; with the `tf_listener_.reset()` line commented out, `FailedConfigureCanBeRetriedWhileTfIsPublished` fails with AddressSanitizer heap-use-after-free in 3 of 3 runs (it passes without ASan, as the comment says). Mutant reverted.
- Full-scope `ci_local.sh` (ADR-0018): the first two runs failed before reaching this package, building `unh_marine_autonomy` packages that hosted CI prunes (`mission_manager*`, the integration tests, `marine_web_view`, via `COLCON_IGNORE` in `AFTER_SETUP_UPSTREAM_WORKSPACE`). This repo had no `ci_local` equivalent, so I added `.agents/ci_local_upstream_extra.sh` with the same prune. I did not add `.agents/ci_local_rosdep_skip_keys.txt` (the hosted `ROSDEP_SKIP_KEYS` analog): with three keys, `ci_local.sh`'s unquoted heredoc drops the quotes around `--skip-keys "a b c"` and rosdep reads `b` as a path ("given path 'marine_nav_tasks' does not exist"); verified with a bash one-liner. That is a workspace `ci_local.sh` bug for the host to decide on, not fixed here. With the prune hook alone: 813 tests, 0 errors, 0 failures, 96 skipped (6 more than the local run's 90; 5 of them are the real-bag smoke test, which the run reports as SKIPPED, not a pass: no bag at `/mnt/nadata/...`, so the depth-adaptive `main()` path was not exercised by this run). The attestation is in `git notes --ref=ci-local show HEAD`; the host hand-back has the final SHA. Nothing pushed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-12 09:59 -04:00
**By**: Claude Code Agent (Claude Opus 5.5)
**Verdict**: approved

**Branch**: feature/issue-164 at `6889634`
**Mode**: pre-push, scoped to `0ab490e..HEAD` (the stop-path work after the third Copilot pass; everything before it passed earlier rounds)
**Depth**: Standard (reason: lifecycle and signal handling in a core node, ~250 lines, C++ + shell)
**Must-fix**: 2 (both fixed) | **Suggestions**: 9 (8 fixed, 1 carried out of scope)
**Round**: 3 | **Ship**: recommended — every in-scope finding fixed; the remaining one belongs to another repo

Specialists: static (ament_uncrustify, ament_cpplint clean; shellcheck not installed on this host), governance (cube ADR-0001 honoured and strengthened), plan drift (step 13 added), Claude Lens A + Lens B (fresh Sonnet sub-agents; Lens B built a throwaway LifecycleNode to check SIGINT, second SIGINT, logging after shutdown and the Jazzy destructor), cross-model gemini + codex (both EXIT=0, reviewed the three commits before the fixes below).

Context for the work (2026-10-12): live checks on the PR build, `.agent/scratchpad/cube164_live/shutdown_probe.sh` in the workspace, showed SIGINT exiting with unsaved tiles and writing nothing, and a FINALIZED node still publishing its catalog. Both now pass live: SIGINT writes the tile and logs "Stopping: saved the changes...", and after `shutdown` no catalog arrives in 12 s (3 in the active control window). Tests: 821, 0 failures, 95 skipped; each new node test was checked failing against a mutant without its fix, and case 11 against the old script.

### Findings
- [x] (must-fix) reset script passed the typed spelling to ros2 after normalising only for the duplicate check (Gemini) — `cube_bathymetry/scripts/reset_cube_nodes.sh`
- [x] (must-fix) sub-nanosecond interval still gave a zero timer period; huge one made create_wall_timer throw (Codex, Lens A) — `cube_bathymetry/src/cube_bathymetry_node.hpp` isUsableTimerInterval
- [x] (suggestion) flush skipped when a callback throws out of spin() (Lens B, Gemini) — spinUntilStopped
- [x] (suggestion) repeated or multiple trailing slashes bypassed the duplicate check (Lens A, Gemini) — qualified()
- [x] (suggestion) NaN / inf intervals untested (Lens A, Gemini) — TimerIntervalFallback
- [x] (suggestion) rejected interval left in the parameter store (Gemini) — usableIntervalOrDefault writes the default back
- [x] (suggestion) case 11c had no no-change assertion (Gemini) — test_reset_cube_nodes.sh
- [x] (suggestion) stop log overcounted written tiles (Lens A) — flushForStop wording
- [x] (suggestion) single-threaded executor is load-bearing for the no-race flush, undocumented (Lens B) — signature now takes SingleThreadedExecutor, comment added
- [ ] (suggestion, OUT OF SCOPE) tile writes are not atomic, and one unreadable tile makes the configure-time load fail whole, after which saves overwrite earlier sessions' data in good tiles (Lens B; reproduced live, `.agent/scratchpad/cube164_live/corrupt_tile_probe.sh`: patch A's 260 cells gone after a session-2 write) — marine_tiled_raster_store saveTile (unh_marine_autonomy) + this node's load fallback; raised with the owner, needs its own issue

## Implementation

### Spike (B1)

Throwaway scripts (not committed): `.agent/scratchpad/cube164b_spike/`. Run 2026-10-12 on the four-boat `demo_boats_launch.py enable_joysticks:=false`, ROS_DOMAIN_ID=61, driven with fake `sensor_msgs/Joy` on `<boat>/joy` (axes[1]=throttle, axes[3]=rudder; beluga left stationary). Grid topic is `<boat>/sensors/mbes/grid` (layers `elevation`, `uncertainty`). Two recordings of 60 s each, 11-12 grid messages per boat (slow 35 % throttle, then full speed).

#### B0: merged #406 messages vs plan text (marine_interfaces on jazzy)
- `CoverageAssessment`: header, `area` (SurveyArea), `cell_size`, `area_total`, `area_covered`, `area_meeting_standard` (plan prose and brief say "meets"; actual name `area_meeting_standard`), `sources[]` of `CoverageContribution{source, area_covered, area_meeting_standard, area_unique}`. All m^2, doubles. Per-source credit including `area_unique` is present.
- `SurveyArea`: `id`, `stamp` (builtin_interfaces/Time, present), `polygon` (GeoPolygon, vertices in `polygon.points`, geographic_msgs/GeoPoint), `standard` (SurveyStandard{name,a,b}), `priority` (int16, not in plan). The polygon field is `polygon`, vertices at `area.polygon.points`.
- Plan CONTRADICTED by message text (wrapper must follow the messages):
  1. Covered = finite elevation (plan) vs finite **depth >= 0** (message). A cell above the surface is not covered. The plan's "negative depth evaluated at 0, counted and warned" (covered, evaluated at 0) conflicts: per #406 such a cell is uncovered. The warn count can stay as a diagnostic.
  2. Competing sources for the standard: plan "smallest finite uncertainty across sources", message "only sources with finite depth >= 0 AND finite uncertainty >= 0 compete" (negative/infinite/nodata-sentinel excluded); ties go to the **shallower depth** (fail closed), plan says earlier configured source.
  3. Covered but no competing source: does not meet (same as plan); but a per-source meeting needs that source's own finite depth >= 0 and uncertainty >= 0.
  4. Standard fails closed: a=b=0, or a/b NaN/negative/infinite, means "not set", nothing meets (checked first). Plan only validates `SurveyArea` a/b at registry (reject negative/non-finite) and does not mention the all-zero default.
  5. Frames: message says if ANY source frame has no georeference the whole assessment for that area fails (no CoverageAssessment, no coverage_grid for it); plan says a message without a transform is skipped with a WARN and the other sources are still scored. Reconcile (the plan's skip-and-count is per message from a source that has had transforms; a source never resolvable needs the "publish nothing" rule).
  6. `sources[].source` = platform name if known else grid topic, unique within a message, stable order. Plan `source_names` falls back to topic: consistent.
  7. `header.stamp` = assessment time; `cell_size` is a message field (plan has the parameter); area_total can be 0.
- Invariants to test in B5: meeting <= covered <= total; per-source unique <= covered; sum of unique <= assessment covered; each source covered <= assessment covered.

#### B1(a): h_surface - elevation
`h_surface` = geodetic altitude (pyproj EPSG:4978 -> 4979) of the origin of `earth <- <boat>/map_tide`; `<boat>/map` origin altitude comes out -0.000 (altitude forced 0, as the plan says).
- h_surface: -28.29..-28.36 m (the sim's tide, drifts ~0.05 m/min at tide_speed_factor 10). It is NOT the 48.88 m of bizzyboat.yaml (that is the Massabesic config); this is Portsmouth. The four boats' map_tide altitudes agree within 0.01 m.
- Elevation of finite cells: -41 to -47.6 m (negative, ellipsoid height, below the surface at -28.3).
- `h_surface - elevation` is POSITIVE everywhere: slow run last message min/p5/median/p95/max, bluefin 12.8/13.9/15.2/16.9/17.9, clownfish 12.7/15.0/16.4/17.6/19.3, urchin 13.0/14.1/15.5/17.2/18.9, beluga 14.6/15.9/16.7/17.2/17.6 m. Full-speed run (up to 33k cells per message): range 12.4..23.4 m. Cells with depth < 0: 0; < -1 m: 0 (all sources, both runs).
- Against the ground-truth chart grid (US5NH02M.tiff via mbes_sim.bathy_grid, positive down) at the same cells (cell centre -> map -> earth -> lat/lon): (h_surface-elevation) minus chart depth has median +0.13..+0.19 m, p5..p95 about -0.7..+1.0 m; correlation 0.57-0.99 (lowest for beluga, which sits on a flat 1.5k-cell patch). So the sign is confirmed and the surface height is right to ~0.2 m (tide not removed from the chart, so the offset is not purely error).
- Boat distance from its own map origin reached 247-323 m, so d^2/2R is about 5-8 mm here, consistent with the plan bound.

#### B1(b): frames
- Every grid `header.frame_id` = `<boat>/map` (bluefin/map, clownfish/map, urchin/map, beluga/map). `outer_start_index`/`inner_start_index` are 0 in every message. Layout: dim0 `column_index`, dim1 `row_index`, column-major; the grid centre is `info.pose.position`, cell (i,j) centre x = px + lx/2 - (i+0.5)*res.
- `lookupTransform` among the four `<boat>/map` frames and `<boat>/map <- earth`: 20 of 20 succeed (latest time). Origins are 40.05, 80.14 and 120.22 m east of bluefin/map, relative rotation 0.0003..0.001 degrees, up offset < 1 mm. `earth` is not visible in the TF buffer for the first ~10 s after launch.

#### B1(c): pinholes and the union
- Pinhole share (NaN cell with finite neighbours on both sides in x or y, over finite+pinhole), per message: slow run 0.0-0.2 %; full speed 0.1-0.4 %. Looser definition (NaN with >=5 of 8 neighbours finite): 0.1-0.5 %. Real but tiny; no fill needed at this scale.
- Stationary boat (beluga): the message geometry is identical every time (59x58, edge phase x 0.6265 y 0.4743 of the 1 m cell, in every one of 12 messages), so the same lattice repeats; its pinhole share is 0-0.4 %. Moving boats change phase every message (bluefin edge x phase 0.46..0.95).
- Direct count (union of the finite 1 m cells painted on a 0.25 m lattice, in bluefin/map) vs pull-resampled latched 1 m union (my throwaway Python, lattice anchored in bluefin/map, every source taken through the real earth transforms), area in m^2 direct / pull: slow run bluefin 13362/13430, clownfish 14303/14280, urchin 13462/13491, beluga 1554/1554; full speed 31964/32012, 34093/33921, 33220/33220, 2691/2741. Ratio 0.995..1.019. Single message: pulled count equals the finite count exactly in every sampled message (ratio 1.000).
- Caveat: the 200 m window never rolled during these runs (the boats stayed within ~330 m of start; each message contained essentially the whole survey), so the latched union was not exercised by a moving window here; the plan's B4 moving-window unit test remains the check.
- Also seen: beluga (not driven) grew from 1554 to 2690 finite cells between runs; not investigated (its Joy axes were zero in both runs).

#### Decisions touched by B1
- Depth sign (`h_surface - elevation` positive down): CONFIRMED (0 negative cells; matches chart depth to ~0.2 m median).
- Water surface read from `earth <- <ns>/map_tide` origin altitude: CONFIRMED (-28.3 m here, boats agree within 1 cm; one tide boat is enough in this sim).
- Frame handling (per-source `<ns>/map`, one transform per message through earth): CONFIRMED (frame_ids are as predicted, all lookups work, relative rotations negligible, origins 40 m apart so the transform matters).
- Pull resampling and union: CONFIRMED (within 2 % of a direct fine-lattice union).
- Pinhole fill needed: CONTRADICTED for this sim (0.1-0.4 % at most): not needed for the event; a stationary boat repeats the same phase as predicted, but the share is too small to matter.
- Covered = finite elevation, negative depth evaluated at 0: CONTRADICTED by #406 text (covered needs depth >= 0); see B0 items 1-2 for the plan edits before B2.

## Implementation

### PR (b) steps B2-B6: the coverage_assessment core library (2026-10-12 11:50 -04:00)

Built `coverage_assessment/` (plain C++ over grid_map_core + std, no ROS, no message types, no TF), five commits on feature/issue-164: skeleton + CI image plumbing; polygon_raster; source_accumulator; coverage_assessor + area_registry; assessment_raster.

- **Skeleton and CI (B2):** package.xml lists the full dependency set the plan names (grid_map_core/ros/msgs, rclcpp, rclcpp_lifecycle, tf2 family, marine_autonomy, marine_interfaces) so the CI image bakes them; only `grid_map_core` is found by CMake so far. Dockerfile: COPY of the new package.xml and `coverage_assessment` added to BOTH `--from-paths` (rosdep check and install); `ci-image.yml` path trigger added. Lint setup as cube_bathymetry (ament_lint_auto, CPPLINT.cfg copied).
- **polygon_raster (B3):** scanline even-odd fill at cell-centre rows; centre on a lower/left edge is in, on an upper/right edge is out, so a w-metre rectangle holds w/cell_size columns at any lattice offset and polygons sharing an edge partition the cells. Rejects <3 distinct vertices, non-finite input, zero area, bounding box over `max_cells` (counted in double before any int conversion). A polygon smaller than a cell is valid with 0 cells. 14 tests.
- **source_accumulator (B4):** per-source lattice in the raster frame, 64x64 paged blocks of (elevation, uncertainty) floats, pull resampling through a horizontal `Affine2` (raster -> source), never-erases per field. `add()` returns a status (missing elevation layer, bad geometry incl. singular/non-finite affine or footprint > 1e8 cells) and whether the uncertainty layer exists. 17 tests incl. phase shift, 90 degree rotation+translation cell-by-cell, 30 degree rotation, moving window (union 400 -> 700 -> 1000, never decreasing, all-NaN window erases nothing), per-field never-erases, clear.
- **coverage_assessor + area_registry (B5):** exactly the merged message rules (covered = finite depth >= 0; competing = finite depth >= 0 and finite uncertainty >= 0; smallest uncertainty supplies the depth, shallower on a tie, in either source order; not-set standard checked first; per-source judged on its own grid; area_unique). `assessArea` also returns a per-cell verdict array (outside / not covered / below / meets) used by the raster, and `cells_above_surface` = cells inside the polygon where some source has depth < -1 m (strict). It throws `std::invalid_argument` for a non-finite `h_surface`, a null source or a source on another cell size (caller bugs; scoring zero would read as "uncovered"). Registry: replace-by-id on stamp >= held (equal stamp: last wins), stale ignored, invalid keeps the old definition and does not add a new id; a = b = 0 is ACCEPTED (published as "not set"); NaN/inf/negative a or b rejected. 25 tests (assessor + registry + standard), all exact numbers; a mutation check (tie rule flipped, standard-set check dropped) made the tests fail.
- **assessment_raster (B6):** `buildCoverageGrid` -> layer `meets_standard` over the bounding box of all area masks, centred on the lattice; NaN not covered/outside, 0 below, 1 meets, overlap = 1 only if every containing area is met; fails with a reason when nothing to draw or the union box exceeds `max_cells`. 6 tests.

**Test totals** (`./sensors_ws/test.sh coverage_assessment`): `Summary: 134 tests, 0 errors, 0 failures, 15 skipped`. That is 62 gtest cases (14 + 17 + 25 + 6) plus ament linters; the 15 skipped are cppcheck (skips itself in this environment). uncrustify, cpplint, copyright, lint_cmake, xmllint, flake8-free all pass.

**Plan deviations:** none to plan.md (not edited). Two small things worth knowing: (1) the `coverage_assessment_core` CMake target arrived with the first component (B3) rather than in the B2 skeleton commit, because an empty library cannot be declared; (2) `AreaAssessment.cells_above_surface` counts only cells inside the polygon (a count outside the area would not describe a verdict).

**Placement state stays in the wrapper (B7), by the plan's split:** "no grid yet -> scores zero" is already what an empty `SourceAccumulator` does in `assessArea` (its slot is kept, zeros). "Latest grid could not be placed -> withhold every area's assessment and coverage_grid" needs TF knowledge, so the wrapper keeps a per-source flag and simply does not call `assessAll` / does not publish while any flag is set.

**Notes for B7:**
- Core structs: `AreaDefinition{id, stamp_ns, polygon (raster metres), standard}` fed to `AreaRegistry(cell_size, max_cells).update()`; re-project and call `update` again with the same stamp when the earth->map transform changes (equal stamp replaces). Read `RegistryResult.reason` for the ERROR log. `RegisteredArea` caches the mask.
- `SourceAccumulator::add(msg, raster_to_source)`: the affine must map RASTER metres into the MESSAGE frame (horizontal only); compose it from `lookupTransform(message_frame <- map_frame)`. Warn once per source when `has_uncertainty_layer` is false or `cell_size != message.getResolution()` (the accumulator does not check resolution).
- `assessAll(registry, sources, h_surface)`: sources must be in `grid_topics` order; every accumulator must have the registry's cell size; h_surface must be finite (wrapper refuses stale/too-far tide first).
- `buildCoverageGrid(assessed, cell_size, map_frame, max_cells)`: the result's GridMap goes through `grid_map::GridMapRosConverter`; it carries only `meets_standard`.
- Message fields: `area_meeting_standard` (not "meets"); `CoverageContribution.area_unique`; `header.stamp` is assessment time. `cells_above_surface` is the count to log (throttled) with the surface height used.
- Unit numbers are all doubles m^2 = cells x cell_size^2.
- Core headers are `.h` under `include/coverage_assessment/` (installed); test fixtures in `test/message_fixtures.h`. Note ament_uncrustify here indents namespace contents (that is what cube_bathymetry's files look like too) and cpplint wants two spaces before trailing comments, so member docs go on the line above.

## Implementation

### PR (b) steps B7-B9: the ROS wrapper, its tests, the docs (2026-10-12 12:23 -04:00)

Built on the B2-B6 core (unchanged). Seven commits on feature/issue-164, none pushed:

- `5896ca7` lifecycle node (`src/coverage_assessment_node.hpp`, thin `main`), `src/frame_chain.hpp`, launch file, example YAML, install rules, exec depends
- `94fc044` `test_frame_chain` (B8 i)
- `ef40e62` per-source warning throttles (found by the withholding test)
- `2c97c16` `test_coverage_assessment_node` (B8 ii-v + refusals + read_only + resolution warning)
- `48f1524` status line carries skip reasons and above-surface count
- `99ab44d` test race fix (startup)
- `43aac99` docs: `coverage_assessment/README.md`, top-level Nodes row + parameter section, reset text

**What was built.** Lifecycle node `coverage_assessment`: all 11 parameters `read_only`, declared once in the constructor (so no re-declare on a second configure; a wrong-typed override fails at construction, loudly, rather than being swallowed in a transition). Configure fails with a reason on empty/empty-entry/duplicate `grid_topics`, a `source_names` of a different length, duplicate resolved names, and non-usable numeric parameters; whatever was created before a failure is released. Placement state in the wrapper as the plan says (no grid yet = zero; latest grid unplaced = flag, withhold everything with a WARN naming every unplaced source and frame, resume when placed). Water surface = altitude of the origin of `earth <- water_surface_frame`; refusals: stale (static transforms, stamp 0, have no age), unavailable, boat beyond the bound (distance = horizontal distance of `water_surface_boat_frame` from the origin of the tide frame's TF parent via `BufferCore::_getParent`; fail closed when parent or boat frame is missing). Outputs, QoS and frame as the plan's Output decision. Release order: timer, subs, publishers, TF listener, TF buffer, sources, registry, areas. Counters (`stats()`) back the tests and the 10 s status line.

**Tests.** `./sensors_ws/test.sh coverage_assessment`: `Summary: 173 tests, 0 errors, 0 failures, 20 skipped` (cppcheck cases skip themselves; uncrustify, cpplint, copyright, lint_cmake, xmllint, flake8, pep257 pass). `./sensors_ws/test.sh cube_bathymetry`: `Summary: 994 tests, 0 errors, 0 failures, 115 skipped`. New gtest cases: 3 in `test_frame_chain`, 9 in `test_coverage_assessment_node` (own ROS_DOMAIN_ID 86, localhost-only). Exact numbers in the node test: area map [5,25]x[0,10] = 200 cells, standard a=0.5; source A (frame a/map, 10 m east) covers 150 / meets 50 / unique 100; source B (frame b/map, moved (30,5) and yawed 90 degrees) covers 100 / meets 100 / unique 50; union covered 200, meeting 150; coverage_grid 20x10 with 150 ones, 50 zeros, 0 NaN. Mutation checks (not committed): not withholding on an unplaced source, using an identity source transform, and skipping the age check each make the matching tests fail. The node test was run 8 times in a row after the last fix with no failure.

**Deviations from the plan, and why.**
1. `coverage_assessment` publisher depth is a fixed 100 (the same as `survey_area`), not "number of areas held": a publisher's depth cannot follow a changing count without recreating the publisher. A late consumer therefore receives up to the last 100 assessments, oldest first, possibly with older repeats of an area before the newest; documented in the README.
2. `test_frame_chain` needs the built cube_bathymetry package: `package.xml` has `test_depend cube_bathymetry`, CMake uses `find_package(cube_bathymetry QUIET)` and, if it is missing, warns and registers a FAILING test (never a silent skip). `./sensors_ws/build.sh coverage_assessment` alone in a fresh worktree therefore needs `cube_bathymetry` built first (colcon `--packages-up-to` does it). Hosted CI builds the repo so the order is automatic; `.github/ci/Dockerfile` already carries both packages.
3. Parameters are declared in the constructor (plan: "declared in the constructor"), not through `declareOnce` in configure as in `cube_bathymetry`; same effect, one declaration.
4. Placement flag: only "transform unavailable" sets it. A grid with no elevation layer, unusable geometry or that cannot be converted is dropped with a throttled ERROR and counted, but does not withhold (the earlier union is still valid).
5. Timer-WARN throttles are per source for per-message warnings; the withholding WARN is one call-site throttle (10 s) listing all unplaced sources.

**For B10's live sim check.**
- Launch with `config/coverage_assessment.yaml`: its topics (`/<boat>/sensors/mbes/grid`), frames (`bluefin/map`, `bluefin/map_tide`, `bluefin/base_link`) and boat names come from the B1 spike and the YAML header, NOT verified in this step. Check `ros2 topic list`, that `bluefin/base_link` exists (otherwise the node withholds with "cannot measure the distance"), and that `survey_area_topic` matches what the scorer publishes.
- `earth` absent for ~10 s: expect the "not assessing" WARNs and no outputs for that long, then outputs.
- Depth sign and plausibility across the four boats; the status line (every 10 s) must show `0 cell(s) above the surface`.
- With all four boats driving: union cells and MB in the status line; time one assessment pass at event scale (not measured here; the `assessAll` cost is the per-area mask times sources).
- Reset: `reset_cube_nodes.sh --consumer /coverage_assessment ...` then confirm areas return from the latched topic and coverage restarts at 0.
- Late joiner: start the scoreboard after a few assessments and confirm it gets every area.
- `ros2 launch` was smoke-tested for configure+activate only (domain 63, no sim); one SIGINT to `ros2 launch` did not stop it within 4 s, a second one did (not investigated; the node itself exits cleanly on SIGINT).
- Not yet done in this PR: full-scope `ci_local.sh`, `git diff -- upstream.repos` check, the `.agents/README.md` checklist item, and marking the PR ready only after #406 is on `jazzy`.

## Implementation

### B10 live sim check (2026-10-12 13:00 -04:00)

Report-only run of the `coverage_assessment` node against the four-boat sim (`demo_boats_launch.py enable_joysticks:=false`, ROS_DOMAIN_ID=64, default discovery), boats driven with fake Joy by the B1 spike's `drive.py`. Scratch: `.agent/scratchpad/cube164b_b10/` (not committed). No code changed.

1. **Names match.** Example YAML topics (`/<boat>/sensors/mbes/grid`) and frames (`bluefin/map`, `bluefin/map_tide`, `bluefin/base_link`, `earth` -> `<boat>/map`) all exist; no corrected params needed. CUBE nodes are `/<boat>/sensors/mbes/cube_bathymetry`. PASS.
2. **Assessments.** One area (500 x 500 m ENU box round the bluefin/map origin, a=0.5 b=0.013) published latched, bluefin, clownfish, urchin driven ~4 min. 164 messages: meeting <= covered <= total, per-source meeting <= covered, unique <= covered, sum of unique <= covered held in every one; union covered never decreased. area_total 250500 constant. Covered grew 6084 -> 17554 (t+67 s) -> 41711 (t+142 s) -> 52120 (end); meeting 3076 -> 44025. Final per source (covered/meeting/unique): bluefin 20757/18532/16806, clownfish 22628/18894/17462, urchin 16674/10106/8107, beluga (not driven) 1650/378/1178. Status lines: "0 cell(s) above the surface" in all 124 lines. PASS.
3. **Startup** (fresh sim, assessor launched 1.5 s later, areas already latched): configured at +5.4 s after sim start, one "Not scoring: water surface unavailable" WARN at +7.4 s (throttled to one per 10 s, so one line, not a stream), 6 ticks skipped, first assessment at +12.8 s, no restart. PASS.
4. **coverage_grid**: frame bluefin/map, 501 x 500 (250500 cells = area_total), layer meets_standard only; NaN 198380 / 0: 8095 / 1: 44025, no other values; 8095+44025 = covered 52120 and 44025 = meeting. PASS.
5. **Timing** (gdb breakpoint timing of the real node, 40 s, four boats publishing, 250k-cell area): assessAll mean 153 ms (p95 168, max 176), assessAndPublish mean 171 ms (p95 185, max 192), onGrid mean 56 ms (p95 132). Node CPU (top, 5 s samples, 45 samples): mean ~10 % of one core, max 16.6 %; RSS 104 -> 110 MB over 8 min; 119k cells held, 2.7 MB. perf is unavailable (perf_event_paranoid=4). PASS (well under the 1 s period).
6. **Reset** with the standalone executable (`ros2 run`, manual configure/activate): script reset all four CUBE nodes and the consumer; coverage 19121 -> 0 in the next assessment, area_total stayed 250500 (area came back from the latched topic), grew again from 0. PASS. **DEFECT with the launch file, see below.**
7. **Late subscriber** (two areas A, B): a fresh transient_local subscriber got a burst of exactly 100 messages (50 + 50), oldest first, the newest of each area last; so older repeats arrive first, as the README says. PASS.
8. **SIGINT**: not reproduced. `ros2 launch` exit after SIGINT: 0.31-1.83 s over 22 trials (kill -INT to the launch pid, and Ctrl-C through a pty at 1..20 s after start, with and without a sim in the domain); the executable alone: 0.15 s. The only slowness seen is a SIGINT during startup, which prints "Abandoning wait for the change_state service response" (1.8 s). NOT REPRODUCED.

### Defects
- **D1 (real): `reset_cube_nodes.sh --consumer /coverage_assessment` fails when the assessor was started by `coverage_assessment_launch.py`.** The launch's `OnStateTransition(goal_state='inactive')` handler re-activates the node on EVERY transition to inactive, including the script's deactivate. Result: script aborts "transition 'deactivate' failed (state is 'active', expected 'inactive')" with all four CUBE nodes left `unconfigured` (boats publish no grid until the script is re-run, which fails the same way), and the assessor kept its old union. Reproduce: launch sim + `ros2 launch coverage_assessment coverage_assessment_launch.py`, then run the script with `--consumer /coverage_assessment` and the four cube nodes. Launch log shows a second "coverage_assessment configured, activating" at the deactivate. Fix idea: activate only on the first inactive (handler removes itself / matches previous_state 'configuring'), or document that the reset needs a node started without the launch handler.
- D2 (minor, not a defect in the node): reset_cube_nodes.sh with 4 nodes + consumer took ~3 min here (each `ros2 lifecycle` CLI call 3-8 s).

## Implementation

Address-findings round. Review findings for PR (b), the `coverage_assessment` package, all 16 items fixed in this PR (owner-approved; item 7 map-origin policy and item 14 `max_areas` are owner decisions). Nothing pushed.

| # | Item | Result | Commit | Test evidence |
|---|---|---|---|---|
| 1 | Default build type | done: RelWithDebInfo block copied from cube_bathymetry | 69a3928 | build log shows the default message |
| 2 | Exception containment | done: guarded() on grid/area/timer callbacks (std::exception and `...`), throttled ERROR, `callback_exceptions` in the status line; main() try/catch logs FATAL, returns 1. Test seam `setFaultInjectorForTest` | 04c62dd | node test ExceptionsInCallbacksAreContained... throws std::runtime_error from grid and timer, `int` from survey_area |
| 3 | Launch re-activation | done: OnStateTransition start_state='configuring' | 7dc385d | new launch_testing test: fails (3 != 2) on the old handler, passes now |
| 4 | Unset standard | done: registry no longer rejects, node no longer rejects, README fixed | fe47736, ad4f197 | registry test with NaN/neg/inf; node test AnUnsetStandardIsScored... (areas_rejected 0, meeting 0) |
| 5 | Area arrival ordering | done: arrival validation, per-id pending queue in stamp order, last accepted + stamp kept | ad4f197 | node test ARejectedNewerArea... (4 messages before any TF) |
| 6 | Numeric bounds | done: 1e7 m coordinate and 1e12 lattice-index bounds in polygon_raster and the accumulator, new statuses with reasons; footprint cap 4e6 with WARN | ed2dfa9, e87aaf2 | boundary tests (at/just over bound, 1e16, 1e300, tiny cell size, cap 3.996e6 vs 4.002e6); node test with a far map frame |
| 7 | Map-origin lock | done: georeference captured at first lookup, used for polygons and placement, tick compares (1 cm / 1e-6 rad), ERROR + withhold until reconfigure | e87aaf2 | node tests MapFrameMovingAfterTheCapture... (moved 5 m, withheld counter, reconfigure recaptures, overlap 50 m^2) and a 5 mm non-movement test |
| 8 | Negative uncertainty | done: u_ok requires u >= 0 | ed2dfa9 | accumulator test |
| 9 | Boundary in float | done: SurveyStandard::meets rounds allowed to float | fe47736 | assessor test a=0.1, u=0.1f meets, nextafter fails |
| 10 | Placement retry | done: per-tick retry; the kept unplaced grid is folded in (deviation: not only the flag clears) | e87aaf2 | node test SilentUnplacedSourceIsRetried... |
| 11 | Boat transform age | done | 9369eb1 | node test StaleTideBoatTransformSkipsScoringToo |
| 12 | Tide frame under earth | done: explicit ERROR, own counter | 9369eb1 | node test TideFrameDirectlyUnderEarth... |
| 13 | Incompatible QoS | done: ERROR naming the topic on survey_area and grid subscriptions | 04c62dd | node test IncompatibleQosPublishers... |
| 14 | max_areas | done: read_only, default 20, 1..100 at configure, new id beyond it refused | ad4f197 | node tests MaxAreasRefuses..., ConfigureRefusesAMaxAreas... |
| 15 | Tests | done: start-index circular-buffer accumulator test (ed2dfa9); positive withheld checks, stale tide 4 s bound (bdd0a41). Also made the throttled log lines per node, since rclcpp *_THROTTLE state is per call site for the process and hid a log line across tests | ed2dfa9, bdd0a41 | node tests pass 3 of 3 repeated runs |
| 16 | README | done: unsent-source zero, tide-boat-only bound, wall timer vs node clock / use_sim_time, georeference lock, max_areas, startup wait for earth | 068f925 | n/a |

Plan: PR (b) section gained a "Review fixes" note (items 7 and 14 as owner decisions, plus deviations) in 068f925. Lint fixes in 2412679.

Results: coverage_assessment 181 xunit tests, 0 failures, 0 errors (20 cppcheck skipped, cppcheck not installed); cube_bathymetry 782, 0 failures. Lint (uncrustify, cpplint, copyright, lint_cmake, xmllint, flake8, pep257) all pass.

Open: the pre-existing uncommitted edits to `.agent/work-plans/issue-164/review-*` files were left alone. B10-style live re-check of the fixes (launch with reset_cube_nodes.sh --consumer on the real sim) was not run; the launch behaviour is covered by the launch_testing test only.
