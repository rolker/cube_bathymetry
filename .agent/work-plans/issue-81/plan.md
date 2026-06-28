# Plan: Apply deferred M3 backscatter angle-correction in shared CUBE estimator

## Issue

https://github.com/rolker/cube_bathymetry/issues/81

## Context

**Prior plan (cos²θ Lambert) SUPERSEDED.** Empirical analysis of `m3_dryrun`
`/detections` (2025 pings, 445k beams) showed cos²θ is the wrong model — it
explains only ~¼ of the observed nadir-to-edge falloff (~24 dB). The remaining
falloff is dominated by sonar-equation terms cos²θ ignores: insonified area,
beam-pattern, and TVG residual. The correct approach is an **empirical
angular-response (ARA/AVG) correction** driven by a per-sonar curve derived
offline from real data.

**Current state.** `Node::extractNodeRecord()` (`node.cpp:315-318`) contains a
Phase B no-op: `corrected == raw` for every beam. Per-beam
`{raw_intensity, beam_angle}` pairs are already stored on each hypothesis
(`Hypothesis::intensity_samples`).

**Design: configured curve + offline derivation tool, sonar-agnostic, default
OFF.** The estimator is sonar-agnostic (M3, Norbit, R2Sonic, sim, …) — no
correction model is baked in. An operator derives a curve for their sonar offline
and configures the path at startup; default OFF preserves the current Phase B
identity behavior on all existing deployments.

**Gate (already verified, document it).** `rx_angles` sign/zero confirmed against
`kongsberg_em_bridge`: Kongsberg pointing +PORT → negated → +STARBOARD; nadir=0;
radians; intensity in dB. Document as inline comment at the correction site.

**Rename needed.** `BeamIntensitySample::grazing_angle` (hypothesis.h:49) is
misnamed — it stores the receive steering / incidence angle from nadir (same as
`sounding.beam_angle`), NOT a true grazing angle (90°−θ). The field doc already
says "receive/steering (beam/incidence) angle" — the name is wrong, the comment is
right. Plan renames it to `beam_angle`.

## Approach

0. **Rename `BeamIntensitySample::grazing_angle` → `beam_angle`** in:
   - `hypothesis.h:49` (struct field decl)
   - `hypothesis.h:115-116` (doc text "a NaN grazing_angle is retained")
   - `hypothesis.h:166` (BeamIntensitySample reference in the memory-budget comment)
   - `recordBeam` declaration (`hypothesis.h:118`) and implementation
     (`hypothesis.cpp:118, 122, 127`) — parameter name
   - `node.cpp` call-site comment
   - `test_hypothesis.cpp:216, 218, 243` — `grazing_angle` field refs

1. **Add `BackscatterAngleCorrection` to `Parameters` (`parameters.h`)**:
   ```
   enum class BackscatterAngleCorrection { None, Empirical };
   BackscatterAngleCorrection backscatter_angle_correction =
       BackscatterAngleCorrection::None;
   std::vector<std::pair<float,float>> angular_response_curve; // {abs_angle_deg, db_relative_to_nadir}
   ```
   The curve vector is populated at startup from the curve file; empty → correction
   is a no-op even if mode is Empirical (log a warning). No new dependencies needed
   (standard library only).

