# Plan: In-RAM reset service: start a new live session on cube_bathymetry and coverage_assessment without a lifecycle round trip or any disk change

## Issue

https://github.com/rolker/cube_bathymetry/issues/167

## Context

`reset_cube_nodes.sh` starts a fresh sheet by walking each node through deactivate, cleanup, configure, activate. On the
four-boat simulation that is 318 to 324 s, against game rounds of about two minutes. The time is `ros2` CLI start-up
(3 to 8 s per call under load), not the transitions: the script makes about 17 `ros2` calls per CUBE node today
(preflight `lifecycle get` plus two `param get`, then roughly seven calls to walk down and seven to walk up). This
issue adds a `std_srvs/Trigger` service `~/reset` to `cube_bathymetry_node` and to `coverage_assessment_node` that
starts a new session in RAM and changes nothing on disk, and makes the script use it (about 2 calls per node).

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
- Every callback of the node (pings, timers, subscriptions) lives in the node's default callback group, which is
  mutually exclusive, and no callback group is created anywhere in either package (grep). `main` also uses a
  `SingleThreadedExecutor`. Either fact alone makes a swap inside a callback safe without a mutex.
- The `grid` header stamp is `GeoMapSheet::lastUpdateTime()` on the **steady clock** (`stampGrid`), so it cannot be
  compared with the assessor's clock; a "drop grids stamped before the reset" guard in the assessor is not possible.
- `ros2 service type <name>` prints the type and exits 0 when the service is in the graph, and exits 1 with no output
  when it is not (verified on jazzy: `ros2service/verb/type.py`, and a live call on a name that does not exist).
  `ros2 service call` waits for the service without bound, so its outcome cannot tell "absent" from "slow".

## Approach

### A. `cube_bathymetry_node`: one path for "start a fresh session"

1. **Hold the sheet-level settings as members.** Add `capture_spacing_scale_` (set where configure reads the
   parameter). `bs_mode_` and `bs_applied_curve_` already exist. These plus `cell_size_`, `prior_store_dir_`,
   `draft_dir_`, `max_resident_tiles_` are everything the builder needs.
2. **Extract `buildSession()` from `on_configure`.** It constructs a candidate (`struct FreshSession { sheet,
   evicted_indices, catalog_builder, primed-tile counts, notes }`), entirely local: new `GeoMapSheet(cell_size_)`,
   `setCaptureSpacingScale(capture_spacing_scale_)`, `setBackscatterCorrection(bs_mode_, bs_applied_curve_ ...)`
   (the stored curve, never re-reading the file or the topic), the `prior_store_dir` prime, the `draft_dir` prime and
   catalog seed (unchanged code, only retargeted at the candidate), and the trim. `trimResidentToBudget()` becomes a
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
   `~/coverage_tiles` convention. Created in `on_configure` (after the entities it depends on), released in
   `releaseConfiguredEntities()` with the others, so "service absent" means "not configured" (or finalized) and a
   caller can tell that from the graph alone. States: answers in `inactive` and `active` (a sheet exists; a reset while
   inactive is harmless and the message says the node is not publishing); the handler also returns failure when
   `geo_map_sheet_` is null, because services are not state-gated.
   Handler order: (a) refuse if the configured `draft_dir_` member is non-empty (not the writable parameter, which can
   change between cycles; same source `on_cleanup` uses): `success=false`, message names the directory, says saved
   tiles would reload so the sheet would not be fresh, and points at `--allow-persistence` with a new empty `draft_dir`;
   (b) `buildSession()` inside try/catch; any failure leaves `geo_map_sheet_` and every other member untouched and
   returns `success=false` with the reason; (c) remember the old tile count, `adoptSession`, return `success=true`
   with a message saying what was reset: "N tile(s) dropped from RAM; prior_store_dir <unset | primed X Reference + Y
   Chart tile(s) | could not be loaded: why, continuing ungated>; node is <active | inactive: no grid until
   activated>; nothing on disk was changed". The old sheet is destroyed after the swap, last.
6. **Comment the concurrency argument** at the handler: default mutually exclusive callback group plus single-threaded
   `main`; the prime is a whole-store disk read that **blocks the executor for its duration while the node is active**
   (unlike configure), so pings queue meanwhile. README states that cost.
7. **Dependencies:** `std_srvs` in `package.xml` (`<depend>`), `find_package(std_srvs REQUIRED)` and the
   `cube_bathymetry_node_link` interface target (`${std_srvs_TARGETS}`) in `CMakeLists.txt`. `std_srvs` is in
   `ros-jazzy-ros-base` (the CI image is `FROM ros:jazzy-ros-base`) and present in `/opt/ros/jazzy`, so the image and
   rosdep need no change; confirm by a clean `./sensors_ws/build.sh` of both packages.

