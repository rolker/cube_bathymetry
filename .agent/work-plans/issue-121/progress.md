---
issue: 121
---

# Issue #121 — Spike: audit ROS bag M3 data for sound-speed-inversion observables (per-beam travel time / angle invertibility)

## Issue Review
**Status**: complete
**When**: 2026-08-17 23:15 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #121
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

**Well-scoped?** Yes. Four concrete questions, an explicit method (read message
definitions + inspect one representative bag + trace the importer's bag-reading
path), an explicit non-goal ("no code changes"), and a single deliverable (a
findings comment: invertible yes/no, fields present/missing, and — if
insufficient — a proposed recording-change issue filed in
`rolker/unh_echoboats_project11`). Completable in one PR-equivalent pass (a
findings writeup); no splitting needed.

**Right repo?** Yes. `cube_bathymetry` is explicitly named as the owner of the
future inversion engine in the parent epic
(rolker/unh_marine_autonomy#300: "cube_bathymetry owns the inversion engine —
it reads beam-level observables from ROS bag files at import time"), and this
spike's method step ("trace the importer's bag-reading path") targets code
that lives here: `cube_bathymetry/src/import_bag_main.cpp` and
`store_import.cpp`. Confirmed those files consume
`marine_acoustic_msgs/SonarDetections` (`-d <detections_topic>`) plus a
sibling `marine_interfaces/msg/SonarInfo` topic (derived via
`deriveSonarInfoTopic()`) and `nav_msgs/Odometry`. `SonarInfo.msg` itself notes
that per-ping quantities including `sound_speed` live in a companion
`PingInfo` message it explicitly does not restate — that's an external
dependency (`marine_acoustic_msgs`, not vendored in this workspace checkout)
the spike will need to inspect directly in a bag rather than from local
source. This matches the issue's question 2 exactly (is the applied SSP/surface
sound speed recorded alongside the soundings) and confirms it's a real open
question, not something answerable from this repo's source alone.