2. **Plumb the parameter on both estimator entry points:**
   - **Live node** (`cube_bathymetry_node.cpp`): declare ROS params
     `backscatter_angle_correction` (string `"none"|"empirical"`, default `"none"`)
     and `backscatter_curve_file` (string path, default `""`). At `on_configure()`,
     parse the mode enum, load the curve file (CSV) into
     `Parameters::angular_response_curve` if mode is Empirical and path is non-empty.
   - **Offline import** (`import_bag_main.cpp`): add `--backscatter-correction
     none|empirical` (default `none`) and `--backscatter-curve <file>` (default
     empty) to `usage()` and arg parsing. Load the curve the same way at startup.
   Both run the same estimator → one mechanism corrects both live (#78) and offline
   (#80) paths.

3. **Replace the Phase B no-op in `node.cpp:315-318`** with the ARA correction:
   ```cpp
   // ARA correction: corrected_dB = raw_dB − curveRel(|beam_angle|), where
   // curveRel is the empirical angular-response curve's db_relative_to_nadir
   // column, linearly interpolated between bin centres. Nadir → curveRel≈0 →
   // identity. Beyond the curve's max angle OR NaN beam_angle → identity.
   // rx_angles sign/zero: producer (kongsberg_em_bridge) negates Kongsberg
   // pointing angle (+PORT→+STARBOARD; nadir=0; radians; intensity dB).
   ```
   Logic: if `mode == Empirical && !isnan(beam_angle) && curve not empty`:
   - `theta = std::abs(beam_angle)` converted to degrees
   - If `theta > curve.back().first` → identity (beyond curve extent)
   - Else linearly interpolate `curveRel` from adjacent bin centres
   - `corrected = raw − curveRel`
   - Else: `corrected = raw` (None mode, NaN angle, or empty curve)
   Update the stale `TODO(#54-B / #15)` comment block (`node.cpp:298-312`): Phase B
   flat-bottom done via ARA; retain note for the full radiometric GeoCoder (#15/#59).

4. **Offline derivation tool** — Python script
   `cube_bathymetry/scripts/derive_angular_response.py`:
   - Reads a bag's `SonarDetections` topic
   - Bins `intensities` by `|rx_angles|` (converted to degrees, 2° bins)
   - Computes mean backscatter per bin; computes `db_relative_to_nadir` (0°-bin
     mean − per-bin mean; nadir bin ≈ 0, off-nadir bins ≤ 0)
   - Writes CSV with columns: `abs_angle_deg_center, mean_bs_db, n,
     db_relative_to_nadir` (matches the host-written seed at
     `~/data/logs/analysis/m3_angular_response_curve.csv`)
   - CLI: `derive_angular_response.py <bag> -t <detections_topic> -o <out.csv>`
   - Install via `ament_python_install_package` or list in `CMakeLists.txt`
     `install(PROGRAMS ...)`

5. **Tests** (`test_node.cpp`):
   - **Default None path**: confirm existing intensity assertions (`FirstBeamInitializationRecordsIntensity`,
     `NodeRecordMeanAndEstimateVariance`, `NodeRecordSkipsNanIntensityBeam`) remain
     unchanged — they do NOT opt into the correction, so `corrected == raw` on the
     default path. No assertion changes needed.
   - **Empirical-ON tests** (pass a Parameters with mode=Empirical and a known
     2-point curve):
     - `ARAInterpolation`: 2-point curve at 0° and 60° → mid-angle (30°) exact
       interpolated subtract; `corrected ≠ raw`.
     - `ARANadirIdentity`: θ≈0 (nadir bin centre) → curveRel≈0 → `corrected ≈ raw`.
     - `ARABeyondMaxAngleIdentity`: θ > curve max → identity.
     - `ARANaNAngleIdentity`: NaN beam_angle → identity.
     - `ARAOffIsIdentity`: mode=None, non-nadir beam → `corrected == raw`.
     - `ARAPortStarboardSymmetry`: ±θ same magnitude → same corrected value
       (curve keyed on `|angle|`).

6. **`test_store_import.cpp` (#80)**: default None means `BackscatterCellsMatchGridRecords`
   stays valid (no correction applied). Update the stale comment at lines 64-67
   ("the surfaced value is uncorrected so the angle does not change it here") to
   say "by default (BackscatterAngleCorrection::None)".

7. **ADR-0007 transition addendum** —
   `docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md`
   (sibling addendum name; does **not** claim the `0007-` canonical slot before
   the full ADR exists). Records: Phase B end, the ARA correction and offline tool,
   the configured-curve + default-off rationale, what remains deferred (full
   radiometric GeoCoder: area + beam-pattern + TVG + slope; ADR-0007 D3 / #15 / #59).
   **Note in the addendum**: file a follow-up to author the full ADR-0007 document
   and a follow-up for intensity-domain (dB vs linear) as a first-class sonar
   property.

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/hypothesis.h` | Rename `grazing_angle` → `beam_angle` (struct field + doc at :115-116, :166 + `recordBeam` decl at :118) |
| `src/hypothesis.cpp` | Rename `grazing_angle` param in `recordBeam` impl (:118, :122, :127) |
| `include/cube_bathymetry/parameters.h` | Add `BackscatterAngleCorrection` enum + field (default None) + `angular_response_curve` vector |
| `src/node.cpp` | ARA correction with gate comment; update TODO block |
| `src/cube_bathymetry_node.cpp` | Declare/parse `backscatter_angle_correction` + `backscatter_curve_file` ROS params; load curve |
| `src/import_bag_main.cpp` | `--backscatter-correction` + `--backscatter-curve` flags + `usage()` text; load curve |
| `scripts/derive_angular_response.py` | New: offline curve-derivation tool |
| `CMakeLists.txt` | Install `scripts/derive_angular_response.py` |
| `test/test_node.cpp` | 6 ARA/param tests; verify 3 existing assertions unchanged on default path |
| `test/test_hypothesis.cpp` | `grazing_angle` → `beam_angle` refs (:216, :218, :243) |
| `test/test_store_import.cpp` | Update stale comment at :64-67 |
| `docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md` | New: addendum |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Default OFF; operator must explicitly enable and supply a curve file — no silent correction. |
| Enforcement over documentation | 6 targeted tests catch wrong interpolation, missing NaN/beyond-max guards, and the default-off path; port/starboard symmetry. |
| Only what's needed | ARA only; full radiometric GeoCoder (area + beam-pattern + TVG + slope) is explicit follow-up. Offline tool and correction plumbing together in one PR (tight scope). |
| A change includes its consequences | Parameters plumbed through both estimator entry points; rename covers all refs including header docs and tests; stale comment updated; addendum captures the decision. |
| Capture decisions | ADR-0007 transition addendum records the ARA design, offline-tool format, default-off rationale, and deferrals. |
| Improve incrementally | Phase B no-op → ARA (here) → full GeoCoder (#15/#59). Small, reviewable PR. |
| Test what breaks | 3 existing tests verified unchanged on default path; 6 new tests on Empirical path; rename covered by test_hypothesis.cpp update. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0007 D3 (incidence correction at node-output) | Yes — primary | ARA correction implemented (param-gated) at `extractNodeRecord()` |
| ADR-0007 D2/D4 (per-beam mean + estimate variance) | Yes — already in place | `corrected` replaces `raw` in the sum; combine unchanged |
| ADR-0008 (ROS 2 conventions) | Yes | New node params declared with defaults; offline CLI flags documented in `usage()` |
| ADR-0001 (adopt ADRs) | Yes | Transition addendum in `docs/decisions/`; follow-up to author full ADR-0007 |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `grazing_angle` → `beam_angle` rename | hypothesis.cpp, test_hypothesis.cpp, node.cpp comment, header docs :115-116, :166 | Yes — Step 0 |
| Add `BackscatterAngleCorrection` to Parameters | node ROS params + import_bag CLI + parameters.h | Yes — Steps 1-2 |
| ARA correction at node-output | test_node.cpp (6 new tests; 3 existing verified unchanged) | Yes — Step 5 |
| Default None preserves current behavior | test_store_import.cpp stale comment fixed | Yes — Step 6 |
| Phase B ends | ADR-0007 transition addendum + follow-up issues noted | Yes — Step 7 |
| Full radiometric GeoCoder | #15, #59; ADR-0007 full doc | No — explicit follow-up |
| Intensity domain (dB vs linear) as sonar property | Separate issue | No — noted in addendum |

## Open Questions

- [ ] **Script install method**: add `derive_angular_response.py` via
  `install(PROGRAMS ...)` in CMakeLists.txt or via `ament_python_install_package`?
  Prefer `install(PROGRAMS ...)` (single-file, no Python package structure needed)
  — confirm if the team has a house standard.
- [ ] **Curve file format**: CSV was chosen (matches host-written seed). Confirm
  whether YAML would be preferred for consistency with other ROS parameter files.
  CSV is simpler for the offline tool to write; either is trivially parseable.

## Estimated Scope

Single PR, one package. ~180 lines changed (plumbing + correction logic + rename)
+ ~120 new test lines + ~80 lines for the Python script + ADR addendum.
Rename (`grazing_angle` → `beam_angle`) is a clean orthogonal split candidate if
the PR grows unwieldy, but it fits the scope as written.
