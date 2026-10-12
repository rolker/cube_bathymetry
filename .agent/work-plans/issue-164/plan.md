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
2. **Test seam.** Two public const accessors, `std::shared_ptr<cube::GeoMapSheet> sheet() const` and `std::size_t catalogSize() const` (documented "diagnostics and tests"; the
   second is needed to assert that the tile catalog is empty after a fresh start), and a defaulted `rclcpp::NodeOptions` constructor argument so a test can supply parameter
   overrides before the first configure (the executable passes the default). No other test hooks; the transitions are the public `configure()/activate()/deactivate()/cleanup()` of `LifecycleNode`.
   *(As implemented, `db74bf8`..`dc9fdca`.)*
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
   *(As implemented: `on_cleanup` resets the ping and tile-request subscriptions, the four publishers, then the listener, then the buffer, then the sheet and the catalog
   registry once the final flush has succeeded. After the pre-push review: `on_configure` also resets a leftover `tf_listener_` before assigning `tf_buffer_`, because a configure that throws
   returns to UNCONFIGURED without `on_cleanup`; `on_cleanup` returns FAILURE, before releasing anything, when `draft_dir` is set and dirty grids remain after a failed
   `saveDirtyTiles()` (ADR-0001: a refused cleanup, not lost tiles); `refresh_timer_` is cancelled there too, and `drainRefreshQueue`, `publishCatalog` and `drainDiskServeQueue` return early when the publisher or sheet is gone. After round 2: an `on_shutdown` flushes the dirty tiles (from UNCONFIGURED, INACTIVE or ACTIVE) and always completes, logging how many tiles it could not write, since a shutdown cannot be refused; the cleanup comment no longer claims tiles are safe on shutdown, and the README says tiles stay in RAM while a cleanup is refused and that a killed process loses what the periodic save has not written.)*
