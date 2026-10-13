# Plan: In-RAM reset service: start a new live session on cube_bathymetry and coverage_assessment without a lifecycle round trip or any disk change

## Issue

https://github.com/rolker/cube_bathymetry/issues/167

## Context

`reset_cube_nodes.sh` starts a fresh sheet by walking each node through deactivate, cleanup, configure, activate. On the
four-boat simulation that is 318 to 324 s, against game rounds of about two minutes. The time is `ros2` CLI start-up
(3 to 8 s per call under load), not the transitions: the script makes about 17 `ros2` calls per CUBE node today
(preflight `lifecycle get` plus two `param get`, then roughly seven calls to walk down and seven to walk up). This
issue adds a `std_srvs/Trigger` service `~/reset` to `cube_bathymetry_node` and to `coverage_assessment_node` that
starts a new session in RAM and writes no tile to disk (a legacy prior-store `survey/` directory can be renamed on load, as configure already does), and makes the script use it (about 2 calls per node).

Scope boundaries (issue text, owner decisions 2026-10-13): the CUBE service refuses while `draft_dir` is set, so
real-boat configurations such as `bizzyboat.yaml` are NOT served by this slice; the on-disk session work belongs to
rolker/unh_marine_autonomy#391. The sim and the demo (no `draft_dir`) are the beneficiaries, and the README and the PR
body say so plainly. The owner approved editing `AGENTS.md:54` to match the README/ADR-0001 wording fix. This is part
of #164 (umbrella, still has open items): the PR body says "Part of #164" and carries no closing keyword for it.

Facts verified in source that shape the design:

- `on_configure` builds the sheet, then calls `setCaptureSpacingScale` (can throw) and `setBackscatterCorrection`
  on it, primes it from `prior_store_dir` and, with `draft_dir` set, from the store, and trims to `max_resident_tiles_`
  (`cube_bathymetry_node.hpp:126-518`). The backscatter curve normally comes from the latched `sonar_info` topic and
  lives in `bs_applied_curve_`; the subscription survives a reset, so it is not redelivered.
- `trimResidentToBudget()` writes the member `evicted_indices_` and `refresh_tracker_`, and the draft prime fills the
  member `catalog_builder_`: building a sheet aside needs those three to be candidate-local until the swap.
- Every callback of the node's own entities (pings, timers, subscriptions) lives in the node's default callback
  group, which is mutually exclusive, and no package code creates a callback group (grep). The other groups are
  made by libraries for their own threads (`automatically_add_to_executor_with_node` is false, so the node's executor
  never spins them): `tf2_ros::TransformListener`'s (`/tf`, `/tf_static`) and, under `use_sim_time`, rclcpp's clock
  thread (`/clock`); they only feed the TF buffer and the clock, which a reset does not replace.
  `main` also uses a `SingleThreadedExecutor`. Either fact alone makes a swap inside a callback safe without a mutex.
- The `grid` header stamp is `GeoMapSheet::lastUpdateTime()` (`stampGrid`): the header stamp (ROS time) of the latest
  ping that changed a tile, carried as `epoch + header.stamp` in a `steady_clock` type (`pingCallback` ->
  `addSoundings` -> `last_update_time_`), and 0 on a sheet with no insert yet. A guard "drop grids stamped before the
  assessor's reset time" is therefore possible and fails safe (a window re-published after the reset carries every
  cell and a newer stamp), but it only works if the sonar stamps and the assessor's clock share a time base across
  hosts. This slice does not add it (see step 16).
- `ros2 service type <name>` prints the type and exits 0 when the service is in the graph, and exits 1 with no output
  when it is not (verified on jazzy: `ros2service/verb/type.py`, and a live call on a name that does not exist).
  `ros2 service call` waits for the service without bound, so its outcome cannot tell "absent" from "slow".

## Approach

### A. `cube_bathymetry_node`: one path for "start a fresh session"

1. **Hold the sheet-level settings as members.** Add `capture_spacing_scale_` (set where configure reads the
   parameter). `bs_mode_` and `bs_applied_curve_` already exist. These plus `cell_size_`, `prior_store_dir_`,
   `draft_dir_`, `max_resident_tiles_` are everything the builder needs.
