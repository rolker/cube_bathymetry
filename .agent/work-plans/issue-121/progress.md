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
- [x] Persist audit findings to progress.md as a typed entry (not only a GitHub comment) once the investigation phase runs. (satisfied — findings persisted as the corrected `## Implementation` entry below)

## Plan Authored
**Status**: complete
**When**: 2026-08-17 23:09 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-121/plan.md` at `92250b9`
**Branch**: feature/issue-121 at `92250b9`
**Phases**: single

### Open questions
- [x] Where is a Massabesic/Lewes bag containing `/bizzy/sensors/m3/detections` actually reachable from (this dev host, gabby, or an external drive)? Only import logs, not source bags, were found locally during planning. (resolved — bags located under `~/data/logs/gabby/logs/`; see plan Open Questions)

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
- [x] (must-fix) Context wrongly declares Q2/Q3 unanswerable from source — the M3 driver `kongsberg_em_bridge` is checked out locally at `layers/main/sensors_ws/src/marine_tools/kongsberg_em_bridge` and its README states it decodes the Kongsberg "Raw Range and Angle 78" datagram and is "purely a wire-format translator: geometry and TPU happen downstream", i.e. recorded travel times are raw, not ray-traced. Add the driver (README + `kongsberg_em_bridge/em_datagrams.py` + `node.py`) as a primary evidence source — `plan.md:50-58` (addressed in `c782621`)
- [x] (must-fix) Step 4 targets a nonexistent path `layers/main/platforms_ws/src/bizzyboat_project11`; correct to `layers/main/platforms_ws/src/unh_echoboats_project11/bizzyboat_project11` — `plan.md:89-90` (addressed in `c782621`)
- [x] (must-fix) No bag containing `/bizzy/sensors/m3/detections` is reachable on this host (searched `~/data` for rosbag2 `metadata.yaml`/`.mcap`; no external media mounted; `~/data/logs/import_lewes_2026-08-05.log` records the topic but not the bag path). The step-1 fallback ("most recent locally-reachable bag containing the topic") probably has no target. Add an explicit escalation (ask the operator for the bag location / read-only check on gabby) and a rule that the findings verdict is marked **provisional** if the empirical population check cannot run — this spike gates rolker/unh_marine_autonomy#300 and is flagged there as a possible epic-killer, so a source-only "yes" must not read as confirmed — `plan.md:64-71` (addressed in `c782621`)
- [x] (suggestion) Step 2's sample only reports array sizes and `ping_info.sound_speed`; it gives no discriminator for Q2. Add a concrete test — e.g. does `sound_speed` vary per ping (a live surface-SSP feed; note `marine_tools/sound_speed_bridge` exists) or sit at a constant sentinel/1500 — and record its provenance — `plan.md:72-78` (addressed in `c782621`)
- [x] (suggestion) `bizzyboat_project11/scripts/retrofit_m3_bag.py` rewrites already-recorded M3 bags (transducer offset in `/tf_static`, integer-second `header.stamp` skew correction, in-place with `.orig` backup). Any sampled bag may be a retrofitted copy, and the timing skew itself bears on invertibility. Note which variant was sampled and mention the retrofit in the findings — `plan.md:72-78` (addressed in `c782621`)
- [x] (suggestion) Step 3 can be partly answered from source before touching a bag: the driver publishes a latched `sonar_info` (`marine_interfaces/SonarInfo`, transient_local, re-published per rosbag2 split). The bag check then only confirms it was recorded, not whether it exists — `plan.md:79-84` (addressed in `c782621`)
- [x] (suggestion) Q3 also has a concrete source lead the plan misses: the driver exposes a `~/set_recording` service for raw `.all` recording, so "what is lost relative to QINSy" is partly a question of whether that raw stream is captured — worth checking alongside the launch config — `plan.md:85-90` (addressed in `c782621`)
- [x] (suggestion) The deliverable is a GitHub comment; state that it carries the AI signature block (AGENTS.md) — `plan.md:91-97` (addressed in `c782621`)
- [x] (note, no action) Verified and correct: `SonarDetections.msg` carries `two_way_travel_times[]`, `tx_angles[]`, `rx_angles[]`, `intensities[]` and an embedded `PingInfo ping_info` (with `sound_speed`, 0 = unavailable); `sounding.h:43`, `error_model.cpp:277/344`, and `detections_projector.h:107` all read those fields. The plan's correction of the Issue Review's `PingInfo` assumption holds. (note — per-file read sites corrected in the `## Implementation` entry: only `sounding.h:43` reads all four)

## Implementation
**Status**: complete
**When**: 2026-08-17 23:21 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**Branch**: feature/issue-121 at `eb5aaf7`
**Corrected**: rewritten in place 2026-08-17 23:45 -04:00 by the `address-findings`
pass for the `## Local Review (Pre-Push)` at `707392c` — the original text
generalized from an undisclosed leading-50-message sample and several of its
claims were falsified by full-bag scans. All numbers below are now **bag-wide**
(every message read, both bags; bags opened read-only — data of record).
**Operator corrections folded in 2026-08-18**: (1) only `bizzyboat_sonar/` bags
are sonar survey data-of-record — source-verified in the launch, not the yaml:
the `sonar_logger` node's `storage.uri` from
`bizzyboat_project11/config/bizzyboat.yaml:765`
(`/home/field/project11/data/bizzyboat_sonar`, a path that does not exist on
disk) is **overridden at launch** by `perception_launch.py:275-278`, which sets
it to `<sonar_log_directory>/<sonar_log_subdirectory>` — the directory being
`$P11_SONAR_LOG_DIR` defaulting to `/home/field/data/logs/bizzyboat_sonar`
(`perception_launch.py:34-41`) and the subdirectory the UTC timestamp that names
each bag. `bizzy_m3/` and similar hand-named directories are temporary
engineering captures. (2) The raw `.all` files were written for debugging
purposes (operator-stated intent — see Q3) — their sparse coverage is by design,
not a recording gap.

