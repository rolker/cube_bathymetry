---
issue: 167
---

# Issue #167 — In-RAM reset service: start a new live session on cube_bathymetry and coverage_assessment without a lifecycle round trip or any disk change

## Issue Review
**Status**: complete
**When**: 2026-10-13 10:59 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #167
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

One PR is realistic (two services, one script change, tests, README). The items below are design decisions the issue leaves open, to be settled in the plan; none needs the owner except the last.

### Actions
- [ ] **Swapping the sheet is not the same as configure: sheet-level settings must be re-applied.** `on_configure` builds the `GeoMapSheet` and then calls `setCaptureSpacingScale(capture_spacing_scale)` and `setBackscatterCorrection(mode, curve)` on it (cube_bathymetry_node.hpp:126-155, :265). The issue's list ("the same resets configure does") omits both. A bare `make_shared<GeoMapSheet>` would silently revert the capture gate to the default and drop the backscatter correction. The curve is the worse one: it normally arrives from the latched `sonar_info` topic and is held in `bs_applied_curve_`; the subscription is kept across a reset, so the message is not redelivered and the new sheet would have no correction until a heartbeat (if any). Re-apply the stored scale and `bs_applied_curve_` to the new sheet, and test both (a reset keeps the gate and the correction).
- [ ] **One code path for "start a fresh session state", not a second hand-written list.** The reset list is currently inlined in `on_configure` (evicted markers, publish cursor, disk-serve queue, refresh tracker, saving ledger, catalog builder). Copying it into the service handler invites drift the next time a field is added. Extract the shared part (session-state clear plus new-sheet construction and prime) into one private method called by both `on_configure` and the service. Extend the existing `ReconfigureResetsTheSessionState` / `staleFields` test (11 fields) to run through the service as well, so a new field that is missed fails in both paths.
- [ ] **Build aside, then swap.** The new sheet needs `setCaptureSpacingScale` (can throw), the backscatter setters and the `prior_store_dir` prime (a whole-store disk read). Construct and prime the new sheet completely, then replace `geo_map_sheet_`, so any failure leaves the old session intact and the service reports failure. Note the executor is `SingleThreadedExecutor` (spinUntilStopped requires it), which is what makes the swap safe against `pingCallback` and the timers without a mutex; say so in a comment and keep a test or assertion that does not depend on a multi-threaded executor. State the prime's blocking cost (it runs inside the callback, with the node active, unlike configure) in the README.
- [ ] **Service semantics the issue leaves open.** Decide and test: (a) name, suggest `~/reset` to match the node's `~/coverage_tiles` convention (resolves to `/boat1/cube_bathymetry/reset`; `mru_transform` uses a relative `reset_map_frame`); (b) which lifecycle states answer. Services on a lifecycle node are not state-gated, so the handler must refuse when `geo_map_sheet_` is null (unconfigured) and decide for inactive; create the service in `on_configure` and release it in `releaseConfiguredEntities()` like every other entity, so "no service" also reads "not configured"; (c) the refusal test uses the member `draft_dir_` (what the node was configured with, as `on_cleanup` does), not the current parameter value, which is writable between cycles; (d) the response string says what was reset (tiles dropped, prior reloaded or not).
- [ ] **coverage_assessment: reset must clear more than the unions, and must say what it cannot clear.** `SourceAccumulator::clear()` exists, but each `SourceState` also holds `have_grid`, `placement_failed`, `failed_frame` and `unplaced_grid` (a kept grid from the previous session would be folded in on the next tick and bring old cells back). `held_limit_reached_` (fail-closed after `max_held_cells`) and `grids_changed_` must be cleared too; the node's own error text says a reset is the cure ("deactivate, cleanup, configure, activate"), so update that message to name the service. Decide whether the `stats_` counters reset. Importantly, the lifecycle walk also clears `have_georef_` / `georef_moved_` (coverage_assessment_node.hpp:517-518) and re-captures the georeference; the issue keeps the georeference on purpose ("the map must not move"), which means a node withholding assessments because the map moved is NOT recovered by the service. Reset must either refuse with that reason when `georef_moved_` is set, or the README must say that case still needs the lifecycle walk (and the script's fallback should cover it). Test: unions empty, areas and projections kept, the unplaced grid and the held-limit flag cleared, a fresh assessment published on the next tick.
- [ ] **Script: `reset_cube_nodes.sh` timing and fallback detection.** The issue's measured cost is `ros2` CLI start-up (3-8 s per call under load), not the transitions themselves. The preflight alone makes a `lifecycle get` plus a `param get draft_dir` per node, so keeping all of it and adding a service-presence probe plus a call per node could stay near the cost it is meant to remove, and the "well under a round (~2 min)" check in the work list would then fail. In the plan, count `ros2` invocations per node and trim: the service already refuses on `draft_dir` itself, so the service path does not need the `param get`; one `ros2 service call` can be the probe and the action. Define precisely how "node has no service (older binary)" is told apart from "service call timed out under load" (`ros2 service type <name>` returning nothing is a clean absence; a timeout is not), because falling back to the lifecycle walk on a timeout would cycle a node that actually reset. A reset call is idempotent, so unlike a transition it may be retried by the existing read-retry logic; say that explicitly. Extend `test_reset_cube_nodes.sh` (stub `ros2` needs `service type` / `service call`): service path, refusal from the service, older binary falling back, `--allow-persistence` taking the lifecycle walk, mixed fleet.
- [ ] **Ordering race the lifecycle walk used to hide.** Today every CUBE node is quiesced before the consumer is reset, so no old-session `grid` can reach the assessor afterwards. With services the CUBE nodes keep publishing; a `grid` published by the old sheet just before its reset and still in the assessor's subscription queue (depth 10) when the assessor is reset would be folded into the now-empty union and stay there (unions only grow). In practice it is a millisecond window against a seconds-long CLI gap, but the script's ordering comment ("consumer after every CUBE node has answered") should be restated honestly as relying on that, and the plan should consider a cheap guard (the assessor could ignore grids whose stamp precedes its reset time on its own clock, if the stamps are comparable under `use_sim_time`).
- [ ] **Consequences beyond the README the issue lists.** (a) Both `package.xml` and `CMakeLists.txt` of the two packages need `std_srvs` (currently declared in neither; it is in `ros-jazzy-ros-base`, so the CI image and rosdep need no change, but confirm). (b) README tables: the service row on each node, the parameter/service tables, the two "reset" explanations (README.md "Starting a fresh sheet without restarting", coverage_assessment/README.md lines ~87-93), and the held-limit wording. (c) The "product of record" wording also appears in `AGENTS.md:54` ("the tile store is the product of record for live coverage") and the issue only mentions the README/ADR-0001. `AGENTS.md` is an instruction file read by Copilot review: editing it needs the owner's yes (workspace Ask First), so either propose the wording in the PR for approval or leave it and say so. ADR-0001's text stays as history; the README should say what superseded it and point at rolker/unh_marine_autonomy#391 rather than restate it. (d) There is still no `.agents/README.md` (noted in `AGENTS.md`); not this PR's job. (e) Do not put a closing keyword for #164 in the PR body or commits: that issue is the umbrella and still has open items.
- [ ] **Watch: the real-boat motivation in the issue is not delivered by this slice.** The issue says the operator asked for "start a new live session" on gabby (2026-08-27), but real configurations set `draft_dir` (the README names `bizzyboat.yaml`, `draft_dir: /home/field/data/world/depths`), and the service refuses while it is set. This slice only helps sim and demo runs and boats run without persistence (which never evict, so RAM grows). That is a deliberate boundary and the issue says so, but the Why section reads as if the gabby case is closed; suggest the issue (and README) state plainly that on a boat with `draft_dir` set the operator request remains open until rolker/unh_marine_autonomy#391. Owner decision, only if he wants the issue text changed; not blocking.

