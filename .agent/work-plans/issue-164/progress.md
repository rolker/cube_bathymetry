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