### Spike findings — bag invertibility audit (all four questions answered)

**Verdict: INVERTIBLE — YES, with stated limits, on the evidence scoped below.**
The per-beam observables an inversion needs — two-way travel time, receive
(across-track) steering angle, and the per-ping sound speed the sonar applied —
are recorded, raw, in the one **data-of-record** bag scanned in full (Lewes,
2026-08-05, 74 min, shoal water), and corroborated by a second full scan of a
non-of-record engineering capture. No recording change is required for
rolker/unh_marine_autonomy#300 phases 1+. The limits (recoverable observable
set, dropped invalid beams, raw `.all` coverage, evidence scope) are stated
under Q1/Q3 and the Limits section rather than buried.

**Evidence scope — one bag of a heterogeneous archive.** `bizzyboat_sonar/`
holds 157 entries: 152 rosbag2 recordings with a readable `metadata.yaml`, 4
directories without one (aborted recordings), and the `m3_all/` raw-`.all`
sibling — so "`bizzyboat_sonar/` is the sonar data-of-record directory" names
the recorder's purpose, not a clean partition (the tree also holds e.g.
`2026-06-04T…_kongsberg_em_test`). Of the 152, **48 carry
`/bizzy/sensors/m3/detections`**, totalling **14,913,030 detection messages over
~8,657 min**. The bag scanned here is 124,375 messages / 74.0 min — **0.83% of
the archive's detection messages**. Temporal window: M3-bearing bags start in
2026-06 (29 in June, 9 in July, 10 in August); the 52 April and 29 May bags
predate the M3 (93 of the 152 carry a DeltaT topic instead), and only 17 of the
48 M3 bags carry `/bizzy/sensors/m3/sonar_info` (the pre-SonarInfo-chain
majority does not).

What that scope does and does not license: the **field-presence and rawness**
claims are properties of the driver's encode path — source-verified below, and
identical for every bag `kongsberg_em_bridge` wrote — so they generalize across
the archive. The **bag-wide distributions** (beam counts, `twtt` ranges, the
38-ping startup transient, zero empty pings) are properties of this single
74-minute shoal-water run and must **not** be read as archive-wide. Deep-water,
multi-sector, and pre-SonarInfo runs were not scanned.

**Bags scanned in full** (read-only; `rosbag2_py` sequential read of every
message on `/bizzy/sensors/m3/detections`; "Duration" is the span of the
detections topic itself, not the bag):

| Bag | Msgs | Detections span | Empty `twtt[]` | Beams/ping | `twtt` range | `ping_info.sound_speed` |
|---|---|---|---|---|---|---|
| **Data-of-record**: `~/data/logs/gabby/logs/bizzyboat_sonar/2026-08-05T19-18-17+00-00` (Lewes, 2026-08-05) | 124,375 | 74.03 min (bag: 74.04) | 0 | 196–254 | 0.361–6.650 ms | non-zero and non-NaN in all 124,375; 1469.0 for the first 38 pings, then 1520.1–1529.6 |
| *Not* data-of-record: `~/data/logs/gabby/logs/bizzy_m3/bag_2026-06-09T14.51.50_m3_detections` (2026-06-09, site unverified; **temp engineering capture** — operator) | 55,100 | 80.1 min (bag: 97.9 — the M3 stream stops ~17.9 min before the bag ends) | **306** (0.56%) | 10–225 (non-empty) | 0.524–75.46 ms | nonzero in all; 1497.1–1499.0 |

