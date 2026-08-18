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
- **The importer's input path carries exactly these fields** (per-file,
  verified — corrected from an earlier overstated version of this bullet):
  - `cube_bathymetry/include/cube_bathymetry/sounding.h:43` is the only
    consumer reading all four: it computes
    `range = detections.two_way_travel_times[i] * detections.ping_info.sound_speed / 2.0`
    and uses `tx_angles` (guarded, default 0 when absent) + `rx_angles` for the
    3D sounding position (`tx_angles` read at `sounding.h:49-50`, `rx_angles`
    at `:53-54`).
  - `src/error_model.cpp` reads `two_way_travel_times` (`:277`, `:344`, `:383`),
    `rx_angles` (`:206`) and `ping_info.sound_speed`/`ping_info.rx_beamwidths`
    (`:277`, `:344`, `:237`) — it does **not** read `tx_angles`.
  - `src/detections_projector.cpp` reads only `ping_info.sound_speed`
    (`:134-135`, into `platform.mean_speed`/`surf_sspeed`); the four-field list
    in `detections_projector.h:107` is a doc comment about the input message,
    not a read site.

  Consequence for the inversion (question 4): the observables **arrive intact
  in the `SonarDetections` message**, but `Sounding` does **not retain** raw
  `two_way_travel_times`, `tx_angles`, or `ping_info.sound_speed` — it keeps
  only the derived `slant_range`, `beam_angle` (= rx angle), `intensity` and
  `sonar_relative_position`. An inversion engine must therefore read the
  message at import time (or `Sounding` must be extended); it cannot recover
  travel time and applied sound speed separately from a stored `Sounding`.

What source inspection *cannot* answer, and the bag inspection step below
is still needed for:

1. Whether `two_way_travel_times[]` / `tx_angles[]` / `rx_angles[]` /
   `ping_info.sound_speed` are actually **populated** (non-empty, non-zero)
   in a real M3 recording, vs. structurally present but left at their
   "unavailable" sentinel/empty state by the driver.
2. ~~Whether the recorded soundings are raw beam detections or a ray-traced
   product~~ — **largely answerable from the driver source** (plan-review
   correction): the M3 driver `kongsberg_em_bridge` is checked out at
   `layers/main/sensors_ws/src/marine_tools/kongsberg_em_bridge` and
   documents itself as a pure wire-format translator decoding the
   Kongsberg "Raw Range and Angle 78" datagram — geometry and TPU happen
   downstream. Verify that claim against its decode path, then use the
   bag sample only as an empirical cross-check (discriminator: sampled
   `two_way_travel_times` should be genuine two-way seconds, i.e.
   ≈ 2·depth/sound_speed at nadir, not pre-ray-traced ranges).
   *(Implementation note: this "discriminator" was **demoted to context** in the
   findings — a pre-ray-traced range divided the same way also yields a
   plausible nadir depth, so it discriminates nothing; the Q2 verdict rests on
   the driver source read alone.)*
3. Whether the ROS-side recording is a reduced product relative to the
   M3's full output flowing to QINSy on mercat — partly source-answerable
   too: `kongsberg_em_bridge`'s `~/set_recording` raw-`.all` service
   shows what full-datagram capture exists; the residual question is
   which datagrams the bridge decodes vs. drops.

## Approach

