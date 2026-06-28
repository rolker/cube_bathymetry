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

## Plan Authored
**Status**: complete
**When**: 2026-06-28 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `cube_bathymetry/.agent/work-plans/issue-81/plan.md` at `94938a6`
**Branch**: feature/issue-81 at `94938a6`
**Phases**: single

### Open questions
- [ ] Formula constant: confirm from Kongsberg EM Datagram Formats that `reflectivity_db` is raw backscatter without Lambert normalization in TVG, to avoid double-counting the cos θ correction.

## Plan Review
**Status**: complete
**When**: 2026-06-28 10:50 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent: plan authored by a Claude Sonnet sub-agent; this review is a
     fresh-context Claude Opus sub-agent. Different model + fresh context →
     genuine second opinion, so no author-self-review annotation. -->

**Plan**: `.agent/work-plans/issue-81/plan.md` at `9efe563`
**PR**: PR-less (--issue mode)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Lambert constant unresolved + physically shaky justification: plan picks one-way `10·log10(cos θ)` arguing "transmit spreading is in TVG, only receive incidence is uncorrected" — but the Lambert `cos²θ` is a single seabed-scattering term (projected area × Lambertian emission), not a transmit/receive split, and TVG compensates range-dependent spreading/absorption, not angular incidence. Classic Lambert backscatter normalization is `20·log10(cos θ)` (=`10·log10(cos²θ)`). Resolve the load-bearing open question (`plan.md:149-153`) BEFORE implementing, not in parallel — `plan.md:31-47`
- [ ] (must-fix) Near-grazing guard is internally contradictory: derivation says "clamp θ to a maximum of MAX_INCIDENCE_RAD=80° before computing" (→ finite +correction at the cap) while the same bullet and the `LambertCorrectionNearGrazing` test say beams beyond max are "emitted uncorrected (identity)". Clamp-θ vs identity-beyond-max give different values and a discontinuity. Pick one, state it unambiguously, align the test — `plan.md:52-54`, `plan.md:89`
- [ ] (must-fix) Test-update list under-enumerates broken assertions: Step 3 names only `NodeRecordMeanAndEstimateVariance`, but two more non-nadir (0.1 rad) intensity assertions also break under the correction — `FirstBeamInitializationRecordsIntensity` (`test_node.cpp:382`, `EXPECT_FLOAT_EQ(record.intensity, -30.0f)`) and `NodeRecordSkipsNanIntensityBeam` (`test_node.cpp:420`). At 0.1 rad the shift is ≈0.022 dB, which fails both `FLOAT_EQ` and the `1e-4` `EXPECT_NEAR`. Enumerate all three. (Note: the variance assertion in `NodeRecordMeanAndEstimateVariance` still holds — all four beams share one angle, so the offset cancels; only the mean assertion changes.) — `plan.md:79-82`
- [ ] (suggestion) `test_store_import.cpp` Step 4 misses a stale comment: lines 64-67 explicitly assert "the surfaced value is uncorrected so the angle does not change it here" — that becomes false once the correction lands. Update the comment regardless of which fix is chosen. Prefer updating the assertion to the corrected value over zeroing `beam_angle` to 0.0f, so a non-nadir beam still exercises the offline-store path — `plan.md:91-97`
- [ ] (suggestion) The `grazing_angle → beam_angle` rename should also cover the header doc comments that mention the old name — `hypothesis.h:115-116` ("a NaN grazing_angle is retained") and the `BeamIntensitySample` reference at `hypothesis.h:166`; Step 1's file list cites only the field decl, recordBeam sig/impl, node.cpp:308, and tests — `plan.md:64-68`
- [ ] (suggestion) ADR-0007 transition-note filename `0007-mbes-backscatter-store-phase-b-transition.md` claims the `0007-` slot before the full ADR-0007 doc exists; confirm the planned follow-up's full doc won't collide (or intend it as a sibling addendum) — `plan.md:99-104`

## Plan Review
**Status**: complete
**When**: 2026-06-28 11:20 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent re-review of the revised plan. Plan authored by a Claude Sonnet
     sub-agent; this is a fresh-context Claude Opus sub-agent (different model +
     fresh context → genuine second opinion), so no author-self-review annotation
     — mirrors the precedent set in the prior Plan Review entry above. -->

**Plan**: `.agent/work-plans/issue-81/plan.md` at `0387a24`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