### Principle and ADR notes (for the record)
| Principle / ADR | Status | Notes |
|---|---|---|
| A change includes its consequences | Action needed | README, `std_srvs` deps, the held-limit log text, `AGENTS.md` wording (above) |
| Test what breaks | Action needed | Parity of the two reset paths, georeference-moved case, unplaced-grid carry-over, script fallback detection |
| Human control and transparency | OK | Refusal with a reason; nothing on disk touched; fallback to the lifecycle walk is explicit |
| Only what's needed / Improve incrementally | OK | RAM-only slice, on-disk session identity deferred to unh_marine_autonomy#391 |
| Capture decisions | Watch | Superseding the "product of record" wording is a design change; record it in the README and the PR, not an ADR (the world-store ADR belongs to #391) |
| ADR-0008 (ROS 2 conventions, workspace) | Watch | `std_srvs/Trigger`, private service name, lifecycle-aware creation and release |
| Project ADR-0001 (eviction / draft store) | Watch | Eviction invariant (persist before drop) is untouched because the service refuses with `draft_dir` set |

## Plan Authored
**Status**: complete
**When**: 2026-10-13 11:13 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-167/plan.md` at `9b00f3f`
**Branch**: feature/issue-167 at `9b00f3f`
**Phases**: single

All eight Issue Review actions are folded in (sheet-level settings re-applied from stored members, one shared `buildSession`/`adoptSession` path with a parity test, build aside then swap, service name/states/refusal/response text, assessor reset scope and map-moved refusal, script call counts and `service type` absent-vs-timeout rule, honest ordering-race text, `std_srvs` in both packages, `AGENTS.md:54` edit, no closing keyword for #164). Two findings beyond the review: the `grid` stamp is steady-clock so the suggested stale-grid guard in the assessor is not possible, and the node's default mutually exclusive callback group (not only the single-threaded executor) is what makes the swap safe, with a test that pins it.

### Open questions
- [ ] Partial reset when one boat refuses (`draft_dir` set): accept and report (plan default, 2 calls per node) or keep an all-or-nothing preflight with a `param get` per node (about 12 to 30 s more).
- [ ] Assessor reset zeroes `NodeStats` for parity with configure (previous totals logged first); keep cumulative instead? Low stakes.
- [ ] The live four-boat timing check needs the simulation stack; who runs it if the implementation session cannot?

## Plan Review
**Status**: complete
**When**: 2026-10-13 11:19 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-167/plan.md` at `9b00f3f`
**PR**: PR-less
**Verdict**: changes-requested

Read cold as a dispatched sub-agent (independent). Checked against source: `cube_bathymetry_node.hpp` (configure, release, `trimResidentToBudget`, `stampGrid`, `pingCallback`, `spinUntilStopped`), `geo_map_sheet.cpp`, `coverage_refresh.h`, `coverage_assessment_node.hpp`, `reset_cube_nodes.sh` and its test stub, and `ros2service/verb/type.py` and `call.py` on the installed jazzy. The overall design holds up: one shared build/adopt path, refusal on the member `draft_dir_`, service created and released with the other entities, the server-side refusal in place of a `param get`, and probe-then-call to tell "absent" from "slow". Verified as correct: the default callback group is the only one (and `spinUntilStopped` takes a `SingleThreadedExecutor &` by type, so the swap-safety claim is also enforced at compile time); `CoverageRefreshTracker::clear()` keeps the interval and budget; `staleFields` covers every session member; `ros2 service type` exits 1 with no output for an absent service; `ros2 service call` waits for the service without bound.

### Findings
- [ ] (must-fix) The `grid` stamp is NOT the process steady clock. `pingCallback` builds `timestamp = steady_epoch + header.stamp` and passes it to `addSoundings`, which stores it as `last_update_time_`; `stampGrid` writes that back out. The value is the header stamp (ROS time) of the latest ping that changed a tile, held in a `steady_clock` type, and 0 on a sheet with no insert yet. Plan lines 33-35, 142-146 and the instruction candidate at 262-263 ("steady-clock, not ROS time ... trap for any consumer that wants to compare it with `now()`") rest on the wrong premise, and candidate (3) would put a false statement into a knowledge note. Re-decide the guard on the real facts: a guard "drop grids stamped before the assessor's reset time" is possible, and fails safe (a window re-published after the reset carries every cell and a newer stamp), but it depends on sonar stamps and the assessor's clock sharing a time base across hosts. Keeping "no guard" is defensible; the reason has to be that, not "cannot be compared". — `plan.md:33`, `plan.md:140`, `plan.md:262`
- [ ] (must-fix) Step 8 runs `checkGeorefMoved()` first, but with `have_georef_` false `raster_from_earth_` is still the identity, so a node reset before its first georeference capture would compare the live transform with identity, can latch `georef_moved_` falsely, and then refuses with a bogus map-moved reason. Run it only when `have_georef_` is set (a node that has not captured has nothing to protect), and add that case to the step-18 tests. — `plan.md:91`
- [ ] (must-fix) Resolve open question 2 against zeroing. `NodeStats` also holds `areas_received`, `areas_rejected` and `callback_exceptions`; the service keeps the areas, so zeroing gives a status line of "areas 0 received, 0 rejected, N held", and the "parity with configure" argument does not hold for those fields (configure also drops the areas). Zeroing `callback_exceptions` also erases evidence of a fault. Keep the counters cumulative (log a one-line "reset: N cells dropped" instead), or at most zero the grid-side counters. — `plan.md:100`, `plan.md:272`
- [ ] (must-fix) Script ordering: say that classification AND the lifecycle preflight (0a/0b/0c) for every walk-class CUBE node and every consumer run before the first `service call`. As written (steps 11 and 14) a mistyped `--consumer`, a finalized node or a refusing walk-class node is found after the service-class CUBE nodes were already reset, which the old script never allowed. — `plan.md:112`, `plan.md:134`
- [ ] (must-fix) Script call count and final state: the script ends with `print_states` (one `lifecycle get` per node), which the table at lines 119-125 leaves out (10 calls becomes 15), and `die` calls it too. Either drop it on the service path, using the service message as the report, or count it. Also decide what the script does with a node that answers "inactive" (the service leaves it inactive, the walk always ended active): the exit status and last line must not read as "reset done, publishing" for a node that publishes no `grid`; the old preflight warned about this and the service path no longer reads the state. — `plan.md:119`, `plan.md:127`
- [ ] (suggestion) Timing claim: 10 calls at 3 to 8 s each is 30 to 80 s sequential, which is not "well under a two-minute round" at the top of the range. Two cheap options: classify every node with one `ros2 service list` (one call, an empty list is "indeterminate", not "absent") instead of N `service type` calls, and/or run the CUBE service calls in parallel (they are independent and idempotent). State the expected range in the plan and let the live check judge it. — `plan.md:112`
- [ ] (suggestion) Retry tail: `RESET_CUBE_CALL_TIMEOUT` 120 s x `RESET_CUBE_GET_TRIES` 3 is 6 minutes for a node whose service is still listed in the daemon's graph cache but is dead or hung (`service type` reports a graph entry, not a live node). Use fewer tries for the call than for reads, or a shorter first attempt. — `plan.md:127`
- [ ] (suggestion) Ordering paragraph (step 15) covers one direction only. The other one: new-session cells that a reset CUBE node accumulates before the assessor is reset are wiped from the union, and come back only through the next re-published 200 m window (about 5 s) so cells outside it are lost. The walk avoided both by quiescing. Boats are not meant to be underway, so say so as the condition, not just the old-grid case. — `plan.md:140`
- [ ] (suggestion) The prime blocks the executor with the node active; the ping subscription is `SensorDataQoS` (best effort, depth 5), so pings during the block are DROPPED after five, not "queued" (steps 6 and README). Say dropped. Likewise, if the water surface is unavailable after the assessor reset, the latched old `coverage_assessment` messages stay published (the tick returns before assessing); mention in the README. — `plan.md:79`
- [ ] (suggestion) Parse the response on `Trigger_Response(success=True` rather than the bare substring `success=True` (the refusal message echoes a user-supplied path), and have the test stub print the real jazzy format (`requester: making request: ...` / `response:` / `std_srvs.srv.Trigger_Response(success=..., message='...')`) captured from a live call, since nothing else exercises the real CLI output. — `plan.md:127`, `plan.md:182`
- [ ] (suggestion) Create `~/reset` as the very last step of `on_configure` (after the `sonar_info` subscription, which can throw) and `reset()` the old handle first: a configure that throws after creating it returns the node to UNCONFIGURED without `on_cleanup`, and "service absent means not configured" would be false. — `plan.md:64`
- [ ] (suggestion) Builder details: copy, never move, the stored `bs_applied_curve_.points` into the candidate (configure moves from a local copy; moving the member would blank the stored curve on the second reset), and add a second reset in a row to `ResetKeepsSheetLevelSettings`. The "Capture distance" INFO and the CA-window coherence WARN in configure read the sheet before the prime; point them at the candidate or compute from `cell_size_`. — `plan.md:46`, `plan.md:158`
- [ ] (suggestion) Consequences: add `<exec_depend>ros2service</exec_depend>` to `cube_bathymetry/package.xml` (the existing comment already lists the `ros2` verbs the script shells out to) and update the "12 case groups" comment at `CMakeLists.txt:407` along with the test header. — `plan.md:236`
- [ ] (suggestion) Open questions: (1) accept the partial reset on a refusal as planned, since it is idempotent, RAM-only, printed and exit 1, and the `draft_dir` fleets are documented out of scope; stop at the first refusal. (3) The live four-boat timing is the owner's to run (needs the simulation stack): leave the issue's work item unchecked and put it in the PR body as an open item, and do not use a closing keyword for #167 until it is done or the owner waives it. — `plan.md:267`, `plan.md:274`

### Dimensions
| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | 11 files across three components, but one slice with the risky split as its own first commit; keep the commit order (split, CUBE service, assessor service, script, docs) |
| Issue alignment | Good | Every issue item and review-issue action is addressed; the real-boat boundary is stated |
| File targeting | Needs work | Add `ros2service` exec_depend and the CMake test comment |
| Consequences | Needs work | Items above |
| Documentation & instruction impact | Needs work | Section is present and non-silent, candidates are proposals; candidate (3) is factually wrong (see first finding) |
| Principle alignment | Good | Transparency, consequences, test-what-breaks are met; the stats and georef items are gaps in the second |
| ADR compliance | Good | Project ADR-0001 invariant preserved by the refusal; workspace ADR-0008 (ROS 2 conventions) followed |
| ROS conventions | Needs work | Service lifecycle placement (create last), QoS drop wording, exec_depend |

## Implementation
**Status**: complete
**When**: 2026-10-13 12:39 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Branch**: feature/issue-167 at `1298b98`
**Plan**: `.agent/work-plans/issue-167/plan.md` (amended at `11237ac` for the plan review; synced to the code at `a660284`)

Implements the plan as amended on 2026-10-13: all five must-fixes and all suggestions of the Plan Review folded in, the three open questions answered by the owner (partial reset accepted, assessor counters cumulative, live four-boat timing check run by the host). The live check is NOT done by this entry, see Open items.

### Commits (oldest first, all on feature/issue-167, nothing pushed)
- `11237ac` plan: fold in the plan-review must-fixes and suggestions
- `eb33873` progress: correct the Plan Authored timestamp to the plan commit's author time (11:13 -04:00, from `git log`; the entry had said 11:25, later than the machine clock at review time)
- `9a546a1` cube_bathymetry_node: split on_configure into buildSession and adoptSession (own commit, existing lifecycle tests unchanged and green before the service was added)
- `0c0c1e4` cube_bathymetry_node: add the ~/reset in-RAM session service (+ `std_srvs`, 9 new tests)
- `faf31f0` coverage_assessment: add the ~/reset in-RAM session service (+ `std_srvs`, held-limit and map-moved log texts, 8 new tests)
- `83db5ee` reset_cube_nodes.sh: reset by ~/reset service, walk only where there is none (+ `ros2service` exec_depend, stub and cases 13-21, CMake case-group comment)
- `2507499` docs: both READMEs and AGENTS.md (the owner-approved eviction bullet)
- `a660284` plan: sync with what was implemented
- `1298b98` style: wrap one comment line and align two lambda continuations for cpplint/uncrustify

### Tests
`./sensors_ws/build.sh cube_bathymetry coverage_assessment` then `./sensors_ws/test.sh cube_bathymetry coverage_assessment` at `1298b98`: **colcon summary 1069 tests, 0 errors, 0 failures, 110 skipped** (all 110 are the cppcheck lint tests, 90 + 20, reported as skipped; a first run at `2507499` had 6 lint failures, all mine: one cpplint line length and two uncrustify alignments, fixed in `1298b98`). gtest counts: `test_node_lifecycle` 29 (was 20), `test_coverage_assessment_node` 48 (was 40); `test_reset_cube_nodes` runs 21 case groups (was 12) and passes in about 30 s. Live checks beyond the unit tests, on a private ROS_DOMAIN_ID: the real `ros2 service call` / `ros2 service list` output of a Python Trigger server was captured and used as the stub format, and the final script was run against two such servers (success, refusal with exit 1 and the reset-before-this report, inactive with exit 4); no processes were killed by name.

### Revert checks (each new test fails when its fix is reverted)
Done with temporary, env-selected mutations in a throw-away build (removed again; nothing of them is committed).
- cube node: dropping `refresh_tracker_.clear()` from `adoptSession` fails ResetServiceResetsTheSessionState, FailedResetLeavesTheOldSessionIntact (retry step) and the existing ReconfigureResetsTheSessionState; skipping `setCaptureSpacingScale` fails ResetKeepsSheetLevelSettings and ReadOnlyOverridesSurviveACycle; skipping the backscatter setter fails ResetKeepsSheetLevelSettings; moving the stored curve instead of copying fails ResetKeepsSheetLevelSettings on the SECOND reset only; no draft_dir refusal fails ResetIsRefusedWhileDraftDirIsSet; judging the draft_dir parameter instead of the member fails the same test's second half; creating the service before the sonar_info subscription fails ConfigureThatThrowsLeavesNoResetService; not releasing it on cleanup fails ResetServiceExistsOnlyWhileConfigured; building in place instead of aside fails FailedResetLeavesTheOldSessionIntact; skipping the prior prime fails ResetReprimesPriorStore; adding a reentrant group to the executor fails AllCallbacksShareTheDefaultGroup.
- assessor: no `accumulator.clear()` fails the union test and the held-limit test; keeping the unplaced grid fails ResetDropsAKeptUnplacedGridSoItsCellsDoNotReturn; keeping the held-limit latch fails ResetClearsTheHeldLimitAndAssessmentsResume; checking the moved map frame without the `have_georef_` guard fails ResetBeforeTheFirstGeoreferenceIsNotRefused; not checking it at all fails both map-moved tests; zeroing `stats_` fails the counters assertions; dropping the forced assessment fails the union test; not releasing the service fails the presence test.
- script (mutated copies of the script run through the same test): an empty `service list` treated as an answer (18b), bare-substring `success=True` parsing (14), the walk selected after a call timeout (19b), the lifecycle preflight skipped for consumers (4a, 20a), the call retry budget shared with reads (19b) or fixed at 1 (19a, 19b, 19c), exit 0 for an inactive node (21), refusal not stopping the script (14), a state listing for service nodes (13), `--allow-persistence` ignored (16a), no consumer fallback (17) and service-class nodes reset after a walk-class node left active (15b) each fail the case named.

### Deviations from the plan (all also edited into the plan)
- **Callback groups.** The plan said no callback group exists in either package. `tf2_ros::TransformListener` makes its own group for its own thread (not added to the node's executor; it only writes the TF buffer). `AllCallbacksShareTheDefaultGroup` therefore counts the groups the executor spins (exactly one, mutually exclusive) instead of all groups; the handler comment says why the swap is still safe.
- **`FreshSession`** carries a `prior_status` string for the response instead of primed-tile counts.
- **No "next ping builds a window" test.** `pingCallback` needs TF and the whole publish chain; the window is built from the sheet's grids, which ResetServiceResetsTheSessionState already pins (no cell of the old patch returns, new data accumulates).
- **Assessor handler** has its own try/catch (with the containment of `guarded`) so the exception text reaches the caller; the fault-injector name is `"reset service"`. Test-only seams added: public `heldCellCount()` and a `CoverageAssessmentTestAccess` friend (assessor), `build_fault_injector_` and test-access methods (CUBE node).
- **Script wording.** A refusal prints `<node>: ~/reset answered: <message>`; classification says which nodes take the walk before the preflight. Exit status 4 and the variables `RESET_CUBE_CALL_TIMEOUT` / `RESET_CUBE_CALL_TRIES` are as planned.
- **A reset of the assessor before its first georeference is tested on a configured, not yet activated node** (TF present, no tick yet), which also covers the inactive response.
- The plan edits made while coding went into the commit that caused them where it was clean, otherwise into `a660284`.

### Audit of session-scoped members (plan step 4)
Grepped every member written in `pingCallback`, `publishBounded`, the timers and `saveDirtyTiles`: nothing beyond the 11 fields in `staleFields`; no new field was needed in `adoptSession` or in the test accessor.

### Open items
- [ ] **Live four-boat timing check (issue work item, plan step 21): NOT done here, left unchecked as decided.** The host runs it: record the wall time of an all-service `reset_cube_nodes.sh --consumer /coverage_assessment` against the 318 to 324 s baseline. The README gives only an estimate from the call count (6 calls, about 18 to 48 s at 3 to 8 s per call) and says it is not a measurement. The PR body must list this as open and carry no closing keyword for #167 until it is done or waived; "Part of #164" only for the umbrella.
- [ ] PR body: the exact new `AGENTS.md` sentence for the owner to see ("draft-tile eviction persists to the durable store *before* dropping from RAM. The live tile store is a derived product for the live view, not the record: the raw bags and offline processing are."); the statement that a boat with `draft_dir` set (`bizzyboat.yaml`) is not served until rolker/unh_marine_autonomy#391 and the 2026-08-27 operator request stays open.
- [ ] Before pushing: run `/review-code` (pre-push mode) on this branch; nothing was pushed, no PR opened, the issue was not edited.
- [ ] CI image: `std_srvs` and `ros2service` come from `ros-jazzy-ros-base`, so the Dockerfile needs no change; the prebaked deps image picks them up at its next weekly rebuild and the CI-time `rosdep install` covers the gap (per the Dockerfile's own comment). Not verified in hosted CI.
- [ ] Agent-instruction candidates from the plan (proposals only, nothing written): a pitfall for a future `.agents/README.md` ("a lifecycle node's service is not state-gated: create it last in `on_configure` and release it in the shared release function"); for `ros2_cli_best_practices.md`: `ros2 service type` exits 1 with no output for an absent service while `ros2 service call` has no absence signal, and a cold daemon answers `ros2 service list` with an empty list the first time (observed); and the corrected fact that the `grid` stamp is the ROS-time stamp of the latest ping that changed a tile.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-13 12:48 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-167 at `cb01d2a`
**Mode**: pre-push
**Depth**: Deep (reason: 3191 changed lines across 15 files; lifecycle and concurrency change)
**Must-fix**: 3 | **Suggestions**: 13
**Round**: 1 | **Ship**: continue — round 1 with three must-fixes in the reset script's refusal parsing and its operator-facing recovery promises; all three are small, but the inactive-node one needs a choice of fix

Specialists: static analysis (shellcheck, ament cpplint, uncrustify, lint_cmake and xmllint on the changed files, all clean; cppcheck 2.13 skipped by ament's slow-version gate); governance; plan drift; Claude adversarial Lens A and Lens B (Deep); cross-model review by gemini and codex (both completed); local model off. No `.agents/review-context.yaml` and no `.agents/README.md` in this repo.

Governance: Project ADR-0001 is preserved, because the service refuses on the configured `draft_dir_`. Workspace ADR-0008 is followed (`std_srvs/Trigger`, private `~/reset`, created last and released first). The consequences are all present: `std_srvs` in both packages, the `ros2service` exec_depend, both READMEs, the CMake case-group comment, and the owner-approved `AGENTS.md` sentence, which still states "persist before drop". Plan drift is none beyond the deviations the Implementation entry records. Every planned test exists. The swap-safety argument holds for every production reader (both lenses checked it).

### Findings
- [x] (must-fix) A refusal is parsed as success when the message contains `(success=True,`: the inner `case` matches anywhere in the line, not at its start. Codex reproduced it with `draft_dir='/data/(success=True,old)'`; case 14's echoed text has no `(`, so it does not catch this. Fix: anchor the success check to the `std_srvs.srv.Trigger_Response(success=True,` prefix and add the regression case. [codex + gemini] — `cube_bathymetry/scripts/reset_cube_nodes.sh:368`
- [x] (must-fix) "Re-run the same command" no longer recovers a node that a failed walk or a failed 0c probe left `inactive`. Such a node is configured, so it serves `/reset`; the re-run classifies it as service class, resets it in place and exits 4, so it is never activated and the boat stays without `grid`. The `die` text at :262/:265 and the README promise that re-running recovers. Fix, either: activate a service node that answers inactive (`to_active`); classify an inactive service node as walk class; or at least change the messages to name `ros2 lifecycle set <node> activate`. Add a stub case for the re-run. Which fix is a design choice for the owner (plan step 15 chose exit 4 for inactive nodes but did not weigh re-run convergence). [Lens A] — `cube_bathymetry/scripts/reset_cube_nodes.sh:410,500-503,262,265`
- [x] (must-fix) When the FIRST service-class node gives no usable answer (a timeout, say), `die` prints "nothing was changed". The request may still have reached the node and reset it, which the die text itself admits ("the call may have reached it"), so the two lines contradict each other. Fix: record the node being called and have `die` say that node may have been reset. Add a stub case with the hang on the first node (19b hangs boat2 only). [Lens B, confirmed by the lead] — `cube_bathymetry/scripts/reset_cube_nodes.sh:509,266-269`
- [x] (suggestion) A timed-out call is retried as if it "costs nothing", but the first request is still queued in the node, so the retry runs a second whole-store prior load with the executor blocked again. Also `RESET_CUBE_CALL_TIMEOUT` (120 s) is below the walk's configure bound (300 s) for the same load, so a prior that takes 120–300 s succeeds by the walk but fails the service path on every run. Fix: default the call timeout to the set timeout, and do not retry an exit 124; retry only fast failures. Correct the header and README claim. [Lens B + gemini] — `cube_bathymetry/scripts/reset_cube_nodes.sh:99-112,139-140,353-377`
- [x] (suggestion) "Nothing on disk was changed" is not unconditional: `marine_bathymetry_store::load()` renames a legacy `survey/` to `processed/` (the ADR-0010 D8 one-time migration) on the prior store. The issue title promises no disk change. Fix: qualify the response and README wording, or refuse or skip a prior that would need the migration (codex graded this must-fix; it is graded down because configure has the same behaviour and the trigger needs a legacy prior store). [codex + Lens B] — `cube_bathymetry/src/cube_bathymetry_node.hpp:1839,2098`
- [x] (suggestion) Peak RAM of a service reset is the old sheet, plus the whole prior store, plus the new sheet. With `draft_dir` empty, the only case the service accepts, the old sheet never evicts, so it is at its largest when a reset is wanted. A `bad_alloc` in the prior load is then caught as "continuing ungated". Fix: document the peak in the docstring and README, or drop the old grids first when `draft_dir_` is empty. [Lens A + Lens B] — `cube_bathymetry/src/cube_bathymetry_node.hpp:2084-2092`
- [x] (suggestion) A prior-store load failure during a reset returns success with the blunder gate off, replacing a gated session with an ungated one. Fix: on reset, treat a failed prior load (when one is configured) as a failed reset that keeps the old session, or have the script warn on it as it does for inactive. [gemini] — `cube_bathymetry/src/cube_bathymetry_node.hpp:1870-1876`
- [x] (suggestion) Classification matches the service name only. A consumer with a `~/reset` of another type would hang the Trigger client for 2 x 120 s, or a Trigger with another meaning would be called blindly. `ros2 service list -t` costs the same single call: match `"<node>/reset [std_srvs/srv/Trigger]"`. [Lens B] — `cube_bathymetry/scripts/reset_cube_nodes.sh:330,342-344`
- [x] (suggestion) The inactive detection `*"node is inactive"*` can match text from `prior_status` (a path or an exception message). Fix: anchor it on `; node is inactive`. [gemini] — `cube_bathymetry/scripts/reset_cube_nodes.sh:500-501`
- [x] (suggestion) The CUBE `resetSession` catches only `std::exception`; a non-std throw escaping a service callback ends the process. The assessor's `onReset` has `catch (...)`; match it. [gemini] — `cube_bathymetry/src/cube_bathymetry_node.hpp:2088-2092`
- [x] (suggestion) After an assessor reset with the water surface unavailable, `coverage_grid` (transient_local) stays latched with the old run's raster, as well as `coverage_assessment`. The READMEs name only the latter. Consider publishing zero results at reset time rather than waiting for a tick that may return early. [Lens A + gemini] — `README.md`, `coverage_assessment/README.md`, `coverage_assessment/src/coverage_assessment_node.hpp`
- [x] (suggestion) The exit-1 description says "refused (draft_dir, map frame moved)" and the README says "a node refused". A consumer refusal falls back to the walk and can exit 0. Say that a refusing consumer is walked. [Lens A] — `cube_bathymetry/scripts/reset_cube_nodes.sh:128-129`, `README.md`
- [x] (suggestion) With `--allow-persistence` the classification line says "lifecycle walk (no ~/reset service)" for nodes that do have the service. Name the actual reason. [gemini] — `cube_bathymetry/scripts/reset_cube_nodes.sh:425`
- [x] (suggestion) `AllCallbacksShareTheDefaultGroup` counts only auto-added groups, so a manually added group spun on its own thread would pass. The handler comment's "the one other callback group" is wrong under `use_sim_time` (the clock-thread group; harmless, but the claim is inaccurate). The assessor has no equivalent pin. Assert what each manually added group holds (only `/tf`, `/tf_static`, `/clock`), run once with `use_sim_time`, and add the pin to coverage_assessment. [Lens B] — `cube_bathymetry/test/test_node_lifecycle.cpp:1175-1197`, `cube_bathymetry/src/cube_bathymetry_node.hpp:2050-2051`
- [x] (suggestion) Nothing enforces "not for use underway": a reset while ACTIVE, from any caller, publishes a CA window holding only new-session cells, which is NaN and lethal under `unsurveyed_is_lethal` around the vessel. At minimum, state this consequence in the service docstring and README; optionally add a parameter gate. [Lens B] — `cube_bathymetry/src/cube_bathymetry_node.hpp:2070-2099`, `README.md`
- [x] (suggestion) The test gap behind must-fix 2: no stub case runs a node left inactive by a failure into a re-run. Add one with whichever fix is chosen. [Lens A] — `cube_bathymetry/test/test_reset_cube_nodes.sh`

Dropped: gemini's "partial fleet reset on a refusal" (an owner decision, 2026-10-13, documented) and its clock-sync argument against the stamp guard (an owner-reviewed plan decision with its reason recorded).

## Implementation
**Status**: complete
**When**: 2026-10-13 13:48 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Branch**: feature/issue-167 at `9a52cf6`
**Addressed**: `## Local Review (Pre-Push)` of 2026-10-13 12:48 -04:00 at `cb01d2a` (round 1, changes-requested; 3 must-fix, 13 suggestions), with the owner decisions of 2026-10-13: fix all 16; an inactive node that serves `~/reset` is reset and then activated; the call timeout defaults to the set timeout and a timed-out call is not retried; "nothing on disk" is qualified, not refused.
**Commits**: `7b1a4c0` `ab52bb2` `ced1b42` `c651035` `49d26b2` `205f7d2` `1713d61` `d522b66` `931f3ae` `f80042f` `9985062` `fc3fbdd` `d8b8308` `a106de6` `9a52cf6` (nothing pushed; no PR; the issue was not edited)

### Actions
- [x] (must-fix) A refusal containing `(success=True,` read as success: the verdict is now read from the start of the response line only; case 14b — `reset_cube_nodes.sh:368` — `7b1a4c0`
- [x] (must-fix) Re-running did not recover a node left inactive: a node that answers inactive is reset and then ACTIVATED (`to_active`), exit 0, exit 4 retired; die texts, usage and README say what the re-run does; cases 21, 21b, 22a (failed walk then re-run), 22b (failed probe then re-run) — `reset_cube_nodes.sh:410,500-503,262,265` — `ced1b42`
- [x] (must-fix) "nothing was changed" after a first-node timeout: the node being called is tracked (`calling`) and `die` names it as possibly reset; case 19e (hang on the first node, and on the second after a reset first node; a refusal does not claim it) — `reset_cube_nodes.sh:509,266-269` — `49d26b2`
- [x] Timed-out call retried and call timeout (120 s) below the walk's bound: call timeout defaults to the set timeout (300 s), exit 124 is not retried, fast failures still are; header and README corrected; cases 19b, 19c, 19d — `reset_cube_nodes.sh:99-112,139-140,353-377` — `ab52bb2`
- [x] "Nothing on disk was changed" is not unconditional: the response, docstring, README, script header and plan now say "no tile written to disk (loading a prior store renames a legacy survey/ directory, as configure does)"; `ResetMayRenameALegacyPriorSurveyDirLikeConfigure` shows the rename happens — `cube_bathymetry_node.hpp:1839,2098` — `9985062`
- [x] Peak RAM of a service reset (old sheet + whole prior + new sheet; the old sheet is at its largest with `draft_dir` empty): documented in the service docstring, README and script header, not changed in code because dropping the old grids first would break "a failed build leaves the old session intact" (documentation only, so no test) — `cube_bathymetry_node.hpp:2084-2092` — `9985062`
- [x] Prior-store load failure on reset returned success ungated: `buildSession(prior_must_load)`, the reset passes true, so the reset fails and keeps the gated session; configure keeps warn-and-continue; `ResetWithAnUnloadablePriorKeepsTheGatedSession` (corrupt tile file, also asserts configure still succeeds) — `cube_bathymetry_node.hpp:1870-1876` — `f80042f`
- [x] Classification matched the name only: one `ros2 service list -t`, match `<node>/reset [std_srvs/srv/Trigger]`; stub prints types only for `-t`; case 23 (a SetBool `/reset` walks) — `reset_cube_nodes.sh:330,342-344` — `205f7d2`
- [x] `*"node is inactive"*` matched echoed text: the state is read from the last `; node is <state>` clause; case 21c — `reset_cube_nodes.sh:500-501` — `c651035`
- [x] `resetSession` caught only `std::exception`: added `catch (...)`; `ANonStdThrowWhileBuildingTheResetIsContained` — `cube_bathymetry_node.hpp:2088-2092` — `931f3ae`
- [x] `coverage_grid` stayed latched after an assessor reset with the water surface unavailable: when active with the areas projected the reset publishes the emptied `coverage_assessment` and `coverage_grid` itself (flags put back so the next tick still assesses); READMEs updated; `ResetPublishesTheEmptyResultWhileTheWaterSurfaceIsStale` keeps the tide stale after the reset and checks both latched topics — `coverage_assessment_node.hpp` — `fc3fbdd`
- [x] Exit-1 description / README said "a node refused": the usage text (which stopped at the first exit-status line) now prints the whole block and says a refusing consumer is walked; README likewise; case 24 — `reset_cube_nodes.sh:128-129`, `README.md` — `d522b66` (the wording itself landed in `ced1b42`)
- [x] `--allow-persistence` classification line: now "lifecycle walk (--allow-persistence, the service would refuse a draft_dir): ..." for CUBE nodes and "(no ~/reset service)" only for consumers that lack it; case 16a — `reset_cube_nodes.sh:425` — `1713d61`
- [x] `AllCallbacksShareTheDefaultGroup` counted only auto-added groups and the handler comment was wrong under `use_sim_time`: an audit of every group outside the executor (no service/client/timer, subscriptions only to `/tf`, `/tf_static`, `/clock`), run with and without `use_sim_time`, a control test that adds a reentrant group, an unspun timer and a foreign subscription and checks they are reported, the same pin and a comment for the assessor, the node comment corrected — `test_node_lifecycle.cpp:1175-1197`, `cube_bathymetry_node.hpp:2050-2051` — `d8b8308`
- [x] Nothing enforces "not for use underway": the consequence (the next CA window holds only new-session cells, NaN and lethal under `unsurveyed_is_lethal` around the vessel) is stated in the service docstring, the README and the script header; no parameter gate added (documentation only, so no test) — `cube_bathymetry_node.hpp:2070-2099`, `README.md` — `9985062`
- [x] No stub case ran an inactive node into a re-run: cases 22a and 22b — `test_reset_cube_nodes.sh` — `ced1b42`
- Also: the plan is synced (`a106de6`), and one cpplint/uncrustify line wrap in a new test (`9a52cf6`).

### Tests
`./sensors_ws/build.sh cube_bathymetry coverage_assessment && ./sensors_ws/test.sh cube_bathymetry coverage_assessment` at `9a52cf6`, private `ROS_DOMAIN_ID`: **colcon summary 1076 tests, 0 errors, 0 failures, 110 skipped** (the 110 are the cppcheck lint tests, reported as skipped; cpplint and uncrustify ran and pass). A first full run, before `9a52cf6`, had exactly one cpplint and one uncrustify failure, both in my new test (a line over 100 columns), fixed in `9a52cf6`. New tests: 6 gtests in `test_node_lifecycle` (`ANonStdThrow...`, `ResetWithAnUnloadablePrior...`, `ResetMayRenameALegacyPriorSurveyDir...`, `TheCallbackGroupAuditNoticesWhatItIsFor`, plus the reworked `AllCallbacksShareTheDefaultGroup`), 3 in `test_coverage_assessment_node` (`ResetPublishesTheEmptyResult...`, the group audit and its control), and `test_reset_cube_nodes.sh` cases 14b, 19b-19e, 21, 21b, 21c, 22a, 22b, 23, 24 (stub extended: `-t` types, `prior_<node>` text). Live check on `ROS_DOMAIN_ID=89` against a Python Trigger server: the real `ros2 service list -t` prints `/boat1/cube_bathymetry/reset [std_srvs/srv/Trigger]`, a `SetBool` `/reset` was classified as walk, and the final script reset the Trigger node by service with exit 0; I killed only the PID I started.

### Revert checks (each new test fails with its fix reverted)
Script cases were run with the script as committed one commit earlier (`git show HEAD:` copy) against the new test, so each fix is reverted individually; the C++ ones with a temporary source mutation, rebuilt, then restored.
- `7b1a4c0`: 14b fails (3 assertions: exit 0 instead of 1, refusal not reported, went on past it).
- `ab52bb2`: 19b (timed-out call repeated) and 19d (default timeout not the set timeout, repeated) fail; 19c passes on the old script too (it only pins the existing fast-failure retry budget).
- `ced1b42`: 17 failures across 21, 21b, 22a, 22b (exit 4, nodes left inactive, no promise in the die text).
- `c651035`: 21c fails (an active node named in the "activated" summary and reported as inactive).
- `49d26b2`: 19e fails (claims "nothing was changed", does not name the node, and does not say both).
- `205f7d2`: case 23 fails (4 assertions: the SetBool `/reset` was called, did not walk, `-t` not asked).
- `1713d61`: case 16a fails (reason not named, wrong "no ~/reset service" text).
- `d522b66`: case 24 fails (the usage stopped at the first exit-status line).
- `931f3ae`: with the `catch (...)` removed `ANonStdThrowWhileBuildingTheResetIsContained` fails (the throw escapes).
- `f80042f`: with `buildSession(false)` in the reset, `ResetWithAnUnloadablePriorKeepsTheGatedSession` fails (6 assertions: success returned, sheet replaced).
- `9985062`: with the old message restored, `ResetServiceResetsTheSessionState` and `ResetMayRenameALegacyPriorSurveyDirLikeConfigure` fail. The peak-RAM and underway text are documentation only and have no test.
- `fc3fbdd`: with the `publishEmptyAssessment()` call removed `ResetPublishesTheEmptyResultWhileTheWaterSurfaceIsStale` fails (the old coverage stays latched).
- `d8b8308`: the old test (auto-added groups only) cannot see a group spun on its own thread; the new control test `TheCallbackGroupAuditNoticesWhatItIsFor` (both packages) shows the audit reports a reentrant group, an unspun timer and a foreign subscription, which is the revert check for this finding.

### Not addressed
None. Two findings (peak RAM, not for use underway) were closed by documentation as the findings themselves allow; the script exit 4 and its `NOT publishing` line are gone (a deliberate change under the owner decision), so anything outside this repo that parsed them must be updated; none was found in this repo.

### Open items (unchanged from the first Implementation entry)
- [ ] Live four-boat timing check by the host (issue work item), against the 318 to 324 s baseline; the README still gives only a call-count estimate. The PR body must carry no closing keyword for #167 until it is done or waived.
- [ ] PR body: the new `AGENTS.md` sentence for the owner to see, and the statement that a boat with `draft_dir` set is not served until rolker/unh_marine_autonomy#391.
- [ ] Run `/review-code` (pre-push mode) again on the fixes before pushing; nothing was pushed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-13 13:55 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-167 at `6d33f6e`
**Mode**: pre-push
**Depth**: Deep (reason: 3582 changed lines across 15 files; lifecycle and concurrency change)
**Must-fix**: 2 | **Suggestions**: 5
**Round**: 2 | **Ship**: recommended — must-fix fell from 3 to 2 and both have a precise fix with a targeted test; apply them and ship without a third full round

Specialists: static analysis (shellcheck 0.11 on the script and its test, clean; build and `colcon test` of both packages at `6d33f6e`, private `ROS_DOMAIN_ID`: 1076 tests, 0 errors, 0 failures, 110 skipped, the cppcheck tests); governance; plan drift; Claude adversarial Lens A and Lens B (Deep); cross-model review by gemini and codex (both completed); local model off.

Round-1 closure: 15 of the 16 findings are closed by their commits (checked against the code, the new stub cases and gtests, and the revert checks in the Implementation entry). Round-1 finding 7 (a prior that cannot be loaded must fail the reset) is only partly closed by `f80042f`: see must-fix 1. Governance is unchanged from round 1 (ADR-0001 preserved, ADR-0008 followed). The plan was synced in `a106de6`. Nothing outside this repo calls `reset_cube_nodes.sh` or reads its exit status, so retiring exit 4 breaks no caller.

### Findings
- [x] (must-fix) A missing, unmounted, empty or all-level-mismatched prior store does not fail the reset. `load()` returns 0 tiles without throwing, so `primed.total()==0` takes the warn branch and the reset returns success with the blunder gate off, replacing a gated session with an ungated one, which `f80042f` was meant to prevent. `ResetWithAnUnloadablePriorKeepsTheGatedSession` covers only a corrupt tile, which throws. Fix: record what the adopted session primed; under `prior_must_load`, fail when the running session was gated and the rebuild primes zero tiles, so a node whose prior never primed anything is not locked out. Add a test that renames the prior directory after configure. README line 103 ("Refuses ... when") should name this case. [Lens A + Lens B + codex + gemini + lead] — `cube_bathymetry/src/cube_bathymetry_node.hpp:1847-1856`
- [x] (must-fix) `has_reset_service` pipes `printf` into `grep -Fxq` under `pipefail` with SIGPIPE ignored. When the service list is over the pipe buffer (about 64 KB, plausible on a multi-boat graph), `grep -q` exits on the match, `printf` gets EPIPE, the pipeline returns 1, and a node that serves `~/reset` is classified walk ("no ~/reset service", about 17 calls instead of 1). The lead reproduced it: rc 1 with the pipe, rc 0 with a here-string. Fix: `grep -Fxq -- "$1/reset [std_srvs/srv/Trigger]" <<<"$service_list"` (and the same at the `grep -m1` on line 387); add a stub case whose `service list -t` prints more than 64 KB before the match. [Lens A, reproduced by lead] — `cube_bathymetry/scripts/reset_cube_nodes.sh:368`
- [x] (suggestion) An inactive consumer is reset and then activated by the script. `publishEmptyAssessment` runs only when the node is active at reset time, so with the water surface unavailable the old `coverage_assessment` and `coverage_grid` stay latched after "reset done". Fix: set a pending flag in `resetSession` and publish the empty result in `on_activate`. [Lens B + lead] — `coverage_assessment/src/coverage_assessment_node.hpp:950-952`
- [x] (suggestion) `publishEmptyAssessment` returns without publishing while an area definition is queued (`areas_dirty_`). Its docstring says "the first tick that can projects them, and publishes", but a tick projects and then returns at an unavailable surface, so the old result stays latched. Fix: project the queued areas there, or correct the docstring. [gemini + lead] — `coverage_assessment/src/coverage_assessment_node.hpp:960-977`
- [x] (suggestion) The `resetService` containment covers only the build. The `prior_status` copy, the message concatenation and the logging run outside it, at the documented RAM peak, so a `bad_alloc` there escapes the callback and ends the process. Wrap the whole handler as the assessor's `onReset` does. [Lens B] — `cube_bathymetry/src/cube_bathymetry_node.hpp:2083-2131`
- [x] (suggestion) The callback-group audit ignores the waitables in groups outside the executor, so an action server or other waitable on its own thread would pass. Allow only the subscriptions' QoS event handlers (compare the counts). [Lens B] — `cube_bathymetry/test/test_node_lifecycle.cpp:1336`, `coverage_assessment/test/test_coverage_assessment_node.cpp:1897`
- [x] (suggestion) The final summary `echo` is the script's last command, so with stdout closed (`| head -1`) a finished reset exits 1, against "0 reset done". End with `exit 0`, as `print_states` already guards. [Lens A] — `cube_bathymetry/scripts/reset_cube_nodes.sh:591-593`

Dropped:
- gemini: "`changed` is never set when an inactive node is activated". False: `to_active` goes through `transition()`, which sets it.
- gemini: "partial fleet reset on a refusal". An owner decision.
- gemini: "the empty assessment publishes a synthetic 0.0 m surface". False: `CoverageAssessment` carries no surface field.
- gemini: "lethal NaN window after a reset". Documented in round 1.
- gemini: "composition with a MultiThreadedExecutor". False: the nodes are not registered components, and `spinUntilStopped` requires a SingleThreadedExecutor.
- gemini: "the refresh-parameter callback during a reset". False: it runs on the same single-threaded executor, and `clear()` keeps the interval and budget.
- gemini: "preflight skips service-class nodes". A design choice: a finalized node serves no `~/reset`.
- gemini: "trailing slash in `has_reset_service`". False: `qualified()` strips it.

## Implementation
**Status**: complete
**When**: 2026-10-13 14:46 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Branch**: feature/issue-167 at `5a55f59`
**Addressed**: `## Local Review (Pre-Push)` of 2026-10-13 13:55 -04:00 at `6d33f6e` (round 2, changes-requested; 2 must-fix, 5 suggestions), with the owner decision of 2026-10-13: fix all seven.
**Commits**: `dc9cf59` `63d077b` `b95eed1` `110658b` `d39586d` `b30b6e8` `da94ab1`, plus `49372ff` (plan sync) and `5a55f59` (style) (nothing pushed; no PR; the issue was not edited)

### Actions
- [x] (must-fix) A prior that loads without error but primes nothing (missing, unmounted, empty, all-level-mismatched) replaced a gated session with an ungated one: the node records how many prior tiles the running session primed (`prior_tiles_primed_`, set by `adoptSession`); the reset fails and keeps the session when that was more than zero and the rebuild primes none; a node whose prior never primed anything is not locked out; README table and `cube_bathymetry` section name the case; `ResetWithAPriorThatPrimesNothingKeepsTheGatedSession` (prior directory renamed after configure, refused, works once renamed back; plus a node with a never-existing prior still resets) — `cube_bathymetry_node.hpp` `resetSession`, `README.md:101,117` — `dc9cf59`
- [x] (must-fix) `has_reset_service` and the `grep -m1` in `service_reset` piped `printf` into `grep` under pipefail with SIGPIPE ignored: both now read a here-string; case 25 gives the stub a `service list -t` of about 200 KB (about 100 KB of other services before the matches and 100 KB after) — `reset_cube_nodes.sh:368,387` — `63d077b`
- [x] (suggestion) An inactive consumer reset and then activated kept the old result latched while the water surface was unavailable: `resetSession` sets `publish_empty_on_activate_` when inactive (cleared in `releaseConfiguredEntities`), `on_activate` calls `publishEmptyAssessment` after the base class has activated the publishers; README (both) updated; `ResetWhileInactivePublishesTheEmptyResultOnActivation` (reset while inactive publishes nothing, activation publishes the empty `coverage_assessment` and `coverage_grid` with the surface still stale, a late joiner gets the empty one) — `coverage_assessment_node.hpp` — `110658b`
- [x] (suggestion) The `publishEmptyAssessment` docstring said the first tick that can project the areas also publishes: corrected (a tick projects and then returns at an unavailable, stale or too-far surface, so the old result stays latched until a tick can assess; for a queued definition the empty result is not guaranteed). Documentation only, so no test — `coverage_assessment_node.hpp:968-980` — `d39586d`
- [x] (suggestion) Containment covered only the build: `resetService` now wraps the whole call in try/catch (`std::exception` and `...`), the response message is built before the swap so nothing that can throw runs after it, the failure text is set by a helper that cannot throw again, and logging cannot undo a done reset; a test seam (`reset_fault_injector_`) throws after the build; `AThrowAfterTheBuildIsContainedByTheHandler` (`bad_alloc` and a non-std throw) — `cube_bathymetry_node.hpp` `resetService`, `resetSession` — `b95eed1`
- [x] (suggestion) The callback-group audits ignored waitables in groups outside the executor: they now allow only the QoS event handlers of the subscriptions in those groups (`get_event_handlers()`, compared by pointer), in both packages; the control tests add an inert `rclcpp::Waitable` to a group outside the executor (via `get_node_waitables_interface()->add_waitable`) and check it is reported — `test_node_lifecycle.cpp`, `test_coverage_assessment_node.cpp` — `b30b6e8`
- [x] (suggestion) A closed stdout failed a finished reset: the script ends with `exit 0`; case 26 runs it with `>&-` and under `| head -1` — `reset_cube_nodes.sh:end` — `da94ab1`
- Also: the plan is synced (`49372ff`), and the lines cpplint and uncrustify flagged in the new code were wrapped (`5a55f59`; the first full run had 9 lint failures, all in my new lines, none in tests).

### Tests
`./sensors_ws/build.sh cube_bathymetry coverage_assessment && ./sensors_ws/test.sh cube_bathymetry coverage_assessment` at `5a55f59`, private `ROS_DOMAIN_ID=86`: **colcon summary 1079 tests, 0 errors, 0 failures, 110 skipped** (the cppcheck lint tests, reported as skipped; cpplint and uncrustify ran and pass), against 1076 before: three new gtests (`ResetWithAPriorThatPrimesNothingKeepsTheGatedSession`, `AThrowAfterTheBuildIsContainedByTheHandler`, `ResetWhileInactivePublishesTheEmptyResultOnActivation`); the two audit control tests were extended; `test_reset_cube_nodes.sh` gained cases 25 and 26 and passes. `shellcheck` is not installed here, so the script and its test were not shellchecked (the pre-commit hooks ran on every commit).

### Revert checks (each new test fails with its fix reverted)
- `dc9cf59`: with the gate comparison disabled (`if (false && ...)`), `ResetWithAPriorThatPrimesNothingKeepsTheGatedSession` fails (reset returns success, no "primed no tiles", session replaced).
- `63d077b`: the previous script (`git show HEAD~1:`) against the new test: 5 failures, all case 25 (the service nodes were classified as having no service and walked).
- `da94ab1`: the previous script against the new test: 2 failures, both case 26 (exit 1 with stdout closed and under `| head -1`).
- `b95eed1`: with `throw;` as the first statement of both handler catch blocks (the catches ineffective), `AThrowAfterTheBuildIsContainedByTheHandler` fails (the exception escapes the service callback).
- `110658b`: with the `on_activate` hook disabled, `ResetWhileInactivePublishesTheEmptyResultOnActivation` fails (no empty result after activation).
- `b30b6e8`: with the event-handler comparison disabled in both audits, both `TheCallbackGroupAuditNoticesWhatItIsFor` controls fail (the foreign waitable is not reported).
- `d39586d`: documentation only, no test.
- The `grep -m1` here-string in `service_reset` has no separate check: a `service call` answer large enough to matter does not occur, and case 25 pins the `service list` one.

### Not addressed
None.

### Open items (unchanged from the first Implementation entry)
- [ ] Live four-boat timing check by the host (issue work item), against the 318 to 324 s baseline; the README still gives only a call-count estimate. The PR body must carry no closing keyword for #167 until it is done or waived.
- [ ] PR body: the new `AGENTS.md` sentence for the owner to see, and the statement that a boat with `draft_dir` set is not served until rolker/unh_marine_autonomy#391.
- [ ] Run `/review-code` (pre-push mode) again on the fixes before pushing; nothing was pushed.
