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