**Q1 — fields populated in real recordings: YES, with two qualifications.** In
the data-of-record bag, `ping_info.sound_speed` is **non-zero and non-NaN in all
124,375 messages** (the plan's actual test — NaN passes a bare `!= 0`), and
`rx_angles[]`/`two_way_travel_times[]` are populated in every message. The
engineering capture agrees except for 306 empty-array messages.
Qualifications:

1. **Not "every message fully populated"** — 306 June-bag messages carry an empty
   `two_way_travel_times[]` (and hence no beams at all), and non-empty beam counts
   run as low as 10. A consumer must handle empty and sparse pings. Scope note
   (operator correction): the June bag is a temp engineering capture; the
   **data-of-record bag (Lewes, `bizzyboat_sonar/`) has zero empty messages** —
   so empty pings are a defensive-consumption requirement demonstrated under
   engineering conditions, not an observed property of survey records.
2. **"Detection flags all 0" is not evidence of data quality.** The driver's
   `skip_invalid_beams` parameter defaults **true**
   (`kongsberg_em_bridge/node.py:186,227,555`) and the platform launch does not
   override it, so beams the sonar flagged invalid (`det_info` bit 7,
   `em_datagrams.py`) are dropped *before* the message is built — every surviving
   beam is `DETECT_OK` by construction. The real consequence: **the ROS stream
   contains no rejected-beam population**, so an inversion cannot re-adjudicate
   the sonar's own bottom detection from bags alone. The empty and 10-beam
   messages are what that filtering looks like on a bad ping.

**Observable set (material, and previously unstated): (twtt, rx_angle) only.**
`tx_angles[]` is **0.0 in every beam of every message in both bags** — not a
"single-sector" artifact. `node.py:574` sets `tx_angles` from the transmit
sector's tilt (`math.radians(sector['tilt_deg'])`), so all-zero means the M3 runs
at **zero transmit tilt**; there is no along-track launch-angle observable to
invert. The zeros are not the driver's empty-sector fallback
(`node.py:557-558`): `ping_info.frequency` is **500000.0 Hz in all 124,375
messages** and is taken from `sectors[0]['centre_frequency']`, defaulting to 0.0
when the sector list is empty (`node.py:544-545`) — a non-zero frequency
therefore proves a real sector was decoded and its tilt really is zero.

`tx_delays[]` is the same story and must not be counted as a second observable:
it is filled from the same sector object as the tilt (`sector['tx_delay']`,
`node.py:564`; wire-sourced at `em_datagrams.py:111,117`) and is likewise
**identically 0.0 in every beam of the data-of-record bag** — a single transmit
sector has no inter-sector delay. So per beam the bags give **two-way travel
time, across-track receive angle and reflectivity**; per ping, the applied sound
speed and centre frequency. An inversion must be formulated on that set.

Angle convention (contract an inversion must honour): `node.py:566-575` maps
Kongsberg's beam pointing angle (+ve to **port**) to the
`marine_acoustic_msgs/SonarDetections` convention by negation, so `rx_angles` is
**+ve to starboard**; transmit tilt (+ve forward) carries through unnegated. The
driver deliberately applies no mount rotation — physical orientation (a normally
mounted downward M3 is roll = π) lives in the URDF `base_link → bizzy/m3`
transform, and the recorded `/tf_static` in the data-of-record bag carries it.

Angular extent: max |`rx_angles`| is 1.0308 rad (59.06°) bag-wide in the Lewes bag
and 1.0362 rad (59.37°) in the June capture. Those are single absolute maxima,
so they bound the half-swath rather than establish port/starboard symmetry: the
observed fan is at most ~118–119° wide and no narrower than the larger side
doubled only if symmetric. Either way this is the **surviving valid-beam**
extent after `skip_invalid_beams`, not a declared fan width — the driver
declares no fan width, and the M3's "120°" figure is a vendor spec not read from
any source in this workspace.

**Q2 — raw, not ray-traced: CONFIRMED from driver source.**
`kongsberg_em_bridge` is a wire-format translator of the Kongsberg Raw Range and
Angle 78 (`N`) datagram: `em_datagrams.py:128` unpacks `twtt` straight from the
wire and `node.py:563` appends it unmodified; `ping_info.sound_speed` is the
datagram's own surface-sound-speed field (`ssp_raw * 0.1`), i.e. the
sonar-applied array-face sound speed recorded **per ping** — exactly what an
inversion needs to undo the sonar's own beam steering. The verdict rests on this
source read: the plan's proposed empirical discriminator (min(`twtt`)·c/2 giving
a plausible nadir depth) is **not** a discriminator, since a pre-ray-traced range
divided the same way also yields a plausible depth; it is therefore reported as
context, not evidence. For scale, bag-wide `twtt` extremes correspond to slant
ranges of 0.28–5.08 m (Lewes, shoal water) and 0.39–56.5 m (June bag).

**Q3 — ROS stream vs full M3 output.**
- **What the bridge decodes:** `parse_datagram` (`em_datagrams.py:191-200`)
  decodes only N78 (`0x4E`) and XYZ88 (`0x58`); every other datagram type —
  including `DG_SURFACE_SOUND_SPEED` (`0x47`), attitude (`0x41`), clock (`0x43`)
  and position (`0x50`) — returns `{'type': dg}`, recognized but **not decoded**,
  so it never reaches ROS. The dropped `0x47` is epic-relevant: it is the sonar's
  own surface-sound-speed stream, of which only the per-ping N78 copy survives
  into `ping_info.sound_speed`.
- **Raw `.all` capture is a debugging tool, not part of the recording plan**
  (operator correction — sparse coverage is by design). The platform launch
  (`unh_echoboats_project11/bizzyboat_project11/launch/perception_launch.py:107-146`)
  sets `save_all_dir` = `<sonar_log_dir>/m3_all` with a 200 MB rollover but does
  **not** set `record_on_start`, which defaults `False` (`node.py:205`), so
  raw recording must be armed at runtime via `~/set_recording`. On disk,
  `bizzyboat_sonar/m3_all/` holds 43 files, all dated **2026-06-16/17 only** —
  a debugging session, consistent with the design intent. The earlier claim that
  "nothing is unrecoverable" remains **withdrawn** as a factual matter — the ROS
  `SonarDetections` stream is the *intended and only* record for normal survey
  days, and datagrams the bridge drops (incl. `0x47`) are not retained — but this
  is the recording design working as intended, not a gap.
- **Recording topology** (from the platform config, not inferred from bags):
  `bizzyboat_project11/config/bizzyboat.yaml:713-733` records
  `/bizzy/sensors/m3/detections` + `/bizzy/sensors/m3/sonar_info` in the
  **`sonar_logger`** bag, not the main deployment recorder — confirmed against
  the three main `bizzyboat/2026-08-05T*` bags of the same day, whose 60–63
  recorded topics include no `m3` topic at all. The `sonar_logger`'s destination
  is **not** the yaml's `uri` (`bizzyboat.yaml:765`, `/home/field/project11/data/…`
  — dead config, no such path on disk): `perception_launch.py:275-278` overrides
  `storage.uri` with `<sonar_log_directory>/<sonar_log_subdirectory>`, i.e.
  `$P11_SONAR_LOG_DIR` (default `/home/field/data/logs/bizzyboat_sonar`,
  `:34-41`) plus a UTC-timestamp subdir — which is what the on-disk bag names
  look like, and what confirms operator guidance that **`bizzyboat_sonar/` is
  the sonar data-of-record directory**. That directory is the recorder's target,
  not a curated partition: it also holds aborted recordings, a
  `2026-06-04T…_kongsberg_em_test` bag and the `m3_all/` raw-`.all` sibling.
  Other M3 directories (`bizzy_m3/`) are temp engineering captures.
  Favourable corollary for the inversion: the sonar bag is **self-sufficient**
  — alongside the detections it carries `/bizzy/odom` (44,360), `/tf` (141,759),
  `/tf_static` and the SV feed `/bizzy/sensors/sound_speed/sound_speed`
  (111,024), so georeferencing needs no cross-bag join with the main
  deployment bag at import time. Companion
  `/bizzy/sensors/m3/sonar_info` (`marine_interfaces/SonarInfo`) is present in the
  Aug 2026 sonar bag (post SonarInfo chain) and absent from the June bag, as
  expected.