2. **Extract `buildSession()` from `on_configure`.** It constructs a candidate (`struct FreshSession { sheet,
   evicted_indices, catalog_builder, prior_status }`, the last being the text the reset response reports about
   `prior_store_dir`), entirely local: new `GeoMapSheet(cell_size_)`,
   `setCaptureSpacingScale(capture_spacing_scale_)`, `setBackscatterCorrection(bs_mode_, bs_applied_curve_ ...)`
   (a COPY of the stored curve's points, never a move: configure moves from a local copy, and moving the member would
   blank `bs_applied_curve_` on the second reset; never re-reading the file or the topic), the `prior_store_dir` prime,
   the `draft_dir` prime and catalog seed (unchanged code, only retargeted at the candidate), and the trim. The two
   configure texts that read the sheet before the prime (the "Capture distance" INFO and the CA-window coherence WARN,
   which uses `nominalCellSizeMeters()`) are computed from the candidate (or from `cell_size_`), not from the live
   member, so configure behaves as before and the builder never reads `geo_map_sheet_`. `trimResidentToBudget()` becomes a
   thin member wrapper over a function taking `(GeoMapSheet &, evicted set &, RefreshTracker *)`, so the candidate
   trim never touches the live tracker (a fresh tracker owes nothing). It throws (or returns an error) instead of
   returning `CallbackReturn::FAILURE` from inside: configure maps the legacy-`survey/`-dir refusal to FAILURE as today.
3. **Extract `adoptSession(FreshSession &&)`:** swap in the sheet, `evicted_indices_`, `catalog_builder_` and clear the
   remaining session state: publish cursor (`last_grid_publish_time_`, `have_publish_tf_`, `last_publish_tf_`,
   vessel fix), disk-serve queue and set, `refresh_tracker_.clear()`, saving ledger. This is the single list that today
   sits inline in `on_configure`; configure calls `buildSession` + `adoptSession`, the service calls the same two.
   Configure keeps the rest (publishers, subscriptions, timers, `refresh_parameters_handle_` reset and re-register,
   which the service need not touch). `on_configure` declares and reads every parameter the builder needs *before*
   calling it, so the order of side effects changes only where the sheet is constructed.
4. **Audit session-scoped members.** Grep the members written in `pingCallback`, `publishBounded`, the timers and
   `saveDirtyTiles` for anything not in the 11-field `staleFields` list; add any found to `adoptSession` and to the
   test accessor, so the parity test covers them.
5. **`~/reset` service** (`std_srvs/srv/Trigger`), resolves to `/boat1/cube_bathymetry/reset`, matching the node's
   `~/coverage_tiles` convention. Created as the **very last step of `on_configure`**, after
   the `sonar_info` subscription (which can throw), and the old handle is `reset()` first: a configure that throws after
   creating it returns the node to UNCONFIGURED without `on_cleanup`, so a service created earlier would outlive the
   failed configure and "service absent means not configured" would be false. Released in
   `releaseConfiguredEntities()` with the others, so "service absent" means "not configured" (or finalized) and a
   caller can tell that from the graph alone. States: answers in `inactive` and `active` (a sheet exists; a reset while
   inactive is harmless and the message says the node is not publishing); the handler also returns failure when
   `geo_map_sheet_` is null, because services are not state-gated.
   Handler order: (a) refuse if the configured `draft_dir_` member is non-empty (not the writable parameter, which can
   change between cycles; same source `on_cleanup` uses): `success=false`, message names the directory, says saved
   tiles would reload so the sheet would not be fresh, and points at `--allow-persistence` with a new empty `draft_dir`;
   (b) `buildSession(prior_must_load=true)` inside try/catch (`std::exception` and `...`; a prior store that cannot be
   loaded fails the reset, while configure keeps its warn-and-continue); any failure leaves `geo_map_sheet_` and every other member untouched and
   returns `success=false` with the reason; (c) remember the old tile count, `adoptSession`, return `success=true`
   with a message saying what was reset: "N tile(s) dropped from RAM; prior_store_dir <unset | primed X Reference + Y
   Chart tile(s)>; node is <active | inactive: no grid until activated>; no tile written to disk (loading a prior
   store renames a legacy survey/ directory, as configure does)". The old sheet is destroyed after the swap, last.
6. **Comment the concurrency argument** at the handler: default mutually exclusive callback group plus single-threaded
   `main`; the prime is a whole-store disk read that **blocks the executor for its duration while the node is active**
   (unlike configure). The ping subscription is `SensorDataQoS` (best effort, depth 5), so pings arriving during the
   block are DROPPED once five are queued, not held; the README states that cost (a reset is not for use underway).
