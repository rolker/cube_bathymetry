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

## Plan Review
**Status**: complete
**When**: 2026-09-10 09:52 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-144/plan.md` at `112b9ba`
**PR**: PR-less
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) The plan's doc-comment fix targets only the 2-arg `horizontal_positioning_error` overload's false "twice the nominal variance" claim (`error_model.h:242-247`, per the owner's second comment). The 5-arg overload of the same function (declared `error_model.h:319-323`, doc comment `error_model.h:310-317`) carries the same class of false claim — "We multiply the standard deviation estimate by 2.0 to approximate this" — and its implementation (`error_model.cpp:319-356`) sums four terms with no factor of 2 anywhere, same as the 2-arg overload's implementation. The plan doesn't mention this second instance at all (not fixed, not flagged as out of scope). Since the justification for touching this doc comment is "unrelated function, bundled... since the file is already open," the same rationale applies here and it costs little to fold in — or, at minimum, the plan should say explicitly why it's left for later. — `plan.md:71` (Approach step 3)

### Verification performed
Independently verified against source (not assuming the plan's claims): `error_model.cpp:236-240` confirms the unit-mismatch bug exactly as described (fallback consumes undocumented-as-converted degrees; per-beam branch double-converts already-radian values). Installed `/opt/ros/jazzy/share/marine_acoustic_msgs/msg/PingInfo.msg:9-13` confirms `rx_beamwidths`/`tx_beamwidths` are documented "reported in radians". `norbit_driver/src/conversions.cpp:19-20,59-60,95-96` confirms zero-filled (not empty) beamwidth arrays. `kongsberg_em_bridge/node.py:547-550` confirms the deliberate-empty-arrays comment citing this exact mismatch and citing `cube_bathymetry#30`. `error_model.cpp:114-163` (2-arg `horizontal_positioning_error`) confirms no factor-of-2 in the implementation, matching the plan's claim about the false doc comment. All existing tests in `test/test_error_model.cpp` that zero `across_track_beamwidth` (lines 282, 332, 395, 420, 471) confirmed, leaving the fix's target term untested today. `docs/divergences_from_calder.md:73-91` confirmed to contain the backwards "normal path" framing and the "no behavioural change" claim the plan proposes to correct, nested under "## 2. Device error budget is parameterized" (not a "#47" subsection as such, but the plan's characterization — that it currently reads as a note inside that section rather than its own entry — is accurate). Read both scope-correcting owner comments on the issue directly (`gh issue view 144 --comments`); the plan's Context/Approach sections track them point-for-point. No ADR governs this class of change; ADR-0008 correctly assessed as not triggered (no new packages/topics/interfaces).

### Summary
The plan is well-grounded in source — every factual claim it makes was independently checked against the actual code, the installed message definition, and both driver repos, and all of them check out. Scope, file targeting, consequences, and the documentation-impact section are all sound, and the plan correctly incorporates both scope-correcting owner comments that the issue body itself was never updated to reflect. The one gap found is minor and doesn't block implementation: a second, sibling instance of the same false "×2" doc-comment bug that the plan already commits to fixing once, sitting a few lines away in the same header. Ready for implementation; addressing the finding (fix or explicit defer-with-reason) during implementation is a suggestion, not a blocker.

### Recommended Actions
- [ ] During implementation, either fold the 5-arg `horizontal_positioning_error` overload's matching false doc comment into commit 1's doc-comment fix, or add one sentence to the plan/PR explaining why it's deliberately left for a later pass.

## Implementation
**Status**: complete
**When**: 2026-09-10 12:04 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**Branch**: `feature/issue-144`
**Commits**: `57e3835` (unit normalization + validation + docs), `952c1da` (restore
Calder's `1/cos(angle)` widening)

### What was built

**Commit 1 — `57e3835`, boundary normalization, validation and the doc corrections.**

- `ErrorModel`'s constructor now computes `device_across_track_beamwidth_rad_` from
  `device.across_track_beamwidth * M_PI / 180.0`, immediately above the existing
  `along_track_beamwidth_coefficient` conversion it mirrors. `Device` itself is untouched and
  stays degrees-valued, per settled decision 1.
- `swath_angle_error` consumes that stored radian value as its default and reads
  `ping_info.rx_beamwidths[i]` **unconverted**, accepting it only when
  `std::isfinite(reported) && reported > 0.0f`. Both branches now carry radians; norbit's
  zero-filled arrays fall back to the device value instead of silently zeroing the term.
- The false "twice the nominal variance / approximate 95% confidence interval" comments are
  gone from both `horizontal_positioning_error` overloads, replaced with wording that matches
  the code (a variance in m^2 at one sigma, no confidence scaling, scaling lives at reporting).
- `Sounding::vertical_error` / `horizontal_error` are documented as variances in m^2 at one
  sigma with no confidence scaling, and the horizontal one is flagged as the radial
  drms-derived quantity whose square root is a horizontal-plane radius.
- `docs/divergences_from_calder.md`: the stale "Angle error" notes are replaced by a new
  section **2b. Angle error: units, validation and angle widening (#144)** carrying Calder's
  source snippet, the corrected statement that the fallback was the live path for every sonar
  in service, the validation rule, the `marine_tools` follow-up flag, and the `/12`-confirmed
  finding.
- Tests added: `AngleErrorBranchesAgreeOnUnits`, `AngleErrorFallbackPinnedAtNadir`,
  `AngleErrorFallsBackOnEmptyBeamwidths`, `AngleErrorFallsBackOnZeroFilledBeamwidths`,
  `AngleErrorFallsBackOnNonFiniteBeamwidth`, `AngleErrorUsesReportedBeamwidthAsRadians`.

**Commit 2 — `952c1da`, restore the `1/cos(angle)` widening.** `ang_meas` is now
`(beamwidth / cos(meas_angle)) / 12.0`, squared — Calder's order of operations from
`original_cube/libsrc/ccom_core/device.c:808-820`, so the widening is squared into the
variance. Test `AngleErrorWidensWithObliquity` pins both the absolute expected value and the
4x ratio at 60 degrees. Its divergences-doc bullet describes it as restoring a term the port
dropped, as does the commit message.

### Divergence from the plan (plan.md updated inline on this branch)

The plan asserted that the existing tests which zero `across_track_beamwidth` remain valid,
which held — but one existing test that does **not** zero it,
`VerticalErrorIncreasesWithBeamAngle`, turned out to encode the bug and failed on the first
run. It held the two-way travel time fixed, so the DEPTH shrank as the beam swung out, and
asserted the total vertical error rose monotonically nadir -> 30 deg -> 60 deg. That only
held because the ~3283x inflated angular term swamped every other contributor, so the budget
tracked `sin^2(angle)` alone. With the term at its correct magnitude the measured-range error
projects into depth as `cos^2(angle)` and shrinks off nadir, so the total dips at 30 deg
before the angular term takes over.

The test was **replaced, not loosened**, by two tests that pin properties actually true of
the corrected model: `VerticalErrorIsWorseAtObliqueAngleAtConstantDepth` (nadir vs 60 deg at
constant depth, the comparison that isolates beam obliquity from a shortening water column)
and `AngularContributionToVerticalErrorRisesWithBeamAngle` (the monotone claim stated over
the isolated angular term it applies to). Both the rewrite's in-code comment and `plan.md`
record why. `plan.md` also records the widening test's comparison baseline (un-widened at the
same angle, not nadir — the angular term reaches `vertical_error` multiplied by
`sin^2(angle)`, which is zero at nadir).

### Build and test results

From the worktree root, `source setup.bash` then:

- `./sensors_ws/build.sh cube_bathymetry` — **passed** both times (1 package finished; stderr
  output is pre-existing `-Wunused-result` / `tmpnam` / `-Wunused-but-set-variable` warnings
  in `bag_to_geotiff.cpp`, `test_angular_response_curve.cpp` and `test_tile_eviction_rss.cpp`,
  none of them in files this branch touches).
- `./sensors_ws/test.sh cube_bathymetry` after commit 1 — **609 tests, 0 errors, 0 failures,
  70 skipped**.
- `./sensors_ws/test.sh cube_bathymetry` after commit 2 — **610 tests, 0 errors, 0 failures,
  70 skipped**.

No test was skipped, disabled or loosened to reach green. The one intermediate failure
(`VerticalErrorIncreasesWithBeamAngle`) is written up above; it was diagnosed to its cause and
the test replaced with stronger assertions.

This repo has no `.pre-commit-config.yaml` and no installed git hooks, so no pre-commit run
applies; `cpplint`/`uncrustify` run as part of the colcon test suite above and are green.

### Follow-up candidates (not acted on)

- `horizontal_latency`'s doc comment in `error_model.h` still says "Compute approximate 95%
  error bound due to latency errors" — the same class of false claim as the two the plan named,
  in the same file, and the implementation applies no confidence scaling either. Left untouched
  because the plan scoped the correction to the two `horizontal_positioning_error` overloads;
  worth a one-line follow-up.
- `marine_tools`: `kongsberg_em_bridge/node.py:547-550`'s "leave empty" comment is stale once
  this merges. Flag on `marine_tools#82` after the PR lands, per the plan's Out of scope.
- The restored widening shares the existing `tan(angle)` singularity at `|angle| -> 90 deg`
  already present in the profile term. Neither is guarded, matching Calder; if a guard is ever
  wanted it should cover both, not just the new term.

### Next step

Pre-push review (`/review-code`) against the two-commit diff, then open the PR. Nothing is
pushed — the host performs pushes.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-10 10:30 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))
**Verdict**: changes-requested

