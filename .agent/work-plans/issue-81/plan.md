# Plan: Apply deferred M3 backscatter angle-correction in shared CUBE estimator

## Issue

https://github.com/rolker/cube_bathymetry/issues/81

## Context

`Node::extractNodeRecord()` (node.cpp:315-318) contains a Phase B no-op where
`corrected == raw` for every beam. The per-beam `{raw_intensity, beam_angle}`
pairs are already stored on each hypothesis (`Hypothesis::intensity_samples`).

**Gate (rx_angles sign/zero convention) — VERIFIED host-side, PASS.**
`kongsberg_em_bridge/node.py:364-373` negates the Kongsberg raw pointing angle
(`+PORT → +STARBOARD`, matching `marine_acoustic_msgs`); nadir = 0 (boresight;
physical mount is in TF), radians. For a flat-bottom Lambert using `|angle|` the
sign is moot (cos is even); nadir=0 and `|angle|`=incidence-from-nadir both hold.

**Is `reflectivity_db` already angle-normalized? — operator-controlled, NOT
hardcoded (operator decision).** `em_datagrams.py:120-126` reads `reflectivity_db`
straight from the EM **"Raw Range and Angle 78"** datagram (signed 16-bit ×0.1 dB)
with **no angle compensation in the bridge**. Whether the *sonar firmware*
pre-normalized that reflectivity for incidence is sonar/config-dependent and is
NOT determinable from workspace code. **This estimator is sonar-agnostic** (it
also runs Norbit, R2Sonic, sim, …), so whether to apply the Lambert correction
**must be an operator-set startup parameter**, not an M3 assumption baked into the
shared estimator. Default is **off (identity = current Phase B behavior)** — safe
and sonar-agnostic; a platform/config enables `lambert` for a sonar known to
deliver raw (non-angle-normalized) backscatter.

The `BeamIntensitySample::grazing_angle` field (hypothesis.h:49) is **misnamed**:
it stores the receive steering / incidence angle from nadir (same semantics as
`sounding.beam_angle`), NOT a true grazing angle (90° − θ). The field doc already
says "receive/steering (beam/incidence) angle", so the comment is correct but the
name is wrong. Plan renames it to `beam_angle`.

ADR-0007 has no document file in `docs/decisions/` yet; this PR adds a Phase B
transition **addendum** (not claiming the `0007-` slot) and files a follow-up
task for the full ADR-0007 doc.

## Lambert Formula (operator-confirmed cos²θ)

`raw_intensity` is `reflectivity_db` (dB). The angular correction is **additive in
dB** and applied ONLY when the operator parameter selects `lambert`:

```
corrected_dB = raw_dB − 20·log10(cos θ)      θ = std::abs(sample.beam_angle)
```

**Form rationale (cos²θ / factor 20 — the classic seafloor Lambert law,
operator-confirmed; the reviewer flagged the earlier ×10 justification as
physically unsound):**
- Lambert backscatter cross-section per unit area `σ(θ) ∝ cos²θ` — a single
  seabed-scattering term (insonified-footprint projection cosine × Lambertian
  re-emission cosine), NOT a transmit/receive split. In dB:
  `BS(θ) = BS(0) + 20·log10(cos θ)`. TVG handles range spreading/absorption, not
  this angular factor — so it is not already removed.
- Normalizing to the nadir-equivalent ⇒ subtract the angular term:
  `corrected_dB = raw_dB − 20·log10(cos θ)`.
- Nadir (θ=0): `cos0=1`, `log10(1)=0` → correction 0 (identity). ✓
- Off-nadir (θ>0): `cos θ<1` → `−20·log10(cos θ) > 0` → boosts the weaker
  off-nadir return toward its nadir-equivalent.

**Near-grazing handling — single unambiguous rule (resolves the prior
contradiction):** beams with `θ ≥ MAX_INCIDENCE_RAD` (= 80° = 1.39626 rad) are
emitted **UNCORRECTED (identity)** — there is **no θ-clamping**. Rationale: the
Lambert model fails and `−20·log10(cos θ)` diverges near grazing; a clamp would
emit a physically meaningless frozen-at-80° value. Identity-beyond-max is the one
behavior; the test asserts identity beyond the cap. (The step at the cap is
accepted: those beams are unreliable either way.)
- NaN `beam_angle` → identity (consistent with `recordBeam` retaining NaN-angle
  beams as "no angle correction available").