7. **Dependencies:** `std_srvs` in `package.xml` (`<depend>`), `find_package(std_srvs REQUIRED)` and the
   `cube_bathymetry_node_link` interface target (`${std_srvs_TARGETS}`) in `CMakeLists.txt`. `std_srvs` is in
   `ros-jazzy-ros-base` (the CI image is `FROM ros:jazzy-ros-base`) and present in `/opt/ros/jazzy`, so the image and
   rosdep need no change; confirm by a clean `./sensors_ws/build.sh` of both packages.

### B. `coverage_assessment_node`: `~/reset`

8. **`resetSession(std::string * message)`**, called from a `~/reset` Trigger handler (`onReset`) that has the
   containment of the node's `guarded` pattern but its own try/catch, because the exception text must reach the caller
   (an exception becomes `success=false` with the text, is logged and counted in `callback_exceptions`; the fault
   injector name is `"reset service"`). Service created last in `configureEntities`, released first in
   `releaseConfiguredEntities`. For tests: a public `heldCellCount()` and a `CoverageAssessmentTestAccess` friend that
   calls the handler directly when no service exists.
   - Refuse when not configured (`registry_` or `tf_buffer_` null).
   - **Refuse when the map frame has moved**: when `have_georef_` is set, run `checkGeorefMoved()` first (the latch
     only updates on ticks, so a stale `false` is possible while inactive). Only when `have_georef_` is set: before the
     first capture `raster_from_earth_` is still the identity, so the comparison would latch `georef_moved_` falsely
     and the refusal would be bogus; a node that has not captured a georeference has nothing to protect and is reset
     normally. Message: the map frame moved since the georeference was captured; the georeference is deliberately
     kept by the service, so only the lifecycle walk (which re-captures it) recovers this.
   - Otherwise per source: `accumulator.clear()`, `have_grid = false`, `placement_failed = false`, `failed_frame`
     cleared, `unplaced_grid` reset (a kept grid would be folded in on the next tick and bring old cells back), the
     once-per-source warning flags and per-source log throttles reset (parity with a freshly built `SourceState`).
     Node: `held_limit_reached_ = false`, `grids_changed_ = false`, `force_assessment_ = true`,
     `have_last_surface_ = false` (so the next tick assesses even when the surface did not change).
     When the node is active, the georeference exists and the areas are projected, the reset also publishes the emptied
     `coverage_assessment` and `coverage_grid` itself (`publishEmptyAssessment`; with nothing held the result does not
     depend on the surface height) and puts the flags above back, so the old run's coverage is not left latched while
     the water surface is unavailable (pre-push review).
   - **Counters stay cumulative** (owner decision 2026-10-13): `stats_` is not touched. `NodeStats` also holds
     `areas_received`, `areas_rejected` and `callback_exceptions`; the service keeps the areas, so zeroing would print
     "areas 0 received, 0 rejected, N held", and zeroing `callback_exceptions` would erase evidence of a fault. The
     configure-parity argument does not hold for those fields (configure also drops the areas). Instead the reset logs
     one INFO line ("reset: N cells dropped from M source(s); counters are cumulative"). `throttle_times_` is cleared
     (log throttles only; the per-source throttles are reset with the source state above).
   - Kept on purpose: areas, registry, projections, `raster_from_earth_`, `have_georef_`, subscriptions, TF.
   - Success message: cells dropped, number of sources, areas kept.
9. **Update the held-limit ERROR text** (line ~1159) to name the service first: "reset with the ~/reset service (or
   deactivate, cleanup, configure, activate) or restart with a larger max_held_cells". The map-moved ERROR keeps the
   lifecycle walk and now adds that the service refuses in this state.
10. **Dependencies:** `std_srvs` in this package's `package.xml` and `CMakeLists.txt` (node link target), as in step 7.
    (The executable is `SingleThreadedExecutor` here too; callbacks share the default group.)

### C. `scripts/reset_cube_nodes.sh`

