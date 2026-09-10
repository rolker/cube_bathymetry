---
issue: 144
---

# Issue #144 — ErrorModel: the beamwidth fallback treats degrees as radians — 57x angular sigma on every M3 sounding

## Issue Review
**Status**: complete
**When**: 2026-09-10 09:14 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #144
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-more-detail

### Summary

Verified against the live checkout (`cube_bathymetry/src/error_model.cpp:236`,
`include/cube_bathymetry/error_model.h:210-213`) and against installed
`marine_acoustic_msgs/msg/PingInfo.msg` (`/opt/ros/jazzy`), `norbit_driver/src/conversions.cpp`,
and `marine_tools/kongsberg_em_bridge/kongsberg_em_bridge/node.py`. All claims in the issue
body and in both follow-up comments from the repo owner (`rolker`) check out against source:

- The fallback (`device_.across_track_beamwidth / 12.0`) consumes a value documented
  `/// degrees` and never converts it — confirmed at `error_model.h:38` vs. the along-track
  beamwidth two lines earlier in the same constructor, which *does* convert
  (`error_model.cpp:61-63`). Same struct, inconsistent handling.
- `PingInfo.msg` at `/opt/ros/jazzy/share/marine_acoustic_msgs/msg/PingInfo.msg:9-13` says
  `rx_beamwidths`/`tx_beamwidths` are "reported in radians" — confirming the owner's
  **scope-correction comment**: the per-beam branch (`rx_beamwidths[i] * (M_PI/180.0) / 12.0`)
  double-converts an already-radian value, making it ~57x too *small*, the opposite direction
  from the fallback's ~57x too *large*. Neither branch is correct as written.
- `docs/divergences_from_calder.md:86-91` currently documents the fallback as the rare path
  and the per-beam path as "the normal one" — verified backwards: `kongsberg_em_bridge/node.py:547-550`
  leaves the arrays empty **deliberately**, citing this exact unit mismatch (and citing
  `cube_bathymetry#30`, an older, narrower issue on the same mismatch — worth cross-referencing
  in the fix). `norbit_driver/src/conversions.cpp:19-20,59-60,95-96` resizes the arrays to
  **zero-filled, non-empty** vectors with the assignment commented out ("not reported") —
  confirming the owner's second comment: the per-beam branch is taken for norbit data with a
  beamwidth of exactly 0, silently zeroing the angular term rather than falling back.
- The "twice the nominal variance" doc comment at `error_model.h:242-247` (on the 2-arg
  `horizontal_positioning_error` overload) does not match the implementation at
  `error_model.cpp:114-118` — confirmed no factor of 2 anywhere in that function or in
  `swath_horizontal`/the 5-arg overload. This is a different function from the beamwidth term
  the issue title names, bundled into the same fix by the owner's second comment "since it
  lives in the same file."
- `test/test_error_model.cpp` zeros both beamwidth fields in every test that would otherwise
  exercise `swath_angle_error`'s `ang_meas` term (lines 282, 331-332, 394-395, 419-420,
  470-471) — confirms the issue's premise that no existing test pins either branch's value or
  distinguishes empty/zero-filled/real `rx_beamwidths`.

### Scope Assessment

