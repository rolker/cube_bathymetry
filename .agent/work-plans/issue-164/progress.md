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