### B. `coverage_assessment_node`: `~/reset`

8. **`resetSession(std::string * message)`**, called from a `~/reset` Trigger handler wrapped in the node's `guarded`
   pattern (an exception becomes `success=false` with the text and is counted in `callback_exceptions`). Service
   created in `configureEntities`, released in `releaseConfiguredEntities`.
   - Refuse when not configured (`registry_` or `tf_buffer_` null).
   - **Refuse when the map frame has moved**: run `checkGeorefMoved()` first (the latch only updates on ticks, so a
     stale `false` is possible while inactive). Message: the map frame moved since the georeference was captured; the
     georeference is deliberately kept by the service, so only the lifecycle walk (which re-captures it) recovers this.
   - Otherwise per source: `accumulator.clear()`, `have_grid = false`, `placement_failed = false`, `failed_frame`
     cleared, `unplaced_grid` reset (a kept grid would be folded in on the next tick and bring old cells back), the
     once-per-source warning flags and per-source log throttles reset (parity with a freshly built `SourceState`).
     Node: `held_limit_reached_ = false`, `grids_changed_ = false`, `force_assessment_ = true`,
     `have_last_surface_ = false` (so the next tick assesses even when the surface did not change),
     `stats_ = NodeStats{}` and `throttle_times_.clear()` (the counters are documented "reset on every configure"; the
     reset line logs the previous totals first, so nothing is lost silently).
   - Kept on purpose: areas, registry, projections, `raster_from_earth_`, `have_georef_`, subscriptions, TF.
   - Success message: cells dropped, number of sources, areas kept.
9. **Update the held-limit ERROR text** (line ~1159) to name the service first: "reset with the ~/reset service (or
   deactivate, cleanup, configure, activate) or restart with a larger max_held_cells". The map-moved ERROR keeps the
   lifecycle walk and now adds that the service refuses in this state.
10. **Dependencies:** `std_srvs` in this package's `package.xml` and `CMakeLists.txt` (node link target), as in step 7.
    (The executable is `SingleThreadedExecutor` here too; callbacks share the default group.)

### C. `scripts/reset_cube_nodes.sh`

11. **Classify before changing anything.** For every CUBE node and consumer run `ros2 service type <node>/reset` once
    per attempt. Exit 0 with output: `service`. Exit 1 with empty output: `walk` (older binary, unconfigured node, or
    wrong name; a wrong name then dies in the existing lifecycle preflight exactly as today). Anything else
    (timeout, other exit): retried like a read, `RESET_CUBE_GET_TRIES` attempts, then the script dies **before any
    change**. A false "absent" under discovery lag is safe: it only sends that node down the walk, whose own
    preflight still refuses `draft_dir`. `--allow-persistence` sends every CUBE node down the walk, as today; the
    consumer is classified normally.
12. **`ros2` calls per node, counted** (comment block in the script and a row in the README):

    | Path | Calls |
    |---|---|
    | service (CUBE or consumer) | 2: `service type`, `service call` (no `lifecycle get`, no `param get`: the service refuses on `draft_dir` itself and reports its own state) |
    | walk (unchanged) | about 17 per CUBE node, about 14 per consumer |
    | four boats plus assessor, all on services | 10, against about 80 today |

13. **The call.** `ros2 service call <node>/reset std_srvs/srv/Trigger '{}'` under `timeout --foreground` with its own
    bound `RESET_CUBE_CALL_TIMEOUT` (default 120 s: the prime is a disk read). Success = output contains
    `success=True`. A reset is idempotent (it only replaces RAM state), so, unlike a transition, **a timed-out or
    failed call is retried** under the read-retry rules (`RESET_CUBE_GET_TRIES`, `RESET_CUBE_RETRY_PAUSE`); the
    script comment says why. "No service" is decided only by the `service type` probe, never from a call outcome, so a
    slow node is never sent down the walk after it already reset. `success=False` from a CUBE node is a refusal:
    print its message, `die`.
14. **Order and partial results.** All CUBE nodes first (service or walk), then consumers, as now. Because the service
    refuses inside the node, a boat with `draft_dir` set is found when it is reached, not in the preflight: boats
    earlier in the list are already reset. The script prints which nodes were reset and which refused, does not touch
    consumers, exits 1, and says re-running is safe. (This trades the old all-or-nothing refusal for 3 fewer calls per
    node; see Open Questions.) A consumer refusal (map moved, or any reason) falls back to the lifecycle walk for that
    consumer, printing the reason: the walk re-captures the georeference, so it is the superset.