6. **Empty-start tests** (same file): with `draft_dir` and `prior_store_dir` empty, add soundings through `sheet()`, deactivate, cleanup, configure;
   assert a new `GeoMapSheet` object with no grids and an empty catalog. A characterization test with `draft_dir` set to a temp dir: after the same cycle
   the saved tiles reload (the sheet is NOT empty), pinning the behaviour the README will state. A test that a parameter set between cycles is honoured.
   *(After the pre-push review, also: `FailedConfigureCanBeRetried`, `CleanupFailsWhenTheFinalFlushFails`, `CleanupReleasesTheSheetAndCatalog`, `ReadOnlyOverridesSurviveACycle` and
   `ReconfigureResetsTheSessionState`, which dirties the cursor/cache/queue/tracker/catalog state through a `friend struct CubeBathymetryTestAccess` and checks each is fresh after the next configure.
   Round 2 added `FailedConfigureCanBeRetriedWhileTfIsPublished` (300 failed configures while a second node publishes `/tf`; it pins the `tf_listener_.reset()` only under AddressSanitizer, the comment says so),
   `on_shutdown` (see item 5) with `ShutdownFromActiveFlushesUnsavedTiles` and `ShutdownCompletesWhenTheFlushFails`.
   The test's `ROS_DOMAIN_ID` is 87, not 164: above 101 the DDS port formula overlaps the ephemeral range.)*
7. **Stop condition.** If the empty-start assertion fails for a reason the guard does not fix, the plan stops here: record the cause in progress.md and
   hand it to the owner. No `clear` service is built before the event (owner decision); it stays a named follow-up.
8. **Elevation sign test and docs.** In `test_publish_equivalence.cpp` add `ElevationIsEllipsoidHeightPositiveUp`: a sounding at known lat/lon/altitude
   projects to an `elevation` equal to that altitude (a seabed is *below the water surface's ellipsoid height*: negative near sea level, positive on a lake whose surface is above the ellipsoid, e.g. Lake Massabesic at +48.88 m, so the test has both cases). README: the `grid` message's `elevation` is ellipsoid height, positive up (cites the existing
   ellipsoidal-heights statement and `ssp_ray_tracer.h`), 95 % (1.96 sigma) uncertainty, how to get depth below the water surface (the surface's ellipsoid height minus `elevation`),
   and the d^2/2R caveat on the tide-derived surface height.
9. **Reset command sequence as a script.** `scripts/reset_cube_nodes.sh [--allow-persistence] [--consumer NODE]... NODE...` (installed to `lib/cube_bathymetry`).
   Order: for each CUBE node, walk to unconfigured (`ros2 lifecycle get`, then deactivate/cleanup as needed, so an already-inactive node is not an error);
   then each `--consumer` node (anything accumulating state derived from `grid`, e.g. the assessor) is walked to unconfigured and back to active;
   then each CUBE node is configured and activated. Quiescing CUBE first means no stale window reaches a reset consumer. It reads `draft_dir` and `prior_store_dir`
   with `ros2 param get` and **refuses** if `draft_dir` is non-empty unless `--allow-persistence` (a non-empty `prior_store_dir` only prints a note: it seeds a predicted surface, not coverage).
   A node that was never configured has no readable `draft_dir` (declared at configure), so after every other check has passed it is configured, read and put back to unconfigured, one node at a time.
   Fails fast with the node and transition named, prints final states. *(After the pre-push review: step 0 first checks that every node and consumer answers and is `active`/`inactive`/`unconfigured`, before any transition;
   a failure after the first change, or SIGINT/SIGTERM, prints every node's state and a re-run hint; every `ros2` call is under `timeout` (`RESET_CUBE_GET_TIMEOUT` 30 s, `RESET_CUBE_SET_TIMEOUT` 300 s) and parsed output is stdout only; `--consumer` rejects a value starting with `-`. After round 2: calls run under `timeout --foreground` so Ctrl-C reaches `ros2`; `trap '' HUP PIPE` plus a README line about tmux/nohup (the final report returns 0 even if stdout is closed); the preflight warns for each CUBE node that is not `active`; a failing probe of a never-configured node is reported as a failed probe ("no CUBE node was reset"), not a failed reset, via a `probing` flag instead of `changed`; the refusal advises a NEW empty `draft_dir` plus `--allow-persistence`, never `draft_dir ''`.)* `package.xml` gains `exec_depend` on `ros2lifecycle` and `ros2param` (the script shells out to both).
   `test/test_reset_cube_nodes.sh` runs it against a stub `ros2` on `PATH` holding per-node state, registered with `add_test`, **three cases at first, eight after round 1, ten case groups after round 2** (adds `timeout --foreground` on every call, SIGHUP and SIGPIPE survival, probe-failure wording and the preflight warning; case 6b now probes last: the never-configured node comes first in the argument order and the `draft_dir` is on the last node): consumer ordering,
   refusal on non-empty `draft_dir`, failing transition naming the node (and the state/re-run output), preflight of a mistyped consumer and a finalized node, `--consumer` not swallowing a flag,
   the never-configured-node probe (three variants), a hung node under the timeout, and stderr noise. The "already inactive" and four-node permutations are covered by the live check (step 11), not by the stub.
   The script must not block PR (a): if it slips, (a) ships with the README command sequence and the script follows.
10. **Docs.** README section "Starting a fresh sheet without restarting": the exact command sequence for four namespaced nodes
    (`/boat1/cube_bathymetry` ... `/boat4/cube_bathymetry`), what is and is not cleared, the `draft_dir` caveat, ordering with consumers. It records the **event configuration
    consequence**: `bizzyboat.yaml` sets `draft_dir: /home/field/data/world/depths` for `/**/cube_bathymetry` and `bizzyboat_sim_core_launch.py` layers that file, so any launch that loads it makes a lifecycle cycle reload old tiles and
    "fresh sheet" false; the demo launch (unh_marine_simulation#88) must leave `draft_dir` empty, which the host has already told that implementer. The script refuses loudly if it is not.
    *(The README also states, after the pre-push review: an empty-`draft_dir` reset permanently discards the in-RAM survey; the named boats publish no `grid` between the quiesce and the bring-up, so it is not for use underway;
    a wrong `read_only` value (`capture_spacing_scale`, `publish_dirty_subwindow`) needs a restart; and a cleanup whose flush failed refuses. After round 2: do not clear `draft_dir` with `''` (no persistence and no eviction, so RAM grows on a long survey, and cleanup flushes against the directory the node was configured with); point it at a new empty directory and pass `--allow-persistence`.)*
11. **Live verification** on a built node (no persistence): run the script against a real `cube_bathymetry_node` (own `ROS_DOMAIN_ID`, localhost discovery), confirm `ros2 lifecycle get` ends `active`
    and the first `grid` after the reset contains none of the previous window's cells; record in progress.md. Full-scope `ci_local.sh` before push.
    *(Done 2026-10-09 with a throwaway publisher feeding two patches 120 m apart; recorded in progress.md `## Implementation`. Full-scope `ci_local.sh` passed before push; the attestation is in `refs/notes/ci-local` and the run is recorded in progress.md.)*
12. **Follow-up named, not built:** a `clear` service (draft below under Follow-ups). Filing the issue needs the owner's yes.
13. **Stop path (added after the third Copilot pass, 2026-10-12, owner's decision to fix in this PR).** Live checks on the
    PR build showed (i) SIGINT/SIGTERM, the way `ros2 launch` stops a node, shuts the context down with no lifecycle
    transition, so `on_shutdown` never ran and every normal stop lost what the periodic save had not written (30 s on
    BizzyBoat); (ii) a `shutdown` from ACTIVE flushed but left the timers, subscriptions and publishers running, so a
    FINALIZED node kept publishing its catalog. Fix: `main()` runs `spinUntilStopped()` (flush after the executor
    stops); `on_cleanup` and `on_shutdown` share `releaseConfiguredEntities()` and the stop paths share
    `flushForStop()`. Same pass: `save_interval`/`catalog_interval` <= 0 fall back to defaults (as
    `disk_serve_interval` already did), and `reset_cube_nodes.sh` refuses a node named twice or as both CUBE node and
    consumer (case 11). Tests fail against the old code; live re-run in progress.md.

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
  The accumulator holds elevation and uncertainty only; coverage is judged at assessment time from depth (below). Memory grows with surveyed cells (~8 B/cell plus paging); logged every 10 s.
- **Frames (new).** The raster frame is the `map_frame` parameter. For each message the wrapper does **one** `lookupTransform(message_frame <- map_frame)` (latest available; this is per message,
  not per cell) and hands the core a horizontal affine; lattice centre -> earth -> source frame is exactly that composed transform with z = 0, since only the horizontal placement matters
  (`elevation` is an ellipsoid height, frame independent, so it is never transformed; the z error from tangent-plane curvature does not move the pull lookup measurably at event scale).
  A message whose transform is unavailable is **skipped with a throttled WARN naming the source and frame**, and counted in the node's diagnostics; the source is never silently dropped.
  **A source that cannot be placed withholds the assessment (#406, revised after the B1 spike).** #406 says that if any source frame has no georeference, the assessor publishes no
  `CoverageAssessment` and no `coverage_grid` for the area until it is resolved, since the message cannot represent a failed source and zeros would read as uncovered. Two cases are kept apart:
  a source that has **sent no grid yet** scores zero, which is true; a source whose **latest grid could not be placed** (transform unavailable) withholds every area's assessment, with a throttled
  WARN naming it, until one of its grids is placed again. The B1 spike measured `earth` absent from TF for about the first 10 s after launch, so the assessor starts by withholding and must not treat that as an error.
  Polygon vertices are projected with exactly the chain `grid_projection.cpp` uses for cell centres: `GeoPointLatLongDegrees(lat, lon, 0)` -> ECEF -> `map_frame <- earth` affine
  (one `lookupTransform` per assessment, cached per area until the area or the transform changes) -> x,y.
- **Water surface.** `h_surface` = geodetic altitude of the origin of `earth <- water_surface_frame` (with origin height 0 this is `map_tide.z`; going through `earth` keeps one code path for any frame).
  **Which boat's `map_tide`:** exactly one, named by the `water_surface_frame` parameter (e.g. `boat1/map_tide`); the same surface is applied to every source. Consequences, stated rather than discovered:
  (1) a source on a boat far from the tide boat sees a surface that is only as good as the one-surface assumption (README: valid for an area of a few km, same body of water);
  (2) **when that boat's estimator withholds `map_tide`** (stale beyond `water_surface_max_age`, or not published), the assessor stops scoring **everyone**: it skips the assessment with a throttled WARN, keeps
  accumulating grids, and the last assessment stays on the transient_local topic (the scoreboard shows an old value, the log says why). (3) The curvature bound above: the assessor measures d, the distance of the
  tide boat (`water_surface_boat_frame`, e.g. `boat1/base_link`) from the origin of the tide's parent frame, and above `water_surface_max_boat_distance` it **warns and skips scoring**, the same behaviour as (2).
- **Rasterising and judging.** Polygon (raster-frame metres) -> scanline even-odd fill; a cell belongs to the area when its **centre** is inside (documented half-open rule), so
  `area_total` = cells x cell_size^2 exactly as #406 defines. Exactly the #406 rules (revised after the B1 spike; the message text wins over this plan wherever they differ):
  depth = `h_surface - elevation`, positive down. A source **covers** a cell when it has a finite depth >= 0 there (a cell above the water surface has no survey depth; uncertainty is not
  needed for coverage); the cell is covered when any source covers it. To judge the standard, only sources with a finite depth >= 0 **and** a finite uncertainty >= 0 compete; the one with the
  smallest uncertainty supplies the depth; **ties go to the shallower depth** (fail closed, since allowed(depth) grows with depth); the cell meets when `u* <= sqrt(a^2+(b*depth)^2)`.
  A covered cell where no source competes does not meet. **A standard that is not set** (a = b = 0, or a or b NaN, negative or infinite) is checked first: nothing meets it, whatever the uncertainty.
  Per source: covered and meeting judged on its own grid with its own depth; `area_unique` = cells only that source covers. The output field is `area_meeting_standard` (not "meets").
  Sources keep the order of `grid_topics` in every message.
- **Negative depths are never hidden.** A cell whose depth is negative (seabed "above" the surface) is not covered (#406), but each assessment **counts the cells with depth below about -1 m**
  and logs a throttled WARN with the count and the surface height used, so a wrong sign, datum or stale tide cannot pose as a plausible verdict. The count is also in the node's diagnostics and tested.
  (The B1 spike found none in the sim: depths 12-23 m at Portsmouth, within ~0.2 m median of the chart grid.)
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

**Review fixes after the first pass (owner-approved; they supersede the text above where they differ).**
- **Map-origin lock (owner decision).** The raster georeference (`map_frame <- earth`) is captured at the first successful lookup after configure and used for BOTH polygon projection and source placement (source frame -> earth -> captured raster); the plan's "re-projected whenever the transform changes" and "`lookupTransform(message_frame <- map_frame)`" no longer apply. Each tick compares the live transform with the captured one (1 cm / 1e-6 rad); beyond it the node logs an ERROR naming the frame and the shift and withholds everything until reconfigured. No silent re-projection.
- **`max_total_raster_cells` (Copilot review of PR #166).** New read_only parameter, default 4000000, at least 1: a total bounding-box cell budget across all held areas, checked in the registry when an area is projected (an update of a held id is judged with its old box removed); an area over it is refused with an ERROR naming the budget. Reason: each pass scans every area's whole box once per source, so 20 areas at the per-area cap would block the single-threaded node for ~12 s. The 10 s status line also reports the last pass duration. Parameter table above gains the row.
- **`max_areas` (owner decision).** New read_only parameter, default 20, 1 to 100 else configure fails; a new id beyond it is refused with an ERROR, so the 100-deep transient_local history holds the newest assessment of every area and the per-pass cost is bounded. Parameter table above gains the row.
- **Unset standard.** A NaN, negative or infinite `a` or `b` is accepted as "not set" (SurveyStandard.msg), not rejected: the "Robustness decisions" list above no longer rejects on the standard.
- **Area arrival.** Deviation from "the registry replaces by id only if stamp >= the held one" judged on arrival: definitions are validated on arrival (vertex count, finite coordinates), queued per id, and handed to the registry in stamp order once `earth` is known; the stamp is judged against the last ACCEPTED definition.
- **Unplaced source retry.** Deviation: the latest unplaced grid is kept (one per source) and folded in when a per-tick retry of its frame succeeds, instead of only clearing the flag, so a silent boat's last grid is not lost. The flag also clears as soon as the transform resolves, whether or not the grid then turns out usable.
- **Bounds.** Raster-frame coordinates beyond 1e7 m (or 1e12 lattice cells) are refused; the per-message footprint cap is 4e6 cells (was 1e8).
- **Throttling.** The node keeps its throttled log lines per node (the rclcpp `*_THROTTLE` macros are per call site for the process).
- Also: callback exception containment, incompatible-QoS ERRORs, the tide boat's transform age check, the explicit `earth`-parented tide frame refusal, `-O2` default build type, the launch handler matching `start_state='configuring'`.

**Steps (build order). B1 is the first thing done in (b), before any package code; B3-B6 build and pass in CI without #406 on their own, B7 is what needs it.**

- B0. Re-read #406 (merged by then) and compare field names to the plan; until it is merged, create the local, **uncommitted** `upstream.repos` override and a layer worktree of the #406 branch.
- B1. **Datum and frame spike, before any wrapper code.** A throwaway script (not committed; findings recorded in progress.md) on the running sim: for each source, print `h_surface - elevation` (with `h_surface` read from `earth <- map_tide` exactly as the wrapper will) and the source's `header.frame_id`, and measure the
  **pinhole share** inside surveyed swaths of single `grid` messages (NaN share between finite cells; a stationary boat repeats the same phase so pull resampling cannot fill it), comparing `area_covered` from a direct count with what the accumulator will report. Its results decide the frame handling above
  (confirm the `<ns>/map` frames and that `lookupTransform` between them works), the sign of depth, and whether a pinhole fill is needed. If the spike contradicts a decision here, the plan is revised before B2.
- B2. Package skeleton, `package.xml` (explicit dependencies: `grid_map_core`, `grid_map_ros`, `grid_map_msgs`, `rclcpp`, `rclcpp_lifecycle`, `tf2`, `tf2_ros`, `tf2_eigen`, `tf2_geometry_msgs`, `marine_autonomy`, `marine_interfaces`), core library target, and CI plumbing: `.github/ci/Dockerfile` COPY of `coverage_assessment/package.xml` **and** the `--from-paths` argument of both `rosdep check` and `rosdep install` (currently `src cube_bathymetry`, Dockerfile ~45-52), plus the `ci-image.yml` path trigger.
- B3. `polygon_raster` (+ tests): axis-aligned squares at several offsets (exact area), triangle vs analytic area within one boundary row, L-shape, edge on a lattice line, negative coordinates, < 3 vertices, zero-area, oversize rejection.
- B4. `source_accumulator` (+ tests): phase-shifted messages of the same surface give identical coverage; a message in a rotated/translated frame (affine) gives the same coverage as the same message in the raster frame; NaN does not erase, **per field** (finite elevation with NaN uncertainty keeps the held uncertainty); later finite values overwrite; **moving window** (3 windows sweeping a strip) leaves coverage equal to the union and never decreasing; elevation without uncertainty is held; `clear()` empties.
- B5. `coverage_assessor` + `area_registry` (+ tests): known areas; two overlapping sources (`area_unique`, sums exceed totals); a missing uncertainty does not meet; depth-dependent threshold (same uncertainty fails shallow, meets deep); equality meets; depth taken from the smallest-uncertainty source (two sources, depths straddling the threshold); per-source judged on own grid while the union meets; equal uncertainties: the shallower depth supplies the verdict; a source with depth but no uncertainty covers but does not compete; standard not set (a = b = 0, NaN, negative, inf): nothing meets; water-surface shift changes the verdict; **negative depth** (not covered; cells below -1 m counted); registry replace-by-id, stale stamp ignored, invalid area keeps the old one.
- B6. `assessment_raster` (+ tests): `meets_standard` values, overlapping areas with different standards, bounding box and frame.
- B7. ROS wrapper (needs #406): conversions, polygon projection, per-message source-frame transform (skip + throttled WARN when unavailable; a source whose latest grid could not be placed withholds the assessment), water-surface height with the age and distance refusals, node class + thin main, `read_only` parameters, launch file, install rules, tf2/`ros2` exec dependencies.
- B8. Wrapper tests, **happy path, frame handling, stale tide, reset-empties-union only** (per-test `ROS_DOMAIN_ID`, `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`): (i) projection agrees with `cube::geoGridsToGridMap`'s cell placement for the same lat/lon under a known `earth -> map` (polygon and grid share one chain); (ii) node integration over a static TF broadcaster (`earth->map`, `earth->map_tide`) and synthetic `GridMap` messages: exact numbers on `coverage_assessment`/`coverage_grid`; (iii) frame handling: two sources in different `<ns>/map` frames both credited; a source whose grids cannot be placed withholds every area's assessment with the WARN, and scoring resumes once it is placed; a source that has sent nothing scores zero; (iv) stale tide, and a tide boat beyond the distance bound, skip scoring; (v) deactivate->cleanup->configure empties the union.
- B9. Docs: `coverage_assessment/README.md` (design decisions above, the d^2/2R bound and the one-surface assumption, which boat's tide is used and what happens when it is withheld, memory note, rasterisation rule), top-level README Nodes row + parameter table, reset section updated to name the assessor as `--consumer`.
- B10. Sim check before the event: run against unh_marine_simulation's CUBE grids and confirm depth below the surface comes out positive and plausible across all four boats' frames (the spike's answer, now end to end). Full-scope `ci_local.sh`; `git diff -- upstream.repos` must be empty; mark ready only after #406 is on `jazzy`.

## Files to Change

| File | Change |
|---|---|
| `cube_bathymetry/src/cube_bathymetry_node.hpp` | (a) class moved here (private to the package, not installed), `declareOnce`, state resets, release order, `sheet()` |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | (a) thin `main()`, running `spinUntilStopped()` (step 13) |
| `cube_bathymetry/CMakeLists.txt` | (a) interface link target, `test_node_lifecycle` (own domain id), script test, install script |
| `cube_bathymetry/package.xml` | (a) `exec_depend` `ros2lifecycle`, `ros2param` |
| `cube_bathymetry/test/test_node_lifecycle.cpp` | (a) new: reconfigure, empty start, draft_dir characterization |
| `cube_bathymetry/test/test_publish_equivalence.cpp` | (a) elevation sign test |
| `cube_bathymetry/scripts/reset_cube_nodes.sh`, `cube_bathymetry/test/test_reset_cube_nodes.sh` | (a) new (stubbed-`ros2` test, eleven cases) |
| `.agents/ci_local_upstream_extra.sh` | (a) new: `ci_local.sh` prune hook mirroring hosted CI's `AFTER_SETUP_UPSTREAM_WORKSPACE` (without it `ci_local.sh` cannot build the upstream monorepo; found when the attestation first ran, round 2) |
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
| project 0001 (eviction / never lose survey data) | Yes | Reset documented as not deleting disk data; flush happens in `on_deactivate`/`on_cleanup`, on `shutdown` and on SIGINT/SIGTERM (step 13); `draft_dir` reload semantics pinned by a test |

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
