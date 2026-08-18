# Plan: Spike — audit ROS bag M3 data for sound-speed-inversion observables

## Issue

https://github.com/rolker/cube_bathymetry/issues/121

## Context

Phase-0 gating spike for the cast-free sound-speed-inversion epic
(rolker/unh_marine_autonomy#300). No code changes — the deliverable is a
findings comment (plus a typed progress.md entry per the Issue Review's
recommendation) answering four questions about whether recorded M3 bag data
carries the per-beam observables (two-way travel time, launch angle) the
inversion needs.

Source-code inspection already done during planning answers most of this
directly, without needing the bag inspection to be exploratory:

- **`marine_acoustic_msgs/SonarDetections`** (`/opt/ros/jazzy/share/marine_acoustic_msgs/msg/SonarDetections.msg`)
  carries `two_way_travel_times[]` (seconds), `tx_angles[]` / `rx_angles[]`
  (radians), `intensities[]`, and an embedded `PingInfo ping_info` field —
  these are raw beam detections, not just ranges.
- **`PingInfo`** (`/opt/ros/jazzy/share/marine_acoustic_msgs/msg/PingInfo.msg`)
  carries `sound_speed` (m/s, 0 if unavailable) **embedded in the same
  `SonarDetections` message** — it is not a separate topic that could go
  missing independently. This contradicts the Issue Review's working
  assumption ("per-ping `sound_speed` lives in a companion `PingInfo`
  message it doesn't restate" — that line describes the *`marine_interfaces/SonarInfo`*
  message's relationship to `PingInfo`, a different, slower-cadence topic
  carrying acquisition/correction metadata, not the travel-time/angle
  message itself).
- **The importer already consumes exactly these fields** for ray tracing:
  `cube_bathymetry/include/cube_bathymetry/sounding.h:43` computes
  `range = detections.two_way_travel_times[i] * detections.ping_info.sound_speed / 2.0`,
  and uses `tx_angles`/`rx_angles` for the 3D sounding position
  (`sounding.h:49-54`). `error_model.cpp` and `detections_projector.cpp`
  independently re-read the same three fields (`two_way_travel_times`,
  `tx_angles`/`rx_angles`, `ping_info.sound_speed`) for uncertainty and
  platform sound-speed propagation. **Nothing is discarded on the way in —
  the importer's input path already carries the exact fields the inversion
  needs**, answering question 4 directly from source.

What source inspection *cannot* answer, and the bag inspection step below
is still needed for:

1. Whether `two_way_travel_times[]` / `tx_angles[]` / `rx_angles[]` /
   `ping_info.sound_speed` are actually **populated** (non-empty, non-zero)
   in a real M3 recording, vs. structurally present but left at their
   "unavailable" sentinel/empty state by the driver.
2. Whether the recorded soundings are raw beam detections or a ray-traced
   product — i.e. whether `two_way_travel_times` reflects the true
   acoustic travel time or has already been adjusted/pre-corrected
   upstream of the ROS driver (unanswerable from the message schema or
   this repo's source; requires comparing recorded values against
   expected geometry in a real bag).
3. Whether the ROS-side recording is a reduced product relative to the
   M3's full output flowing to QINSy on mercat (a driver/config question,
   not visible in this repo).

## Approach