15. **Restate the ordering claim honestly** in the script header and README. The walk quiesced every CUBE node before
    the consumer was reset. With services the CUBE nodes keep publishing, so a `grid` published by an old sheet just
    before its reset and still queued in the assessor's subscription (reliable, depth 10, at most about one per node per
    5 s) when the assessor is reset would be folded into the empty union and stay (unions only grow). The window is
    milliseconds against a CLI gap of seconds; the ordering (consumer only after every CUBE node has answered) relies on
    that and no stamp guard is possible (steady-clock stamps, above). A consumer that goes down the walk is immune: its
    subscription is destroyed.
16. Update the script header (usage, order, new variable), keep `usage()` working (it prints the `# Usage:` to
    `# Exit status` block).

### D. Tests

17. **`test_node_lifecycle.cpp`** (a real `Trigger` client and an in-process `SingleThreadedExecutor`; a helper
    `callReset(node)`), sharing one assertion helper with `ReconfigureResetsTheSessionState`:
    - `ResetServiceResetsTheSessionState`: dirty the 11 fields (plus any step 4 adds), call the service, expect every
      name from `staleFields` clean, new sheet object (old `weak_ptr` expires), `catalogSize()==0`, node still active.
    - the fresh-sheet test of `EmptyStartWithoutPersistence` through the service (no finite cell, no dirty grids), and
      the next ping builds a window with none of the old cells.
    - `ResetKeepsSheetLevelSettings`: launch override `capture_spacing_scale=0.9` and a curve delivered through
      `sonarInfoCallback`; after reset `sheet()->parameters()` has the same scale, mode, curve, `tl_removed` and
      absorption as before (fails if a bare `make_shared<GeoMapSheet>` is used). A second `sonar_info` heartbeat with the
      same curve stays silent afterwards.
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
    - `AllCallbacksShareTheDefaultGroup`: the node has exactly one callback group, so the swap-safety argument cannot
      be broken silently by someone adding a reentrant group.
18. **`test_coverage_assessment_node.cpp`** (harness as in `ReconfigureEmptiesTheUnion`): reset service empties the
    union and the next tick publishes `area_covered == 0`; areas, their projections and the georeference are kept
    (`area_total` unchanged, no new `Raster georeference captured` line); a kept unplaced grid does not return after
    the reset (source with an unresolvable frame, then reset, then resolve the frame: no old cells); `held_limit_reached_`
    cleared (the `MaxHeldCellsStopsFoldingAndWithholdsWithAnError` setup, then reset, then assessments resume);
    counters reset; refused with the map-moved reason when `georef_moved_` is set (and also when the live transform has
    moved but the latch is not yet set), with the union untouched; refused when unconfigured; a grid arriving after the
    reset is folded in normally.
19. **`test_reset_cube_nodes.sh`**: extend the stub with `service type` (reads a per-node marker file; exit 1 and no
    output when absent; flaky/hang switches like the existing ones) and `service call` (sets state counters, writes
    `success=True/False` responses, logs calls). New cases: all-service path (no `lifecycle set`, no `param get`, call
    count equals the table above, consumer after every CUBE node); a CUBE refusal from the service (message printed,
    exit 1, consumers untouched, earlier nodes reported as reset); older binary falls back to the walk; mixed fleet
    (two services, two walk, one consumer on a service); `--allow-persistence` takes the walk for CUBE nodes; a
    consumer refusal falls back to the walk; **a `service type` timeout is retried and then dies before any change, and
    never selects the walk**; a `service call` timeout is retried and never selects the walk; every new call still goes
    through `timeout --foreground`; the existing 12 cases keep passing (they exercise the walk path, which the stub
    marks as "no service" by default).
