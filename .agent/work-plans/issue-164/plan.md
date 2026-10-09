# Plan: coverage_assessment node (area vs survey standard, per-source credit) + fresh CUBE sheet without a restart

## Issue

https://github.com/rolker/cube_bathymetry/issues/164

## Context

Two independent pieces for the Ocean Discovery Day scorer (unh_marine_simulation#87), both to the full standard
(tests, docs). Owner decisions of 2026-10-09 are settled and planned to as stated; the message family is
unh_marine_autonomy#406 (read as data; field names are re-checked against the merged files before wrapper code).
This is the revision after the plan review at `8fbc067` (changes-requested); every must-fix and suggestion from it is
either applied below or listed under "Review disposition" at the end.

- **Two PRs.** (a) fresh sheet: fix the lifecycle reconfigure bug, test it, give operators a reset command sequence.
  Its PR body says "Part of #164" (it must not close the issue). (b) the `coverage_assessment` package;
  "Closes #164". Implementation starts with (a). **#406 is merged first**: PR (b) includes the ROS wrapper, which does not
  build against `upstream.repos` at `jazzy` until the four `.msg` files are there, so (b) cannot be green earlier.
  "Merge #406 first" is the plan; (b) stays one PR after that. The wrapper is developed against the #406 branch through an
  **uncommitted** `upstream.repos` override.
- **Findings from reading source** (not yet run): the second `on_configure` re-declares ~18 parameters with no
  `has_parameter` guard, and rclcpp_lifecycle swallows the exception, so the "cleanup then configure starts empty" claim
  is probably unreachable today. The node class lives in the same `.cpp` as `main()` and cannot be linked into a gtest.
  `grid` is only a vessel-centred 200 m window (`publishCaGrid`).
- **`elevation` sign, verified in source.** `GeoSounding` is built from `point[2]` of the geodetic fix
  (`geo_sounding.h`), i.e. the **WGS84-ellipsoid height, positive up** (confirmed by the owner); `grid_projection.cpp:149` copies it
  unchanged. This is not new: `ssp_ray_tracer.h` says "Sounding::depth ... is an elevation, positive up", and the top-level README
  (~line 170, uma ADR-0002 D4) already states that stored depths are WGS84 ellipsoidal heights. What the README does not say is
  that the `grid` message's `elevation` layer carries the same quantity; PR (a) states that and pins it with a test, citing those two
  places rather than presenting the sign as a discovery. `elevation` is neither depth below the surface nor map-frame z.
  Consequence for (b): "depth below the water surface" = `h_surface - elevation`, `h_surface` an ellipsoid height too.
- **The surface height is approximate, and the plan says by how much.** `map` is an ENU tangent plane at the first fix with altitude
  forced to 0 (`mru_transform.cpp:61-63`, `map_frame.cpp`); `map -> map_tide` carries only a z translation, the average of the
  tide-publishing **boat's** odometry z (`sea_surface_estimator.hpp:370`). Odometry z is a tangent-plane coordinate at the boat's
  position, so `map_tide.z = h - d^2/(2R)` with d the boat's horizontal distance from its own map origin: the surface read from
  `earth <- map_tide` (which, with origin height 0, is just `map_tide.z`) is **low by d^2/2R**: about 8 cm at 1 km, 0.3 m at 2 km, 2 m at 5 km.
  For the event this is bounded, not corrected: the assessor measures d, and beyond a configured distance it **warns and skips scoring** rather than
  scoring on a surface it knows is off. The geoid also varies by centimetres per km here, and `h_surface` is one number for the whole assessment
  area (the sim holds it at 48.88 m, `asv_sim/config/bizzyboat.yaml`). A proper spatially varying surface (geoid model plus per-boat tide) is later work and the
  README says so.
- **Sources are in different frames.** The sim launches each CUBE with `map_frame = <ns>/map` (`sim_robot_launch.py` ~388-392,
  `bizzyboat_sim_core_launch.py` 172) and each boat's mru_transform sets its own map origin at its first fix: four boats, four frames, four
  origins; a fleet likewise. So the assessor's raster has **its own frame** (`map_frame` parameter), and each source grid is transformed into it via TF
  (through `earth`) as part of the pull-resampling, which already runs once per message. #406's wording is being changed in parallel to
  "`header.frame_id` is the frame of the assessment raster; sources in other frames are transformed into it". A source whose transform is unavailable is
  **skipped with a throttled warning**, never silently dropped.

## Approach

### PR (a): fresh sheet without a restart (no message dependency)

1. **Pure refactor, no behaviour change.** `git mv src/cube_bathymetry_node.cpp src/cube_bathymetry_node.hpp`
   (class definition, header guard `CUBE_BATHYMETRY__CUBE_BATHYMETRY_NODE_H_`), new thin `src/cube_bathymetry_node.cpp`
   (`main()` only). The header lives in `src/`, **not** `include/`: it is not installed and is not public API; the test adds `src/` to its private
   include path. CMake: an INTERFACE target `cube_bathymetry_node_link` carrying the node's current link list, used by the
   executable and by the new test, so the two cannot drift. `git mv` keeps blame.
2. **Test seam.** One public const accessor `std::shared_ptr<cube::GeoMapSheet> sheet() const` (documented "diagnostics and tests").
   No other test hooks; the transitions are the public `configure()/activate()/deactivate()/cleanup()` of `LifecycleNode`.
3. **Red test first.** `test/test_node_lifecycle.cpp` (rclcpp initialised in `SetUpTestSuite`; `set_tests_properties ... ENVIRONMENT` gives this test its **own
   `ROS_DOMAIN_ID`** and `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST` (jazzy; `ROS_LOCALHOST_ONLY` is deprecated) so parallel colcon tests, live boats and the CI container cannot
   cross-talk): configure -> activate -> deactivate -> cleanup -> configure, and assert the current state is INACTIVE after the second configure.
   Run it on the unfixed code (after the pure refactor, before the guard) and record in progress.md whether it fails
   (confirming or refuting the `ParameterAlreadyDeclaredException` reading). This is the reproduction work item.
4. **Guard.** Add `declareOnce<T>(name, default, descriptor)` as in unh_marine_autonomy helm_manager (PR#401): declare only when `!has_parameter`,
   then return the current value. Replace **every** `declare_parameter` in `on_configure` (the ~18 sites, including those with `read_only` descriptors, and `map_frame`).
   Consequence documented: a value set at runtime between cycles is what the next configure reads. (helm_manager's `declareReadOnlyString` undeclare/redeclare dance exists for
   `automatically_declare_parameters_from_overrides`, which this node does not enable; a comment says so.)
5. **Reset leftover state on configure and release the listener safely in cleanup**, found by auditing the members: `last_grid_publish_time_`, `have_publish_tf_`/`last_publish_tf_`,
   `have_vessel_pos_`/`last_vessel_*`. In `on_cleanup`, release `tf_listener_` **before** `tf_buffer_` (the old listener holds a reference to the buffer and
   can touch it after destruction at `on_configure`'s reassignment, where the unique_ptr buffer is replaced before the shared_ptr listener) and the publishers/subscriptions that are recreated, symmetric with `sonar_info_subscription_`.
6. **Empty-start tests** (same file): with `draft_dir` and `prior_store_dir` empty, add soundings through `sheet()`, deactivate, cleanup, configure;
   assert a new `GeoMapSheet` object with no grids and an empty catalog. A characterization test with `draft_dir` set to a temp dir: after the same cycle
   the saved tiles reload (the sheet is NOT empty), pinning the behaviour the README will state. A test that a parameter set between cycles is honoured.
7. **Stop condition.** If the empty-start assertion fails for a reason the guard does not fix, the plan stops here: record the cause in progress.md and
   hand it to the owner. No `clear` service is built before the event (owner decision); it stays a named follow-up.
8. **Elevation sign test and docs.** In `test_publish_equivalence.cpp` add `ElevationIsEllipsoidHeightPositiveUp`: a sounding at known lat/lon/altitude
   projects to an `elevation` equal to that altitude (negative for seabed). README: the `grid` message's `elevation` is ellipsoid height, positive up (cites the existing
   ellipsoidal-heights statement and `ssp_ray_tracer.h`), 95 % (1.96 sigma) uncertainty, how to get depth below the water surface (the surface's ellipsoid height minus `elevation`),
   and the d^2/2R caveat on the tide-derived surface height.
9. **Reset command sequence as a script.** `scripts/reset_cube_nodes.sh [--allow-persistence] [--consumer NODE]... NODE...` (installed to `lib/cube_bathymetry`).
   Order: for each CUBE node, walk to unconfigured (`ros2 lifecycle get`, then deactivate/cleanup as needed, so an already-inactive node is not an error);
   then each `--consumer` node (anything accumulating state derived from `grid`, e.g. the assessor) is walked to unconfigured and back to active;
   then each CUBE node is configured and activated. Quiescing CUBE first means no stale window reaches a reset consumer. It reads `draft_dir` and `prior_store_dir`
   with `ros2 param get` and **refuses** if `draft_dir` is non-empty unless `--allow-persistence` (a non-empty `prior_store_dir` only prints a note: it seeds a predicted surface, not coverage).
   Fails fast with the node and transition named, prints final states. `package.xml` gains `exec_depend` on `ros2lifecycle` and `ros2param` (the script shells out to both).
   `test/test_reset_cube_nodes.sh` runs it against a stub `ros2` on `PATH` holding per-node state, registered with `add_test`, **three cases only**: consumer ordering,
   refusal on non-empty `draft_dir`, failing transition naming the node. The "already inactive" and four-node permutations are covered by the live check (step 11), not by the stub.
   The script must not block PR (a): if it slips, (a) ships with the README command sequence and the script follows.
10. **Docs.** README section "Starting a fresh sheet without restarting": the exact command sequence for four namespaced nodes
    (`/boat1/cube_bathymetry` ... `/boat4/cube_bathymetry`), what is and is not cleared, the `draft_dir` caveat, ordering with consumers. It records the **event configuration
    consequence**: `bizzyboat.yaml` sets `draft_dir: /home/field/data/world/depths` for `/**/cube_bathymetry` and `bizzyboat_sim_core_launch.py` layers that file, so any launch that loads it makes a lifecycle cycle reload old tiles and
    "fresh sheet" false; the demo launch (unh_marine_simulation#88) must leave `draft_dir` empty, which the host has already told that implementer. The script refuses loudly if it is not.
11. **Live verification** on a built node (no persistence): run the script against a real `cube_bathymetry_node` (own `ROS_DOMAIN_ID`, localhost discovery), confirm `ros2 lifecycle get` ends `active`
    and the first `grid` after the reset contains none of the previous window's cells; record in progress.md. Full-scope `ci_local.sh` before push.
12. **Follow-up named, not built:** a `clear` service (draft below under Follow-ups). Filing the issue needs the owner's yes.

### PR (b): the `coverage_assessment` package (core first, wrapper last; after #406 is on `jazzy`)

**Decisions.**
- **Where:** new colcon package `coverage_assessment/` in this repo next to `cube_bathymetry/`. Generic by name: it reads any grid_map source that carries
  `elevation` + `uncertainty`, not only CUBE. Node `coverage_assessment` (lifecycle), executable `coverage_assessment_node`.
- **Testable:** library + thin main. `coverage_assessment_core` is plain C++ (grid_map_core + std only, no ROS, no message types); `coverage_assessment_ros`
  holds the node class in a header with the wrapper code (in `src/`, not installed); `main()` only spins. The core never sees `SurveyArea` or TF; it has local structs
  (`SurveyStandard{name,a,b}`, `AreaDefinition{id, polygon in raster-frame metres, standard}`, an affine `raster -> source` horizontal mapping) that the wrapper fills.
- **Input design (the issue's open item):** the node keeps a **latched union over time per source**, built from that source's `grid` messages. It does not
  read `coverage_tiles` or the store: the live `grid` is what every source (sim boats and real ones) already publishes, and the union fixes the 200 m window problem.
  Per source: a lattice anchored in the **assessor's raster frame** (cell (i,j) covers `[i*cs,(i+1)*cs)`), paged 64x64 blocks of (elevation, uncertainty) floats.
  Each incoming message is resampled **pull-style**: for every lattice centre whose image in the message's frame falls inside the message's extent, take the message cell containing it.
  Pull resampling is needed because each CUBE message sizes its geometry from its own min/max, so successive messages are not on one lattice, and a push would alias into pinholes.
  **Never-erases applies per field**: a finite elevation overwrites the held elevation; a finite uncertainty overwrites the held uncertainty; a NaN in either field never erases that field
  (a finite elevation with NaN uncertainty, which the sim produces before TF settles, must not wipe a good uncertainty and flip a meeting cell to not meeting).
  Covered = finite elevation. Memory grows with surveyed cells (~8 B/cell plus paging); logged every 10 s.
- **Frames (new).** The raster frame is the `map_frame` parameter. For each message the wrapper does **one** `lookupTransform(message_frame <- map_frame)` (latest available; this is per message,
  not per cell) and hands the core a horizontal affine; lattice centre -> earth -> source frame is exactly that composed transform with z = 0, since only the horizontal placement matters
  (`elevation` is an ellipsoid height, frame independent, so it is never transformed; the z error from tangent-plane curvature does not move the pull lookup measurably at event scale).
  A message whose transform is unavailable is **skipped with a throttled WARN naming the source and frame**, and counted in the node's diagnostics; the source is never silently dropped.
  Polygon vertices are projected with exactly the chain `grid_projection.cpp` uses for cell centres: `GeoPointLatLongDegrees(lat, lon, 0)` -> ECEF -> `map_frame <- earth` affine
  (one `lookupTransform` per assessment, cached per area until the area or the transform changes) -> x,y.
- **Water surface.** `h_surface` = geodetic altitude of the origin of `earth <- water_surface_frame` (with origin height 0 this is `map_tide.z`; going through `earth` keeps one code path for any frame).
  **Which boat's `map_tide`:** exactly one, named by the `water_surface_frame` parameter (e.g. `boat1/map_tide`); the same surface is applied to every source. Consequences, stated rather than discovered:
  (1) a source on a boat far from the tide boat sees a surface that is only as good as the one-surface assumption (README: valid for an area of a few km, same body of water);
  (2) **when that boat's estimator withholds `map_tide`** (stale beyond `water_surface_max_age`, or not published), the assessor stops scoring **everyone**: it skips the assessment with a throttled WARN, keeps
  accumulating grids, and the last assessment stays on the transient_local topic (the scoreboard shows an old value, the log says why). (3) The curvature bound above: the assessor measures d, the distance of the
  tide boat (`water_surface_boat_frame`, e.g. `boat1/base_link`) from the origin of the tide's parent frame, and above `water_surface_max_boat_distance` it **warns and skips scoring**, the same behaviour as (2).
- **Rasterising and judging.** Polygon (raster-frame metres) -> scanline even-odd fill; a cell belongs to the area when its **centre** is inside (documented half-open rule), so
  `area_total` = cells x cell_size^2 exactly as #406 defines. Per cell: covered if any source has a depth; best uncertainty = smallest finite uncertainty across sources
  (ties go to the earlier configured source); depth from that source = `h_surface - elevation`; meets when `u* <= sqrt(a^2+(b*depth)^2)`;
  no finite uncertainty anywhere -> covered, not meeting. Per source: covered and meeting judged on its own grid with its own depth; `area_unique` = cells only that source covers.
  Sources keep the order of `grid_topics` in every message.
- **Negative depths are never clamped silently.** A cell whose depth is negative (seabed "above" the surface) is evaluated with depth 0 in the threshold (so the formula stays defined), but each assessment **counts the cells with depth below about -1 m**
  and logs a throttled WARN with the count and the surface height used, so a wrong sign, datum or stale tide cannot pose as a plausible verdict. The count is also in the node's diagnostics and tested.
- **Output.** `coverage_assessment`: one `CoverageAssessment` per area per assessment, **transient_local** (depth = number of areas held, so a scoreboard that starts late receives every area). `coverage_grid`: one raster over the bounding box of all areas
  (**transient_local**, depth 1), layer `meets_standard` (NaN not covered or outside every area, 0 below standard, 1 meets); where areas overlap a cell is 1 only if it meets the standard of **every** area containing it.
  `header.frame_id` of both outputs is `map_frame`. Assessment runs on a timer, only when a grid, an area, or the water surface (> 1 cm) changed, so `coverage_grid` is republished at most once per `assessment_period`.
- **Robustness decisions.** Invalid `SurveyArea` (< 3 vertices, non-finite coordinates, negative or non-finite `a`/`b`, raster larger than `max_raster_cells`) is rejected with an ERROR and
  an earlier definition of that id is kept; the registry replaces by id only if `stamp >=` the held one. `grid_topics` empty, duplicated, or `source_names` of a different length: configure FAILS with the reason.
  Message conversion errors are caught per message. `survey_area` uses a fixed QoS depth (transient_local, depth 100 hard-coded with a comment; no parameter).

**Parameters** (declared in the constructor with `read_only` descriptors for everything read at configure, so a runtime set is rejected by rclcpp itself and there is no accepted-but-inert set; no custom set-callback):

| Parameter | Default | Meaning |
|---|---|---|
| `grid_topics` | `[]` (required) | CUBE `grid` topics, one per source; order defines `sources` order |
| `source_names` | `[]` | per-source label (platform name); empty entry or empty list falls back to the topic |
| `survey_area_topic` | `survey_area` | area source (`SurveyArea`, transient_local) |
| `assessment_period` | `1.0` s | timer period |
| `cell_size` | `1.0` m | assessment lattice; should equal CUBE's `cell_size`, a source resolution differing by > 1e-6 warns once per source |
| `map_frame` | `map` | the frame of the assessment raster; sources in other frames are transformed into it |
| `water_surface_frame` | `map_tide` | origin of this frame is the water surface; one boat's (e.g. `boat1/map_tide`) |
| `water_surface_boat_frame` | `base_link` | the tide-publishing boat, for the curvature-bound distance |
| `water_surface_max_boat_distance` | `1500.0` m | beyond this the surface is low by > ~18 cm: warn and skip scoring |
| `water_surface_max_age` | `30.0` s | oldest acceptable water-surface transform |
| `max_raster_cells` | `1000000` | per-area raster cap (guards a swapped lat/lon and the `coverage_grid` message, ~4 MB at this size) |

(`earth_frame` is hard-coded `earth`; `survey_area_queue_depth` is hard-coded.)

**Steps (build order). B1 is the first thing done in (b), before any package code; B3-B6 build and pass in CI without #406 on their own, B7 is what needs it.**

- B0. Re-read #406 (merged by then) and compare field names to the plan; until it is merged, create the local, **uncommitted** `upstream.repos` override and a layer worktree of the #406 branch.
- B1. **Datum and frame spike, before any wrapper code.** A throwaway script (not committed; findings recorded in progress.md) on the running sim: for each source, print `h_surface - elevation` (with `h_surface` read from `earth <- map_tide` exactly as the wrapper will) and the source's `header.frame_id`, and measure the
  **pinhole share** inside surveyed swaths of single `grid` messages (NaN share between finite cells; a stationary boat repeats the same phase so pull resampling cannot fill it), comparing `area_covered` from a direct count with what the accumulator will report. Its results decide the frame handling above
  (confirm the `<ns>/map` frames and that `lookupTransform` between them works), the sign of depth, and whether a pinhole fill is needed. If the spike contradicts a decision here, the plan is revised before B2.
- B2. Package skeleton, `package.xml` (explicit dependencies: `grid_map_core`, `grid_map_ros`, `grid_map_msgs`, `rclcpp`, `rclcpp_lifecycle`, `tf2`, `tf2_ros`, `tf2_eigen`, `tf2_geometry_msgs`, `marine_autonomy`, `marine_interfaces`), core library target, and CI plumbing: `.github/ci/Dockerfile` COPY of `coverage_assessment/package.xml` **and** the `--from-paths` argument of both `rosdep check` and `rosdep install` (currently `src cube_bathymetry`, Dockerfile ~45-52), plus the `ci-image.yml` path trigger.
- B3. `polygon_raster` (+ tests): axis-aligned squares at several offsets (exact area), triangle vs analytic area within one boundary row, L-shape, edge on a lattice line, negative coordinates, < 3 vertices, zero-area, oversize rejection.
- B4. `source_accumulator` (+ tests): phase-shifted messages of the same surface give identical coverage; a message in a rotated/translated frame (affine) gives the same coverage as the same message in the raster frame; NaN does not erase, **per field** (finite elevation with NaN uncertainty keeps the held uncertainty); later finite values overwrite; **moving window** (3 windows sweeping a strip) leaves coverage equal to the union and never decreasing; elevation without uncertainty is covered; `clear()` empties.
- B5. `coverage_assessor` + `area_registry` (+ tests): known areas; two overlapping sources (`area_unique`, sums exceed totals); a missing uncertainty does not meet; depth-dependent threshold (same uncertainty fails shallow, meets deep); equality meets; depth taken from the smallest-uncertainty source (two sources, depths straddling the threshold); per-source judged on own grid while the union meets; tie order; water-surface shift changes the verdict; **negative-depth count** (cells below -1 m counted, threshold evaluated at 0); registry replace-by-id, stale stamp ignored, invalid area keeps the old one.
- B6. `assessment_raster` (+ tests): `meets_standard` values, overlapping areas with different standards, bounding box and frame.
- B7. ROS wrapper (needs #406): conversions, polygon projection, per-message source-frame transform (skip + throttled WARN when unavailable), water-surface height with the age and distance refusals, node class + thin main, `read_only` parameters, launch file, install rules, tf2/`ros2` exec dependencies.
- B8. Wrapper tests, **happy path, frame handling, stale tide, reset-empties-union only** (per-test `ROS_DOMAIN_ID`, `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`): (i) projection agrees with `cube::geoGridsToGridMap`'s cell placement for the same lat/lon under a known `earth -> map` (polygon and grid share one chain); (ii) node integration over a static TF broadcaster (`earth->map`, `earth->map_tide`) and synthetic `GridMap` messages: exact numbers on `coverage_assessment`/`coverage_grid`; (iii) frame handling: two sources in different `<ns>/map` frames both credited, one with no transform skipped with the WARN; (iv) stale tide, and a tide boat beyond the distance bound, skip scoring; (v) deactivate->cleanup->configure empties the union.
- B9. Docs: `coverage_assessment/README.md` (design decisions above, the d^2/2R bound and the one-surface assumption, which boat's tide is used and what happens when it is withheld, memory note, rasterisation rule), top-level README Nodes row + parameter table, reset section updated to name the assessor as `--consumer`.
- B10. Sim check before the event: run against unh_marine_simulation's CUBE grids and confirm depth below the surface comes out positive and plausible across all four boats' frames (the spike's answer, now end to end). Full-scope `ci_local.sh`; `git diff -- upstream.repos` must be empty; mark ready only after #406 is on `jazzy`.

## Files to Change

| File | Change |
|---|---|
| `cube_bathymetry/src/cube_bathymetry_node.hpp` | (a) class moved here (private to the package, not installed), `declareOnce`, state resets, release order, `sheet()` |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | (a) thin `main()` |
| `cube_bathymetry/CMakeLists.txt` | (a) interface link target, `test_node_lifecycle` (own domain id), script test, install script |
| `cube_bathymetry/package.xml` | (a) `exec_depend` `ros2lifecycle`, `ros2param` |
| `cube_bathymetry/test/test_node_lifecycle.cpp` | (a) new: reconfigure, empty start, draft_dir characterization |
| `cube_bathymetry/test/test_publish_equivalence.cpp` | (a) elevation sign test |
| `cube_bathymetry/scripts/reset_cube_nodes.sh`, `cube_bathymetry/test/test_reset_cube_nodes.sh` | (a) new (three-case stub test) |
| `README.md` | (a) elevation sign, reset section, event `draft_dir` note; (b) Nodes row, parameter table |
| `coverage_assessment/**` | (b) new package: core, ROS wrapper, launch, tests, README |
| `.github/ci/Dockerfile` (COPY + both `--from-paths`), `.github/workflows/ci-image.yml` | (b) bake the new package's dependencies |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Each PR carries its tests, README, parameter table, CI-image plumbing, cross-repo consequences below |
| Test what breaks | Regression test written red first for the reconfigure bug; moving-window, per-field never-erases, missing-uncertainty, tide-staleness, negative-depth tests target the field failure modes |
| Only what's needed | `clear` service not built; `.agents/README.md` not created here; no new ADR; no custom parameter callback; no `earth_frame`/queue-depth parameters |
| Improve incrementally | Two PRs; pure-refactor commit separate from behaviour; datum/frame spike before wrapper code |
| Human control and transparency | Reset script refuses when persistence would reload old tiles; every rejection, skipped source, refused tide and negative-depth count is a named log line |
| Capture decisions | Input design, frame handling, depth reference and overlap rule recorded in the package README (a design draft, not an ADR, while the message family is a prototype) |

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
| New package | Dockerfile COPY and both `--from-paths`, ci-image path filter, top-level README | Yes |
| `elevation` documented as ellipsoid height | README Nodes row; downstream consumers (CAMP, costmap) unchanged | Yes (docs only) |
| #406 message names / `header.frame_id` wording | Wrapper conversions; re-checked at B0 | Yes |
| Reset script and assessor exist | **unh_marine_simulation#88 demo launch**: must start the assessor (with `grid_topics` for the four boats, `water_surface_frame` of one boat), must leave CUBE `draft_dir` empty (the host has told that implementer), and must not rely on a shared `map` frame | Cross-repo, stated here; not changed by these PRs |
| Issue #164 checklist still lists "create `.agents/README.md`" | Before "Closes #164" the checklist item is reworded or ticked (e.g. "split to its own issue, not part of this change") so closing is honest | Owner's yes to edit the issue; done before (b) is marked ready |

## Documentation & Instruction Impact

- **Stale docs** (must land in these PRs): top-level `README.md` Nodes table row for `grid` (state the `elevation` convention), new reset section, new `coverage_assessment` row and parameter table; `coverage_assessment/README.md`.
- **Agent-instruction candidates** (proposals only): (1) "declare every lifecycle parameter once (`declareOnce` or in the constructor): rclcpp_lifecycle swallows the re-declare exception, so a second configure fails silently"; (2) "`CUBE elevation` is ellipsoid height, not map z: compose with `earth`, never subtract `map_tide.z` without the d^2/2R bound"; (3) the repo still needs `.agents/README.md`.

## Open Questions

- `.agents/README.md`: the issue lists it, but workspace AGENTS.md says it is a dedicated task with its own issue. Default: not in these PRs; the parameter table goes in the top-level README, where this repo keeps it; the issue checklist is reworded before closing.
- Overlapping areas with different standards: one `coverage_grid`, a cell is 1 only if it meets all (settled by the owner; revisit only if the scorer needs per-area rasters).

(Settled and removed from the open list: depth reference = ellipsoid height of the `map_tide` origin, with the d^2/2R bound, checked by the B1 spike; frames; no `clear` before the event.)

## Follow-ups (not in these PRs)

- **CUBE `clear` service (after the event; needs an issue, owner's yes first):** service on `cube_bathymetry_node`; flush dirty tiles first; rotate `draft_dir` to a new session subdirectory (nothing deleted on disk, so the old session stays the product of record); reset sheet, catalog builder, eviction markers, disk-serve queue and refresh tracker in one callback so a save or publish timer cannot interleave; publish an all-unknown grid and a catalog reset so consumers drop old tiles; document `prior_store_dir` as unchanged.
- **Spatially varying water surface** (geoid model plus per-boat tide) replacing the one-number `h_surface` and its distance bound.

## Review disposition (plan review at `8fbc067`)

Applied: frames per source (new Frames decision, B4/B8); curvature claim corrected and bounded (Context, Water surface); spike moved first (B1); negative depths counted and warned (Output/judging, B5); "merge #406 first" (Context); per-field never-erases; `max_raster_cells` lowered to 1,000,000; transient_local on both outputs; per-test `ROS_DOMAIN_ID` and `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`; Dockerfile `--from-paths`; `ros2lifecycle`/`ros2param` exec_depends; explicit (b) dependencies; README citation of the existing ellipsoidal-heights statement and `ssp_ray_tracer.h`; which boat's tide and the withheld-tide behaviour; cross-repo and issue-checklist consequences; node header under `src/`; script test shrunk to three cases; custom set-guard replaced by `read_only` descriptors; `earth_frame` and `survey_area_queue_depth` dropped; B8 shrunk. Pinhole share is measured in B1.
Not applied: none. Deferred by the reviewer and kept deferred: registry edge tests beyond replace-by-id, stale stamp and invalid-keeps-old.

## Estimated Scope

Two PRs. (a) is small-to-medium (one refactor commit, test, guard, script, docs). (b) is large (new package, ~10 commits), starts with a throwaway spike, and is merged only after unh_marine_autonomy#406 is on `jazzy`.