1. **Locate a representative bag.** Prefer a recent Massabesic or Lewes
   survey day (per the Issue Review's recommendation), topic
   `/bizzy/sensors/m3/detections` (confirmed from
   `~/data/logs/import_lewes_2026-08-05.log`, "Detections topic:
   /bizzy/sensors/m3/detections"). No bag under that name was found on
   this dev machine during planning (`~/data/logs/` has import logs but
   not the source bags) — bags likely live on gabby or an external
   drive. If none is reachable from this worktree/host, fall back to the
   most recent locally-reachable bag containing the topic, and note the
   substitution in the findings.
2. **Inspect message population in the bag** using `ros2 bag info` (topic
   presence, message counts, types) and a small Python/`ros2 bag play` +
   `ros2 topic echo` or `rosbag2_py` read-loop to sample N messages from
   `/bizzy/sensors/m3/detections` and report, per sampled message:
   `two_way_travel_times.size()`, `tx_angles.size()`, `rx_angles.size()`,
   `ping_info.sound_speed`, `ping_info.frequency` — confirming
   non-emptiness and non-zero/non-NaN sound_speed across the sample.
3. **Check for a companion `SonarInfo` topic** (`marine_interfaces/msg/SonarInfo`,
   `deriveSonarInfoTopic()` convention: `.../sonar_info` sibling of the
   detections topic) in the same bag, to establish whether the current
   deployment publishes it at all (independent of the inversion — informs
   whether backscatter-correction provenance is present, a secondary but
   related finding worth noting).
4. **Cross-check question 3** (reduced product vs. QINSy full output) by
   checking `bizzyboat_project11`'s M3 driver launch/config for any
   filtering, decimation, or field-stripping between the driver and the
   `/bizzy/sensors/m3/detections` publish — this is a source-only check
   (no bag needed) in a sibling repo
   (`layers/main/platforms_ws/src/bizzyboat_project11`), read-only.
5. **Write the findings comment** on
   https://github.com/rolker/cube_bathymetry/issues/121 answering all
   four questions, citing the source evidence above plus the bag-sample
   results, with an explicit invertible yes/no verdict. If any field
   proves absent/unpopulated, scope the follow-up
   `rolker/unh_echoboats_project11` recording-change issue per the
   deliverable.
6. **Persist a typed progress.md entry** (`## Investigation Findings` or
   equivalent ADR-0013 entry type) mirroring the comment, per the Issue
   Review's recommendation, so the result is queryable without GitHub
   read access and available to `run-issue`'s next-phase logic.

No code changes in this issue — steps 1-4 are read-only inspection; step 5
is a GitHub comment; step 6 is a progress.md entry. The `.agent/work-plans/issue-121/`
plan and progress files are the only files this branch touches structurally
(plus whatever the findings step appends).

## Files to Change

| File | Change |
|------|--------|
| `.agent/work-plans/issue-121/plan.md` | This plan (already being committed) |
| `.agent/work-plans/issue-121/progress.md` | Append `## Plan Authored` now; append findings entry during the investigation phase |
| *(none — no source files change; investigation-only per issue's explicit non-goal)* | |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Plan scopes exactly the four questions in the issue; no speculative extra analysis or code changes. |
| Capture decisions, not just implementations | Findings land in both a GitHub comment (human visibility) and a typed progress.md entry (durable, GitHub-independent record), per the Issue Review's explicit recommendation. |
| Test what breaks | N/A — no code under test; read-only investigation. |
| A change includes its consequences | Plan carries forward the issue's own scoped consequence (recording-change follow-up issue in `rolker/unh_echoboats_project11` if data proves insufficient). |
| Workspace vs. project separation | All investigation and findings stay in the project repo (`cube_bathymetry`); no workspace-repo changes. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| Workspace ADR-0002 (worktree isolation) | Yes | Already satisfied — plan committed inside the issue's layer worktree on `feature/issue-121`. |
| Workspace ADR-0013 (progress.md entry-type vocabulary) | Yes | `## Plan Authored` entry now; `## Investigation Findings`-style entry at the deliverable step. |
| cube_bathymetry ADR-0001/0002/0003/0007 | No | Govern tile eviction/staleness/backscatter-store lifecycle; not implicated by a read-only sensor-data audit. |
| ADR-0009 (`marine_interfaces/SonarInfo` as local prototype, cited in `SonarInfo.msg`) | No (informational) | Confirms `SonarInfo` and `PingInfo` are deliberately separate messages with a documented division of fields — relevant context for question 2, not a compliance obligation here. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Findings show fields insufficient/unpopulated | File follow-up recording-change issue in `rolker/unh_echoboats_project11` | Yes — step 5 |
| Findings show fields sufficient | Unblocks rolker/unh_marine_autonomy#300 phases 1+ (sim harness, inversion MVP) — no plan action needed here, epic owner acts on the comment | N/A (informational, out of this issue's scope) |
| Findings surface a related but secondary gap (e.g. `SonarInfo` not published in current deployment config) | Note in the comment as a secondary finding, do not scope new work unprompted | Yes — step 3, called out as secondary/non-blocking |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): None — no source or documentation
  files describe M3 recording contents in a way this spike would
  invalidate; the findings are new information, not a correction to
  existing docs.
- **Agent-instruction candidates** (proposals only — operator decides):
  The Issue Review's finding that the recorded `SonarDetections.ping_info.sound_speed`
  is embedded per-message (not a separate topic that could be silently
  absent) is a useful correction to keep in mind for future
  `cube_bathymetry` sensor-data work — worth a one-line note in this repo's
  `.agents/README.md` (if one exists) once the findings are confirmed
  against a real bag, but not before, since the plan-time claim is still
  source-only and unverified against actual recorded data.

## Open Questions

- [ ] Where is a Massabesic/Lewes bag containing `/bizzy/sensors/m3/detections`
      actually reachable from (this dev host, gabby, or an external
      drive)? Planning found only import *logs*, not the source bags,
      locally. If genuinely unreachable, the findings comment should say
      so explicitly and note the substitute evidence used (source-code
      answer for Q1/Q2/Q4, deferring the "is it actually populated"
      empirical check).

## Estimated Scope

Single pass — no PR needed beyond the plan/progress commits and the
findings comment; this is an investigation deliverable, not a code change.