11. **Classify before changing anything, with one call.** One `ros2 service list -t` (via the read-retry rules: a failure,
    a timeout or an EMPTY list is "indeterminate", retried `RESET_CUBE_GET_TRIES` times, then the script dies before any
    change; an empty list cannot be a real graph, since every node exposes its parameter services) gives the set of
    services and their types in the graph. A node whose `<node>/reset [std_srvs/srv/Trigger]` is in the list (a `/reset` of
    another type is not) is `service` class; a node that is not in a
    non-empty list is `walk` class (older binary, unconfigured node, wrong name, or discovery lag). A wrong name then
    dies in the existing lifecycle preflight exactly as today, and a false "absent" under discovery lag is safe: it only
    sends that node down the walk, whose own preflight still refuses `draft_dir`. `--allow-persistence` sends every CUBE
    node down the walk, as today; the consumers are classified normally. (This replaces N `ros2 service type` calls by
    one; `service type` exiting 1 with no output remains the verified clean-absence signal for a single name, but the
    list is cheaper for a fleet.)
12. **`ros2` calls, counted** (comment block in the script and a row in the README):

    | Path | Calls |
    |---|---|
    | classification | 1 `service list -t`, shared by every node |
    | service node (CUBE or consumer) | 1 `service call` (no `lifecycle get`, no `param get`, no final `print_states`: the service refuses on `draft_dir` itself and reports its own state) |
    | walk node (unchanged) | about 17 per CUBE node, about 14 per consumer, plus the final `print_states` |
    | four boats plus assessor, all on services | 6, against about 80 today |

    At 3 to 8 s per call under load that is about 18 to 48 s sequentially, against 318 to 324 s today. The CUBE service
    calls are independent and idempotent and could run in parallel, but the range is already well under a two-minute
    round, so the script keeps them sequential (simpler failure reporting); the live check (item 20) judges whether
    that is enough.
13. **The call.** `ros2 service call <node>/reset std_srvs/srv/Trigger '{}'` under `timeout --foreground` with its own
    bound `RESET_CUBE_CALL_TIMEOUT` (default: the transition bound, 300 s, because the prime is a whole-store disk read
    that the walk's configure is allowed the same time for). The verdict is read from the START of the response line
    (`Trigger_Response(success=True,` / `success=False,`), never from a substring, because a refusal message echoes a
    user-supplied path that can contain `(success=True,`. A reset is idempotent (it only replaces RAM state), so,
    unlike a transition, **a call that fails fast is retried** (own budget `RESET_CUBE_CALL_TRIES`, default 2); a
    call that **times out is not**: its request is still queued in the node and a retry would queue a second
    whole-store load. The script then says the node may or may not have been reset. "No service" is decided only by the classification, never from
    a call outcome, so a slow node is never sent down the walk after it already reset. `success=False` from a CUBE node
    is a refusal: print its message, stop.
14. **Order: everything that can refuse runs before the first change.** (a) classification; (b) the existing lifecycle
    preflight (0a state check, 0b `draft_dir` / `prior_store_dir` reads, 0c never-configured probe) for **every walk-class
    CUBE node and every walk-class consumer**, so a mistyped `--consumer`, a finalized node or a refusing walk-class
    node is found before any service call, as the old script guaranteed; (c) service-class CUBE nodes, one `service
    call` each; (d) the walk-class CUBE nodes to unconfigured; (e) consumers (service call, or the walk); (f) the
    walk-class CUBE nodes back to active. Service-class CUBE nodes come first so that a `draft_dir` refusal, which only
    the service can report, is found before the script has taken any boat out of `active`. The remaining partial result
    is accepted (owner decision 2026-10-13): a boat with `draft_dir` set is found when the script reaches it, so
    service-class boats earlier in the list are already reset (idempotent, RAM only, no tile written). The script
    **stops at the first refusal**, prints which nodes were reset and which refused, does not touch consumers or the
    walk-class nodes, exits 1, and says re-running is safe. A consumer refusal (map moved, or any reason) falls back to
    the lifecycle walk for that consumer, printing the reason: the walk re-captures the georeference, so it is the
    superset.
15. **Final report and the inactive case.** The service path has no `print_states`; the script prints each service
    node's response message (what was reset, active or inactive), and `print_states` only for nodes that took the walk.
    A node whose response says it is inactive (the state is read from the last `; node is <state>` clause, since
    earlier text can be echoed) was reset but would publish no `grid` (CUBE) or assess nothing (consumer): the script
    **activates it** right after its reset, names it, and ends with "reset done; activated after the reset (they were
    inactive): <nodes>" and exit 0. (First drafted as a warning and exit 4; the pre-push review found that a node a
    failed run left inactive is configured, so it serves `~/reset`, and a re-run would then reset it and exit 4
    forever, never activating it. Owner decision 2026-10-13: reset, then activate, so re-running converges; exit 4 is
    retired.) A failed activation is a failure with the re-run hint. The old preflight warning for a not-active CUBE node is kept for walk-class nodes.