**Q4 — what the importer does with the fields (source-verified, per file).**
The observables arrive intact and are consumed, but they are **not retained**
past sounding construction:
- `include/cube_bathymetry/sounding.h:43,52-54,72` — the only site reading all
  four: `two_way_travel_times`, `ping_info.sound_speed`, `tx_angles` (guarded,
  defaults 0), `rx_angles` (plus `intensities`).
- `src/error_model.cpp` — reads `two_way_travel_times` (`:277,:344,:383`),
  `rx_angles` (`:206`), `ping_info.sound_speed` (`:277,:344`) and
  `ping_info.rx_beamwidths` (`:237`). It does **not** read `tx_angles`.
- `src/detections_projector.cpp:134-135` — reads only `ping_info.sound_speed`
  (into `platform.mean_speed`/`surf_sspeed`). The four-field list at
  `include/cube_bathymetry/detections_projector.h:107` is a doc comment about the
  input message, not a read site.
- **`Sounding` stores only derived quantities** — `slant_range`, `beam_angle`
  (= rx angle), `intensity`, `sonar_relative_position`. Raw `twtt`, `tx_angle`
  and the applied `sound_speed` are consumed and discarded. An inversion engine
  must therefore read `SonarDetections` at import time (or `Sounding` must be
  extended); it cannot separate travel time from applied sound speed after the
  fact.

**Secondary observations**
- **The 1469.0 m/s value is a sonar startup transient, not an SV-feed problem**
  (correcting the original entry, which had this backwards). Bag-wide, the Lewes
  bag's `ping_info.sound_speed` is 1469.0 for exactly the **first 38 pings** and
  then 1520.1–1529.6 for the remaining 74 minutes. The boat's own SV feed in the
  same bag (`/bizzy/sensors/sound_speed/sound_speed`,
  `marine_interfaces/SoundSpeed`, 111,024 samples) reads **1528.102 m/s at
  t+1.3 s** and stays in 1520.13–1529.62 throughout, with only 10 anomalous
  samples (one NaN at t=0 and a 0.3 s burst of nine 0.0 values at t≈2340 s). So
  the AML feed was healthy from the start; the M3 simply applied an internal
  default for its first 38 pings before the surface SV value took effect. There
  is no AML-continuity question here — the earlier "check the AML feed for that
  day" note targeted the wrong subsystem and is withdrawn. The inversion-relevant
  consequence is the opposite one: **the first tens of pings of a run carry an
  applied sound speed that does not match the measured surface SV**, and must be
  either corrected or discarded.