**Well-scoped?** Not as currently written. The issue **body** still describes a
single-branch fix ("convert in the fallback... or better, normalise the units once at the
boundary") with an Acceptance section written against that narrower scope. Two subsequent
owner comments substantively **expand and partially reverse** that scope:
1. First comment: the per-beam branch is *also* wrong (opposite direction), so the body's
   first proposed option ("convert in the fallback as the per-beam branch does") is now
   flagged unsafe — it would make both branches wrong the same way. Only boundary
   normalization survives. It also adds an angle-dependent (`1/cos`) widening term ported
   from Calder's `device.c:808-820` as a fourth item to fold in, plus a correction owed to
   `divergences_from_calder.md`.
2. Second comment: adds a validation requirement (reject non-finite/non-positive beamwidths,
   don't just branch on array length) and an unrelated-but-bundled doc-comment deletion in
   `error_model.h`.

None of this is in dispute — I independently verified every claim above — but the issue
**body's Acceptance criteria were never updated** to reflect it, so a plan built strictly from
the body would reproduce the now-flagged-unsafe fix and miss the validation requirement, the
angle-widening addition, and both doc corrections. Recommend the acceptance criteria be
restated (in the plan, since the owner chose not to edit the body) to cover, explicitly:
   - boundary-normalize `Device::across_track_beamwidth` (degrees) and
     `PingInfo::rx_beamwidths`/`tx_beamwidths` (radians) so neither branch can silently
     consume the other's units;
   - reject non-finite/non-positive per-beam beamwidths (covers norbit's zero-filled arrays)
     and fall back to the device value instead of trusting a zero;
   - fold in the `1/cos(angle)` swath-edge widening from Calder's `device.c`;
   - state the `/12` convention in one place (`error_model.h`), matching what
     `marine_perception_tools` already cites this file for;
   - correct `docs/divergences_from_calder.md:86-91` (backwards "normal path" claim);
   - delete the false "twice the nominal variance" doc comment at `error_model.h:242-247`;
   - tests: unit-agreement between both branches, a pinned regression value for a 2° device
     beamwidth, and one test per validation case (empty array, zero-filled array, real radian
     value);
   - re-measure `depths/processed` uncertainty over the same Lake Massabesic 10 m box cited in
     the issue, and record the before/after numbers.

**Right repo?** Yes. `error_model.cpp`/`.h` and `docs/divergences_from_calder.md` all live in
this repo (`cube_bathymetry`). The doc-comment fix in `kongsberg_em_bridge/node.py:547-550`
(a different repo, `marine_tools`) is now stale once the boundary is normalized — it currently
says leaving beamwidths empty "avoids a unit mismatch," but a boundary-normalized fix removes
the mismatch it's working around, so populating them would become *more* accurate than the
fallback, not equally safe. That's outside this repo; flag it for `marine_tools` (tracked
issue exists: `cube_bathymetry#30`) rather than pulling it into this PR.

**Dependencies**: `marine_tools#82` — the owner's first comment states that converting the
fallback in isolation would be actively unsafe once `marine_tools#82` (which was found to
propose promoting the per-beam branch to the live path) lands, since both branches would then
independently read as valid but wrong. Sequencing note for `plan-task`: this issue's fix
(boundary normalization, not a fallback-only patch) must land in a form that stays correct
regardless of `marine_tools#82`'s landing order, and `marine_tools#82` should be re-checked
against whichever normalization boundary this issue lands.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| A change includes its consequences | Action needed | `docs/divergences_from_calder.md` correction is explicitly called out by the owner and confirmed by source; not in the issue body's Acceptance section — must be in scope, not a follow-up |
| Test what breaks | Action needed | Existing tests zero out the exact term this issue changes (verified above); Acceptance section needs the validation-case tests added by the second comment, not just the two tests named in the body |
| Only what's needed / Improve incrementally | Watch | Scope grew across three posts (unit fix → also-wrong per-beam branch → validation → angle-widening enhancement → unrelated doc-comment deletion). The angle-widening term is a modeling **enhancement**, not a bug fix — legitimate to bundle since it's the same expression, but `plan-task` should split it into its own atomic commit(s) so "fix" and "enhancement" are separately reviewable and revertable (AGENTS.md atomic-commit rule) |
| Capture decisions, not just implementations | OK | The scope correction and its reasoning are well-recorded in the issue comments; the divisor-convention question in `divergences_from_calder.md` (`/12` vs `/sqrt(12)`) is resolved by the owner citing Calder's source directly |
| Human control and transparency | OK | No hidden behavior change; the fix is a correction toward documented intent, and the store-uncertainty re-measurement acceptance criterion keeps the product-level effect visible |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | No | Pure C++ math/logic fix in an existing translation unit; no new packages, launch files, or interfaces |
| ADR-0013 (progress.md vocabulary) | N/A to the issue itself | Applies to this review's own persistence, not to the code change |
| None else | — | No ADR governs numerical/error-model conventions specifically; `docs/divergences_from_calder.md` is the informal record for this class of decision and is exactly what this issue must update |

### Consequences

- `docs/divergences_from_calder.md:73-104` — must be corrected (backwards "normal path" claim;
  the `/12` vs `/sqrt(12)` open question is now resolved and should be closed out).
- `error_model.h:242-247` — false "twice the nominal variance" doc comment, unrelated function,
  bundled deletion per owner's second comment.
- `test/test_error_model.cpp` — every existing test that zeros the beamwidth fields to avoid
  this term stays valid, but new tests are needed alongside them (see Scope Assessment).
- Outside this repo, not in scope for this PR: `marine_tools/kongsberg_em_bridge/kongsberg_em_bridge/node.py:547-550`'s
  comment becomes stale once the boundary is normalized (tracked separately, `cube_bathymetry#30`).
- `marine_perception_tools`'s CUBE-lab placeholder (mpt#49, per the issue's Context section)
  cites this file's `/12` convention — no code coupling, but worth a heads-up comment on that
  PR/issue once this lands, since it deliberately followed the (correct) radian branch.

### Actions
- [ ] Restate the issue's Acceptance criteria in the plan to cover boundary normalization
      (not fallback-only conversion), beamwidth validation (reject non-finite/non-positive,
      covers norbit's zero-filled arrays), the Calder `1/cos(angle)` widening term, the
      `divergences_from_calder.md` correction, and the unrelated `error_model.h` doc-comment
      deletion — the issue body was never edited to reflect the two scope-correcting comments.
- [ ] Split the angle-widening addition (a modeling enhancement) into its own atomic commit(s)
      separate from the unit-mismatch bug fix, per AGENTS.md's atomic-commit rule.
- [ ] Sequence against `marine_tools#82` — confirm the landed fix is safe regardless of that
      issue's landing order (per the owner's first comment).
- [ ] Flag (don't fix here) the now-stale comment in `kongsberg_em_bridge/node.py:547-550`
      (repo: `marine_tools`) for follow-up once this normalization lands; note the existing
      `cube_bathymetry#30` cross-reference.
- [ ] Re-measure `depths/processed` uncertainty over the same Lake Massabesic 10 m box cited
      in the issue and record before/after numbers, per the issue's Acceptance section.

## Plan Authored
**Status**: complete
**When**: 2026-09-10 09:34 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-144/plan.md` at `112b9ba`
**Branch**: feature/issue-144 at `112b9ba`
**Phases**: single (two atomic commits within it: unit-mismatch fix + Calder angle-widening enhancement)

### Open questions
- [ ] No open questions — plan is review-plan-ready.
