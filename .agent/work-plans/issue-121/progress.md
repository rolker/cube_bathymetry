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