Re-review after the plan was revised (`0387a24`) to address the prior
`changes-requested` review. All prior findings resolved, verified against live
source: ✓ formula → cos²θ / `−20·log10(cos θ)` (classic single-term Lambert seabed
law, replacing the unsound one-way ×10); ✓ near-grazing now a single unambiguous
rule (identity beyond 80°, no clamp/identity contradiction); ✓ all three broken
non-nadir assertions enumerated (`test_node.cpp:382`, the `:401` mean,
`:420`); ✓ `test_store_import` stale comment; ✓ rename covers `hypothesis.h:115-116`
and `:166`; ✓ addendum filename relabeled. Line numbers checked: no-op site
`node.cpp:315-318`, `grazing_angle` at `hypothesis.h:49`, `recordBeam` decl `:118`
+ impl `hypothesis.cpp:118/122/127`, both entry points present, ADR-0007 has no
file yet (only `0001-` exists). Formula has no objections.

### Findings
- [ ] (suggestion) Step 3's "fix 3 broken non-nadir assertions" is now internally inconsistent with the default-off parameter design: under default `None`, `FirstBeamInitializationRecordsIntensity` (`test_node.cpp:382`), the `NodeRecordMeanAndEstimateVariance` mean (`:401`), and `NodeRecordSkipsNanIntensityBeam` (`:420`) do NOT break (`corrected == raw` when correction is off). They need no change; only the 6 new Lambert-ON tests assert corrected values. The "fix 3 broken" wording is carried over from the pre-parameter review — clarify that those three stay unchanged on the default path — `plan.md:112-121`
- [ ] (suggestion) Confirm the operator endorsed the **default-off** parameter design before implementing. It expands scope beyond the dispatched SCOPE BOUNDARY (which names no parameter) and changes #81's outcome: by default the no-op identity ships unchanged, so #78/#80 are corrected only when the flag is enabled. The plan attributes default-off to an operator decision (`plan.md:198-201`) but the dispatch's listed operator decisions cover only the gate-PASS and the ADR note. Design is defensible (sonar-agnostic; avoids double-counting a possibly pre-normalized reflectivity) — just verify the endorsement — `plan.md:24-29`, `plan.md:80-92`
- [ ] (suggestion, optional) Scope at upper bound (10 files). The `grazing_angle → beam_angle` rename is orthogonal to the correction and a clean split candidate if the PR grows large — not required — `plan.md:94-99`