1. **Representative bags — resolved (operator-confirmed on this host).**
   Deployment recordings live under `~/data/logs/gabby/logs/`:
   `bizzy_m3/bag_2026-06-09T14.51.50_m3_detections/` carries
   `/bizzy/sensors/m3/detections` (`marine_acoustic_msgs/SonarDetections`)
   plus `/bizzy/sensors/m3/soundings` (PointCloud2), and
   `bizzyboat/2026-08-03T18-07-40+00-00/` (Lewes) is a full main
   deployment bag — check that one too, since whether the *main* bag
   carries the detections topic is itself part of question 3. Bags are
   data-of-record: read-only, no modification. *(Post-implementation
   operator correction, 2026-08-18: only `bizzyboat_sonar/` bags — the
   `sonar_logger` output, whose destination comes from
   `perception_launch.py:275-278` + `:34-41`, **not** the overridden
   `bizzyboat.yaml:844-845` uri — are sonar survey data-of-record; `bizzy_m3/`
   and similar are temp engineering captures, and the `.all` files are
   debug-purpose — operator-stated intent, which the launch comment at
   `perception_launch.py:114-117` reads differently; see the findings entry's
   Q3. Findings scoped accordingly; the
   `bizzyboat/2026-08-03…` bag named below was not scanned — the same-day
   `bizzyboat/2026-08-05T*` bags were checked instead and carry no M3
   topics.)* Note per finding from
   rolker/unh_echoboats_project11#342: `retrofit_m3_bag.py` rewrote some
   M3 bags in place (transducer TF offset + integer-second clock-skew) —
   record which variant (retrofitted or not) was sampled, since the skew
   itself bears on invertibility. If the bag check cannot complete, the
   verdict must be labelled **provisional (source-only)** in the findings.
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
   reading the M3 driver source and launch/config: the driver is
   `kongsberg_em_bridge` (`layers/main/sensors_ws/src/marine_tools/kongsberg_em_bridge`)
   — check which datagrams it decodes vs. drops and its `~/set_recording`
   raw-`.all` capture path; the platform launch/config lives in the
   `bizzyboat_project11` *package* inside the
   `layers/main/platforms_ws/src/unh_echoboats_project11` repo (the
   package name is not the repo dir name). Source-only, read-only.
5. **Write the findings comment** on
   https://github.com/rolker/cube_bathymetry/issues/121 answering all
   four questions, citing the source evidence above plus the bag-sample
   results, with an explicit invertible yes/no verdict and the standard
   AI signature block (`$AGENT_NAME` / `$AGENT_MODEL`). If any field
   proves absent/unpopulated, scope the follow-up
   `rolker/unh_echoboats_project11` recording-change issue per the
   deliverable.
6. **Persist a typed progress.md entry** mirroring the comment, per the
   Issue Review's recommendation, so the result is queryable without GitHub
   read access and available to `run-issue`'s next-phase logic. *(As
   implemented: recorded under the ADR-0013 `## Implementation` type with an
   "Entry role" line marking it as the spike deliverable — ADR-0013 defines
   no investigation-result heading, so the closest canonical type is reused
   and the gap flagged, not worked around with a non-canonical heading.)*

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
| Workspace ADR-0013 (progress.md entry-type vocabulary) | Yes | `## Plan Authored` entry now; deliverable recorded under the canonical `## Implementation` type with an "Entry role" marker (ADR-0013 defines no investigation-result heading). |
| cube_bathymetry ADR-0001/0002/0003/0007 | No | Govern tile eviction/staleness/backscatter-store lifecycle; not implicated by a read-only sensor-data audit. |
| `unh_marine_autonomy` ADR-0009 (`marine_interfaces/SonarInfo` as local prototype, cited in `SonarInfo.msg`) — distinct from workspace ADR-0009 (dev-tool venv policy) | No (informational) | Confirms `SonarInfo` and `PingInfo` are deliberately separate messages with a documented division of fields — relevant context for question 2, not a compliance obligation here. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Findings show fields insufficient/unpopulated | File follow-up recording-change issue in `rolker/unh_echoboats_project11` | Yes — step 5 |
| Findings show fields sufficient | Unblocks rolker/unh_marine_autonomy#300 phases 1+ (sim harness, inversion MVP) — epic owner acts on the comment | **Done** — epic signal posted 2026-08-17 and refreshed 2026-08-18 with the final scope/citation corrections (https://github.com/rolker/unh_marine_autonomy/issues/300#issuecomment-5323318922); no open host items |
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
  `cube_bathymetry` sensor-data work. This repo has no `.agents/README.md`
  yet (noted gap), so this stays a knowledge-capture candidate surfaced in
  the PR body for the operator to place (e.g. when a `.agents/` guide is
  created as its own task) — now empirically confirmed against real bags by
  the findings entry.

## Open Questions

- [x] ~~Where is a Massabesic/Lewes bag containing `/bizzy/sensors/m3/detections`
      actually reachable from?~~ **Resolved at the plan-review checkpoint
      (operator)**: bags are on this host under `~/data/logs/gabby/logs/`
      (`bizzy_m3/` M3-specific, `bizzyboat/` full deployment bags) —
      see Approach step 1.

## Estimated Scope

Single pass — the deliverable is the findings comment plus the typed
progress.md entry, not a code change. *(Updated: the branch's docs-only
plan/progress commits do go up as a PR, per the workspace's
all-changes-via-PR rule; "no PR needed" meant no implementation PR.)*