16. **Restate the ordering claim honestly in both directions** in the script header and README. The walk quiesced every
    CUBE node before the consumer was reset. With services the CUBE nodes keep publishing, which has two consequences.
    (i) A `grid` published by an old sheet just before its reset and still queued in the assessor's subscription
    (reliable, depth 10, at most about one per node per 5 s) when the assessor is reset would be folded into the empty
    union and stay (unions only grow). (ii) The other direction: new-session cells that a reset CUBE node accumulates
    before the assessor is reset are wiped from the union, and return only through the next re-published 200 m window
    (about 5 s), so cells outside that window are lost. Both windows are the CLI gap between the CUBE call and the
    consumer call, seconds long for (ii); the stated condition is that **the boats are not underway** (a reset is for a
    stationary or simulated vessel), and the ordering (consumer only after every CUBE node has answered) bounds (i). A
    stamp guard against (i) is possible but not added: it depends on the sonar stamps and the assessor's clock sharing a
    time base across hosts, and it does nothing for (ii). A consumer that goes down the walk is immune: its subscription
    is destroyed.
17. Update the script header (usage, order, new variables `RESET_CUBE_CALL_TIMEOUT` and `RESET_CUBE_CALL_TRIES`, exit
    status), keep `usage()` working (it prints the comment block from `# Usage:` through the exit-status lines). Add
    `<exec_depend>ros2service</exec_depend>` to `cube_bathymetry/package.xml` (the existing comment there already lists
    the `ros2` verbs the script shells out to).

### D. Tests

18. **`test_node_lifecycle.cpp`** (a real `Trigger` client and an in-process `SingleThreadedExecutor`; a helper
    `callReset(node)`), sharing one assertion helper with `ReconfigureResetsTheSessionState`:
    - `ResetServiceResetsTheSessionState`: dirty the 11 fields (plus any step 4 adds), call the service, expect every
      name from `staleFields` clean, new sheet object (old `weak_ptr` expires), `catalogSize()==0`, node still active.
    - the fresh-sheet test of `EmptyStartWithoutPersistence` through the service (no finite cell, no dirty grids), and
      the new sheet accumulates only new data (no cell of the old patch returns). The "next ping builds a window"
      form is not tested: `pingCallback` needs TF and the whole publish chain, and the window is built from the
      sheet's grids, which this test already pins.
    - `ResetKeepsSheetLevelSettings`: launch override `capture_spacing_scale=0.9` and a curve delivered through
      `sonarInfoCallback`; after reset `sheet()->parameters()` has the same scale, mode, curve, `tl_removed` and
      absorption as before (fails if a bare `make_shared<GeoMapSheet>` is used). A second `sonar_info` heartbeat with the
      same curve stays silent afterwards. **Two resets in a row** keep the same settings (fails if the stored curve's
      points are moved into the candidate instead of copied).
    - `ResetIsRefusedWhileDraftDirIsSet`: `success=false`, message names the directory, the sheet pointer is unchanged,
      resident tiles still there, and the directory listing (names, sizes, mtimes) is identical before and after.
      Plus: `draft_dir` parameter set to `""` after configure is still refused (member, not parameter); configured empty
      then parameter set to a path is not refused.
    - `ResetServiceExistsOnlyWhileConfigured`: absent before configure, present in inactive and active, absent after
      cleanup and after shutdown; handler called directly with a null sheet returns `success=false`.
    - `FailedResetLeavesTheOldSessionIntact`: a test-only hook on the builder throws after the candidate sheet exists;
      expect `success=false`, same sheet object, dirtied fields untouched, tiles still resident.
    - `ResetReprimesPriorStore`: a small prior store with one Reference tile (builder helpers as in
      `test_store_import.cpp`); the message reports primed tiles and the new sheet's predicted surface is seeded.
    - `ResetWhileInactiveSaysSo`: an inactive node is reset and the response says it publishes no grid until activated.
    - `ConfigureThatThrowsLeavesNoResetService`: a configure that fails after the sonar_info subscription would have
      been created (invalid `sonar_info_topic`) leaves the node UNCONFIGURED with no `~/reset` service in the graph.
    - `AllCallbacksShareTheDefaultGroup`: exactly one callback group is spun by the node's executor (the default,
      mutually exclusive one), no group is reentrant, and every group NOT spun by it holds no service, client or timer
      and subscribes only to `/tf`, `/tf_static` or `/clock`; run with and without `use_sim_time`. A control test shows
      the audit reports a reentrant group, an unspun timer and a foreign subscription. The assessor has the same pin.