**Branch**: feature/issue-144 at `aca27ef`
**Mode**: pre-push
**Depth**: Deep (reason: numerically load-bearing change to the uncertainty budget consumed by CUBE for every sounding; requires cross-checking against the vendored Calder original)
**Must-fix**: 5 | **Suggestions**: 12
**Round**: 1 | **Ship**: continue — one genuine correctness gap (the per-beam validation has no upper bound, and two real in-workspace producers would now feed it absurd values) plus an unresolved semantic question about the widening on the per-beam branch; the rest are one-to-three-line doc corrections.

Specialists: Static Analysis (clean), Governance, Plan Drift (clean), Claude Adversarial Lens A + Lens B. Copilot and local-model passes off by default.

**Verified, not a finding:** the deletion of `VerticalErrorIncreasesWithBeamAngle` is justified. Two independent numerical checks (lead reviewer by hand from the source; Lens A against the built library) both confirm `EXPECT_LT(nadir, 30deg)` genuinely fails under the corrected model, and fails at constant depth too — the old assertion was unsound, not merely inconvenient. The two replacement tests cover what it was reaching for. Note that `EXPECT_LT(nadir, 60deg)` still passes unchanged at the original fixed travel time, so a minimal edit was available; the constant-depth rewrite is a deliberate improvement worth saying so in the PR body.