**This formula was the highest-risk decision; it has been operator-confirmed
(cos²θ) and independently flagged by review-plan. Re-review the revised plan
before implementing.**

## Approach

0. **Add the sonar-agnostic correction parameter** to `Parameters`
   (`parameters.h`): an enum
   `enum class BackscatterAngleCorrection { None, Lambert };` and a field
   `BackscatterAngleCorrection backscatter_angle_correction = BackscatterAngleCorrection::None;`
   (default None = identity = current behavior). Plumb it as a startup setting on
   both estimator entry points:
   - **Live node** (`cube_bathymetry_node.cpp`): declare a ROS parameter
     `backscatter_angle_correction` (string `"none"`|`"lambert"`, default
     `"none"`), parse into the enum when building `Parameters`.
   - **Offline import** (`import_bag_main.cpp`): a `--backscatter-correction
     none|lambert` flag (default `none`), documented in `usage()`.
   Both run the same estimator, so one parameter corrects both the live (#78) and
   offline (#80) paths consistently when enabled.

1. **Rename `BeamIntensitySample::grazing_angle` → `beam_angle`** — struct +
   field doc (`hypothesis.h:40-50`, **incl. the doc text at `hypothesis.h:115-116`
   and the `BeamIntensitySample` reference at `hypothesis.h:166`**), `recordBeam`
   signature/impl (`hypothesis.h:118`, `hypothesis.cpp:118,122,127`), the
   `node.cpp` call-site comment, and test references
   (`test_hypothesis.cpp:216,218,227,230,243`).

2. **Replace the Phase B no-op in `node.cpp:315-318`** — when
   `parameters.backscatter_angle_correction == Lambert`, set
   `corrected = raw − 20·log10(cos(std::abs(beam_angle)))` with the near-grazing /
   NaN identity rules above; otherwise `corrected = raw` (identity). Add the inline
   comment capturing the producer cross-check (Kongsberg +PORT→negate→+STARBOARD;
   nadir=0; radians; dB) AND that the correction is operator-gated because the
   sonar-internal normalization state is not knowable here. Update the stale
   `TODO(#54-B / #15)` block (`node.cpp:298-312`): Phase B done for the flat-bottom
   case; retain the follow-up note for the full GeoCoder slope-aware correction
   (#15/#59).

3. **Update `test_node.cpp`** — enumerate **all three** non-nadir intensity
   assertions that break under `lambert` (the new tests run with the param ON):
   - `NodeRecordMeanAndEstimateVariance` — the **mean** assertion changes; the
     **variance** assertion still holds (all four beams share one angle, so the
     per-beam offset is constant and cancels in the variance).
   - `FirstBeamInitializationRecordsIntensity` (`test_node.cpp:382`,
     `EXPECT_FLOAT_EQ(record.intensity, -30.0f)`).
   - `NodeRecordSkipsNanIntensityBeam` (`test_node.cpp:420`).
   These existing tests stay on the **default (None)** path unless they opt into
   `lambert`; assertions that exercise the correction must set the param ON.
   Add Lambert tests (param = Lambert):
   - `LambertCorrectionNonNadir`: θ=0.5 rad ⇒ `corrected = raw − 20·log10(cos 0.5)`,
     `corrected ≠ raw`.
   - `LambertCorrectionNadirIdentity`: θ≈0 ⇒ `corrected == raw` (float tol).
   - `LambertCorrectionPortStarboardSymmetry`: ±0.3 rad ⇒ equal corrected value.
   - `LambertCorrectionNaNAngle`: NaN angle ⇒ identity.
   - `LambertCorrectionNearGrazing`: θ ≥ MAX_INCIDENCE_RAD ⇒ identity.
   - `CorrectionOffIsIdentity`: param = None ⇒ `corrected == raw` for a non-nadir
     beam (guards the default path).

4. **`test_store_import.cpp` (#80 backscatter test) stays valid by default** —
   the offline tests build `Parameters` with the default (None), so
   `BackscatterCellsMatchGridRecords` (uses `beam_angle = 0.1f`, asserts surfaced
   value == `kIntensity`) **still holds unchanged** (no correction by default).
   **Update the stale comment** at `test_store_import.cpp:64-67` ("the surfaced
   value is uncorrected so the angle does not change it here") to say *by default*
   (None); optionally add a focused case that sets the param to Lambert and
   asserts the corrected value flows through the offline path. (Preferred over
   zeroing `beam_angle`, per review-plan — keeps a non-nadir beam exercising it.)

5. **Add ADR-0007 transition addendum** — create
   `docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md`
   (sibling **addendum** name — does NOT claim the canonical `0007-…` slot before
   the full ADR exists): records Phase B end, the operator-gated flat-bottom
   Lambert (cos²θ), the sonar-agnostic parameter + default-off rationale, and what
   stays deferred (full GeoCoder incidence via #15/#59). **File a separate GitHub
   issue** to author the full ADR-0007 document.

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/parameters.h` | Add `BackscatterAngleCorrection` enum + `backscatter_angle_correction` field (default None) |
| `include/cube_bathymetry/hypothesis.h` | Rename `grazing_angle` → `beam_angle` (struct + `recordBeam` decl + doc comments at 115-116, 166) |
| `src/hypothesis.cpp` | Rename param in `recordBeam` impl |
| `src/node.cpp` | Param-gated cos²θ Lambert correction; producer cross-check comment; update TODO block |
| `src/cube_bathymetry_node.cpp` | Declare/parse `backscatter_angle_correction` ROS param → Parameters |
| `src/import_bag_main.cpp` | `--backscatter-correction none|lambert` flag + usage() text → Parameters |
| `test/test_node.cpp` | Fix 3 broken non-nadir assertions; add 6 Lambert/param tests |
| `test/test_hypothesis.cpp` | `grazing_angle` → `beam_angle` refs |
| `test/test_store_import.cpp` | Update stale comment; (optional) add a Lambert-on offline case |
| `docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md` | New: transition addendum |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Enforcement over documentation | 6 targeted tests catch wrong formula, wrong sign, missing NaN/grazing guards, and the default-off path |
| Only what's needed | Flat-bottom Lambert only; full GeoCoder (#15/#59) deferred; one shared site, no per-path duplication |
| A change includes its consequences | Param plumbed through both estimator entry points; #78 + #80 corrected together; tests + stale comment updated in-PR |
| Capture decisions | ADR-0007 transition addendum records the formula, the operator-gated design, and the deferral |
| Robustness / sonar-agnostic | Correction is operator-set per sonar (the estimator cannot know the sonar's internal normalization); safe default off |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0007 D3 (incidence correction at node-output) | Yes — primary | Implemented (param-gated) at `extractNodeRecord()` |
| ADR-0007 D2/D4 (per-beam mean + estimate variance) | Yes — in place | `corrected` replaces `raw` in the sum; unchanged combine |
| ADR-0008 (ROS 2 conventions) | Yes | New node param declared with default; offline CLI flag documented |
| ADR-0001 (adopt ADRs) | Yes | Transition addendum in `docs/decisions/` + follow-up to author the full ADR |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Add correction parameter | node ROS param + import_bag CLI + Parameters | Yes — Step 0 |
| `grazing_angle` field name | hypothesis.cpp, test_hypothesis.cpp, node.cpp comment, header docs | Yes — Step 1 |
| `corrected` value when param=Lambert | test_node.cpp (3 broken + 6 new) | Yes — Step 3 |
| Default None preserves current behavior | test_store_import.cpp default path unchanged; stale comment fixed | Yes — Step 4 |
| Phase B ends | ADR-0007 transition addendum + follow-up issue | Yes — Step 5 |
| Full GeoCoder correction | cube#15, cube#59 | No — explicit follow-up |

## Open Questions

- [x] **Lambert form** — RESOLVED (operator): cos²θ ⇒ `−20·log10(cos θ)`.
- [x] **Double-counting (is reflectivity_db pre-normalized?)** — RESOLVED
  (operator): not knowable in the sonar-agnostic estimator → expose as an
  operator startup parameter (default off). M3's correct setting is the operator's
  to set per the sonar config; the bridge applies no angle compensation itself.

## Estimated Scope

Single PR, one package. ~160 lines changed + ~110 new test lines (param plumbing
adds to the earlier estimate).