20. **Live check** on the four-boat simulation (work-list item): record the wall time of the all-service reset against
    the 318 to 324 s baseline, and note it in the PR. This needs the simulation stack, which this sandbox may not have;
    if it cannot be run in the implementation session it is reported as not done, not claimed.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/cube_bathymetry_node.hpp` | `buildSession` / `adoptSession` extracted from `on_configure`; `capture_spacing_scale_` member; trim function retargetable; `~/reset` service create/release/handler; concurrency comment; test hook |
| `cube_bathymetry/CMakeLists.txt`, `cube_bathymetry/package.xml` | `std_srvs` |
| `cube_bathymetry/scripts/reset_cube_nodes.sh` | service classification, call, retry rules, fallback, ordering comment, call-count comment |
| `cube_bathymetry/test/test_node_lifecycle.cpp` | service tests and shared parity helper (step 17) |
| `cube_bathymetry/test/test_reset_cube_nodes.sh` | stub verbs and the new cases (step 19) |
| `coverage_assessment/src/coverage_assessment_node.hpp` | `resetSession`, `~/reset` service, held-limit and map-moved log text |
| `coverage_assessment/CMakeLists.txt`, `coverage_assessment/package.xml` | `std_srvs` |
| `coverage_assessment/test/test_coverage_assessment_node.cpp` | reset cases (step 18) |
| `README.md` | see Documentation impact |
| `coverage_assessment/README.md` | the "Starting a fresh assessment between runs" paragraph, a service row, the held-limit wording |
| `AGENTS.md` | line 54 "product of record" wording (owner-approved instruction-file edit) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | The service refuses with a reason and never touches disk; the response says what was reset; the script prints refusals and partial results; the fall back to the walk is announced. |
| A change includes its consequences | `std_srvs` in both packages, both READMEs, `AGENTS.md:54`, the held-limit text, the script header and its tests are all in this PR (see Consequences). |
| Test what breaks | Parity of the two reset paths (same stale-field list), sheet-level settings, failure leaves the old session, georeference-moved refusal, unplaced-grid carry-over, "absent" vs "timed out" in the script. |
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
| `on_configure` split into build/adopt | `ReconfigureResetsTheSessionState`, `CleanupReleasesTheSheetAndCatalog`, `ReadOnlyOverridesSurviveACycle` must still pass unchanged | Yes (run in step 17) |
| New service on a node in a package pair | `std_srvs` in both `package.xml` and `CMakeLists.txt`; CI image and rosdep (`ros:jazzy-ros-base` already has it) | Yes |
| `reset_cube_nodes.sh` behaviour | its header, the README section, `test_reset_cube_nodes.sh` header list of cases | Yes |
| Held-limit and map-moved log text | `test_coverage_assessment_node.cpp` assertions on those strings (grep before editing) | Yes |
| "Product of record" wording | `README.md:151`, `AGENTS.md:54` | Yes (AGENTS.md owner-approved) |
| `.agents/README.md` is missing in this repo (noted in `AGENTS.md`) | creating it | No: a dedicated task, not this PR |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR):
  - `README.md` "Starting a fresh sheet without restarting": lead with the service and the one-line command, keep the
    walk as the fallback; the service refuses with `draft_dir` set; new step list and the call-count table; the
    ordering paragraph from step 15; the prime's blocking cost while active; which states answer; that a real boat with
    `draft_dir` set (`bizzyboat.yaml`, `draft_dir: /home/field/data/world/depths`) is **not** served until
    rolker/unh_marine_autonomy#391 and the 2026-08-27 operator request stays open; replace "the draft store is the product
    of record, ADR-0001" with what superseded it (live tiles are a derived product, the bags are the record) and the
    pointer. Add the `~/reset` row to the node tables and the new `RESET_CUBE_CALL_TIMEOUT` variable.
  - `README.md` coverage-assessment section (line ~258, `max_held_cells` row: "until reset") and line ~262.
  - `coverage_assessment/README.md` lines ~87-93: service first, what it keeps (areas, projections, georeference) and
    that a moved map frame still needs the lifecycle walk; the held-limit wording.
  - `AGENTS.md:54`: replace "the tile store is the product of record for live coverage" with wording that keeps the
    eviction invariant ("persist before drop") and drops the "of record" claim; the exact sentence goes in the PR body
    for the owner to see (he approved the edit, not the sentence).
  - Script header comment (usage, order, ordering claim, call counts).
- **Agent-instruction candidates** (proposals only): (1) a pitfall for `.agents/README.md` once one exists: "a lifecycle
  node's service is not state-gated; create it in `on_configure` and release it in the shared release function";
  (2) knowledge note candidate for `ros2_cli_best_practices.md`: `ros2 service type` exit 1 with no output is a clean
  absence, `ros2 service call` has no absence signal, so probe first; (3) the `grid` stamp is steady-clock, not ROS
  time (a trap for any consumer that wants to compare it with `now()`).

## Open Questions

- [ ] **Partial reset on a refusal (step 14).** With services a boat that has `draft_dir` set is found when the script
  reaches it, so boats earlier in the list are already reset (idempotent, RAM only, nothing on disk). Default in this
  plan: accept that and report it. Alternative: keep an all-or-nothing preflight with one `ros2 param get draft_dir`
  per node (3 calls per node instead of 2, about 12 to 30 s more on four boats under load). Say if the old guarantee
  matters more than the time.
- [ ] **Stats counters on reset (step 8).** Reset zeroes `NodeStats` for parity with configure (previous totals are
  logged first). Alternative: keep cumulative counters across resets. Low stakes; default is parity.
- [ ] **Live check (step 20)** needs the four-boat simulation; if the implementation session cannot run it, who runs it?

## Estimated Scope

Single PR (two services, one script change, tests, READMEs, one instruction-file line). Part of #164; the PR body names
#167 as the issue it completes and #164 only as "Part of #164". The largest risk is the `on_configure` split (step 2),
so it lands as its own commit with the existing lifecycle tests unchanged and green before the service is added.
