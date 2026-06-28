---
issue: 81
---

# Issue #81 — Apply deferred M3 backscatter angle-correction in shared CUBE estimator

## Issue Review
**Status**: complete
**When**: 2026-06-28 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #81
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #81 proposes lifting the Phase B no-op at `node.cpp:298-322` (the
`TODO(#54-B / #15)` block) by applying a first-cut Lambert beam-angle-vs-nadir
correction per beam before combining into the settled intensity estimate. The
gate — verifying `rx_angles[i]` sign/zero convention — is resolvable from
existing sources and the full GeoCoder correction is explicitly deferred.

### Scope Assessment

**Well-scoped?** Yes — two sequential sub-tasks with a hard gate between them:
(1) verify sign convention, then (2) apply first-cut Lambert correction in
`node.cpp:298-322`. The full GeoCoder correction (#15) is explicitly a
follow-up. Both sub-tasks affect exactly one site in the shared estimator.

**Right repo?** Yes — `cube_bathymetry` is the correct placement per the
accepted backscatter design (ADR-0007 D9): CUBE-coupled processing belongs in
this package, and the shared `Node::extractNodeRecord()` path is already the
correct single-dispatch point.

**Dependencies:**
- #54 (per-beam `{intensity, beam_angle}` stats) — already merged; the
  necessary `Hypothesis::intensity_samples` container and `beam_angle` field on
  `Sounding` are in place.
- #78 (live tile transport) and #80 (offline store layer) — consumers, not
  blockers. Correction landing here automatically propagates to both.
- #15 (slope correction / predicted-surface producer) — needed only for the
  full GeoCoder correction (follow-up); this issue's first-cut Lambert does
  not depend on it.
- #59 (predicted-surface producer) — inert dependency; still out of scope here.

### Sign-Convention Verification (Gate Assessment)

The gate is resolvable from available sources. `SonarDetections.msg` (v2.1.0,
`/opt/ros/jazzy/share/marine_acoustic_msgs/msg/SonarDetections.msg`) explicitly
states:

```
# Sonar-reported across-track steering angle (applied to rx beam)
# + to starboard, - to port for a downlooking sonar
# reported in radians
float32[] rx_angles
```

`sounding.h:109` already documents `beam_angle` as "positive to starboard (the
detections.rx_angles convention)" — consistent with the producer. The position
computation at `sounding.h:49` (`sonar_relative_position.y = range *
sin(detections.rx_angles[i])`) confirms the convention is correctly applied.

**Sign-convention conclusion:** The producer and the stored `beam_angle` field
agree. The gate can be passed by documenting this cross-check in the commit or
as an inline comment at the correction site. No discrepancy found.

**Watch:** For the Lambert correction `corrected = raw - f(angle)`, the
incidence angle from vertical is `|beam_angle|` (always non-negative; nadir =
0). The implementation must use `std::abs(sample.beam_angle)` rather than the
raw signed value, or the correction will have an asymmetric sign error across
port/starboard beams. This is the key correctness invariant to enforce in
implementation and test.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Issue clearly phases the work (verify gate → first-cut → full GeoCoder), names the blocking issue (#15), and scopes explicitly. No hidden side effects. |
| Enforcement over documentation | Watch | The sign-convention gate is a documentation/code-review step; there is no automated check to enforce it before the correction runs. A code comment and a test covering the sign (e.g., non-zero `beam_angle` beam should produce a corrected value ≠ raw) would strengthen this. |
| Capture decisions, not just implementations | Action needed | ADR-0007 (MBES Backscatter Store) is accepted and referenced in code/plans but has no document file in `cube_bathymetry/docs/decisions/`. When Phase B ends (this issue), the ADR-0007 transition from no-op to first-cut correction should be captured — either by writing the ADR-0007 document or adding an addendum noting the Phase B end date and approach. |
| A change includes its consequences | Action needed | Replacing the no-op with an actual Lambert correction changes settled intensity values. `test_node.cpp` tests that verify intensity output (asserting `corrected == raw`) will need updating to expect corrected values. Update tests in the same PR. |
| Only what's needed | OK | Issue explicitly limits scope to first-cut Lambert (no slope); the full GeoCoder correction is deferred. The change is a single-site replacement. |
| Improve incrementally | OK | Well-phased: Phase B no-op → first-cut Lambert (here) → full GeoCoder (#15). Small, reviewable. |
| Test what breaks | Action needed | (1) The Lambert correction needs at least one test with a non-nadir beam angle to confirm `corrected ≠ raw`. (2) Nadir-beam boundary case (`beam_angle ≈ 0`): correction should equal identity. (3) Confirm that `std::abs(beam_angle)` is used and a port beam and its symmetric starboard counterpart produce the same corrected value. |
| Workspace vs. project separation | OK | All changes in `cube_bathymetry` project repo. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 (workspace — adopt ADRs) | Yes | Implementing the deferred ADR-0007 D3 correction is a design decision transition; document it. |
| ADR-0008 (ROS 2 conventions) | No | No ROS 2 interface changes. |
| ADR-0013 (progress.md vocabulary) | Yes — this entry | Standard `## Issue Review` entry. |
| Project ADR-0007 (MBES Backscatter Store) | Yes — primary | D3 (incidence correction at node-output) is exactly what this issue implements. D2/D4 (per-beam combine into mean + estimate variance) are already in place from #54. |

### Consequences

- Settled backscatter values in `NodeRecord::intensity` will differ from the
  current (uncorrected) values for any non-nadir beam — existing integration
  tests that check intensity numerics will need updating.
- Both the live tile publisher (#78) and the offline store importer (#80)
  automatically receive the corrected value via `extractNodeRecord()` — no
  per-path duplication needed (the point of landing it here).
- The `TODO(#54-B / #15)` comment block at `node.cpp:298-322` should be
  replaced or updated to reflect Phase B completion and note what the remaining
  full-GeoCoder follow-up (#15) entails.
- If project ADR-0007 is written as part of this issue, the `docs/decisions/`
  entry should be committed in the same PR.

### Actions
- [ ] Document sign-convention cross-check (`rx_angles`: `+ starboard, - port` confirmed against `SonarDetections.msg` v2.1.0) as an inline comment at the correction site in `node.cpp`.
- [ ] Implement first-cut Lambert correction using `std::abs(sample.beam_angle)` (not the signed angle) to avoid port/starboard asymmetry.
- [ ] Update `test_node.cpp`: replace any intensity assertions that assume `corrected == raw` with expected Lambert-corrected values; add a test with a non-nadir beam angle confirming `corrected ≠ raw`.
- [ ] Add symmetric port/starboard test: equal `|beam_angle|` on each side should produce equal corrected intensity.
- [ ] Write or update project ADR-0007 document in `cube_bathymetry/docs/decisions/` to record the Phase B → first-cut transition (when it happens, what correction, what's still deferred).