19. **`test_coverage_assessment_node.cpp`** (harness as in `ReconfigureEmptiesTheUnion`): reset service empties the
    union and the next tick publishes `area_covered == 0`; areas, their projections and the georeference are kept
    (`area_total` unchanged, no new `Raster georeference captured` line); a kept unplaced grid does not return after
    the reset (source with an unresolvable frame, then reset, then resolve the frame: no old cells); `held_limit_reached_`
    cleared (the `MaxHeldCellsStopsFoldingAndWithholdsWithAnError` setup, then reset, then assessments resume);
    counters stay cumulative (`stats()` unchanged by the reset, `callback_exceptions` survives); refused with the
    map-moved reason when `georef_moved_` is set (and also when the live transform has moved but the latch is not yet
    set), with the union untouched; **a reset before the first georeference capture succeeds and does not latch
    `georef_moved_` or refuse** (fails if `checkGeorefMoved()` runs without the `have_georef_` guard); the service
    exists only while configured and the handler refuses when unconfigured; an exception injected at the handler is
    contained, counted and returned as the reason with the state unchanged; a reset of an inactive node says so; a grid
    arriving after the reset is folded in normally.
20. **`test_reset_cube_nodes.sh`**: extend the stub with `service list` (prints a per-test list of services; flaky, hang
    and empty switches like the existing ones) and `service call` (sets state counters, prints the **real jazzy
    output format** captured from a live `ros2 service call` of a `Trigger` service during implementation --
    `requester: making request: ...`, `response:`, `std_srvs.srv.Trigger_Response(success=..., message='...')` --
    with `success=True/False` responses, and logs calls). New cases: all-service path (no `lifecycle set`, no `param
    get`, no `lifecycle get`, call count equals the table above, consumer after every CUBE node); a CUBE refusal from
    the service (message printed, exit 1, consumers and walk-class nodes untouched, earlier nodes reported as reset,
    stops at the first refusal); older binary falls back to the walk; mixed fleet (two services, two walk, one
    consumer on a service) with the order of step 14; **a mistyped `--consumer` or a refusing walk-class node dies
    before any service call**; `--allow-persistence` takes the walk for CUBE nodes; a consumer refusal falls back to the
    walk; **an empty or failing `service list` is retried and then dies before any change, and never selects the walk**;
    a `service call` that fails fast is retried (with `RESET_CUBE_CALL_TRIES`), one that times out is not, and neither
    selects the walk; a message echoing `(success=True,` in a path while `success=False` is still a refusal; an
    inactive node is reset and then activated (exit 0), and a re-run recovers a node a failed walk or probe left
    inactive; the state is read from the last clause; a `/reset` of another type walks; every new call still goes through `timeout --foreground`; the existing 12 cases keep
    passing (they exercise the walk path; the stub's default service list contains no `/reset`). The test header list
    of cases and the "12 case groups" comment at `CMakeLists.txt:407` are updated with it.
21. **Live check** on the four-boat simulation (work-list item): record the wall time of the all-service reset against
    the 318 to 324 s baseline, and note it in the PR. **Owner decision 2026-10-13: the host
    runs this check after implementation, not the implementation session.** The work item stays UNCHECKED, the PR body
    lists it as an open item, and the PR carries no closing keyword for #167 until it is done or the owner waives it.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/cube_bathymetry_node.hpp` | `buildSession` / `adoptSession` extracted from `on_configure`; `capture_spacing_scale_` member; trim function retargetable; `~/reset` service create/release/handler; concurrency comment; test hook |
| `cube_bathymetry/CMakeLists.txt`, `cube_bathymetry/package.xml` | `std_srvs`; `<exec_depend>ros2service</exec_depend>`; the "12 case groups" comment at `CMakeLists.txt:407` |
| `cube_bathymetry/scripts/reset_cube_nodes.sh` | one-call classification, service call, retry rules, order of step 14, inactive handling (reset, then activate), fallback, ordering comment (both directions), call-count comment |
| `cube_bathymetry/test/test_node_lifecycle.cpp` | service tests and shared parity helper (step 18) |
| `cube_bathymetry/test/test_reset_cube_nodes.sh` | stub verbs and the new cases (step 20), header list of cases |
| `coverage_assessment/src/coverage_assessment_node.hpp` | `resetSession`, `~/reset` service, held-limit and map-moved log text |
| `coverage_assessment/CMakeLists.txt`, `coverage_assessment/package.xml` | `std_srvs` |
| `coverage_assessment/test/test_coverage_assessment_node.cpp` | reset cases (step 19) |
| `README.md` | see Documentation impact |
| `coverage_assessment/README.md` | the "Starting a fresh assessment between runs" paragraph, a service row, the held-limit wording |
| `AGENTS.md` | line 54 "product of record" wording (owner-approved instruction-file edit) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | The service refuses with a reason and never touches disk; the response says what was reset; the script prints refusals and partial results; the fall back to the walk is announced. |
| A change includes its consequences | `std_srvs` in both packages, both READMEs, `AGENTS.md:54`, the held-limit text, the script header and its tests are all in this PR (see Consequences). |
| Test what breaks | Parity of the two reset paths (same stale-field list), sheet-level settings (twice in a row), failure leaves the old session, georeference-moved refusal and the pre-capture case, unplaced-grid carry-over, "absent" vs "timed out" and "everything that can refuse runs first" in the script, real CLI output format in the stub. |
| Only what's needed / Improve incrementally | RAM only; on-disk session identity stays with unh_marine_autonomy#391; no stamp guard (not feasible), no new ADR. |
| Capture decisions | Superseding "product of record" is a design change recorded in the README and the PR body, not an ADR (the world-store ADR belongs to #391). |
| Enforcement over documentation | The shared `buildSession`/`adoptSession` path plus the parity test enforce "no second hand-written reset list". |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| Project ADR-0001 (eviction, draft store) | Yes | "Persist before drop" is untouched: the service refuses while `draft_dir_` is set, so no unsaved tile is dropped. ADR text stays as history; the README says the "product of record" wording is superseded and points at unh_marine_autonomy#391 without restating it. |
| Workspace ADR-0008 (ROS 2 conventions) | Yes | `std_srvs/Trigger`, private `~/reset`, created and released with the other lifecycle entities. |
| Workspace ADR-0013 (progress.md vocabulary) | Yes | `## Plan Authored` entry appended. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `on_configure` split into build/adopt | `ReconfigureResetsTheSessionState`, `CleanupReleasesTheSheetAndCatalog`, `ReadOnlyOverridesSurviveACycle` must still pass unchanged | Yes (run in step 18) |
| New service on a node in a package pair | `std_srvs` in both `package.xml` and `CMakeLists.txt`; CI image and rosdep (`ros:jazzy-ros-base` already has it) | Yes |
| `reset_cube_nodes.sh` behaviour | its header, the README section, `test_reset_cube_nodes.sh` header list of cases, `CMakeLists.txt:407` case-group comment, `<exec_depend>ros2service</exec_depend>` | Yes |
| Held-limit and map-moved log text | `test_coverage_assessment_node.cpp` assertions on those strings (grep before editing) | Yes |
| "Product of record" wording | `README.md:151`, `AGENTS.md:54` | Yes (AGENTS.md owner-approved) |
| `.agents/README.md` is missing in this repo (noted in `AGENTS.md`) | creating it | No: a dedicated task, not this PR |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR):
  - `README.md` "Starting a fresh sheet without restarting": lead with the service and the one-line command, keep the
    walk as the fallback; the service refuses with `draft_dir` set; new step list and the call-count table; the
    ordering paragraph from step 16 (both directions, with "boats not underway" as the condition); the prime's blocking
    cost while active (pings are dropped during it); the inactive case (activated after its reset); which states answer; that a real boat with
    `draft_dir` set (`bizzyboat.yaml`, `draft_dir: /home/field/data/world/depths`) is **not** served until
    rolker/unh_marine_autonomy#391 and the 2026-08-27 operator request stays open; replace "the draft store is the product
    of record, ADR-0001" with what superseded it (live tiles are a derived product, the bags are the record) and the
    pointer. Add the `~/reset` row to the node tables and the new `RESET_CUBE_CALL_TIMEOUT` variable.
  - `README.md` coverage-assessment section (line ~258, `max_held_cells` row: "until reset") and line ~262.
  - `coverage_assessment/README.md` lines ~87-93: service first, what it keeps (areas, projections, georeference,
    cumulative counters) and that a moved map frame still needs the lifecycle walk; the held-limit wording; that when
    the water surface is unavailable after a reset the latched old `coverage_assessment` message stays published (the
    tick returns before assessing).
  - `AGENTS.md:54`: replace "the tile store is the product of record for live coverage" with wording that keeps the
    eviction invariant ("persist before drop") and drops the "of record" claim; the exact sentence goes in the PR body
    for the owner to see (he approved the edit, not the sentence).
  - Script header comment (usage, order, ordering claim, call counts).
- **Agent-instruction candidates** (proposals only): (1) a pitfall for `.agents/README.md` once one exists: "a lifecycle
  node's service is not state-gated; create it in `on_configure` and release it in the shared release function";
  (2) knowledge note candidate for `ros2_cli_best_practices.md`: `ros2 service type` exit 1 with no output is a clean
  absence, `ros2 service call` has no absence signal, so probe first; (3) the `grid` header stamp is the ROS-time stamp of the latest ping that changed a tile (held in a `steady_clock`
  type by `GeoMapSheet::lastUpdateTime()`), and 0 on a sheet with no insert yet; a consumer comparing it with `now()`
  must account for the sonar stamps' time base.

## Open Questions

All three decided by the owner on 2026-10-13.

- [x] **Partial reset on a refusal (step 14).** Accepted: the script stops at the first refusal, prints what happened,
  and exits 1 (re-running is safe; the service is idempotent and RAM only). No all-or-nothing `param get` preflight.
- [x] **Stats counters on reset (step 8).** The assessor's counters stay cumulative; the reset never zeroes them.
- [x] **Live check (step 21)** is run by the host after implementation, not by the implementation session. The work
  item stays unchecked.

## Estimated Scope

Single PR (two services, one script change, tests, READMEs, one instruction-file line). Part of #164; the PR body says
"Part of #164" and carries no closing keyword for #164. The PR carries no closing keyword for #167 either until the
host's live four-boat timing check (step 21) is done or the owner waives it; the PR body lists it as an open item. The largest risk is the `on_configure` split (step 2),
so it lands as its own commit with the existing lifecycle tests unchanged and green before the service is added.

## Implementation Notes

Rationale for revisions made after the plan review (2026-10-13, verdict `changes-requested`; the owner folded in all
five must-fixes and all suggestions). The plan text above is edited in place; these are the why.

- The `grid` stamp was misdescribed as steady-clock: it is the ping header stamp carried in a `steady_clock` type, so
  a stale-grid guard is possible; "no guard" now rests on the cross-host time-base dependency, not on
  incomparability.
- `checkGeorefMoved()` only runs when `have_georef_` is set: before the first capture `raster_from_earth_` is the
  identity and the comparison would latch a false "map moved".
- Counters stay cumulative: `NodeStats` holds area and fault counters that a reset which keeps the areas must not zero.
- The script classifies with one `service list` and runs every refusing check (classification, lifecycle preflight for
  walk-class nodes and consumers) before the first service call; service-class CUBE nodes are reset before any
  walk-class node leaves `active`, so a `draft_dir` refusal is found before a boat stops publishing.
- The service path drops `print_states`; an inactive node is reported from the response message and activated (see
  step 15; the first design, a warning and exit 4, was replaced after the pre-push review).
- `~/reset` is created last in `on_configure` because a configure that throws returns to UNCONFIGURED without
  `on_cleanup`.
- `AllCallbacksShareTheDefaultGroup` first counted only the groups the node's executor spins, so a group spun on a
  thread of its own would have passed; it now also audits what every unspun group holds (see the test list). The
  unspun groups are `tf2_ros::TransformListener`'s and, under `use_sim_time`, the clock thread's.
- The first georeference-capture test needs TF present but the node not yet ticked, so it resets a configured but
  not yet activated node; it also covers the inactive response.

### Pre-push review round 1 (2026-10-13), owner decisions
All 3 must-fixes and 13 suggestions were addressed; the progress entry lists the commit for each. The decisions that
changed the design: a node that answers inactive is reset and then activated (exit 4 retired); the call timeout defaults
to the set timeout and a timed-out call is not retried; "nothing on disk" is qualified (the legacy `survey/` rename)
rather than refused; a reset whose prior store cannot be loaded fails and keeps the gated session; the assessor reset
publishes the emptied `coverage_assessment` and `coverage_grid` itself when active.