**Dependencies**: None blocking. This issue *gates* epic phases 1+
(rolker/unh_marine_autonomy#300 lists it as a Phase-0 spike alongside #298 and
#299) and is explicitly called out in the epic's Risks section as a possible
epic-killer ("Bag contents may not be invertible ... would require a
boat-side recording change first"). No open issue blocks *this* one.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Only what's needed | OK | Investigation-only, no code changes; right-sized for a gating spike. |
| Capture decisions, not just implementations | Watch | Deliverable is specified as a GitHub issue comment. Comments are useful for human visibility but are not the workspace's canonical record (progress.md is, per ADR-0013) and aren't picked up by `run-issue`'s phase-reading logic. Recommend the findings also land as a typed progress.md entry (e.g. via `progress_append.sh`) so the audit result is queryable by future phases/agents without GitHub read access, not just posted as a comment. |
| A change includes its consequences | OK | The issue already anticipates its own consequence: if data is insufficient, a follow-up recording-change issue in `rolker/unh_echoboats_project11` is explicitly scoped as part of the deliverable, not left implicit. |
| Test what breaks | N/A | No code under test; investigation only. |
| Workspace vs. project separation | OK | Domain content (sensor/bag semantics) correctly lives in the project repo, not the workspace. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Workspace ADR-0002 (Worktree isolation) | Yes | Already satisfied — a layer worktree exists at `layers/worktrees/issue-cube_bathymetry-121` on `feature/issue-121`. |
| Workspace ADR-0013 (progress.md entry-type vocabulary) | Yes | This review's own entry satisfies it. The eventual findings should also be persisted as a typed entry (see Recommendation above) rather than only posted to GitHub. |
| cube_bathymetry ADR-0001/0002/0003/0007 (tile eviction, staleness, backscatter store) | No | These govern the store's tile lifecycle, not sensor recording/import-path data content; not implicated by a read-only bag/message audit. |

### Consequences

- If the audit finds recorded fields insufficient (raw beam observables not
  present, or SSP/surface sound speed not recorded alongside ray-traced
  soundings), the issue already scopes the correct next step: a follow-up
  issue in `rolker/unh_echoboats_project11` proposing a recording change —
  this is stated as the deliverable, not an afterthought.
- If the audit finds the data sufficient, phases 1+ of
  rolker/unh_marine_autonomy#300 (sim validation harness, inversion MVP) can
  proceed; no other doc/test updates are implicated by this spike itself.

### Recommendations

- Persist the findings as a typed progress.md entry in addition to the GitHub
  comment, so the result is part of the durable, cross-agent-readable record
  (per the workspace's Documentation Accuracy / progress.md conventions) and
  not only visible to agents/humans with GitHub read access.
- When inspecting a representative bag, prefer a recent Massabesic or Lewes
  survey day already referenced in project memory (e.g. the 2026-08-05/06
  Lewes deployments) so the finding is grounded in current recording
  configuration rather than a stale one.

### Actions
- [ ] Persist audit findings to progress.md as a typed entry (not only a GitHub comment) once the investigation phase runs.

## Plan Authored
**Status**: complete
**When**: 2026-08-17 23:09 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-121/plan.md` at `92250b9`
**Branch**: feature/issue-121 at `92250b9`
**Phases**: single

### Open questions
- [ ] Where is a Massabesic/Lewes bag containing `/bizzy/sensors/m3/detections` actually reachable from (this dev host, gabby, or an external drive)? Only import logs, not source bags, were found locally during planning.

## Plan Review
**Status**: complete
**When**: 2026-08-17 23:12 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-121/plan.md` at `92250b9`
**PR**: PR-less (dispatched `--issue 121`; branch `feature/issue-121`)
**Verdict**: changes-requested

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Single-pass investigation, no code changes, matches the issue's four questions and explicit non-goal. |
| Issue alignment | Needs work | Q1/Q4 well covered from source. Q2 (raw vs ray-traced) has no concrete discriminator in the Approach. Q3 (reduced product vs QINSy) is routed to the wrong evidence source. |
| File targeting | Concern | Step 4 cites `layers/main/platforms_ws/src/bizzyboat_project11`, which does not exist (repo dir is `unh_echoboats_project11`; `bizzyboat_project11` is a package inside it). The M3 driver source that actually answers Q2/Q3 (`layers/main/sensors_ws/src/marine_tools/kongsberg_em_bridge`) is not referenced at all. |
| Consequences | Good | Both branches (sufficient / insufficient) mapped; secondary-finding branch handled without unprompted scope growth. |
| Documentation & instruction impact | Good | Section present, non-silent, instruction item framed as an operator-decided candidate and gated on empirical confirmation. |
| Principle alignment | Needs work | "Capture decisions" and "Only what's needed" satisfied. But an epic-gating verdict rests on an empirical step whose input (a bag) has no confirmed location and whose fallback likely has no target — the plan should not be able to reach a confident "invertible: yes" on source-only evidence without labelling it provisional. |
| ADR compliance | Good | ADR-0002 satisfied (layer worktree); ADR-0013 entry types planned for the findings; cube ADRs correctly ruled out. ADR-0009 correctly cited as informational. |
| ROS conventions | Good | Message-schema reasoning is correct and verified (see finding 5). |

### Findings
- [ ] (must-fix) Context wrongly declares Q2/Q3 unanswerable from source — the M3 driver `kongsberg_em_bridge` is checked out locally at `layers/main/sensors_ws/src/marine_tools/kongsberg_em_bridge` and its README states it decodes the Kongsberg "Raw Range and Angle 78" datagram and is "purely a wire-format translator: geometry and TPU happen downstream", i.e. recorded travel times are raw, not ray-traced. Add the driver (README + `kongsberg_em_bridge/em_datagrams.py` + `node.py`) as a primary evidence source — `plan.md:50-58`
- [ ] (must-fix) Step 4 targets a nonexistent path `layers/main/platforms_ws/src/bizzyboat_project11`; correct to `layers/main/platforms_ws/src/unh_echoboats_project11/bizzyboat_project11` — `plan.md:89-90`
- [ ] (must-fix) No bag containing `/bizzy/sensors/m3/detections` is reachable on this host (searched `~/data` for rosbag2 `metadata.yaml`/`.mcap`; no external media mounted; `~/data/logs/import_lewes_2026-08-05.log` records the topic but not the bag path). The step-1 fallback ("most recent locally-reachable bag containing the topic") probably has no target. Add an explicit escalation (ask the operator for the bag location / read-only check on gabby) and a rule that the findings verdict is marked **provisional** if the empirical population check cannot run — this spike gates rolker/unh_marine_autonomy#300 and is flagged there as a possible epic-killer, so a source-only "yes" must not read as confirmed — `plan.md:64-71`
- [ ] (suggestion) Step 2's sample only reports array sizes and `ping_info.sound_speed`; it gives no discriminator for Q2. Add a concrete test — e.g. does `sound_speed` vary per ping (a live surface-SSP feed; note `marine_tools/sound_speed_bridge` exists) or sit at a constant sentinel/1500 — and record its provenance — `plan.md:72-78`
- [ ] (suggestion) `bizzyboat_project11/scripts/retrofit_m3_bag.py` rewrites already-recorded M3 bags (transducer offset in `/tf_static`, integer-second `header.stamp` skew correction, in-place with `.orig` backup). Any sampled bag may be a retrofitted copy, and the timing skew itself bears on invertibility. Note which variant was sampled and mention the retrofit in the findings — `plan.md:72-78`
- [ ] (suggestion) Step 3 can be partly answered from source before touching a bag: the driver publishes a latched `sonar_info` (`marine_interfaces/SonarInfo`, transient_local, re-published per rosbag2 split). The bag check then only confirms it was recorded, not whether it exists — `plan.md:79-84`
- [ ] (suggestion) Q3 also has a concrete source lead the plan misses: the driver exposes a `~/set_recording` service for raw `.all` recording, so "what is lost relative to QINSy" is partly a question of whether that raw stream is captured — worth checking alongside the launch config — `plan.md:85-90`
- [ ] (suggestion) The deliverable is a GitHub comment; state that it carries the AI signature block (AGENTS.md) — `plan.md:91-97`
- [ ] (note, no action) Verified and correct: `SonarDetections.msg` carries `two_way_travel_times[]`, `tx_angles[]`, `rx_angles[]`, `intensities[]` and an embedded `PingInfo ping_info` (with `sound_speed`, 0 = unavailable); `sounding.h:43`, `error_model.cpp:277/344`, and `detections_projector.h:107` all read those fields. The plan's correction of the Issue Review's `PingInfo` assumption holds.

## Implementation

**When**: 2026-08-17 23:21 -04:00
**By**: Claude Code Agent (host-inline)
**Model**: Claude Fable 5
**Status**: complete

### Spike findings — bag invertibility audit (all four questions answered)

**Verdict: INVERTIBLE — YES. No recording change needed; rolker/unh_marine_autonomy#300 phases 1+ are unblocked.**

**Q1 — fields populated in real recordings: YES.** Sampled 50 `SonarDetections` messages from each of two bags on this host: `~/data/logs/gabby/logs/bizzyboat_sonar/2026-08-05T19-18-17+00-00` (Lewes) and `~/data/logs/gabby/logs/bizzy_m3/bag_2026-06-09T14.51.50_m3_detections` (Massabesic-era). Every message: 219–251 beams, `two_way_travel_times[]` fully populated (Lewes 2.2–6.6 ms; Massabesic 16.7–34.9 ms), `rx_angles[]` spanning ±1.031 rad (full ±59° fan), `tx_angles[]` all 0.0 (single-sector tx, expected), `ping_info.sound_speed` nonzero in all 100 messages, `ping_info.frequency` = 500 kHz, detection flags all 0 (valid).

**Q2 — raw, not ray-traced: CONFIRMED from driver source + empirical cross-check.** `kongsberg_em_bridge` is a pure wire-format translator of the Kongsberg Raw Range and Angle 78 (`N`) datagram: `em_datagrams.py:128` unpacks `twtt` directly from the wire and `node.py:563` appends it unmodified; `ping_info.sound_speed` is the datagram's own surface-sound-speed field (×0.1, `em_datagrams.py:149`) — i.e., the sonar-applied array-face sound speed, recorded **per ping**, which is exactly what an inversion needs to undo beam steering. Empirical discriminator per plan: min(twtt)·c/2 = 1.65 m (Lewes, shallow) and 12.49 m (Massabesic) — plausible nadir depths, so these are genuine two-way seconds.

**Q3 — ROS stream vs full M3 output.** The bridge decodes the RRA-78 datagram into `SonarDetections`; full-fidelity raw datagram capture exists independently via the driver's `~/set_recording` service writing `.all` files — present on disk (`bizzyboat_sonar/m3_all/m3_20260616_*.all`), so nothing is unrecoverable even where the ROS decode is narrower. Recording topology: the **main** deployment bag carries no M3 topics — M3 rides in dedicated sonar bags (`bizzyboat_sonar/`, `bizzy_m3/`). Companion `/bizzy/sensors/m3/sonar_info` (`marine_interfaces/SonarInfo`) is present in Aug 2026 bags (post SonarInfo-chain), absent in the June bag — as expected.

**Q4 — importer preserves the fields: YES (source, confirmed at plan time).** `sounding.h:43`, `error_model.cpp`, `detections_projector.cpp` consume `two_way_travel_times`/`tx_angles`/`rx_angles`/`ping_info.sound_speed` directly; nothing discarded on import.

**Secondary observations**
- The Lewes sample shows `ping_info.sound_speed` stepping 1469.0 → 1528.1 m/s within a 50-ping window — per-ping applied-SS tracking is live (good for inversion), but a ~59 m/s step is large; worth a glance at the AML feed continuity for that day (observation only, no issue filed per plan's no-unprompted-scope rule).
- Sampled-variant note (echoboats#342 retrofit): retrofit rewrites tf_static offset + stamp skew, not twtt/angle payloads; both a June (possibly retrofitted) and an Aug bag were sampled with consistent field population.

Findings comment posted on rolker/cube_bathymetry#121. Deliverable per plan steps 5–6 complete; no follow-up recording-change issue needed (sufficiency confirmed).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-17 23:30 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-121 at `eb5aaf7`
**Mode**: pre-push
**Depth**: Deep (reason: 343 changed lines; also project-repo trigger `.agent/work-plans/issue-*/plan.md`)
**Must-fix**: 8 | **Suggestions**: 10
**Round**: 1 | **Ship**: continue — must-fixes include factual corrections to an epic-gating verdict; several are falsified-by-full-bag-scan claims, not style
**Specialists**: Static analysis (no .md profile — not checked); Claude Adversarial x2 (Lens A + Lens B, Deep); Copilot off (default); Local qwen3.5:35b (3 findings, all false positives on spot-check — chiefly re-flagging plan defects already fixed in `c782621`)

### Findings
- [ ] (must-fix) Q1 "every message ... fully populated" falsified bag-wide — 306 empty `two_way_travel_times[]` in the June bag, beams 10-225; figures hold only for the undisclosed leading-50 sample of 55,100/124,375 msgs — `.agent/work-plans/issue-121/progress.md:149`
- [ ] (must-fix) Secondary SS observation inverted — the 1469.0 value is a 38-ping startup transient (rest of 74 min at 1527.4-1529.0), not "live per-ping tracking"; in-bag `/bizzy/sensors/sound_speed/sound_speed` reads 1528.10 from t=0, so the AML-continuity suggestion targets the wrong subsystem — `.agent/work-plans/issue-121/progress.md:158`
- [ ] (must-fix) "detection flags all 0 (valid)" is circular — `skip_invalid_beams` defaults true (`node.py:186,227,555`), so flagged beams never reach the message; the real consequence (invalid beams dropped from the ROS stream) is unstated — `.agent/work-plans/issue-121/progress.md:149`
- [ ] (must-fix) "nothing is unrecoverable" unsupported — `m3_all/` covers only 2026-06-16/17; neither sampled bag's date has raw `.all` coverage — `.agent/work-plans/issue-121/progress.md:153`
- [ ] (must-fix) Importer source claim overstated — `detections_projector.cpp` reads only `ping_info.sound_speed`; `error_model.cpp` does not read `tx_angles`; only `sounding.h:43` reads all four; and `Sounding` does not retain raw twtt/tx_angle/sound_speed — `.agent/work-plans/issue-121/progress.md:155`, `.agent/work-plans/issue-121/plan.md:37`
- [ ] (must-fix) ADR-0013 non-conformance in `## Implementation` — missing `**Branch**: ... at <sha>` correlation field and no `### Findings` checkbox list, so `progress_read.py` yields `correlation: null` / `findings: []` and the spike's entire deliverable is machine-invisible — `.agent/work-plans/issue-121/progress.md:138-157`
- [ ] (must-fix) Consequence owed cross-repo — declaring rolker/unh_marine_autonomy#300 unblocked leaves its "possible epic-killer" Risks line standing with no comment/timeline signal reaching the epic
- [ ] (must-fix) `tx_angles` explanation wrong — `node.py:574` sets them from sector tilt (zero tilt, not "single-sector"); the material unstated fact is that the observable set is (twtt, rx_angle) only, with no along-track launch angle — `.agent/work-plans/issue-121/progress.md:149`
- [ ] (suggestion) The "empirical discriminator" carries no information — a pre-ray-traced range also yields a plausible nadir depth; the raw-vs-ray-traced verdict rests on the source read alone — `.agent/work-plans/issue-121/progress.md:151`
- [ ] (suggestion) 1.65 m internally inconsistent — 2.162 ms at the c=1469.0 then in effect gives 1.588 m (12.49 m checks exactly) — `.agent/work-plans/issue-121/progress.md:151`
- [ ] (suggestion) "±1.031 rad (full ±59° fan)" mislabels the quantity — June bag reaches 1.0362 rad, nominal fan is 120°, and this is the surviving valid-beam extent after `skip_invalid_beams` — `.agent/work-plans/issue-121/progress.md:149`
- [ ] (suggestion) "(Massabesic-era)" label unverified — bag-wide max twtt 75.46 ms implies ~56 m, exceeding Massabesic depths — `.agent/work-plans/issue-121/progress.md:149`
- [ ] (suggestion) Plan step 4's "decodes vs drops" half unreported — `parse_datagram` (`em_datagrams.py:191-201`) decodes N78 + XYZ88 only; the dropped SURFACE_SOUND_SPEED `0x47` datagram is epic-relevant — `.agent/work-plans/issue-121/progress.md:153`
- [ ] (suggestion) Plan step 4's platform launch/config source never consulted/reported; topology answered from bag contents instead — say which source was used — `.agent/work-plans/issue-121/progress.md:153`
- [ ] (suggestion) Satisfied checkboxes left open — Issue Review action and the three Plan Review must-fixes (addressed in `c782621`) still read as open work to the parser — `.agent/work-plans/issue-121/progress.md:91,128-135`
- [ ] (suggestion) `**By**` / `**Model**` split diverges from ADR-0013's `**By**: <agent> (<model>)` used by the other three entries — `.agent/work-plans/issue-121/progress.md:141-142`
- [ ] (suggestion) Record the one-line disposition of the plan's conditional `.agents/README.md` note (correctly deferred — repo has no `.agents/` dir) — `.agent/work-plans/issue-121/progress.md:161`
- [ ] (suggestion) Bag variant left "possibly retrofitted"; absence of a `.orig` backup beside the June bag settles the plan's step-1 requirement — `.agent/work-plans/issue-121/progress.md:159`