## Plan Authored
**Status**: complete
**When**: 2026-06-28 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-81/plan.md` at `d1eacd3`
**Branch**: feature/issue-81 at `d1eacd3`
**Phases**: single

### Open questions
- [ ] Script install method: `install(PROGRAMS ...)` in CMakeLists.txt vs `ament_python_install_package` for `derive_angular_response.py` — confirm house standard.
- [ ] Curve file format: CSV chosen (matches host-written seed) — confirm whether YAML is preferred for consistency with ROS parameter files.

## Plan Review
**Status**: complete
**When**: 2026-06-28 12:23 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent: latest plan authored by a Claude Sonnet sub-agent; this is a
     fresh-context Claude Opus sub-agent (different model + fresh context →
     genuine second opinion), so no author-self-review annotation. -->

**Plan**: `.agent/work-plans/issue-81/plan.md` at `d1eacd3`
**PR**: PR-less (dispatched re-plan review)
**Verdict**: changes-requested

Re-review of the new empirical-ARA plan (`d1eacd3`) that replaced the superseded
cos²θ plan. Verified against live source. The plan is thorough and faithful to the
operator-confirmed re-plan; all prior-review carry-overs are resolved (formula now
moot under ARA; default-off now operator-confirmed in dispatch; `|beam_angle|` use,
port/starboard symmetry test, and ADR addendum all present). One structural
file-targeting gap blocks implementation-readiness: the new correction parameters
have no plumbing path to the estimator.

### Findings
- [ ] (must-fix) No API path from the app entry points to the new `Parameters` fields: `Parameters parameters_` is a **private** member of `GeoMapSheet` (`geo_map_sheet.h:102`), built internally from `(cell_size, iho_order)` (`geo_map_sheet.cpp:34`). Live node constructs the sheet with cell-size only (`cube_bathymetry_node.cpp:89`); offline tool with `(resolution, iho_order)` (`import_bag_main.cpp:411`). Step 2 ("load the file into `Parameters::angular_response_curve`") has nothing to write to. Add `geo_map_sheet.{h,cpp}` to Files-to-Change + Consequences with a ctor-arg-or-setter; decide whether the legacy Cartesian `MapSheet` path needs the same. Note: `test_node.cpp` calls `extractNodeRecord(params)` with a directly-built `Parameters` (`test_node.cpp:44,379`), so unit tests bypass this gap — it surfaces only as a no-effect correction in real wiring — `plan.md:60-71`, `plan.md:134-149`
- [ ] (suggestion) Stale comments beyond the listed ones: `import_bag_main.cpp:72-74` (`--bs-store` help, "UNCORRECTED intensity") and `test_store_import.cpp:271,303` ("uncorrected") should become "...by default" — plan cites only `test_store_import.cpp:64-67` — `plan.md:119-122`
- [ ] (suggestion) Rename should cover both `grazing_angle` sites in `node.cpp` — the prose at `:299` and `{raw_intensity, grazing_angle}` at `:308` (the only refs; params at `:71/:213` are already `beam_angle`) — `plan.md:40-47`
- [ ] (suggestion) `parameters.h` needs `<vector>` and `<utility>` added for the new `std::vector<std::pair<float,float>>` field (currently only `<cstdint>`,`<limits>`,`<string>`) — `plan.md:49-58`

### Next step
Lifecycle: **Plan Review** → **implement** → **review-code**. Verdict is
changes-requested: the implementer (or plan author) should add `geo_map_sheet.{h,cpp}`
plumbing to the plan and address the must-fix before/while implementing; the
suggestions are minor comment/include hygiene.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-28 13:52 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-81 at `271e52f`
**Mode**: pre-push
**Depth**: Deep (reason: 1325 changed lines / 19 files / new ADR addendum)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 1 | **Ship**: recommended — no must-fix; 2 minor suggestions (edge-clamp design + a misleading comment), all plan-review findings resolved

### Findings
- [ ] (suggestion) `curveRelativeDb` clamp asymmetric: below-first-bin clamps to `curve.front().second` (assumes nadir bin), beyond-last-bin returns 0 (identity) creating a discontinuity at the outermost bin — clamp to `curve.back().second` or assert/doc the nadir-anchored curve — `cube_bathymetry/src/node.cpp:47`
- [ ] (suggestion) Loader comment overstates the `a_used`/`d_used` guard: `std::stof` parses a numeric prefix, so `"1deg"` is accepted as `1.0`, not rejected — comment-accuracy nit — `cube_bathymetry/src/angular_response_curve.cpp:93`

### Notes
- Base scope: local `gitcloud/jazzy` mirror is stale (predates merged #80 / PR #82 at `7467e1b`); review scoped to the #81-only diff `7467e1b...HEAD` (19 files, +1325 −39). The bundled #80 portion was already reviewed/integrated.
- Deep review: 2 fresh-context Claude Adversarial passes (Lens A logic + Lens B systemic) — both clean, no must-fix. Cross-pass confirmation on the curve-edge clamp (suggestion 1). Copilot off (default).
- Static analysis clean: ament_cpplint, ament_uncrustify, ament_cppcheck (slow-version override), ament_flake8 (Python tool) all "No problems found".
- Tests **pass** against built binaries: `NodeTest.ARA*` (6/6) and `AngularResponseCurve.*` (4/4).
- Verified correct: rad→deg units + sign convention (`std::abs(beam_angle)`); interpolation incl. duplicate-angle (`span<=0`) guard; mean + variance-of-mean math (clamped ≥0); Python writer columns (0=center,3=db_rel) match C++ loader reads; correction applied once at `extractNodeRecord` (warm-start/eviction reload reseeds depth only — no double-correction); SingleThreadedExecutor → no param-mutation race.
- Plan adherence: full. Plan-review must-fix (geo_map_sheet plumbing) resolved; setter ordering verified before any grid processes soundings on both live + offline paths. `<vector>`/`<utility>` includes present.
- Pre-existing (not a #81 finding): configure→cleanup→configure would throw `ParameterAlreadyDeclaredException` — node-wide pattern, not introduced here.

### Next step
Lifecycle: **Local Review** → push / open PR → **triage-reviews**. Verdict is
**approved** with no must-fix — the diff is shippable; the 2 suggestions can be
applied or tracked. Next phase is dispatched by the host (`/run-issue`).