### Findings
- [x] (must-fix) per-beam beamwidth validation has no upper bound — `ros2sonic` stamps `rx_beamwidths` with the transmit fan (2.27 rad) and `garmin_sidescan` with 0.48 rad; the removed `pi/180` was laundering both. Add a plausibility ceiling — **premise corrected by the operator**: only `ros2sonic`'s value is wrong (an upstream driver fault, `rolker/ros2sonic#1`); `garmin_sidescan`'s 55 deg is correct data. A hard physical ceiling (>= pi rad) was added instead of a plausibility clamp — `cube_bathymetry/src/error_model.cpp:255-257`
- [x] (must-fix) the `1/cos` widening may be double-applied on the per-beam branch — Calder's widening exists because his table value is a nominal nadir width; a per-beam array plausibly reports the already-broadened width. Decide: widen the fallback branch only, or pin the nominal-nadir contract on `rx_beamwidths` and flag it on marine_tools#82. Untested (the only per-beam test is at nadir) — resolved by DROPPING the widening from this PR entirely (operator decision); the commit was rebased out — `cube_bathymetry/src/error_model.cpp:250-270`
- [x] (must-fix) Calder applies the widening to only 3 of 9 device families (EM120, EM3000/D, SB8125 — all annotated "flat plate and FFT beamformer"); the other five do not widen. Applying it unconditionally is a modelling decision about our sonars, not a pure restoration — say so and name the non-widening families — resolved by DROPPING the widening; the divergences doc now records it as an un-ported divergence with Calder's device-family gating as the reason, tracked at cube_bathymetry#148 — `cube_bathymetry/docs/divergences_from_calder.md:83-93,133-140`
- [x] (must-fix) "documented (and configured) in degrees" is false — `across_track_beamwidth` is exposed by no ROS parameter, YAML or launch file and is permanently the hardcoded 2.0. Post-fix it is the sole driver of the angular term for every M3 sounding. Strike "configured" and cross-reference cube_bathymetry#145 — corrected in both the constructor comment and the divergences doc — `cube_bathymetry/docs/divergences_from_calder.md:104-106`
- [x] (must-fix) a third instance of the same false 95% claim survives in the same header, already self-disclosed as a follow-up: `horizontal_latency`'s "Compute approximate 95% error bound"; the implementation applies no scaling either. One-line edit in a file already open — deleted — `cube_bathymetry/include/cube_bathymetry/error_model.h:253`
- [x] (suggestion) `Platform::roll`/`pitch` are documented degrees and converted by the model, but `DetectionsProjector` feeds radians from `tf2::getEulerYPR` — the identical bug class, one function away, and this change makes it the leading residual. Every `makePlatform()` sets roll=pitch=0 so no test can see it. File its own issue before merging — **fixed in this PR**, not deferred to the follow-up: cube_bathymetry#147, which this PR closes — `cube_bathymetry/src/detections_projector.cpp:112-113`
- [x] (suggestion) `rx_beamwidths`' doc comment says "transmit beamwidths" (copy-paste from the line above) — the field this whole issue is about — corrected to RECEIVE — `cube_bathymetry/include/cube_bathymetry/error_model.h:213`
- [x] (suggestion) no guard on `|meas_angle|` beyond 90 degrees: `cos < 0` gives a negative widened beamwidth that squares back to a small, plausible-looking variance — garbage laundered into over-confidence, which is worse than the documented singularity at exactly 90 (deferred: the concern was specific to the widening's `1/cos`, which is no longer in this PR; the pre-existing `tan(angle)` singularity in `ang_svp` is unchanged and matches Calder) — `cube_bathymetry/src/error_model.cpp:271`
- [x] (suggestion) validation asymmetry: a per-beam 0.0 is rejected, a `Device::across_track_beamwidth` of 0.0/negative/NaN is accepted. Note the tension — five existing tests deliberately set it to 0.0 to isolate other terms, so a constructor that refuses to build would break them; a NaN/negative guard is the safe subset — the tension is now stated in the constructor comment and the divergences doc, with `#145` named as what would first make the field human-settable; no guard added, for the reason the finding gives — `cube_bathymetry/src/error_model.cpp:61-67`
- [x] (suggestion) `AngleErrorFallbackPinnedAtNadir` and `AngleErrorFallsBackOnEmptyBeamwidths` are the same test with different comments — merge, or give the first a non-default beamwidth — `AngleErrorFallbackPinnedAtNadir` now uses a non-default 7.5 deg width — `cube_bathymetry/test/test_error_model.cpp:661-700`
- [x] (suggestion) `AngleErrorFallsBackOnNonFiniteBeamwidth` includes `-0.01f`, which is finite, and there is no case for a finite-but-absurd value — the predicate's real weakness is untested — split into `AngleErrorFallsBackOnNonPositiveBeamwidth`, and the finite-but-absurd case is now covered by the ceiling tests — `cube_bathymetry/test/test_error_model.cpp:704-726`
- [x] (suggestion) the per-beam rejection is silent per-sounding in a hot loop; `DetectionsProjector::Result` already carries a diagnostics struct (`missing_attitude`, `missing_heave`, `filtered_range`) that a `rejected_beamwidths` counter would fit at zero cost — `ProjectionDiagnostics::rejected_beamwidths`, filled via the model's own public predicate; throttled warning in the live node, run-summary line plus a warning in all three offline tools — `cube_bathymetry/src/error_model.cpp:251-259`
- [x] (suggestion) nothing invalidates pre-#144 store tiles, so `depths/processed` becomes a mixed store with two uncertainty scales ~800x apart. The operator dropped re-measurement, which is not the same as dropping invalidation; ADR-0003's `build_fingerprint.json` is unimplemented and `package.xml` is pinned 0.0.0. Add an operator-visible note and file the follow-up (deferred: per the operator, a real consequence but not a code change — carried into the PR body instead) — `cube_bathymetry/docs/decisions/0003-staleness-fingerprint.md`
- [x] (suggestion) record that CUBE's tuned constants (`bayes_factor_threshold`, `variance_scale`, `blunder_scalar`, ...) were calibrated against the inflated budget, so outlier rate and swath weighting shift with this change — documentation only, not the dropped re-measurement — recorded in `divergences_from_calder.md` as a named consequence, explicitly distinguished from the dropped store re-measurement — `cube_bathymetry/src/hypothesis.cpp:54-88`
- [x] (suggestion) `Vessel::gps_drms = 2.0` is a Calder-era ship default that now dominates `horizontal_error` on RTK boats (drms ~0.02 m), adding ~3.9 m of spurious propagation distance at `node.cpp:163`. A consequential default, worth raising even though out of scope (deferred: out of scope for this PR as the finding itself says — carried into the PR body so it is raised, not dropped) — `cube_bathymetry/include/cube_bathymetry/error_model.h:75`
- [x] (suggestion) README documents the published `vertical_uncertainty`/`horizontal_uncertainty` fields with no units — a name that says sigma carrying a value that is sigma^2, the same trap one layer out — and says "six float32 fields" where the code publishes seven (`beam_angle` trails) — both corrected: seven fields, and the variance-in-m^2-at-one-sigma meaning spelled out — `README.md:15-16`
- [x] (suggestion) `#144` introduced a whole new section 2b but is absent from the doc's "Related issues" list, and its in-text `#144` references are bare where the file's convention is full markdown links — `#144`, `#145`, `#147`, `#148` and `rolker/ros2sonic#1` all added to Related issues, as full markdown links — `cube_bathymetry/docs/divergences_from_calder.md:200`

## Implementation
**Status**: complete
**When**: 2026-09-10 11:45 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**Branch**: `feature/issue-144` at `f579876` (nothing pushed; no PR yet)
**Addressed**: the `## Local Review (Pre-Push)` of 2026-09-10 10:30 -04:00 (round 1, verdict
changes-requested, at `aca27ef`) — all 5 must-fix and all 12 suggestions, **as triaged by the
operator on 2026-09-10**. Where the operator's triage overrides the review, the triage won and
the finding's checkbox records why.
**Commits**: `cb2e347`, `b50bf87`, `0154e22`, `0987234`, `16af250`, `f579876`

### Scope changes the operator made to this PR

Three, and they reshape the PR rather than just correcting it:

1. **The `1/cos(angle)` widening is OUT.** Commit `952c1da` (which restored it) was dropped by
   rebasing the branch onto its parent — nothing was pushed, so no revert commit exists and
   that SHA is no longer reachable. Its code, its test and its divergences-doc entry went with
   it. Calder gates the term by device family (three of nine, all "flat plate and FFT
   beamformer"), the geometry is device-specific, and the error model should not have to know
   whether an array is flat — that contract belongs with the drivers. Tracked at
   [cube_bathymetry#148](https://github.com/rolker/cube_bathymetry/issues/148).
2. **The attitude bug is IN.** [cube_bathymetry#147](https://github.com/rolker/cube_bathymetry/issues/147)
   is fixed here and **the PR body must close it alongside #144**.
3. **Must-fix 1's premise was wrong.** No plausibility clamp. Only a hard physical ceiling.

### What changed, finding by finding

**Must-fix 2 and 3 — the widening (`cb2e347`).** Rebased out; the divergences doc now carries
the omission as a live, reasoned divergence citing #148, so the gap is deliberate and
traceable rather than silent. `plan.md` no longer claims the widening is in scope and records
the two scope changes that replaced it.

**Must-fix 1 — the validation ceiling (`b50bf87`).** `ErrorModel::kMaxPerBeamBeamwidthRad` =
π rad, alongside the existing finite and strictly-positive checks; a rejected value falls back
to the device beamwidth. Deliberately no tighter: `garmin_sidescan`'s 55° across-track is
correct data in the correct field (a sidescan does no across-track beamforming), and any clamp
tight enough to catch the R2Sonic transmit fan would discard it. The code comment, the header
and the divergences doc all state that the ceiling catches nonsense and does **not** catch a
misplaced transmit fan.

Rejections are reported, not silent. The predicate is public
(`ErrorModel::per_beam_beamwidth_usable`) so `DetectionsProjector` counts rejections into
`ProjectionDiagnostics::rejected_beamwidths` without duplicating it;
`detections_to_pointcloud` emits a throttled warning (`RCLCPP_WARN_STREAM_THROTTLE`, 10 s,
matching the existing `missing_attitude` pattern), and `bag_to_geotiff`, `import_bag` and
`batch_regen_bag` add the count to their run summary plus a warning when it is non-zero.
`import_bag`'s summary moved into `report_projection_summary()` — the extra counter pushed
`main()` past cpplint's 500-line limit, and the summary was the part that wanted to move.

Tests: `AngleErrorFallsBackOnPhysicallyImpossibleBeamwidth` (π, 4, 100, 2π),
`AngleErrorAcceptsWideButLegitimateSidescanBeamwidth` (garmin's 55° used, not swapped for the
default), `PerBeamBeamwidthUsablePredicateMatchesTheCeiling` (boundary: `nextafter(π,0)` in,
π out), and `DetectionsProjectorTest.RejectedBeamwidthsAreCounted` (two rejections in a
three-beam ping whose third beam is legitimately wide; an empty array is not a rejection).

**The attitude bug — #147 (`0154e22`).** `Platform::roll`/`pitch` are now **radians** and the
four `* M_PI / 180.0` conversions in `swath_depth` and `compute` are deleted — the
hold-radians-internally route, not conversion at the projector, exactly as #144 handled the
beamwidth. `Device`/`Vessel` stay in degrees: they are human-entered configuration read off
datasheets, whereas `Platform` is a measurement from TF with no degrees-facing surface. Sign
convention **confirmed, not assumed**: roll is a right-handed rotation about REP-103's +x
(forward), which lifts +y (port), so "+ve is port side up" as documented; `beam_angle()`
negates the starboard-positive `rx_angles`, so `meas_angle` is port-positive too and the two
add coherently.

Three tests with **non-zero** roll and pitch, each of which fails against the old code:
`PlatformRollIsRadiansAndSteersTheBeam` (reported depth tracks `-range·cos(roll + beam
angle)`), `PlatformAttitudeEntersTheVerticalBudgetInRadians` (the closed form the isolating
config leaves, `(bw/12)²·range²·sin²(roll+angle)·cos²(pitch)`; the degrees reading differs by
>10×), and `PositiveRollIsPortSideUp` (a port beam reads shallower than a starboard one at
equal and opposite angles).

**Must-fix 4 and 5, and the doc suggestions (`0987234`).** `horizontal_latency`'s "approximate
95% error bound" deleted — the third copy of a claim no implementation matches. The false
"(and configured) in degrees" replaced: `across_track_beamwidth` is exposed by no parameter,
YAML or launch file, is permanently the hardcoded 2.0, and after this change is the sole driver
of the angular term for every M3 sounding — recorded in both the code and the doc, cross-linked
to [#145](https://github.com/rolker/cube_bathymetry/issues/145). `rx_beamwidths`' doc comment
said "transmit"; corrected. The validation asymmetry (device value unguarded while the per-beam
one is) is now stated rather than left to be noticed. README: seven `float32` fields, not six,
and the two uncertainty fields documented for the first time as variances in m² at one sigma —
the same trap one layer out. The tuned-constants consequence is recorded in the divergences doc
as a named consequence, explicitly distinguished from the dropped store re-measurement.

**`rolker/ros2sonic#1` (`16af250`).** Mid-task correction from the operator: cube_bathymetry#149
is closed as misfiled and the bug now lives in the driver's own repo as
[`rolker/ros2sonic#1`](https://github.com/rolker/ros2sonic/issues/1), verified upstream (the
USF-COMIT parent and four SeawardScience branches). Cited by full `owner/repo#number` form in
the divergences doc; dropped from the code comments entirely, where the *explanation* earns its
place and a cross-repo number would rot. The earlier commit's message was reworded in place
(nothing pushed) so no dead reference survives in the history.

**The three-angle test (`f579876`) — and what it found.** The suggestion was to extend
`VerticalErrorIsWorseAtObliqueAngleAtConstantDepth` from two angles to three so the monotone
claim is pinned by more than a pair. Done, and the added point falsified the monotone claim: at
constant depth the total vertical budget **dips at 30° before rising**, because the
measured-range term projects into depth as `cos²(angle)` and shrinks faster than the angular
term's `tan²(angle)` grows until somewhere past 30°. A three-point monotone assertion was
written first and failed on exactly this — the model was right, the assertion was not. The test
now pins the *shape*, dip included, and says where monotonicity really does hold (the isolated
angular term, `AngularContributionToVerticalErrorRisesWithBeamAngle`). This is the same class
of finding as the earlier pass's `VerticalErrorIncreasesWithBeamAngle` rewrite: a two-point
comparison could not tell "rises" from "dips and recovers".

### Build and test results

From the worktree root, `source setup.bash`, then:

- `./sensors_ws/build.sh cube_bathymetry` — **passed**. Remaining stderr is pre-existing
  `-Wunused-result` / `tmpnam` / `-Wunused-parameter` warnings in `bag_to_geotiff.cpp`,
  `error_model.cpp` (unused `platform`/`per_ping_sources` parameters, untouched by this branch)
  and two other test files.
- `./sensors_ws/test.sh cube_bathymetry` — **617 tests, 0 errors, 0 failures, 70 skipped**
  (609 before this pass; +8: five error-model tests for the ceiling and validation split, three
  attitude tests, one projector diagnostics test, less the widening test that went out with the
  rebase). `cpplint` and `uncrustify` are green, including the `import_bag_main.cpp` function-size
  limit.

No test was skipped, disabled or loosened. Two tests failed during the pass and both were
diagnosed to cause rather than relaxed: `cpplint`'s function-size limit (fixed by extracting
`report_projection_summary()`, not by raising the limit) and the three-point monotone assertion
described above (the assertion was wrong about the model, and was replaced by one that is
right).

This repo has no `.pre-commit-config.yaml` and no installed git hooks, so no pre-commit run
applies; `cpplint`/`uncrustify` run inside the colcon test suite above.

### Deliberately not done — carry these into the PR body

- **Store invalidation.** Nothing invalidates pre-#144 store tiles, so `depths/processed`
  becomes a mixed store with two uncertainty scales ~800× apart. The operator dropped
  *re-measurement*, which is not the same as dropping *invalidation*. Per the operator this is a
  real consequence but not a code change: it belongs in the PR body, and a follow-up issue is
  still owed.
- **`Vessel::gps_drms = 2.0`** is a Calder-era ship default that now dominates
  `horizontal_error` on RTK boats (drms ~0.02 m), adding ~3.9 m of spurious propagation
  distance. Out of scope, but raise it in the PR body rather than let it drop.
- **The past-90° guard** disappeared with the widening. The pre-existing `tan(angle)`
  singularity in `ang_svp` is unchanged and matches Calder.
- **The device-value validation asymmetry** is documented, not guarded — several tests set
  `across_track_beamwidth` to 0.0 deliberately to isolate other terms. Worth revisiting with
  #145, which is what would first make the field human-settable.

### New finding, not acted on — worth its own issue

`StaticErrorSources::min_angle`/`max_angle` are set from `-vessel.static_roll`, which is
**degrees** (`error_model.cpp`, constructor), and then compared against `meas_angle`, which is
**radians** (`swath_angle_error`). The same unit-mismatch class as #144 and #147, in the same
function. It is latent today only because `Vessel::static_roll` defaults to 0.0, where both
readings coincide — a non-zero mounting angle would gate the surface-sound-speed term at the
wrong beam angles. Found while fixing #147; deliberately **not** fixed here, because the
operator's triage set this PR's scope and a third unit fix would blur it. Should be filed.

### Next step

Pre-push re-review (`/review-code`) against the six-commit diff, then open the PR. The PR body
must close **both** `#144` and `#147`, and must carry the store-invalidation and `gps_drms`
notes above. Nothing is pushed — the host performs pushes.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-10 11:57 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))
**Verdict**: changes-requested

**Branch**: feature/issue-144 at `3e56683`
**Mode**: pre-push
**Depth**: Deep (reason: numerically load-bearing change to the per-sounding uncertainty budget CUBE consumes, cross-checked against the vendored Calder original)
**Must-fix**: 4 | **Suggestions**: 14
**Round**: 2 | **Ship**: continue — two verified correctness/faithfulness findings in the newly-live attitude path (the `Platform::pitch` sign convention, and `swath_depth`'s Eqn-3.49 pitch term diverging from Calder) need an operator decision, not a mechanical edit; the other two must-fixes are precise doc corrections.

Specialists: Static Analysis (clean), Governance, Plan Drift, Claude Adversarial Lens A + Lens B. Copilot and local-model passes off by default.

**Independently re-verified, not findings.** (a) The 30-degree dip is real: recomputing the constant-depth vertical budget from source gives 0.005657 / 0.005003 / 0.008464 m^2 at nadir / 30 / 60 deg for the depth-dependent part (the two angle-independent terms cannot change the ordering). The dip is ~12%, not knife-edge, and both new assertions hold — this is the second independent confirmation of that test rewrite. (b) The `1/cos(angle)` widening is genuinely absent from the tree, and no dead `#149` reference survives in any commit message, comment or doc (the sole occurrence is the progress.md narrative explaining the re-file to `rolker/ros2sonic#1`, which is a live explanation). (c) `report_projection_summary()` is behaviour-identical — parameter order, `ping_count` -> `georeferenced_pings` in both its roles, byte-identical texts, stdout/stderr split, same call position, non-negative `int` -> `size_t`. (d) `StaticErrorSources::min_angle`/`max_angle` is **genuinely latent** at the default: `Vessel::static_roll` is 0.0, is exposed by no parameter, YAML or launch file, and is set only to 0.0 in one test; at 0 the degrees and radians readings coincide and `beam_angle()`'s `(pi/180)*static_roll` term is also 0. Not fixed, per operator triage. (e) Build and the full suite re-run here: 617 tests, 0 errors, 0 failures, 70 skipped. (f) No producer or consumer of `cube::Platform` exists outside `DetectionsProjector` anywhere in `layers/main/*/src/`.

**Adjudicated against a specialist.** The Governance pass raised the store/tile UINT8 `uncertainty` band (fixed 0.05 m scale, ADR-0001) as newly under-resolved. Checked and **dropped**: that band carries `stddev_to_confidence_interval_scale * sqrt(input_sample_variance)`, and `input_sample_variance` is a running sample variance of observed depths about the estimate (`hypothesis.cpp:102`), not the TPU. Affected only second-order, through which soundings the monitor admits — already covered by the tuned-constants consequence the divergences doc records.

### Findings
- [x] (must-fix) `Platform::pitch`'s documented sign is false and the claim is advertised as verified: `tf2::getEulerYPR` on `level_frame <- base_link` gives `pitch = -asin(R[2][0])`, so under REP-103 FLU bow-up is NEGATIVE pitch. Calder's `Platform` documents "+ve is bow up" and three ported terms are ODD in `sinP` (`error_model.cpp:115-117`, `:157-163`, `:178-180`), so the sign bites as soon as any IMU/GPS lever arm is non-zero — latent only because they all default to 0 and #145 is what will give them a surface. Decide: negate at the producer, or restate the convention and re-derive. No test can catch it (pitch reaches `vertical_error` only via the even `cos_pitch^2`). Derived independently by the lead reviewer and by Lens A. The ROLL half of the claim is correct — verified twice. — `cube_bathymetry/include/cube_bathymetry/error_model.h:69-72`
- [x] (must-fix) `swath_depth`'s pitch term uses `cos_pitch^2` where Calder's Eqn. 3.49 (`original_cube/libsrc/errmod/errmod_full.c:376-377`) uses `cosT^2` = `cos(roll + meas_angle)^2`; the sibling terms in the same function are faithful and the divergence is in no doc. Pre-existing, but `sinP^2` was ~3283x too small before #147, so this branch is what turns the term on — in a mis-formed state that over-estimates by `1/cos^2T` (~4x at 60 deg), biased to the swath edge. Fix, or record it in the divergences doc. — `cube_bathymetry/src/error_model.cpp:359-361`
- [x] (must-fix) the `/12`-is-confirmed claim is over-broad and the quoted C is not verbatim: Calder's `rtn = bw/12.0` sits inside `if (SOUNDING_ISAMPDET(snd->flags))`; phase detections use `0.2*bw/sqrt(np)` (`original_cube/libsrc/ccom_core/device.c:807-820`, same shape at `:893` and `:938`; EM300 uses neither). The port applies `/12` unconditionally — an unrecorded divergence, in the document whose job is to enumerate them. Mark the block abridged, show the branch, restate the closure. — `cube_bathymetry/docs/divergences_from_calder.md:85-95,178-180`
- [x] (must-fix) the plan is out of sync with the diff — the entire diagnostics surface is missing: six changed files absent from Files to Change (`detections_projector.h/.cpp`, `detections_to_pointcloud.cpp`, `bag_to_geotiff.cpp`, `batch_regen_main.cpp`, `import_bag_main.cpp`, plus `test_detections_projector.cpp`), the new public API (`per_beam_beamwidth_usable`, `kMaxPerBeamBeamwidthRad`) unlisted, `report_projection_summary()` unmentioned, README missing from Documentation & Instruction Impact, and `plan.md:227-232` still calls the constant-depth test a two-point comparison after `f579876` made it three and pinned the dip. Cross-confirmed by Plan Drift and Governance. — `.agent/work-plans/issue-144/plan.md:227-232,279-287`
- [x] (suggestion) `rejected_beamwidths` is silent in the operational case it exists for: `kongsberg_em_bridge` leaves `rx_beamwidths` EMPTY on every M3 ping, so every tool prints "0 rejected" while 100% of beams run on the hardcoded generic 2 deg; a single-element array (a legitimate fixed-beamwidth encoding) is the same. It also over-reports when the array is longer than the beam count and counts range-filtered beams. Count beams that TOOK THE FALLBACK, over `i < two_way_travel_times.size()`. Cross-confirmed by both adversarial passes, which each rated it must-fix; held at suggestion because the header states the counter's domain honestly. — `cube_bathymetry/src/detections_projector.cpp:143-149`
- [x] (suggestion) README's new warning says a consumer treating the value as sigma "will understate the error band by its own square root" — true only below 1.0. `horizontal_error` includes `total_gps_variance = gps_drms^2 = 4.0` m^2 at defaults, where reading it as a sigma OVERSTATES. Reword. — `README.md:18-27`
- [x] (suggestion) two lines above that bold variance warning, the same table advertises the `grid` output's `uncertainty` layer, which is the opposite convention — a confidence-scaled standard deviation in metres (`node.cpp:266`). One sentence closes the trap the block was written to prevent. — `README.md:12`
- [x] (suggestion) `soundingsToPointCloud2`'s comment claims "the same 6-field ... layout that detections_to_pointcloud publishes" — the exact count this PR just corrected in README to seven, in a file this PR edits. Corollary worth stating: the offline `-d` path therefore carries no `beam_angle`, which `cube_bathymetry_node` treats as optional and NaN-fills. — `cube_bathymetry/src/bag_to_geotiff.cpp:83-86`
- [x] (suggestion) `Platform::heave` is still documented "+ve down" while `DetectionsProjector` feeds `tide.transform.translation.z`, REP-103 +up. Harmless (it enters only squared), but this diff is the sign/units audit of this very struct. — `cube_bathymetry/include/cube_bathymetry/error_model.h:73`
- [x] (suggestion) "three of his NINE device families" — `device_compute_angerr`'s switch has EIGHT family branches (EM300; EM120; EM1000/1002/SB8101/SB8111; ELAC1180; SB9001/9003; HSWEEPDS/SB2112; EM3000/3000D; SB8125) plus `default`. Three of eight. Repeated in progress.md. — `cube_bathymetry/docs/divergences_from_calder.md:184`
- [x] (suggestion) "the four `* M_PI / 180.0` factors are gone" — six were removed: two in `swath_depth`, four in `compute`. Same miscount in the Implementation entry and in `plan.md:262,283`. — `cube_bathymetry/docs/divergences_from_calder.md:239`
- [x] (suggestion) the summary line reports a rejection count with no denominator; every other figure on the line is paired with a total. Print "N of M beams". — `cube_bathymetry/src/import_bag_main.cpp:785-791`
- [x] (suggestion) the run-summary line and its warning are now three verbatim copies across the three offline mains and only `import_bag` extracted a function; they will drift. Put `report_projection_summary()` in a shared header. (Checked: no early-exit path loses the count in any of the three.) — `cube_bathymetry/src/batch_regen_main.cpp:1074-1086`
- [x] (partially addressed: the shared summary keeps main() under the gate, but the underlying long-main pattern in all three tools is unchanged and unticketed) (suggestion) `main()` in `import_bag_main.cpp` is now 490 of cpplint's 500 counted lines — it was exactly 500 on `origin/jazzy`, so the extraction bought 10 lines and the next addition re-breaks the gate. `bag_to_geotiff` (416) and `batch_regen_main` (467) are the same pattern. — `cube_bathymetry/src/import_bag_main.cpp:808`
- [x] (suggestion) `detections_subscriber_` is created in `on_configure` and not reset in `on_cleanup`, so the callback — and the new warning — runs in `inactive`, where the LifecyclePublisher silently drops the cloud. Pre-existing, but the new warning fires every ping for a zero-filling driver, so the noise floor is much higher. Consider gating on `PRIMARY_STATE_ACTIVE`. — `cube_bathymetry/src/detections_to_pointcloud.cpp:150-154,202-210`
- [x] (suggestion) `rx_angles[i]` is indexed unchecked in `beam_angle()` and in the `Sounding` constructor, while `sounding.h:71` bounds-guards the identical access ("NaN when the source omits rx_angles for this beam") — so the short-array case was already anticipated. This branch is specifically about hardening against driver-supplied array shape. — `cube_bathymetry/src/error_model.cpp:235`
- [x] (suggestion) plan nits: the divergences doc is cited as `docs/...` where it is `cube_bathymetry/docs/...` with stale line ranges; "Five decisions are settled" then lists six; the Approach narrates three commits where six landed. — `.agent/work-plans/issue-144/plan.md:55,203,286,317,326`
- [x] (deferred: no PR exists yet and this sub-agent must not push; both items and the two-issue closing requirement are restated in the Implementation entry below for whoever opens it) (suggestion) before `gh pr create`, confirm the two operator-deferred round-1 items actually reach the PR body — nothing carries them today: the ADR-0003 mixed-store point (`build_fingerprint.json` unimplemented, `package.xml` still 0.0.0, so nothing invalidates pre-#144 tiles) and `Vessel::gps_drms = 2.0` dominating `horizontal_error` on RTK boats. The body must also close both `#144` and `#147`. — `.agent/work-plans/issue-144/progress.md`

## Implementation
**Status**: complete
**When**: 2026-09-10 12:58 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**Branch**: `feature/issue-144` at `887bc5c`
**Addressed**: `## Local Review (Pre-Push)` (round 2), 2026-09-10 11:57 -04:00, branch at `3e56683` — 4 must-fix, 14 suggestions
**Commits**: `0d4ca5c`, `abc0f1a`, `9c3fbd5`, `247412d`, `fe7a535`, `337535c`, `887bc5c`

### Build and test

Real results, from the worktree root after `source setup.bash`:

- `./sensors_ws/build.sh cube_bathymetry` — clean (the pre-existing GDAL
  `warn_unused_result` and two unused-parameter warnings are unchanged).
- `./sensors_ws/test.sh cube_bathymetry` — **624 tests, 0 errors, 0 failures, 71 skipped.**
  Baseline at the start of this pass was 617/0/0/70; +7 tests (three new ones, and the
  rewritten default-beamwidth test now covers four cases), +1 "skipped" which is cppcheck's
  per-file skip for the new header.

Two intermediate failures, both fixed rather than worked around, neither an assertion
loosened:

- `uncrustify` flagged a continuation indent in the new `rx_angles` guard in `sounding.h`.
  Re-indented to what uncrustify wants.
- One earlier run reported `test_import_eviction` as "the test did not generate a result
  file" (1 error, 1 failure). It passed on every subsequent run, including the two full
  clean runs above, and nothing in this pass touches import eviction — recorded as a flake,
  not investigated further.

### Actions

**Must-fix**

- [x] Pitch sign — implemented the operator's decision: negate at the producer.
  `DetectionsProjector` now assigns `platform.pitch = -p` from `tf2::getEulerYPR`, and
  `Platform` keeps Calder's bow-up-positive convention so every ported term stays faithful
  to the equation it came from. Roll untouched. Heave is negated at the same boundary for
  the same reason — Calder documents `+ve down` against the TF translation's REP-103 `+up`;
  numerically inert (heave enters only squared), but crossed conventions inside one struct
  are what produced the pitch defect in the first place. The "confirmed rather than
  assumed" doc comment now says which half was actually checked. New test
  `DetectionsProjectorTest.BowUpPitchFollowsCalderSignConvention` compares the projector's
  output against the error model driven with `pitch = +bow_up`, and asserts the two signs
  are distinguishable — it needs `gps_x/gps_z/imu_x/imu_z` non-zero, because the three
  terms odd in `sin(pitch)` are all multiplied by lever arms that default to zero.
  — `cube_bathymetry/src/detections_projector.cpp:114-140`,
  `include/cube_bathymetry/error_model.h:67-91` (`0d4ca5c`)
- [x] Eqn. 3.49's pitch term — implemented the operator's decision: fixed to match Calder.
  `cos_pitch^2` -> `cosT^2`, the same swath-geometry factor the two faithful siblings use
  (`errmod_full.c:376-377`). Pinned by
  `ErrorModelTest.DepthPitchTermUsesSwathAngleNotPitchCosine`, in closed form at a
  60-degree beam where the slipped form is 3.5x larger. Recorded in the divergences doc as
  new §2d, a **fixed porting error** in the same category as the two under #46, explicitly
  not a divergence. The commit message says it is pre-existing but that this branch makes
  it live. — `cube_bathymetry/src/error_model.cpp:359-368` (`abc0f1a`, `9c3fbd5`)
- [x] The `/12` claim — narrowed and re-quoted. The C block is now the verbatim
  `DEVICE_EM120` arm showing `if (SOUNDING_ISAMPDET(...)) rtn = bw/12.0; else rtn =
  0.2*bw/sqrt(np);`, and the note says EM300 uses neither. The unconditional `/12` this
  port applies is recorded as its own real divergence, with what it costs (on a phase
  detection Calder's sigma is `0.2*bw/sqrt(np)`, several times smaller for a typical `np`,
  so the port over-estimates) and why narrowing it is not an error-model change (neither
  the detection flag nor the window size exists in `SonarDetections`).
  — `cube_bathymetry/docs/divergences_from_calder.md:83-120,186-205` (`9c3fbd5`)
- [x] Plan out of sync — new **commit 2b** section covers the whole diagnostics surface
  (the two new public `ErrorModel` members, `ProjectionDiagnostics`, the new
  `projection_summary.h` and `report_projection_summary()`, the node's warning and its
  lifecycle gate, the `rx_angles` hardening), new **commit 2c** covers the round-2 operator
  decisions, six missing files plus the new header are in Files to Change, and README is in
  Documentation & Instruction Impact. The constant-depth test is described as the
  three-point comparison it became. — `.agent/work-plans/issue-144/plan.md` (`887bc5c`)

**Suggestions**

- [x] The default-beamwidth diagnostic — see the reconciliation note below.
  `rejected_beamwidths` became `default_beamwidth_beams`, counted over
  `two_way_travel_times` and reported as "N of M beams", with the denominator from
  `diagnostics.total`. Covers empty, short, over-long and range-gated cases in
  `DefaultBeamwidthBeamsAreCounted`. — `cube_bathymetry/src/detections_projector.cpp:164-183`
  (`247412d`)
- [x] README variance wording — both directions now stated, with the crossing point at
  1 m² and the `gps_drms^2 = 4 m^2` example that overstates. — `README.md:18-28` (`fe7a535`)
- [x] The `grid` layer's opposite convention — called out in the node table row, verified
  against `node.cpp:266` (`stddev_to_confidence_interval_scale * sqrt(...)`, metres).
  — `README.md:12` (`fe7a535`)
- [x] `bag_to_geotiff`'s stale "6-field" comment — it is the first six of seven; the
  corollary is stated (no `beam_angle` on the offline `-d` path, which
  `cube_bathymetry_node` NaN-fills — verified at `cube_bathymetry_node.cpp:2008-2061`).
  — `cube_bathymetry/src/bag_to_geotiff.cpp:83-95` (`fe7a535`)
- [x] `Platform::heave` "+ve down" — resolved by negating at the producer rather than by
  restating the convention, so the field keeps Calder's documented sense and the producer
  converts, matching the pitch decision. — `include/cube_bathymetry/error_model.h:80-82`
  (`0d4ca5c`)
- [x] "three of nine" -> **three of eight**, with the count justified in place
  (`device_compute_angerr`'s switch has eight family arms plus a `default`; counted at
  `original_cube/libsrc/ccom_core/device.c:791-970`).
  — `cube_bathymetry/docs/divergences_from_calder.md:210` (`9c3fbd5`)
- [x] "four factors" -> **six**, itemised (two in `swath_depth`, four in `compute`).
  — `cube_bathymetry/docs/divergences_from_calder.md:266` (`9c3fbd5`)
- [x] Rejection count with no denominator — the shared summary prints "N of M beams" in
  both the summary line and the warning. (`247412d`)
- [x] Three verbatim copies of the run summary — extracted to header-only
  `cube::report_projection_summary()` over a `ProjectionRunTotals` struct, in the new
  `include/cube_bathymetry/projection_summary.h`. All three tools now accumulate straight
  into that struct. Printed text is unchanged apart from the beamwidth clause;
  `bag_to_geotiff` sets `reports_georeferencing = false` and keeps its shorter line.
  — `cube_bathymetry/include/cube_bathymetry/projection_summary.h` (`247412d`, `337535c`)
- [x] (partially addressed) `main()` at 490 of cpplint's 500 lines — the extraction plus
  accumulating directly into `ProjectionRunTotals` keeps `import_bag_main`'s `main()` under
  the gate (cpplint passes), but the underlying long-`main` pattern in all three tools is
  untouched and has no ticket. Worth one if the next addition trips it.
- [x] `detections_subscriber_` runs in `inactive` — the callback now returns early unless
  the node is `PRIMARY_STATE_ACTIVE`. The subscription is still not reset in `on_cleanup`
  (that would change the configure/cleanup contract for no benefit now that the callback is
  inert), but the wasted projection and the per-ping warning are gone.
  — `cube_bathymetry/src/detections_to_pointcloud.cpp:172-181` (`337535c`)
- [x] `rx_angles[i]` indexed unchecked — bounds-guarded in both places (the `Sounding`
  geometry and `ErrorModel::beam_angle()`), matching the guards already on `tx_angles`,
  `intensities` and the `beam_angle` field two lines away. Absent is NaN, which propagates
  into position and TPU rather than reading off the end or being taken for a nadir beam.
  New test `ErrorModelTest.ShortRxAnglesYieldNaNRatherThanReadingOffTheEnd`.
  — `include/cube_bathymetry/sounding.h:47-72`, `src/error_model.cpp:231-240` (`337535c`)
- [x] Plan nits — paths corrected to `cube_bathymetry/docs/...` with the stale line ranges
  dropped, "Five decisions" now reads six (plus the two settled at round 2, listed
  separately), and the Approach narrates the nine commits that landed. (`887bc5c`)
- [x] (deferred: no PR exists yet and this sub-agent must not push) PR-body items. Restated
  here so whoever opens the PR carries them:
  1. The PR body must close **both** `#144` and `#147`.
  2. Operator-deferred round-1 item A — the ADR-0003 mixed-store point: `build_fingerprint.json`
     is unimplemented and `package.xml` is still `0.0.0`, so nothing invalidates tiles built
     before #144; a store can silently mix pre- and post-fix uncertainties.
  3. Operator-deferred round-1 item B — `Vessel::gps_drms = 2.0` dominates `horizontal_error`
     on RTK boats (4 m² of the budget at the default, before any other term).

### Reconciliation: the two specialist must-fixes the lead reviewer did not list

Asked to check how the round-2 lead disposed of the systemic-adversarial specialist's two
must-fixes (the diagnostic being silent for empty/short `rx_beamwidths`, and the counter
iterating the wrong domain). **They were not dropped.** The lead folded both into one
suggestion — the first `- [ ] (suggestion)` in the round-2 Findings — and gave the reason
explicitly: *"Cross-confirmed by both adversarial passes, which each rated it must-fix; held
at suggestion because the header states the counter's domain honestly."* The suggestion text
carries both halves, including the fix the specialists asked for ("Count beams that TOOK THE
FALLBACK, over `i < two_way_travel_times.size()`").

So there was a stated reason, and this pass followed the disposition — but the disposition
was to fix it, at suggestion priority, and pre-push suggestions are actioned. Fixed on the
merits regardless: an honest doc comment does not rescue a number that reads **zero for the
most common real configuration in the field**. `kongsberg_em_bridge` leaves `rx_beamwidths`
empty on every M3 ping, so 100% of M3 beams run on the hardcoded generic 2 degrees while
every tool printed "0 rejected per-beam beamwidths". The counter now measures the thing an
operator needs to know — how many beams' angular budget is a default — over the right
domain, and the single-element-array and over-long-array cases the specialists also named
are covered by tests.

### Deliberately not done

- **Correcting "three of nine" and "four factors" where they appear in earlier progress.md
  entries.** The review noted both figures are repeated there. progress.md is an append-only
  timeline; editing a past entry rewrites the record of what was believed at the time. The
  corrected figures (three of **eight**, **six** factors) are recorded here and in the
  divergences doc, which is the durable artefact.
- **Resetting `detections_subscriber_` in `on_cleanup`.** See above — the callback is inert
  outside `active`, which was the finding's actual concern.
- **Any store re-measurement or reprocessing**, still out of scope by the operator's
  standing decision (plan, "Out of scope").

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-10 13:14 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))
**Verdict**: changes-requested

**Branch**: feature/issue-144 at `45fadc3`
**Mode**: pre-push
**Depth**: Deep (reason: numerically load-bearing change to the per-sounding uncertainty budget CUBE consumes, cross-checked against the vendored Calder original)
**Must-fix**: 3 | **Suggestions**: 12
**Round**: 3 | **Ship**: recommended — every one of the three must-fixes is a doc-text correction with an obvious edit (a renamed field still named by its old name, a README table row whose sign this PR inverted, and one word in the plan that contradicts the divergences doc in the same PR). **No correctness defect was found in any code changed by this branch.** The five round-2 additions the operator asked to be scrutinised were each verified independently and all five hold.

Specialists: Static Analysis (clean), Governance, Plan Drift, Claude Adversarial Lens A + Lens B. Copilot and local-model passes off by default.

**The five round-2 additions, independently verified — all correct.** (a) **Pitch/heave negation.** The sign was re-derived from scratch by the lead reviewer and by Lens A: `tf2::getEulerYPR` returns `pitch = -asin(R[2][0])`, and for `R = Ry(θ)` the body +x maps to `(cosθ, 0, -sinθ)`, so positive tf pitch is bow-DOWN in FLU — the negation is right, and Calder's own `sounding.h:175` / `DCM_rthand.h:13` confirm "+ve is bow up". Roll correctly needs no flip. The three terms odd in `sin(pitch)` match Calder lines 510-511, 562-563, 577-579 term for term. Lens A verified the new test **empirically**: it removed the negation, rebuilt, and `BowUpPitchFollowsCalderSignConvention` failed on both assertions; the edit was reverted and the tree re-verified clean at `45fadc3`. (b) **Eqn. 3.49.** `errmod_full.c:376-377` reads `range^2 * cosT*cosT * sinP*sinP * total_pitch_var` with `cosT = cos(DEG2RAD(roll) + meas_angle)`; the port now matches exactly, and dropping `DEG2RAD` is the correct consequence of #147. The closed-form pin is genuinely independent, not self-referential: Lens A walked the constructor and confirmed every other contributor is exactly zero under the isolating vessel/device, so `vertical_error` really is the 3.49 term alone. (c) **`default_beamwidth_beams`.** The counter's predicate is the exact logical complement of the model's own branch, so the two cannot disagree; `compute()` emits one sounding per travel time with no skipping, so the `N of M` denominator can never be exceeded. All four cases are covered and the M3 (empty-array) case reports 3 of 3, not zero. (d) **`rx_angles` guard.** Those two sites were the only unguarded reads. The NaN does **not** poison the store: the projector's own range gate rejects it (NaN fails both comparisons), and `geo_grid.cpp:55-58`, `grid.cpp:53-56` and `cube_bathymetry_node.cpp:2036-2041` reject it again. (e) **`report_projection_summary()`.** Byte-for-byte behaviour-identical at all three sites — text, warning conditions, stream split, call position relative to early exits, and `int -> size_t` on a monotonic non-negative counter.

**The reported test flake is not a flake — it is a timeout, and the record should say so.** Established three ways independently. `CTestTestfile.cmake` sets `TIMEOUT 60` (the `ament_add_gtest` default; the target passes none). Run alone and unloaded on this host `test_import_eviction` takes **65.0 s** — over budget outright; Lens A measured 62.6 s, Lens B saw 8 of 15 cases consume 57.1 s before the kill. `test_batch_regen` takes 24.9 s and needs only ~2.4x contention to join it, which is what happened here with three test suites running at once. It is pre-existing (nothing on this branch touches import eviction) and needs its own issue — raise the `TIMEOUT` or split the three slow cases. The round-2 Implementation entry's "recorded as a flake, not investigated further" should be superseded by this.

**Adjudicated down.** Governance re-raised nothing settled. Plan Drift's "nine commits" reading was dropped: the plan enumerates nine *logical changes* and lists them, which is accurate. The `kMaxPerBeamBeamwidthRad` float-vs-`M_PI` rounding (float(pi) is ~1e-7 above pi, so a value in that sliver is accepted) is below threshold — physically meaningless.

### Findings
- [x] (must-fix) the divergences doc still names `ProjectionDiagnostics::rejected_beamwidths`, a field that no longer exists, and the bullet ("**Rejections are not silent**") re-asserts the rejection-only semantics round 2 deliberately replaced — reintroducing, in the accuracy-of-record document, exactly the "reads 0 for every M3 ping" misreading the rename was made to kill. Contradicts `detections_projector.h:84-104` and `detections_projector.cpp:164-183` in the same PR. Rewrite as the fallback count over `two_way_travel_times` with `total` as denominator. Found independently by the lead reviewer and Governance. — `cube_bathymetry/docs/divergences_from_calder.md:177-184`
- [x] (must-fix) README's pose-sourcing table still describes the boundary this PR inverted. The `heave` row says `z(base_link_frame) − z(tide_frame)`, while `detections_projector.cpp:150` now reads `-tide.transform.translation.z` — the row's implied sign is now backwards. The `roll`, `pitch` row states the TF source with no mention that pitch is negated at the producer, which is the one thing an integrator must know. This is the integrator-facing copy of the convention the whole PR is about; one clause per row closes it. — `README.md:237-238`
- [x] (must-fix) the plan says Calder applies the widening at "**three of his nine** device families" where the divergences doc, corrected in this same branch, says **eight** (`device_compute_angerr`'s switch has eight family arms plus a `default`) — the plan now contradicts the document it was resynced against, and will misdirect a reader of #148. The following clause, "the other five do not widen", is already consistent with eight. One word. — `.agent/work-plans/issue-144/plan.md:113`
- [x] (suggestion — cross-confirmed by both adversarial lenses; the strongest of these, a reviewer could reasonably promote it) a NaN sounding from the new `rx_angles` guard is dropped by the range gate and therefore counted as `filtered_range`. A driver that omits `rx_angles` entirely makes every sounding NaN, so the operator is told `0 soundings (all range-filtered)` and then warned to check their `--*-frame` overrides — a confident pointer at the one thing that is fine. The branch just built the right pattern for this class of problem; a sibling `missing_rx_angle_beams` counter and a summary clause would finish it. Also: `sounding.h:56-58` says the NaN "propagates into the position and the TPU", true inside `ErrorModel` but it never leaves the projector — the comment reads as though it reaches the product. — `cube_bathymetry/src/detections_projector.cpp:194,199`
- [x] (suggestion) the lifecycle gate reads `get_current_state()`, which returns a reference into the state machine that transitions mutate; rclcpp guarantees no thread-safety on it. It is safe only because `main()` uses a `SingleThreadedExecutor` and `change_state` shares the default callback group — an unstated invariant, one `MultiThreadedExecutor` away. `pointcloud_publisher_->is_activated()` is backed by `std::atomic<bool>` and expresses exactly the condition that matters. — `cube_bathymetry/src/detections_to_pointcloud.cpp:180`
- [x] (suggestion) a consequence of the new gate: `on_cleanup` frees nothing (it forwards to the base), so after cleanup the node still deserializes every ping — and the gate now suppresses `LifecyclePublisher`'s one-shot "publisher is not activated" WARN, so `inactive`/`unconfigured` with data flowing is now **entirely silent** where it previously said something. Either reset the subscriptions in `on_cleanup` (the override exists only as a no-op) or log once per deactivation. — `cube_bathymetry/src/detections_to_pointcloud.cpp:149-153,180`
- [x] (suggestion) `tx_angle` still defaults to `0.0f` two lines above the new `rx_angle` NaN guard, so the new comment's own rationale — "rather than being read as 0 (a nadir beam that was never measured)" — argues against its immediate neighbour. Missing `tx_angles` still yields a plausible-looking finite sounding the range gate keeps. — `cube_bathymetry/include/cube_bathymetry/sounding.h:50-60` (partly deferred: the asymmetry is now stated at the site, but tx keeps its 0 default — guarding it the same way would turn every sounding from an unsteered fan that omits tx_angles into a NaN the range gate drops, a field-behaviour decision rather than a review fix)
- [x] (suggestion) the sign test's self-guard does not guard what it claims: the discriminating separation is ~3.3e-5 relative on `horizontal_error` while `EXPECT_FLOAT_EQ` tolerates 4 ULP (~5e-7), and `EXPECT_NE` is *exact* inequality — a future change that shrank the separation below 4 ULP but kept it nonzero would leave `EXPECT_NE` passing while `EXPECT_FLOAT_EQ` passed against the wrong sign. Make the guard the same shape as the assertion (`EXPECT_GT(std::abs(diff), 1e-3 * expected)`), or raise the lever arms until the separation is percent-scale. — `cube_bathymetry/test/test_detections_projector.cpp:412-413`
- [x] (suggestion) "Before #144 they disagreed by `(pi/180)^2 == ~3283x`" — inverted; `(pi/180)^2` is 3.05e-4. The magnitude is right, the expression is not. Line added by this branch (`57e3835`). — `cube_bathymetry/test/test_error_model.cpp:671`
- [x] (suggestion) the header says the predicate is public so a caller can "count and report **rejections**" — the caller now counts fallbacks, of which rejections are a subset. Same one-word drift as the divergences-doc must-fix. — `cube_bathymetry/include/cube_bathymetry/error_model.h:287`
- [x] (suggestion) neither new operator-visible signal is in README: the throttled default-beamwidth warning and the offline "N of M beams on the default beamwidth" line. The operationally important corollary — until `marine_tools#82` lands, 100% of M3 beams run on a hardcoded generic 2 degrees belonging to no sonar — lives only in the divergences doc, which an operator reading the warning will not find. — `README.md:11-13,271-276`
- [x] (suggestion) `projection_summary.h` has no test, though it is new public API and the single reporting surface for all three offline tools, and both its "N of M" phrasing and the `reports_georeferencing = false` branch were review findings in their own right. A ~15-line `std::ostringstream` test pinning the two branches and the warning gate is cheap. — `cube_bathymetry/include/cube_bathymetry/projection_summary.h`
- [x] (suggestion) `projection_summary.h` is a private helper for three tool `main()`s but sits in the installed public include dir, and its `std::cout`/`std::cerr` default arguments force `<iostream>` (and a `std::ios_base::Init` static) into every TU that includes it. `src/` would be a better home; if it stays public, dropping the defaults drops the dependency. — `cube_bathymetry/include/cube_bathymetry/projection_summary.h:74-77` (partly deferred: the stream defaults and their `<iostream>` dependency are gone; the header stays in the public include dir — moving an installed header is a placement decision for the operator, and the three tools plus the new test all include it by package path)
- [x] (suggestion, repeat of round 2) `main()` in `import_bag_main.cpp` measures ~483 of cpplint's 500 counted lines (re-measured by the static pass with cpplint's own rule; the round-2 figure of 490 was an over-count). Still no ticket for the long-`main` pattern across the three tools. — `cube_bathymetry/src/import_bag_main.cpp:768-1451` (deferred: splitting a 483-line main is a refactor of its own, not a round-3 fix pass, and no ticket exists yet to hang it on — carried to the PR body as a follow-up to file)
- [x] (suggestion) the units/sign boundary rule — Device/Vessel degrees because human-entered config, Platform radians because measurement, signs stay Calder's and the producer converts — is stated five times in comments and in a Calder-comparison document, and nowhere a future author would look. `#145` adds the first human-entered device/vessel config surface, which is exactly where it has to be re-decided. A short repo ADR or two lines in the repo's `AGENTS.md` "Review Context" would make it discoverable. — `cube_bathymetry/AGENTS.md:44-51` (deferred: both remedies the finding offers — a repo ADR, or lines in the repo's AGENTS.md — are instruction-file/governance changes, which are Ask-First under the workspace rules; raised for the operator in the PR body instead)
- [x] (out of scope — needs its own issue, not this PR) `test_import_eviction` exceeds the 60 s `ament_add_gtest` default outright (65.0 s alone and unloaded on this host); `test_batch_regen` at 24.9 s joins it under load. Pre-existing; raise the TIMEOUT or split the slow cases. See the timeout note above. — `cube_bathymetry/CMakeLists.txt:420,429` (deferred: out of scope and pre-existing, per the round-3 note and the dispatch instruction — wants its own issue; timing re-measured in this pass, see below)
- [x] (out of scope — cross-repo follow-ups, cross-confirmed by both lenses) `marine_perception_tools/src/sounding_uncertainty.hpp:108-129` documents the #144 bug as *current* cube behaviour and quotes both branches this PR removed; its numbers now agree with cube exactly, but its rationale is false in every particular. `marine_perception_tools/src/mbes_geometry.hpp:85-86` resolves a missing `rx_angles[i]` to `0.0` while claiming to mirror the importer "exactly" — after this PR the viewer draws the beam at nadir where the importer drops it. Separate repo (mpt). (deferred: separate repo (mpt); carried to the PR body as cross-repo follow-ups)
- [x] (out of scope — follow-up) `imagenex_deltat/nodes/deltat.py:132-134` publishes `max(0.1, depth*0.01)` — a standard deviation in metres — into the same `vertical_uncertainty`/`horizontal_uncertainty` field names that this PR's README now makes normatively **variances in m²**, consumed as such at `cube_bathymetry_node.cpp:2058-2059`. Formalising the contract is the moment to file the mismatch. (deferred: separate repo; carried to the PR body as a follow-up)
- [x] (out of scope — untouched here, apparently unticketed) `Ping::detection_flags` ("0 means good") is declared and never read anywhere in the package, so sonar-flagged bad detections are filtered nowhere in the chain. — `cube_bathymetry/include/cube_bathymetry/error_model.h:249` (deferred: untouched by this branch and unticketed; carried to the PR body as a follow-up to file)
- [x] (PR-body carry items — nothing carries them today; no PR exists) (1) the body must close **both** `#144` and `#147`. (2) ADR-0003 mixed-store disclosure: `build_fingerprint.json` is unimplemented and `package.xml` is still `0.0.0`, and `StoreMetadata` has no version field either, so nothing invalidates pre-#144 tiles. (3) `Vessel::gps_drms = 2.0` contributes 4 m² to `horizontal_error` at the default on RTK boats. (4) name the `PRIMARY_STATE_ACTIVE` gate as a behaviour change belonging to neither closed issue. (5) **new, and deployment-relevant**: warm-start priming seeds variance from stored uncertainty and the blunder gate is `target - blunder_scalar*sqrt(var)` (`store_import.h:184-199`), so draft tiles already on the boats carry the pre-#144 inflated uncertainty and will warm-start the live node with a prior ~3 orders of magnitude too weak and a blunder gate ~57x too wide — blunder rejection effectively off wherever an old draft tile primes the surface. Separately, GeoTIFF import resolves contention by lowest uncertainty (`geotiff_import.hpp:39,108,160-164`), so post-fix cells will systematically beat pre-fix cells on the model change alone. This is a purge-or-regen-draft-tiles-at-deploy note, **not** the store re-measurement the operator dropped. (deferred: by construction these belong in the PR body, which does not exist yet; items (2), (3) and (5) are explicitly the operator's to decide and were left untouched here)

## Implementation
**Status**: complete
**When**: 2026-09-10 13:35 -04:00
**By**: Claude Code Agent (Claude Opus 5 (1M context))

**Branch**: feature/issue-144 at `e40e78c`
**Addressed**: `## Local Review (Pre-Push)` (round 3, 2026-09-10 13:14 -04:00, branch at `45fadc3`) — 3 must-fix + 12 suggestions + 4 out-of-scope/carry items
**Commits**: `1b29c22`, `d78bab2`, `c0b3edf`, `eab454a`, `2bea9af`, `88670db`, `88c29f9`, `aeb7b80`, `a8be2bd`, `e40e78c`

A deliberately narrow pass: round 3 verified every round-2 code change and found no
correctness defect, so the only substantive code change here is the receive-angle
diagnostic the review named as its strongest suggestion, plus the two lifecycle
follow-ups. Everything else is documentation, comments and tests.

**The one behavioural addition**: `ProjectionDiagnostics::missing_rx_angle_beams`. A beam
whose `rx_angles` entry is absent or non-finite becomes a NaN sounding that the range gate
drops, and that drop was reported only as `filtered_range` — so a driver that omits
`rx_angles` entirely produced "0 soundings (all range-filtered)" and a warning pointing the
operator at their `--*-frame` overrides, the one part of the configuration that is fine. The
counter is the exact sibling of `default_beamwidth_beams`: same loop, same domain
(`two_way_travel_times`), same `total` denominator, reported by the live node and all three
offline tools. The "0 soundings" warning now names `rx_angles` instead of the frames when
every beam lacked one.

**Two lifecycle follow-ups, both weighed rather than applied reflexively.** The activation
gate now reads `LifecyclePublisher::is_activated()` (`std::atomic<bool>`-backed) rather than
`get_current_state()`, which hands back a reference into the state machine with no
thread-safety guarantee — correct today only because `main()` spins a
`SingleThreadedExecutor`. The null check on the publisher additionally covers `unconfigured`,
where it does not exist. `on_cleanup` now resets the subscriptions, publisher, TF listener
and projector instead of forwarding to the base and freeing nothing; `on_configure`'s
`declare_parameter` calls are already `has_parameter`-guarded, so configure → cleanup →
configure works. Because the gate also suppresses `LifecyclePublisher`'s own one-shot "not
activated" warning, inactive-with-data now logs (throttled) rather than being entirely
silent. `detections_to_pointcloud` has no test target, so both are covered by the build and
by inspection only — worth a reviewer's eye.

**Build**: `./sensors_ws/build.sh cube_bathymetry` — clean. The remaining stderr is
pre-existing (GDAL `warn_unused_result`, an unused parameter in `swath_angle_error`, an
unused variable in `test_tile_eviction_rss`); no new warnings from this pass.

**Test**: `./sensors_ws/test.sh cube_bathymetry` — **637 tests, 0 errors, 0 failures, 72
skipped**; all 31 ctest targets Passed, including `copyright`, `cpplint`, `uncrustify`,
`lint_cmake`, `cppcheck`, `flake8`, `pep257`, `xmllint`. New: `test_projection_summary` (7
cases) and `DetectionsProjectorTest.MissingReceiveAngleBeamsAreCounted`.

**`test_import_eviction` timing, measured not assumed**: it **passed at 45.56 s** in this
run (load average ~2.6-3.9 on 16 cores) against the `TIMEOUT 60` that `ament_add_gtest`
sets at `CMakeLists.txt:426` — 76% of its budget, 14 s of headroom. `test_batch_regen` took
18.49 s. That does not contradict round 3's 65.0 s measurement so much as bound it: the test
is *marginal*, not flaky, and whether it lands at 45 s or 65 s depends on machine state. The
verdict stands — it wants its own issue to raise the TIMEOUT or split the slow cases, and it
was not touched here. No test was skipped, disabled or loosened in this pass.

### Actions
- [x] (must-fix) divergences doc named the removed `rejected_beamwidths` and re-asserted rejection-only semantics — `cube_bathymetry/docs/divergences_from_calder.md` (`1b29c22`)
- [x] (must-fix) README pose-sourcing table: heave sign inverted, pitch negation unstated — `README.md` (`d78bab2`)
- [x] (must-fix) plan said nine device families where the divergences doc says eight — `.agent/work-plans/issue-144/plan.md:113` (`c0b3edf`)
- [x] (suggestion, cross-confirmed) truncated/absent `rx_angles` reported as range-filtered — added `missing_rx_angle_beams` end to end, and corrected `sounding.h`'s "propagates into the position and the TPU" comment (`eab454a`)
- [x] (suggestion) lifecycle gate read the unsynchronised `get_current_state()` — now the publisher's atomic flag (`2bea9af`)
- [x] (suggestion) `on_cleanup` freed nothing, and the gate silenced the publisher's own warning — both fixed (`2bea9af`)
- [x] (suggestion) sign test's `EXPECT_NE` self-guard was a different shape from the `EXPECT_FLOAT_EQ` it guards (`88670db`)
- [x] (suggestion) `(pi/180)^2 == ~3283x` inverted (`88c29f9`)
- [x] (suggestion) `per_beam_beamwidth_usable`'s "rejections" wording (`88c29f9`)
- [x] (suggestion) neither new operator-visible signal was in the README (`aeb7b80`)
- [x] (suggestion) `projection_summary.h` had no test — 7 cases pinning both branches and the three warning gates (`a8be2bd`)
- [x] (suggestion) `projection_summary.h`'s `<iostream>` dependency — stream defaults dropped; **partly deferred**: the header stays in the public include dir, since moving an installed header is a placement decision for the operator
- [x] (suggestion) `tx_angle`'s 0 default beside the `rx_angle` NaN guard — asymmetry documented at the site; **partly deferred**: tx keeps the 0 default, because an unsteered fan legitimately omits `tx_angles` and NaN-guarding it would drop every one of its soundings. A field-behaviour decision, not a review fix (`e40e78c`)
- [x] (suggestion, repeat) `import_bag_main.cpp`'s 483-line `main()` — **deferred**: a refactor of its own, and no ticket exists to hang it on. Carried to the PR body
- [x] (suggestion) units/sign boundary rule is undiscoverable — **deferred**: both remedies the finding offers (a repo ADR, or lines in the repo's `AGENTS.md`) are instruction-file/governance changes, which are Ask-First. Raised for the operator in the PR body
- [x] (out of scope) `test_import_eviction` timeout — **deferred**: pre-existing, wants its own issue; re-timed here (45.56 s this run, see above)
- [x] (out of scope) `marine_perception_tools` `sounding_uncertainty.hpp` / `mbes_geometry.hpp` — **deferred**: separate repo
- [x] (out of scope) `imagenex_deltat` publishes a standard deviation into the variance fields — **deferred**: separate repo
- [x] (out of scope) `Ping::detection_flags` declared and never read — **deferred**: untouched by this branch, unticketed
- [x] (PR-body carry items) — **deferred**: no PR exists yet. Items (2) mixed-store disclosure, (3) `gps_drms`, and (5) draft tiles priming the blunder gate from pre-fix variance are explicitly the operator's to decide and were left untouched, per the dispatch instruction

### Not done, and worth the operator's attention
- The two lifecycle changes alter node behaviour (`on_cleanup` now tears down; the inactive
  path now logs at INFO) and have no automated coverage, because `detections_to_pointcloud`
  has no test target. They were applied because round 3 raised both and each closes a real
  gap, but they are the riskiest thing in this otherwise documentation-heavy pass.
- Nothing was done about the deployment consequence (draft tiles on the boats priming the
  blunder gate from pre-fix variance), the mixed-store disclosure, or `gps_drms` — all three
  await the operator's decision.