- **Clock skew and bag variant (echoboats#342 retrofit): the data-of-record bag
  is clean — measured, not assumed.** This matters beyond the payload: an
  inversion georeferences beams through `/bizzy/odom` and TF, so a one-second
  `header.stamp` skew is ~1.5 m of along-track position error at 3 kn — enough
  to swamp the effect being inverted for. The Lewes bag (2026-08-05) sits
  **inside** the skew window `retrofit_m3_bag.py:19-22` describes (integer-second
  jumps from 2026-06-24 onward), so it was probed directly with
  `retrofit_m3_bag.py --report-only` — the script's non-destructive path: pass 1
  measures and prints, and it returns before any writer is opened
  (`retrofit_m3_bag.py:506-517`). Result: **integer-second offset histogram
  `+0 s: 124375`** — every ping already time-aligned, zero timing corrections.
  The accompanying "would correct: 1 tf_static" is a frame-presence count, not a
  value mismatch (`measure_pass`, `:301-304`); reading that transform out of the
  bag shows it already at the corrected measured offset `(-0.29, 0.0, -0.28)`,
  so the geometry fix would be a no-op too. No `.orig` backup exists beside
  either bag (the retrofit's in-place marker), so both are original recordings
  that never needed it. The 2026-06-09 engineering capture predates the skew
  onset and so is uninformative about it — the earlier round's `.orig` check on
  that bag alone did not settle the question. Independent of all this, the
  retrofit rewrites `/tf_static` offsets and `header.stamp`, never `twtt`/angle
  payloads.
- **June bag site label removed.** The bag was previously called
  "Massabesic-era"; that is unverified — its bag-wide max `twtt` of 75.46 ms
  implies a ~56 m slant range, deeper than Lake Massabesic. Date (2026-06-09) is
  all that is asserted.
- Plan's conditional `.agents/README.md` note: **not applicable** — this repo has
  no `.agents/` directory, so no note was added (correctly deferred).

**Limits on the verdict**
- **Evidence scope**: one data-of-record bag scanned in full — 74 min of shoal
  water on 2026-08-05, 0.83% of the 14.9 M detection messages in
  `bizzyboat_sonar/`, and the M3-bearing part of that archive only begins
  2026-06. Field presence/rawness generalize (they are encode-path properties);
  the distributions quoted here do not. No deep-water or pre-SonarInfo run was
  scanned.
- Observable set is (twtt, rx_angle) + per-ping applied `sound_speed`; **no
  along-track launch angle** (zero transmit tilt).
- Sonar-rejected beams are absent from the ROS stream (`skip_invalid_beams`
  default true), so bags cannot support re-detection work.
- Non-N78 datagrams (incl. `0x47` surface sound speed) are dropped by the bridge;
  raw `.all` capture is a debug facility (sparse by design — 2026-06-16/17 only),
  so for survey days the `SonarDetections` stream is the record.
- Startup pings carry a stale applied sound speed (38 pings in the Lewes bag).
- **Timing is not a limit for this bag, but is a per-bag precondition.** The
  data-of-record bag measures zero integer-second skew, so its beams georeference
  as recorded. Other in-window bags (2026-06-24 onward) are *not* covered by that
  measurement: an inversion consuming them must run
  `retrofit_m3_bag.py --report-only` per bag first, since ~1 s of skew is ~1.5 m
  of along-track error at 3 kn.

### Findings
- [x] Q1 — in the data-of-record bag, `two_way_travel_times[]` / `rx_angles[]` populated in all 124,375 messages and `ping_info.sound_speed` non-zero and non-NaN in all of them; the non-of-record engineering capture agrees except for 306 empty messages — full scans, scope stated under "Evidence scope"
- [x] Q1a — invalid beams never reach the ROS stream (`skip_invalid_beams` default true) — `kongsberg_em_bridge/node.py:186,227,555`
- [x] Q1b — observable set is (twtt, rx_angle) only; `tx_angles` and `tx_delays` are both identically zero (one sector, zero tilt — not the empty-sector fallback: `ping_info.frequency` = 500 kHz) — `kongsberg_em_bridge/node.py:544-545,564,574`
- [x] Q1c — angle convention: `rx_angles` +ve to starboard (Kongsberg pointing angle negated); mount orientation lives in the URDF/`tf_static`, which the sonar bag records — `kongsberg_em_bridge/node.py:566-575`
- [x] Q2 — recorded travel times are raw N78 wire values, not ray-traced — `kongsberg_em_bridge/em_datagrams.py:128`, `node.py:563`
- [x] Q3a — bridge decodes N78 + XYZ88 only; `0x47` surface-sound-speed and other datagrams dropped — `em_datagrams.py:191-200`
- [x] Q3b — raw `.all` capture is opt-in (`record_on_start` default false) and on disk covers 2026-06-16/17 only; neither scanned bag's date is covered — `perception_launch.py:107-146`, `node.py:205`
- [x] Q3c — M3 topics ride in the `sonar_logger` bag (self-sufficient: also carries odom, TF and the SV feed), not the main deployment bag — `bizzyboat_project11/config/bizzyboat.yaml:713-733`; destination set by `perception_launch.py:275-278` (`:34-41`), not the yaml `uri`
- [x] Q4 — observables reach the importer but `Sounding` retains only derived values; inversion must read `SonarDetections` at import time — `include/cube_bathymetry/sounding.h:43`, `src/error_model.cpp:277`, `src/detections_projector.cpp:134`
- [x] Timing — data-of-record bag probed for the #338 integer-second skew despite sitting in the post-2026-06-24 window: histogram `+0 s: 124375` (no skew), `/tf_static` already at the corrected `(-0.29, 0.0, -0.28)`, no `.orig` — read-only `retrofit_m3_bag.py --report-only`; per-bag precondition recorded in Limits
- [x] Secondary — 1469.0 m/s is a 38-ping sonar startup transient; the in-bag SV feed reads 1528.102 from t+1.3 s (AML healthy) — Lewes bag full scan
- [x] Operator corrections (Roland, 2026-08-18) — `bizzyboat_sonar/` = sonar data-of-record (launch-verified: `perception_launch.py:275-278` + `:34-41`, **not** the overridden `bizzyboat.yaml:765` uri); `bizzy_m3/` etc. = temp engineering captures; `.all` files = debug-purpose, sparse by design (operator-stated intent; the launch comment reads differently — see Q3) — folded into Q1/Q3/Limits above
- [x] Owed (host): post/refresh the findings comment on rolker/cube_bathymetry#121 with these corrected numbers, and signal the outcome to rolker/unh_marine_autonomy#300 so its "bag contents may not be invertible" epic-killer risk line is closed out with the stated limits. Done by host 2026-08-17: comment rewritten in place (https://github.com/rolker/cube_bathymetry/issues/121#issuecomment-5323171911) and epic signal posted (https://github.com/rolker/unh_marine_autonomy/issues/300#issuecomment-5323318922).

No follow-up recording-change issue is filed: the observables required by the
epic are present. The limits above are recording-*configuration* facts (raw
`.all` opt-in, `skip_invalid_beams`) that the epic can act on if it later needs
rejected beams or the dropped datagrams — noted, not scoped here, per the plan's
no-unprompted-scope rule.

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
- [x] (must-fix) Q1 "every message ... fully populated" falsified bag-wide — 306 empty `two_way_travel_times[]` in the June bag, beams 10-225; figures hold only for the undisclosed leading-50 sample of 55,100/124,375 msgs — `.agent/work-plans/issue-121/progress.md:149`
- [x] (must-fix) Secondary SS observation inverted — the 1469.0 value is a 38-ping startup transient (rest of 74 min at 1527.4-1529.0), not "live per-ping tracking"; in-bag `/bizzy/sensors/sound_speed/sound_speed` reads 1528.10 from t=0, so the AML-continuity suggestion targets the wrong subsystem — `.agent/work-plans/issue-121/progress.md:158`
- [x] (must-fix) "detection flags all 0 (valid)" is circular — `skip_invalid_beams` defaults true (`node.py:186,227,555`), so flagged beams never reach the message; the real consequence (invalid beams dropped from the ROS stream) is unstated — `.agent/work-plans/issue-121/progress.md:149`
- [x] (must-fix) "nothing is unrecoverable" unsupported — `m3_all/` covers only 2026-06-16/17; neither sampled bag's date has raw `.all` coverage — `.agent/work-plans/issue-121/progress.md:153`
- [x] (must-fix) Importer source claim overstated — `detections_projector.cpp` reads only `ping_info.sound_speed`; `error_model.cpp` does not read `tx_angles`; only `sounding.h:43` reads all four; and `Sounding` does not retain raw twtt/tx_angle/sound_speed — `.agent/work-plans/issue-121/progress.md:155`, `.agent/work-plans/issue-121/plan.md:37` (also `plan.md`, commit `2db77bf`)
- [x] (must-fix) ADR-0013 non-conformance in `## Implementation` — missing `**Branch**: ... at <sha>` correlation field and no `### Findings` checkbox list, so `progress_read.py` yields `correlation: null` / `findings: []` and the spike's entire deliverable is machine-invisible — `.agent/work-plans/issue-121/progress.md:138-157`
- [x] (must-fix) Consequence owed cross-repo — declaring rolker/unh_marine_autonomy#300 unblocked leaves its "possible epic-killer" Risks line standing with no comment/timeline signal reaching the epic (deferred: this sub-agent has no GitHub write access — recorded as an owed-host action in the corrected `## Implementation` entry's Findings list)
- [x] (must-fix) `tx_angles` explanation wrong — `node.py:574` sets them from sector tilt (zero tilt, not "single-sector"); the material unstated fact is that the observable set is (twtt, rx_angle) only, with no along-track launch angle — `.agent/work-plans/issue-121/progress.md:149`
- [x] (suggestion) The "empirical discriminator" carries no information — a pre-ray-traced range also yields a plausible nadir depth; the raw-vs-ray-traced verdict rests on the source read alone — `.agent/work-plans/issue-121/progress.md:151`
- [x] (suggestion) 1.65 m internally inconsistent — 2.162 ms at the c=1469.0 then in effect gives 1.588 m (12.49 m checks exactly) — `.agent/work-plans/issue-121/progress.md:151`
- [x] (suggestion) "±1.031 rad (full ±59° fan)" mislabels the quantity — June bag reaches 1.0362 rad, nominal fan is 120°, and this is the surviving valid-beam extent after `skip_invalid_beams` — `.agent/work-plans/issue-121/progress.md:149`
- [x] (suggestion) "(Massabesic-era)" label unverified — bag-wide max twtt 75.46 ms implies ~56 m, exceeding Massabesic depths — `.agent/work-plans/issue-121/progress.md:149`
- [x] (suggestion) Plan step 4's "decodes vs drops" half unreported — `parse_datagram` (`em_datagrams.py:191-200`) decodes N78 + XYZ88 only; the dropped SURFACE_SOUND_SPEED `0x47` datagram is epic-relevant — `.agent/work-plans/issue-121/progress.md:153`
- [x] (suggestion) Plan step 4's platform launch/config source never consulted/reported; topology answered from bag contents instead — say which source was used — `.agent/work-plans/issue-121/progress.md:153`
- [x] (suggestion) Satisfied checkboxes left open — Issue Review action and the three Plan Review must-fixes (addressed in `c782621`) still read as open work to the parser — `.agent/work-plans/issue-121/progress.md:91,128-135`
- [x] (suggestion) `**By**` / `**Model**` split diverges from ADR-0013's `**By**: <agent> (<model>)` used by the other three entries — `.agent/work-plans/issue-121/progress.md:141-142`
- [x] (suggestion) Record the one-line disposition of the plan's conditional `.agents/README.md` note (correctly deferred — repo has no `.agents/` dir) — `.agent/work-plans/issue-121/progress.md:161`
- [x] (suggestion) Bag variant left "possibly retrofitted"; absence of a `.orig` backup beside the June bag settles the plan's step-1 requirement — `.agent/work-plans/issue-121/progress.md:159`

## Implementation
**Status**: complete
**When**: 2026-08-17 23:52 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-121 at `57d15a8`
**Addressed**: `## Local Review (Pre-Push)` (2026-08-17 23:30 -04:00, branch head `eb5aaf7`, verdict changes-requested, round 1) — 8 must-fix + 10 suggestions
**Commits**: `2db77bf` (plan.md importer-source correction), `57d15a8` (spike `## Implementation` entry rewritten from full-bag scans + ADR-0013 conformance), plus this progress commit (checkbox reconciliation)

**Method**: every corrected number comes from a **full-bag** `rosbag2_py` scan
(all messages, not a head sample) of the two bags named in the corrected entry,
opened read-only; source claims were re-read against the current checkouts of
`cube_bathymetry`, `marine_tools/kongsberg_em_bridge` and
`unh_echoboats_project11/bizzyboat_project11`. Bag scan results (independent of
the review's): Lewes `2026-08-05T19-18-17+00-00` — 124,375 msgs, 0 empty, beams
196–254, `twtt` 0.361–6.650 ms, `sound_speed` 1469.0 for exactly 38 pings then
1520.1–1529.6, max |rx| 1.0308 rad, all `tx_angles` 0; June
`bag_2026-06-09T14.51.50_m3_detections` — 55,100 msgs, 306 empty, non-empty
beams 10–225, `twtt` 0.524–75.46 ms, `sound_speed` 1497.1–1499.0, max |rx|
1.0362 rad, all `tx_angles` 0; `sound_speed` nonzero in all 179,475 messages.
The Lewes bag's own SV feed (111,024 `marine_interfaces/SoundSpeed` samples)
reads 1528.102 m/s at t+1.3 s and 1520.13–1529.62 thereafter, with 10 anomalous
samples (one NaN at t=0, nine 0.0 in a 0.3 s burst at t≈2340 s) — confirming the
1469.0 is sonar-side, not an AML-feed gap.

### Actions
- [x] Q1 "every message fully populated" corrected — bag-wide figures, 306 empty June-bag messages and 10–225 non-empty beam counts now stated — `progress.md` `## Implementation`
- [x] Secondary sound-speed observation inverted → rewritten: 38-ping sonar startup transient, in-bag SV feed healthy from t+1.3 s; the "check AML continuity" note withdrawn and replaced with the real consequence (startup pings carry a stale applied sound speed)
- [x] "Flags all 0" circularity stated — `skip_invalid_beams` defaults true (`node.py:186,227,555`), consequence (no rejected-beam population in the ROS stream) now explicit
- [x] "Nothing is unrecoverable" withdrawn — `.all` capture is opt-in (`record_on_start` default false, `node.py:205`) and the 43 files on disk cover 2026-06-16/17 only
- [x] Importer-source claims corrected per file, in both the entry and `plan.md` — `sounding.h:43` is the only all-four read site; `error_model.cpp` does not read `tx_angles`; `detections_projector.cpp:134-135` reads only `ping_info.sound_speed`; `Sounding` retains no raw twtt/tx_angle/sound_speed
- [x] ADR-0013 conformance — `**Status**`/`**When**`/`**By**: <agent> (<model>)` header, `**Branch**: feature/issue-121 at eb5aaf7` correlation field and a `### Findings` checkbox list added to the spike entry; verified with `progress_read.py --type Implementation`
- [x] `tx_angles` explanation corrected — set from sector tilt (`node.py:574`), all-zero = zero transmit tilt; the observable set **(twtt, rx_angle) only, no along-track launch angle** is now stated as a first-class limit
- [x] Cross-repo consequence to rolker/unh_marine_autonomy#300 (deferred: no GitHub write access in this sub-agent — recorded as the one open owed-host item in the spike entry's Findings; the host posts the corrected findings comment on rolker/cube_bathymetry#121 and the epic signal)
- [x] "Empirical discriminator" demoted to context — the raw-vs-ray-traced verdict now rests explicitly on the source read
- [x] The inconsistent 1.65 m figure removed; slant-range extremes recomputed bag-wide (0.28–5.08 m Lewes, 0.39–56.5 m June)
- [x] "±1.031 rad (full ±59° fan)" relabelled — bag-wide maxima 1.0308/1.0362 rad, described as the surviving valid-beam extent against a nominal 120° swath
- [x] "(Massabesic-era)" label removed — 56 m implied range is deeper than Lake Massabesic; only the date is asserted
- [x] Datagram decode-vs-drop reported — `parse_datagram` (`em_datagrams.py:191-200`) decodes N78 + XYZ88 only; dropped `0x47` surface-sound-speed flagged as epic-relevant
- [x] Platform launch/config now cited as the topology source — `perception_launch.py:107-146` and `bizzyboat.yaml:713-733` (`sonar_logger`), rather than inferring from bag contents
- [x] Satisfied checkboxes reconciled — Issue Review action, Plan Authored open question, and all Plan Review items marked resolved with their commit
- [x] `**By**`/`**Model**` split folded into ADR-0013's `**By**: <agent> (<model>)`
- [x] `.agents/README.md` disposition recorded — repo has no `.agents/` directory, so the conditional note was correctly not added
- [x] Bag-variant question settled — no `.orig` backup beside the June bag, so it is an original (non-retrofitted) recording

### Notes
- No source or test files changed; the branch remains docs-only (`plan.md` +
  `progress.md`). Bags were read-only throughout — nothing under `~/data` was
  modified.
- **Owed by the host**: post/refresh the findings comment on
  rolker/cube_bathymetry#121 with the corrected numbers and signal the epic
  consequence to rolker/unh_marine_autonomy#300 (its "bag contents may not be
  invertible" risk line). No `gh` writes were performed by this pass.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-18 00:20 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-121 at `6043cee`
**Mode**: pre-push
**Depth**: Deep (reason: 617 changed lines; also project-repo trigger `.agent/work-plans/issue-*/plan.md`)
**Must-fix**: 5 | **Suggestions**: 20
**Round**: 2 | **Ship**: continue — must-fix fell 8→5, but two of them (evidence scope, clock skew) change what the epic owner should conclude from an epic-gating verdict
**Specialists**: Static analysis (no `.md` profile — not checked); Claude Adversarial x2 (Lens A + Lens B, Deep); Copilot off (default); Local qwen3.5:35b (5 findings — 1 corroborating the clock-skew must-fix, 4 discarded as speculative future-proofing of a docs-only spike)

**Theme**: round 2 verified every re-checked source citation as exact and reproduced the arithmetic and bag-wide numbers; the round-1 corrections hold. The remaining defect is that the operator-correction commit `6043cee` shrank the evidence base without weakening the claims.

### Findings
- [ ] (must-fix) Verdict scope not updated after the June bag was demoted — still "every sampled deployment bag" / "all 179,475 messages across both bags" when only one 74-min shoal-water bag of 156 in `bizzyboat_sonar/` is data-of-record; no representativeness or temporal-window limit (Apr bags carry DeltaT not M3; `2026-06-30` has no `sonar_info`) — `.agent/work-plans/issue-121/progress.md:158-172,305-313`
- [ ] (must-fix) "Config-verified" citation is dead config — `bizzyboat.yaml:765` uri is overridden at launch by `perception_launch.py:269-278` from `sonar_log_directory` (`:36-41`) + UTC subdir; the yaml path does not exist on disk. Conclusion right, evidence wrong — `.agent/work-plans/issue-121/progress.md:151,249-251,325`, `.agent/work-plans/issue-121/plan.md:92`
- [ ] (must-fix) `tx_delays` listed as a bag-supplied observable but is identically 0.0 (same sector object as `tilt_deg`, `node.py:564`) — internally inconsistent with the entry's own first-class `tx_angles` limit — `.agent/work-plans/issue-121/progress.md:202-204`
- [ ] (must-fix) Clock-skew limit missing and the `.orig` check was run on the June-09 bag, which predates the Jun-24-2026 skew onset (`retrofit_m3_bag.py:19-22`); the data-of-record Lewes bag sits inside the window and was never checked. Skew is dismissed as payload-irrelevant, but an inversion georeferences beams via odom/TF (1 s at 3 kn ≈ 1.5 m). `--report-only` is a non-destructive probe — `.agent/work-plans/issue-121/progress.md:293-297,305-313`
- [ ] (must-fix) ".all = debug by design" recorded as if source-corroborated; `perception_launch.py:114-117` describes the archive as "genuine .all files (Caris/Qimera/MB-System) alongside the sonar bags", configured every deployment — record as operator-stated intent and note the divergence — `.agent/work-plans/issue-121/progress.md:232-243`, `.agent/work-plans/issue-121/plan.md:88-92`
- [ ] (suggestion) Report `ping_info.frequency` (= 500000.0 Hz) — the only discriminator ruling out the empty-sector fallback as the cause of all-zero `tx_angles` — `.agent/work-plans/issue-121/progress.md:197-204`
- [ ] (suggestion) "nonzero" reported where the plan required "non-zero/non-NaN"; NaN passes `!= 0` and the entry found one in the SV feed — `.agent/work-plans/issue-121/progress.md:174-176`
- [ ] (suggestion) "Duration" column is the detections-topic span, unlabeled — June bag runs 97.9 min but the M3 stream stops 17.9 min before the bag ends — `.agent/work-plans/issue-121/progress.md:169-172`
- [ ] (suggestion) `Sounding` "stores only" enumeration incomplete — also `depth`, `vertical_error`, `horizontal_error`, `predicted_depth_at_touchdown` (`sounding.h:78-124`) — `.agent/work-plans/issue-121/progress.md:270-271`
- [ ] (suggestion) `tx_angles` read-site is `sounding.h:49-50`, not `:52-54` — `.agent/work-plans/issue-121/progress.md:260-262`, `.agent/work-plans/issue-121/plan.md:38`
- [ ] (suggestion) "75.46 ms implies ~56 m, deeper than Massabesic" conflates slant range with depth (up to 59.4° off nadir ⇒ as little as ~29 m water) — `.agent/work-plans/issue-121/progress.md:298-301`
- [ ] (suggestion) "~118–119° observed swath" assumes port/starboard symmetry from one absolute maximum; "nominal 120°" is uncited — `.agent/work-plans/issue-121/progress.md:206-209`
- [ ] (suggestion) "main `bizzyboat/` bags carry no M3 topics" is unsourced — it holds for `bizzyboat/2026-08-05T19-18-17+00-00`; name the bag — `.agent/work-plans/issue-121/progress.md:251-252`
- [ ] (suggestion) Q3c omits the favourable corollary — the sonar bag also carries `/bizzy/odom`, `/tf`, `/tf_static` and the SV feed, so no cross-bag join is needed at import — `.agent/work-plans/issue-121/progress.md:244-255`
- [ ] (suggestion) State the angle-convention contract — `node.py:565-575` negates the Kongsberg pointing angle (+port → ROS +starboard); mount orientation lives in the URDF — `.agent/work-plans/issue-121/progress.md:197-204`
- [ ] (suggestion) Sharpen Q1a/Q3a — `nrx`/`nvalid` are decoded (`em_datagrams.py:152-153`) but never published, so reject counts are unrecoverable; and the dropped-`0x47` claim has empirical backing (1816 × `0x4E` and 1816 × `0x47` in `m3_20260616_155246.all`) — `.agent/work-plans/issue-121/progress.md:186-195,224-231`
- [ ] (suggestion) Add a typed entry for the `2900bf1` + `6043cee` pass at `6043cee` — the spike entry's correlation key still reads `eb5aaf7`, which does not contain the operator-corrected text, and the second `## Implementation` entry's Commits list is two commits stale — `.agent/work-plans/issue-121/progress.md:138-155,367-374`
- [ ] (suggestion) Hand-typed timestamps contradict git (rewrite claimed 23:45 vs `57d15a8` at 23:40:26; entry `**When**: 23:52` inside 23:40–23:41 commits) and the Local Review is cited "at `707392c`" where its own header gives `eb5aaf7` — `.agent/work-plans/issue-121/progress.md:144-148,340,369-374`
- [ ] (suggestion) "Done by host 2026-08-17" is stale — both comments were refreshed 2026-08-18 with the operator corrections — `.agent/work-plans/issue-121/progress.md:326`
- [ ] (suggestion) Two entries share the `## Implementation` heading — a last-match consumer gets the remediation log, not the deliverable — `.agent/work-plans/issue-121/progress.md:138,367`
- [ ] (suggestion) "`bizzyboat_sonar/` is the data-of-record directory" stated without exception, but the tree also holds `2026-06-04T…_kongsberg_em_test` and `m3_all/` — heuristic, not partition — `.agent/work-plans/issue-121/progress.md:249-252`
- [ ] (suggestion) Repo-qualify the ADR-0009 citation (`unh_marine_autonomy ADR-0009`) — bare it collides with workspace ADR-0009 — `.agent/work-plans/issue-121/plan.md:162`
- [ ] (suggestion) Plan drift (cosmetic): step 1 still names the un-scanned `bizzyboat/2026-08-03…` bag; the demoted empirical discriminator step carries no inline annotation; Consequences row 2 not updated after the epic signal was posted — `.agent/work-plans/issue-121/plan.md:81-88,105-118`
- [ ] (suggestion) Drop the unresolvable "referenced in project memory" phrase (dates are inline) and reconcile "no PR needed" if a PR is opened — `.agent/work-plans/issue-121/progress.md:86`, `.agent/work-plans/issue-121/plan.md:196-198`
- [ ] (suggestion) Knowledge-capture candidates, **proposal only — operator decides**: `bizzyboat_sonar/` vs `bizzy_m3/`/`m3_all/` data-of-record distinction; `skip_invalid_beams` default true ⇒ no rejected-beam population in bags; M3's ~38-ping startup sound-speed default. Propose in the PR body; do not edit an instruction file on this branch.
- [ ] (owed, host) Must-fix 1-5 also need pushing back into the two published comments (rolker/cube_bathymetry#121 findings comment and the rolker/unh_marine_autonomy#300 epic signal) — the epic currently reads "two deployment recordings".
