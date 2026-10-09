# Plan: coverage_assessment node (area vs survey standard, per-source credit) + fresh CUBE sheet without a restart

## Issue

https://github.com/rolker/cube_bathymetry/issues/164

## Context

Two independent pieces for the Ocean Discovery Day scorer (unh_marine_simulation#87), both to the full standard
(tests, docs). Owner decisions of 2026-10-09 are settled and planned to as stated; the message family is
unh_marine_autonomy#406 (read as data; field names are re-checked against the merged files before wrapper code).

- **Two PRs.** (a) fresh sheet: fix the lifecycle reconfigure bug, test it, give operators a reset command sequence.
  Its PR body says "Part of #164" (it must not close the issue). (b) the `coverage_assessment` package;
  "Closes #164". Implementation starts with (a).
- **Findings from reading source** (not yet run): the second `on_configure` re-declares ~18 parameters with no
  `has_parameter` guard, and rclcpp_lifecycle swallows the exception, so the "cleanup then configure starts empty" claim
  is probably unreachable today. The node class lives in the same `.cpp` as `main()` and cannot be linked into a gtest.
  `grid` is only a vessel-centred 200 m window (`publishCaGrid`).
- **`elevation` sign, verified in source.** `GeoSounding` is built from `point[2]` of the geodetic fix
  (`geo_sounding.h`), i.e. the **WGS84-ellipsoid height, positive up**; `grid_projection.cpp:149` copies it unchanged.
  So `elevation` is neither depth below the surface nor map-frame z (the map frame is a tangent plane at the datum,
  `mru_transform/map_frame.cpp`). Seabed values are negative and offset by the geoid separation. The README says nothing of
  this; PR (a) documents it and pins it with a test. Consequence for (b): "depth below the water surface" =
  `h_surface - elevation`, where `h_surface` must be an **ellipsoid height too**. `map -> map_tide` carries only a z
  translation in map z (`sea_surface_estimator.hpp:370`), so the assessor composes it with `earth -> map`: it looks up
  `earth <- map_tide` and takes the geodetic altitude of that frame's origin. This uses the transform the owner named and
  stays consistent with the sounding chain (both sides go through `earth`); the tangent-plane curvature term never enters.

## Approach

### PR (a): fresh sheet without a restart (no message dependency)

1. **Pure refactor, no behaviour change.** `git mv src/cube_bathymetry_node.cpp include/cube_bathymetry/cube_bathymetry_node.hpp`
   (class definition, header guard `CUBE_BATHYMETRY__CUBE_BATHYMETRY_NODE_H_`), new thin `src/cube_bathymetry_node.cpp`
   (`main()` only). CMake: an INTERFACE target `cube_bathymetry_node_link` carrying the node's current link list, used by the
   executable and by the new test, so the two cannot drift. `git mv` keeps blame.
2. **Test seam.** One public const accessor `std::shared_ptr<cube::GeoMapSheet> sheet() const` (documented "diagnostics and tests").
   No other test hooks; the transitions are the public `configure()/activate()/deactivate()/cleanup()` of `LifecycleNode`.
3. **Red test first.** `test/test_node_lifecycle.cpp` (rclcpp initialised in `SetUpTestSuite`; `ROS_DOMAIN_ID` and localhost-only set through
   `set_tests_properties ... ENVIRONMENT` so it cannot talk to live boats): configure -> activate -> deactivate -> cleanup -> configure, and
   assert the returned/current state is INACTIVE after the second configure. Run it on the unfixed code and record in progress.md whether it fails
   (confirming or refuting the `ParameterAlreadyDeclaredException` reading). This is the reproduction work item.
4. **Guard.** Add `declareOnce<T>(name, default, descriptor)` as in unh_marine_autonomy helm_manager (PR#401): declare only when `!has_parameter`,
   then return the current value. Replace every `declare_parameter` in `on_configure` (the ~18 sites, including those inside the `read_only` descriptors).
   Consequence documented: a value set at runtime between cycles is what the next configure reads.
5. **Reset leftover state on configure**, found by auditing the members: `last_grid_publish_time_`, `have_publish_tf_`/`last_publish_tf_`,
   `have_vessel_pos_`/`last_vessel_*`; and release `tf_listener_`/`tf_buffer_` in `on_cleanup` (symmetric with `sonar_info_subscription_`).
6. **Empty-start tests** (same file): with `draft_dir` and `prior_store_dir` empty, add soundings through `sheet()`, deactivate, cleanup, configure;
   assert a new `GeoMapSheet` object with no grids and an empty catalog. A characterization test with `draft_dir` set to a temp dir: after the same cycle
   the saved tiles reload (the sheet is NOT empty), pinning the behaviour the README will state. A test that a parameter set between cycles is honoured.
7. **Stop condition.** If the empty-start assertion fails for a reason the guard does not fix, the plan stops here: record the cause in progress.md and
   hand it to the owner. Nothing in this plan builds the `clear` service speculatively.
8. **Elevation sign test and docs.** In `test_publish_equivalence.cpp` add `ElevationIsEllipsoidHeightPositiveUp`: a sounding at known lat/lon/altitude
   projects to an `elevation` equal to that altitude (negative for seabed). README states: ellipsoid height, positive up, 95 % (1.96 sigma) uncertainty, how to get
   depth below the water surface (subtract from the ellipsoid height of the `map_tide` origin).
9. **Reset command sequence as a script.** `scripts/reset_cube_nodes.sh [--allow-persistence] [--consumer NODE]... NODE...` (installed to `lib/cube_bathymetry`).
   Order: for each CUBE node, walk to unconfigured (`ros2 lifecycle get`, then deactivate/cleanup as needed, so an already-inactive node is not an error);
   then each `--consumer` node (anything accumulating state derived from `grid`, e.g. the assessor) is walked to unconfigured and back to active;
   then each CUBE node is configured and activated. Quiescing CUBE first means no stale window reaches a reset consumer. It reads `draft_dir` and `prior_store_dir`
   with `ros2 param get` and **refuses** if `draft_dir` is non-empty unless `--allow-persistence` (a non-empty `prior_store_dir` only prints a note: it seeds a predicted surface, not coverage).
   Fails fast with the node and transition named, prints final states. `test/test_reset_cube_nodes.sh` runs it against a stub `ros2` on `PATH` holding per-node state
   (registered with `add_test`): four nodes, already-inactive node, failing transition, refusal on `draft_dir`, consumer ordering.
10. **Docs.** README section "Starting a fresh sheet without restarting": the exact command sequence for four namespaced nodes
    (`/boat1/cube_bathymetry` ... `/boat4/cube_bathymetry`), what is and is not cleared, the `draft_dir` caveat, ordering with consumers.
11. **Live verification** on a built node (no persistence): run the script against a real `cube_bathymetry_node`, confirm `ros2 lifecycle get` ends `active`
    and the first `grid` after the reset contains none of the previous window's cells; record in progress.md. Full-scope `ci_local.sh` before push.
12. **Follow-up named, not built:** a `clear` service (draft below under Follow-ups). Filing the issue needs the owner's yes.

### PR (b): the `coverage_assessment` package (core first, wrapper last)

**Decisions.**
- **Where:** new colcon package `coverage_assessment/` in this repo next to `cube_bathymetry/`. Generic by name: it reads any grid_map source that carries
  `elevation` + `uncertainty`, not only CUBE. Node `coverage_assessment` (lifecycle), executable `coverage_assessment_node`.
- **Testable:** library + thin main. `coverage_assessment_core` is plain C++ (grid_map_core + std only, no ROS, no message types); `coverage_assessment_ros`
  holds the node class in a header with the wrapper code; `main()` only spins. The core never sees `SurveyArea`; it has local structs
  (`SurveyStandard{name,a,b}`, `AreaDefinition{id, polygon in grid-frame metres, standard}`) that the wrapper fills from the #406 messages.
- **Input design (the issue's open item):** the node keeps a **latched union over time per source**, built from that source's `grid` messages. It does not
  read `coverage_tiles` or the store: the live `grid` is what every source (sim boats and real ones) already publishes, and the union fixes the 200 m window problem.
  Per source: a world-anchored lattice (cell (i,j) covers `[i*cs,(i+1)*cs)`), paged 64x64 blocks of (elevation, uncertainty) floats. Each incoming message is
  resampled **pull-style**: for every lattice centre inside the message's extent, take the message cell containing it; a finite elevation overwrites, NaN never erases.
  Pull resampling is needed because each CUBE message sizes its geometry from its own min/max, so successive messages are not on one lattice, and a push would
  alias into pinholes. Covered = finite elevation. Memory grows with surveyed cells (~8 B/cell plus paging); logged every 10 s.
- **Rasterising and judging.** Polygon (grid-frame metres) -> scanline even-odd fill; a cell belongs to the area when its **centre** is inside (documented half-open rule), so
  `area_total` = cells x cell_size^2 exactly as #406 defines. Per cell: covered if any source has a depth; best uncertainty = smallest finite uncertainty across sources
  (ties go to the earlier configured source); depth from that source = `h_surface - elevation` (clamped at 0 for a seabed above the surface); meets when `u* <= sqrt(a^2+(b*depth)^2)`;
  no finite uncertainty anywhere -> covered, not meeting. Per source: covered and meeting judged on its own grid with its own depth; `area_unique` = cells only that source covers.
  Sources keep the order of `grid_topics` in every message.
- **Polygon projection (the transform chain).** Wrapper projects each geographic vertex with exactly the chain `grid_projection.cpp` uses for cell centres:
  `GeoPointLatLongDegrees(lat, lon, 0)` -> ECEF -> `map_frame <- earth` affine (one `lookupTransform` per assessment, cached per area until the area or the transform changes) -> x,y.
  `header.frame_id` of every source grid must equal `map_frame`; a grid in another frame is rejected with a throttled ERROR, never resampled. The water surface comes from
  `earth <- water_surface_frame` (see Context).
- **Output.** `coverage_assessment`: one `CoverageAssessment` per area per assessment. `coverage_grid`: one raster over the bounding box of all areas (transient_local, depth 1),
  layer `meets_standard` (NaN not covered or outside every area, 0 below standard, 1 meets); where areas overlap a cell is 1 only if it meets the standard of **every** area containing it.
  Assessment runs on a timer, only when a grid, an area, or the water surface (> 1 cm) changed.
- **Robustness decisions.** Invalid `SurveyArea` (< 3 vertices, non-finite coordinates, negative or non-finite `a`/`b`, raster larger than `max_raster_cells`) is rejected with an ERROR and
  an earlier definition of that id is kept; the registry replaces by id only if `stamp >=` the held one. Water surface missing or older than `water_surface_max_age`: skip the assessment (warn, throttled) rather than judge on a stale tide.
  `grid_topics` empty, duplicated, or `source_names` of a different length: configure FAILS with the reason. Message conversion errors are caught per message.

**Parameters** (all declared in the constructor, read at `on_configure`; a set while ACTIVE is rejected with "deactivate and cleanup first", so there is no accepted-but-inert set):

| Parameter | Default | Meaning |
|---|---|---|
| `grid_topics` | `[]` (required) | CUBE `grid` topics, one per source; order defines `sources` order |
| `source_names` | `[]` | per-source label (platform name); empty entry or empty list falls back to the topic |
| `survey_area_topic` | `survey_area` | area source (`SurveyArea`, transient_local) |
| `survey_area_queue_depth` | `100` | QoS depth, so every latched polygon is delivered |
| `assessment_period` | `1.0` s | timer period |
| `cell_size` | `1.0` m | assessment lattice; should equal CUBE's `cell_size`, a source resolution differing by > 1e-6 warns once per source |
| `map_frame` | `map` | the one frame all grids share |
| `earth_frame` | `earth` | for polygon projection and the water surface |
| `water_surface_frame` | `map_tide` | origin of this frame is the water surface |
| `water_surface_max_age` | `30.0` s | oldest acceptable water-surface transform |
| `max_raster_cells` | `4000000` | per-area raster cap (guards a swapped lat/lon and the `coverage_grid` message size) |

**Steps (build order; B1-B5 build and pass in CI without #406):**

- B0. Re-read #406 (or its PR) and compare field names to the plan; create the local, **uncommitted** `upstream.repos` override and a layer worktree of the #406 branch for local builds.
- B1. Package skeleton, `package.xml` (adds `grid_map_*`, `marine_autonomy`, `marine_interfaces`, `tf2_ros`), core library target, `.github/ci/Dockerfile` COPY of `coverage_assessment/package.xml` and the `ci-image.yml` path trigger.
- B2. `polygon_raster` (+ tests): axis-aligned squares at several offsets (exact area), triangle vs analytic area within one boundary row, L-shape, edge on a lattice line, negative coordinates, < 3 vertices, zero-area, oversize rejection.
- B3. `source_accumulator` (+ tests): phase-shifted messages of the same surface give identical coverage; NaN does not erase; later finite values overwrite; **moving window** (3 windows sweeping a strip) leaves coverage equal to the union and never decreasing; elevation without uncertainty is covered; `clear()` empties.
- B4. `coverage_assessor` + `area_registry` (+ tests): known areas; two overlapping sources (`area_unique`, sums exceed totals); a missing uncertainty does not meet; depth-dependent threshold (same uncertainty fails shallow, meets deep); equality meets; depth taken from the smallest-uncertainty source (two sources, depths straddling the threshold); per-source judged on own grid while the union meets; tie order; water-surface shift changes the verdict; seabed above the surface; registry replace-by-id, stale stamp ignored, invalid area keeps the old one.
- B5. `assessment_raster` (+ tests): `meets_standard` values, overlapping areas with different standards, bounding box and frame.
- B6. ROS wrapper (needs #406): conversions, polygon projection, water-surface height, node class + thin main, parameters with the active-set guard, launch file, install rules.
- B7. Wrapper tests: projection agrees with `cube::geoGridsToGridMap`'s cell placement for the same lat/lon under a known `earth -> map` (so polygon and grid share one chain); node integration over a static TF broadcaster (`earth->map`, `earth->map_tide`) and synthetic `GridMap` messages: exact numbers on `coverage_assessment`/`coverage_grid`; deactivate->cleanup->configure empties the union; frame mismatch rejected; stale tide skips; sources order; a moving window over the wire.
- B8. Docs: `coverage_assessment/README.md` (design decisions above, memory note, rasterisation rule), top-level README Nodes row + parameter table, reset section updated to name the assessor as `--consumer`.
- B9. Sim check before the event: run against unh_marine_simulation's CUBE grids and confirm depth below the surface comes out positive and plausible (this is the first place the `earth <- map_tide` ellipsoid-height reading meets real data). Full-scope `ci_local.sh`; `git diff -- upstream.repos` must be empty; mark ready only after #406 is on `jazzy`.

## Files to Change

| File | Change |
|---|---|
| `cube_bathymetry/include/cube_bathymetry/cube_bathymetry_node.hpp` | (a) class moved here, `declareOnce`, state resets, `sheet()` |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | (a) thin `main()` |
| `cube_bathymetry/CMakeLists.txt` | (a) interface link target, `test_node_lifecycle`, script test, install script |
| `cube_bathymetry/test/test_node_lifecycle.cpp` | (a) new: reconfigure, empty start, draft_dir characterization |
| `cube_bathymetry/test/test_publish_equivalence.cpp` | (a) elevation sign test |
| `cube_bathymetry/scripts/reset_cube_nodes.sh`, `cube_bathymetry/test/test_reset_cube_nodes.sh` | (a) new |
| `README.md` | (a) elevation sign, reset section; (b) Nodes row, parameter table |
| `coverage_assessment/**` | (b) new package: core, ROS wrapper, launch, tests, README |
| `.github/ci/Dockerfile`, `.github/workflows/ci-image.yml` | (b) bake the new package's dependencies |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Each PR carries its tests, README, parameter table, CI-image plumbing |
| Test what breaks | Regression test written red first for the reconfigure bug; moving-window, missing-uncertainty and tide-staleness tests target the field failure modes |
| Only what's needed | `clear` service not built; `.agents/README.md` not created here; no new ADR |
| Improve incrementally | Two PRs, core merged-ready before the wrapper; pure-refactor commit separate from behaviour |
| Human control and transparency | Reset script refuses when persistence would reload old tiles; every rejection is a named log line |
| Capture decisions | Input design, depth reference and overlap rule recorded in the package README (a design draft, not an ADR, while the message family is a prototype) |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| workspace 0008 (ROS 2 conventions) | Yes | Lifecycle node, parameters declared and validated, relative topic names, launch file |
| workspace 0017 | Partly | Root `AGENTS.md` exists; `.agents/README.md` deferred (Open Questions) |
| workspace 0018 (local-first CI) | Yes | Full-scope `ci_local` is the gate; for (b) it needs #406 on `jazzy`; no feature-branch pin committed |
| project 0001 (eviction / never lose survey data) | Yes | Reset documented as not deleting disk data; flush happens in `on_deactivate`/`on_cleanup`; `draft_dir` reload semantics pinned by a test |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Node class moved to a header | CMake targets, includes, any doc naming `cube_bathymetry_node.cpp` | Yes |
| `declare_parameter` -> `declareOnce` | README parameter notes on runtime-set persistence | Yes |
| New package | Dockerfile COPY, ci-image path filter, top-level README | Yes |
| `elevation` documented as ellipsoid height | README Nodes row; downstream consumers (CAMP, costmap) unchanged | Yes (docs only) |
| #406 message names | Wrapper conversions | Re-checked at B0 |

## Documentation & Instruction Impact

- **Stale docs** (must land in these PRs): top-level `README.md` Nodes table row for `grid` (state the `elevation` convention), new reset section, new `coverage_assessment` row and parameter table; `coverage_assessment/README.md`.
- **Agent-instruction candidates** (proposals only): (1) "declare every lifecycle parameter once (`declareOnce` or in the constructor): rclcpp_lifecycle swallows the re-declare exception, so a second configure fails silently"; (2) "`CUBE elevation` is ellipsoid height, not map z: compose with `earth`, never subtract `map_tide.z`"; (3) the repo still needs `.agents/README.md`.

## Open Questions

- `.agents/README.md`: the issue lists it, but workspace AGENTS.md says it is a dedicated task with its own issue. Default: not in these PRs; the parameter table goes in the top-level README, where this repo keeps it. Say if you want it folded into (b).
- Depth reference: the plan takes the ellipsoid height of the `map_tide` origin (via `earth`), not `map_tide.z` directly, because `elevation` is ellipsoid height. Confirm that reading of "depth from the map-to-water-surface transform"; B9 checks it on sim data.
- Overlapping areas with different standards: one `coverage_grid`, a cell is 1 only if it meets all. Acceptable for the scorer?

## Follow-ups (not in these PRs)

- **CUBE `clear` service (after the event; needs an issue, owner's yes first):** service on `cube_bathymetry_node`; flush dirty tiles first; rotate `draft_dir` to a new session subdirectory (nothing deleted on disk, so the old session stays the product of record); reset sheet, catalog builder, eviction markers, disk-serve queue and refresh tracker in one callback so a save or publish timer cannot interleave; publish an all-unknown grid and a catalog reset so consumers drop old tiles; document `prior_store_dir` as unchanged.

## Estimated Scope

Two PRs. (a) is small-to-medium (one refactor commit, test, guard, script, docs). (b) is large (new package, ~10 commits) and its wrapper commits are blocked on unh_marine_autonomy#406 reaching `jazzy`; B1-B5 can be reviewed earlier.
