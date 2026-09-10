# Plan: ErrorModel: the beamwidth fallback treats degrees as radians — 57x angular sigma on every M3 sounding

## Issue

https://github.com/rolker/cube_bathymetry/issues/144

## Context

`ErrorModel::swath_angle_error` (`cube_bathymetry/src/error_model.cpp:236-240`) forms the
angular-measurement term (`ang_meas`) from one of two sources, and neither is currently
correct:

```cpp
double ang_meas = device_.across_track_beamwidth / 12.0;                       // degrees, unconverted
if(i < detections.ping_info.rx_beamwidths.size()) {
  ang_meas = detections.ping_info.rx_beamwidths[i] * (M_PI / 180.0) / 12.0;    // radians, double-converted
}
ang_meas *= ang_meas;
```

- `Device::across_track_beamwidth` (`error_model.h:38`) is documented `/// degrees` and never
  converted — the fallback is ~57x too large (and ~3,283x too large once squared into a
  variance).
- `marine_acoustic_msgs/msg/PingInfo.msg` documents `rx_beamwidths`/`tx_beamwidths` as
  **radians** (confirmed at `/opt/ros/jazzy/share/marine_acoustic_msgs/msg/PingInfo.msg:9-13`,
  and echoed by `cube::Ping`'s own doc comment at `error_model.h:210-213`, "in radians"). The
  per-beam branch multiplies by `M_PI/180.0` anyway, so it is ~57x too **small** whenever it
  runs.
- The fallback is not a rare path: `kongsberg_em_bridge/node.py:547-550` (repo `marine_tools`)
  deliberately leaves `rx_beamwidths`/`tx_beamwidths` empty to dodge this exact mismatch, so
  every M3 sounding takes the too-large fallback today. `norbit_driver/src/conversions.cpp`
  resizes the arrays to zero-filled (non-empty) vectors with the assignment commented out
  "not reported" — so for norbit data the per-beam branch *is* taken, with a beamwidth of
  `0.0`, silently zeroing the angular term instead of falling back.

**The issue body's Acceptance section was never updated** after two scope-correcting owner
comments (see `.agent/work-plans/issue-144/progress.md`, `## Issue Review`, for full
verification against source). This plan is built from the issue body **and** both comments,
per the review's recommendation. The corrected scope:

1. **Boundary normalization**, not a fallback-only conversion. Converting only the fallback
   (the issue body's first proposed option) would make both branches wrong the same
   direction once `marine_tools#82` (github.com/rolker/marine_tools/issues/82) populates
   `rx_beamwidths` in radians — an under-estimate that lets CUBE over-trust bad soundings.
   Boundary normalization means: hold the device beamwidth in radians internally, and treat
   `rx_beamwidths[i]` as already-radian with no further conversion — this stays correct
   whether or not `marine_tools#82` has landed.
2. **Validate, not just branch on array length.** A non-finite or non-positive per-beam
   beamwidth (norbit's zero-filled arrays) is not a measurement; reject it and fall back to
   the device value instead of trusting a `0.0`.
3. **Calder's `1/cos(angle)` widening term is NOT ported** (see "Out of scope"). Reading
   `original_cube/libsrc/ccom_core/device.c:808-820` for the widening does still settle the
   `/12` vs `/sqrt(12)` open question in `cube_bathymetry/docs/divergences_from_calder.md` — Calder divides by
   `12.0` — so that half of the reading survives as a doc-only correction.
4. **`cube_bathymetry/docs/divergences_from_calder.md` ("Angle error") correction** — currently claims the fallback is
   the rare path and the per-beam path the "normal" one; backwards for every sonar in service.
5. **Delete the false "twice the nominal variance" doc comment from *both*
   `horizontal_positioning_error` overloads** — `error_model.h:242-247` (2-arg) and
   `error_model.h:310-317` (5-arg). Both carry the identical false claim and neither
   implementation applies a factor of 2. Bundled per the owner's second comment since the
   file is already open; the second instance was found by `## Plan Review` and confirmed
   against the implementation.
6. **Document the units and statistical meaning of `Sounding::vertical_error` and
   `Sounding::horizontal_error`** (`include/cube_bathymetry/sounding.h:79-80`), today bare
   `float` fields with no units, no confidence level, and no comment. See "Settled unit
   decisions" below for the answer and its evidence. This is the field the whole
   confidence-interval confusion actually escapes through.

### Settled unit decisions (operator, 2026-09-10)

Three unit questions were raised and answered during review, against Calder's vendored
source in `original_cube/`. Recording them here so they are not relitigated.

**1. `Device::across_track_beamwidth` stays in DEGREES; convert once at the boundary.**
Not switched to radians. Rationale: every angular field in the neighbouring `Vessel`
struct is degrees (roughly a dozen of them, each converted in the constructor), so a lone
radian field in `Device` would create a fresh trap of exactly the kind this issue closes;
sonar datasheets quote beamwidths in degrees, which is what a person configuring a new
device will type; and REP-103's radians rule binds ROS interfaces, not internal C++ config
structs. The bug was never that the field is degrees — it was that the conversion sat at
the use site instead of the boundary, applied to one sibling field and not the other.

**Amended 2026-09-10 (round-1 review):** this decision originally covered `Platform`
alongside `Device` and `Vessel`, and `Platform` has since gone the other way — see settled
decision 4. The distinction is configuration versus measurement, not struct adjacency:
`Device` and `Vessel` are typed by a human off a datasheet or a survey report and stay in
degrees; `Platform` is filled by our own projector from TF and holds radians outright.

**2. `Sounding::vertical_error` and `Sounding::horizontal_error` are VARIANCES in m^2, at
one sigma, with NO confidence scaling applied.** Evidence, all from Calder:

- `original_cube/libsrc/errmod/errmod_iho.c:165-167` converts *into* this contract in the
  open and comments each step: it takes the IHO 95% figure, divides by 1.96 (`/* Convert 95%
  CI to standard deviation */`), then squares it (`/* And convert to variances */`). That is
  the contract stated by the code that has to convert into it.
- The full model (`errmod_full.c:792-793`) needs no such conversion because it builds
  variances from the ground up and stores the sum unscaled.
- CUBE consumes them as variances: `cube_node.c:1853` uses `snd->dz` directly as the
  measurement variance in the depth update. The confidence interval is produced only at
  reporting, as `sd2conf_scale * sqrt(variance)`, defaulting to 1.96 (`cube.c:109-113`).
- Our port already behaves this way throughout (`parameters.cpp:69,88,95`;
  `node.cpp:163,257,266`). Only the two doc comments say otherwise, which is why deleting
  them — rather than implementing the factor of 2 — is the correct fix.

Document the two fields accordingly, and note the asymmetry: the vertical figure is a
one-dimensional error about depth, while the horizontal one is a two-dimensional radial
quantity derived from the drms convention, so its square root is a radius in the horizontal
plane rather than an error along a single axis. Both are m^2; they are not the same shape of
thing.

**3. The `1/cos(angle)` widening does not belong in this PR** (operator, 2026-09-10, after
the round-1 review). It was implemented and then backed out before the PR. Calder applies it
at only **three of his nine** device families — EM120, EM3000/D and SB8125, each annotated
"flat plate and FFT beamformer" — and the other five do not widen, so applying it
unconditionally is a modelling claim about our hardware, not a restoration of Calder's
behaviour. It would also risk double-counting against a driver-reported per-beam width that
may already be broadened. The error model should not have to know whether an array is flat;
that geometry belongs with the driver. Tracked as
[`cube_bathymetry#148`](https://github.com/rolker/cube_bathymetry/issues/148), and recorded
as a live divergence in `cube_bathymetry/docs/divergences_from_calder.md` so the gap is deliberate, not silent.

**4. `Platform::roll`/`pitch` move to RADIANS in this PR** (operator, 2026-09-10). The
round-1 review found the identical unit-mismatch bug one function away:
`DetectionsProjector` assigns `tf2::getEulerYPR`'s radians into fields `Platform` documents
as degrees and `ErrorModel` converts as degrees, understating attitude by 57.3x. Filed as
[`cube_bathymetry#147`](https://github.com/rolker/cube_bathymetry/issues/147) and fixed here,
the same way this issue fixed the beamwidth: hold radians internally and drop the
conversions, rather than converting at the projector. `Platform` is filled by our own
projector, not by a human reading a datasheet, so there is no degrees-facing configuration
surface to preserve — the argument that keeps `Device::across_track_beamwidth` in degrees
does not apply. The PR closes #147 alongside #144.

**5. Per-beam validation gets a HARD PHYSICAL CEILING, not a plausibility clamp** (operator,
2026-09-10). The round-1 review proposed a plausibility ceiling on `rx_beamwidths[i]` because
two in-workspace producers report large values. Only one of them is wrong: `ros2sonic` puts
the *transmit* horizontal fan (2.27 rad) into the receive field, a driver fault filed as
[`rolker/ros2sonic#1`](https://github.com/rolker/ros2sonic/issues/1) — an
upstream driver bug, present on the USF-COMIT parent and four SeawardScience
branches, filed in the driver's own repo;
`garmin_sidescan`'s 55° across-track is **correct data in the right field**, because a
sidescan does no across-track beamforming. A clamp tight enough to catch R2Sonic would reject
Garmin's legitimate value. So the model rejects only what is physically impossible — a
per-beam beamwidth of π radians or more — and says plainly, in both the code and the
divergences doc, that this catches nonsense and does **not** catch a misplaced transmit fan.

### Out of scope

- **Store re-measurement is dropped.** The issue's Acceptance section asks that
  `depths/processed` uncertainty over the Lake Massabesic 10 m box be re-measured after the
  fix and the before/after numbers recorded. **The operator has decided not to do that** — no
  store rebuild, no reprocessing, in this issue or a follow-up. This fix is proven with unit
  tests only.
- **`kongsberg_em_bridge/node.py:547-550`** (repo `marine_tools`) is flagged, not fixed, here —
  it lives in a different repo and its "leave empty" comment only becomes stale once this
  normalization lands. Tracked via the existing `cube_bathymetry#30` cross-reference; raise a
  follow-up note on `marine_tools#82` once this PR merges.
- **Calder's `1/cos(angle)` widening is not ported** — see "Settled unit decisions" point 3.
  Tracked as `cube_bathymetry#148`; recorded as a live divergence in
  `cube_bathymetry/docs/divergences_from_calder.md`.
- **A plausibility clamp on `rx_beamwidths[i]`** is deliberately not added — see point 5. Only
  the hard physical ceiling (≥ π rad) is. `ros2sonic`'s misplaced transmit fan is
  `rolker/ros2sonic#1`, a driver fix, not a consumer-side clamp.
- **`marine_tools#82`** itself is not touched by this plan. This fix is written to be correct
  regardless of that issue's landing order (see point 1 above); `marine_tools#82` should be
  re-checked against this normalization once it lands, but that re-check is out of scope here.

## Approach

**Commit 1 — unit-mismatch bug fix (boundary normalization + validation):**

1. In `ErrorModel`'s constructor (`error_model.cpp:32-84`), compute and store a new private
   member, e.g. `device_across_track_beamwidth_rad_ = device.across_track_beamwidth * M_PI /
   180.0;` — converting `Device::across_track_beamwidth` from degrees to radians exactly once,
   at the boundary, the same way `along_track_beamwidth_coefficient` already converts
   `along_track_beamwidth` in the constructor (`error_model.cpp:61-63`). Declare the member in
   `error_model.h` next to `device_` (or fold into `StaticErrorSources`, matching the existing
   pattern for other converted device/vessel fields — pick whichever keeps `Device` itself
   unit-labeled-but-unconverted, since callers/tests still construct `Device` in degrees).
2. Rewrite `swath_angle_error`'s beamwidth selection (`error_model.cpp:236-240`) to:
   - Default to `device_across_track_beamwidth_rad_` (already radians — no conversion at the
     call site).
   - If `i < rx_beamwidths.size()`, read `rx_beamwidths[i]` **as radians, unconverted**, but
     only accept it when `std::isfinite(value) && value > 0.0f`; otherwise keep the device
     fallback. This satisfies the second comment's validation requirement and fixes norbit's
     zero-filled-array case (currently silently zeroes the term; will now fall back to the
     device value).
   - No behavioral difference for `tx_beamwidths` — it is unused by `swath_angle_error`
     (confirmed: only `rx_beamwidths` is read there); out of scope to add a use that doesn't
     exist today.
3. Delete the false doc comment from **both** `horizontal_positioning_error` overloads —
   `error_model.h:242-247` (2-arg) and `error_model.h:310-317` (5-arg) — each claiming
   "Returns approximate 95% confidence interval" and a doubling of the estimate. Neither
   implementation applies a factor of 2 (`error_model.cpp:114-163` and `:319-356`), and
   neither does Calder's (`errmod_full.c`, whose own headers carry the same stale claim;
   his 95% scaling is applied at reporting, with 1.96, not inside these functions).
   Replace both with wording that matches the code: returns a variance in m^2 at one sigma,
   no confidence-interval scaling applied.
3b. Document `Sounding::vertical_error` and `Sounding::horizontal_error`
   (`include/cube_bathymetry/sounding.h:79-80`) as variances in m^2 at one sigma, with no
   confidence scaling, and note that the horizontal one is a radial (drms-derived) quantity
   whose square root is a horizontal-plane radius rather than a single-axis error. Evidence
   in "Settled unit decisions" above.
4. Correct `cube_bathymetry/docs/divergences_from_calder.md` (the "Angle error" material under
   "## 2. Device error budget is parameterized, not a per-device table"):
   - Remove the "no behavioural change" framing for the angle term — commit 1 **is** a
     behavioral change, not documentation-only, so this needs its own entry rather than a
     note inside the #47 section it currently lives in.
   - State plainly that the fallback was the live path for every sonar in service (deliberate
     in `kongsberg_em_bridge`, incidental-but-effective in `norbit_driver`), not the rare one.
   - Record that both the fallback and the per-beam path are now boundary-normalized to
     radians, and that per-beam values are validated (finite, positive) before use.
   - Close out the `/12` vs `/sqrt(12)` open question: Calder's `device.c:808-820` uses
     `/12.0`; keep the current divisor, note it's now confirmed rather than assumed.
   - Cross-reference `cube_bathymetry#144` (this issue) and flag the still-open
     `marine_tools` follow-up (kongsberg_em_bridge's now-stale "leave empty" comment).
5. Tests (`test/test_error_model.cpp`), added alongside the existing tests that zero
   `across_track_beamwidth` (those remain valid — they isolate other terms and don't need to
   change).

   **Implementation note (found during implementation, 2026-09-10):** one existing test that
   does *not* zero `across_track_beamwidth` — `VerticalErrorIncreasesWithBeamAngle` — turned
   out to encode the bug. It held the two-way travel time fixed (so the depth shrank as the
   beam swung out) and asserted the total vertical error rose monotonically nadir → 30° →
   60°. That only held because the ~3283× inflated angular term swamped every other term, so
   the budget tracked `sin^2(angle)` alone. With the term at its correct magnitude the
   measured-range error's `cos^2(angle)` projection dominates at moderate angles and the
   total *dips* at 30° before rising. The test was replaced, not loosened, by two tests that
   pin properties that are actually true of the corrected model:
   `VerticalErrorIsWorseAtObliqueAngleAtConstantDepth` (nadir vs 30° vs 60° at constant
   *depth*, the comparison that isolates beam obliquity from a shortening water column —
   round-1 review made it a THREE-point comparison that pins the dip's shape, since a
   two-point endpoint check cannot tell "rises monotonically" from "dips and recovers") and
   `AngularContributionToVerticalErrorRisesWithBeamAngle` (the monotone property stated over
   the isolated angular term it actually applies to). The rewrite's comment records why.

   New tests:
   - **Unit agreement**: drive the fallback and per-beam branches with equivalent inputs (a
     device beamwidth of `D` degrees vs. a single-beam `rx_beamwidths[0] = D * M_PI / 180.0`
     radians) at the same beam angle, and assert equal `ang_meas`/resulting horizontal or
     vertical error contribution.
   - **Regression pin**: fallback value for a non-default device beamwidth at nadir — pin the
     exact `ang_meas` contribution. (Round-1 review: pinning the 2° *default* made this test
     a duplicate of the empty-array fallback test, so it uses a non-default width instead.)
   - **Validation — empty array**: `rx_beamwidths` empty (today's real M3 behavior) uses the
     device fallback.
   - **Validation — zero-filled array**: `rx_beamwidths = {0.0f, ...}` (today's real norbit
     behavior) is rejected and falls back to the device value, not silently zeroed.
   - **Validation — non-finite value**: `rx_beamwidths[i] = NaN` (or `-inf`) is rejected and
     falls back. Finite-but-non-positive values (`0.0`, `-0.01`) are a separate case with its
     own test, not folded into this one — the round-1 review found them mislabelled here.
   - **Validation — physically impossible value**: `rx_beamwidths[i] >= π` rad is rejected and
     falls back to the device value; `garmin_sidescan`'s legitimate 55° (0.96 rad) is
     **accepted**, pinning that the ceiling is a physical bound and not a plausibility clamp.
   - **Validation — real value accepted**: a positive finite `rx_beamwidths[i]` is used
     directly as radians (no conversion), distinguishing this from the old
     double-conversion bug.

**Commit 2 — `Platform::roll`/`pitch` in radians (#147, fixed in this PR):**

6. `DetectionsProjector` assigns `tf2::getEulerYPR`'s radians straight into
   `Platform::roll`/`pitch` (`src/detections_projector.cpp:111-113`), which `error_model.h:58-59`
   documents as degrees and `error_model.cpp:306-307` and `:397-400` convert as degrees —
   attitude enters the model 57.3x understated. Redocument the two fields as **radians** and
   delete the six `* M_PI / 180.0` factors that converted them (two in `swath_depth`, four in
   `compute`'s per-ping trig), so the one producer and the one consumer agree.
   Check (do not assume) the sign convention, BOTH halves of it. Round 2 found the first pass
   had checked only roll: `Platform` documents "+ve is port side up", and REP-103's roll about
   +x-forward gives port up for a positive rotation — correct; but `Platform` also documents
   pitch "+ve is bow up", while `tf2::getEulerYPR` returns `-asin(R[2][0])` for an FLU
   rotation, which is bow-DOWN positive. Operator decision: keep Calder's conventions on
   `Platform` (every ported equation was derived under them) and negate at the producer, the
   same boundary where units are converted. Heave (`+ve down` vs the TF translation's REP-103
   `+up`) is negated there too — numerically inert, it enters only squared, but the struct is
   held to one convention.
7. Tests: every existing test zeroes roll and pitch, which is exactly why nothing caught this,
   so the new tests must exercise **non-zero** roll and pitch — a radian-valued roll must move
   the budget by the amount a radian-valued roll should, and the depth/`cosT` geometry must
   track the roll rather than a 57x-shrunken version of it.
8. `cube_bathymetry/docs/divergences_from_calder.md`: record the attitude unit fix alongside the beamwidth one.

**Commit 2b — the rejection/fallback diagnostic surface (added during implementation):**

Normalizing the units settles what the number *means*; it does not tell an operator when the
angular budget is a generic default rather than an instrument measurement — which, for every
M3 ping (`rx_beamwidths` empty) and every norbit ping (zero-filled), is all of them. So the
projector counts it and every caller reports it:

- `ErrorModel` gains two public members so the predicate and its bound have one definition:
  `static bool per_beam_beamwidth_usable(float beamwidth_rad)` and
  `static constexpr float kMaxPerBeamBeamwidthRad` (π rad, a hard physical bound — see
  settled decision 5, not a plausibility clamp).
- `ProjectionDiagnostics` gains `default_beamwidth_beams`: beams whose angular term came from
  `Device::across_track_beamwidth` because the ping reported no usable per-beam value for
  them. Round 2 corrected this: it began life as `rejected_beamwidths`, counted over
  `rx_beamwidths`, which was **silent in the field's commonest case** — an EMPTY array
  rejects nothing, so 100% of M3 beams ran on the default while every tool printed "0
  rejected". It now counts the FALLBACK over the beams (`two_way_travel_times`), the same
  domain `swath_angle_error` indexes, and is reported as "N of M beams".
- `cube::report_projection_summary()` over a `ProjectionRunTotals` struct, in the new
  header-only `include/cube_bathymetry/projection_summary.h`: the run-summary paragraph was
  three verbatim copies across the offline tools, which is how wording drifts. One copy now.
- `detections_to_pointcloud` reports the count as a throttled warning, and — round 2 — returns
  early unless the node is `active`, since the subscription outlives `on_activate`/`deactivate`
  and the LifecyclePublisher silently drops what an inactive callback produces.
- Per-beam array shape is hardened in the same spirit: `rx_angles` is bounds-guarded in
  `Sounding`'s geometry and in `beam_angle()` (a short array was an out-of-bounds read;
  absent is now NaN, like the neighbouring `tx_angles`/`intensities` reads).

**Commit 2c — round-2 review: the pitch sign and Eqn. 3.49 (operator decisions):**

- **Pitch sign.** See commit 2's item 6: negate at the producer, keep Calder's convention on
  `Platform`. Pinned by `DetectionsProjectorTest.BowUpPitchFollowsCalderSignConvention`, which
  needs a NON-ZERO IMU/GPS lever arm — the only terms odd in `sin(pitch)` are multiplied by
  those offsets and vanish at the zero defaults, which is why the wrong sign was latent.
- **Eqn. 3.49's pitch term.** `swath_depth` scaled it by `cos(pitch)²` where Calder
  (`errmod_full.c:376-377`) uses `cosT²` = `cos(roll + beam angle)²`, the factor its two
  faithful siblings in the same function already use. A transcription slip, not a decision;
  over-estimates by `1/cos²T` (~4× at 60°, biased to the swath edge). Fixed to match Calder,
  pinned by `ErrorModelTest.DepthPitchTermUsesSwathAngleNotPitchCosine`, and recorded in the
  divergences doc as a **fixed porting error** (§2d), explicitly not a divergence. It is
  pre-existing but invisible until #147 turned the pitch term on.
- **The unconditional `/12`.** Calder's `bw/12.0` is inside an amplitude-detection branch
  (`SOUNDING_ISAMPDET`); a phase detection takes `0.2*bw/√np`, and EM300 neither. This port
  has no detection flag or window size in `SonarDetections`, so it applies `/12` to every
  beam. Recorded as a real divergence (the earlier "/12 is confirmed" note over-claimed the
  scope and paraphrased the C); the quoted block is now verbatim.

**Commit 3 — the remaining round-1 review corrections:** the third false 95%-confidence claim
on `horizontal_latency` (`error_model.h:253`), the false "documented (and configured) in
degrees" wording in the divergences doc (`across_track_beamwidth` is exposed by no parameter,
YAML or launch file — cross-reference `cube_bathymetry#145`), the `rx_beamwidths` doc comment
that says "transmit", and the README's missing units and stale field count.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/error_model.h` | Add a private member for the boundary-normalized (radians) device beamwidth; add the public `per_beam_beamwidth_usable()` predicate and `kMaxPerBeamBeamwidthRad` so the projector's diagnostic cannot drift from the model's own rule (commit 2b); state the sign conventions and which producer converts (commit 2c); delete/replace the false "twice the nominal variance" doc comment on **both** `horizontal_positioning_error` overloads (2-arg at :242-247, 5-arg at :310-317) |
| `cube_bathymetry/include/cube_bathymetry/sounding.h` | Document `vertical_error` / `horizontal_error` as variances in m^2 at one sigma, no confidence scaling; note the horizontal field is radial; bounds-guard the `rx_angles` reads (commit 2b) |
| `cube_bathymetry/src/error_model.cpp` | Constructor: convert `device.across_track_beamwidth` to radians once. `swath_angle_error`: validate + select beamwidth without a second conversion, including the ≥ π rad physical ceiling (commit 1). Drop the six `* M_PI / 180.0` attitude conversions (commit 2). `swath_depth`: restore Calder's `cosT^2` in the Eqn. 3.49 pitch term; `beam_angle()`: bounds-guard `rx_angles` (commits 2b, 2c) |
| `cube_bathymetry/src/detections_projector.cpp` | Document that `getEulerYPR`'s radians now go into radian-valued `Platform` fields (commit 2) |
| `cube_bathymetry/test/test_error_model.cpp` | Add unit-agreement, regression-pin, and the validation-case tests (commit 1); add non-zero roll/pitch attitude tests (commit 2) |
| `cube_bathymetry/docs/divergences_from_calder.md` | Correct the "Angle error" subsection: fallback-is-normal claim reversed, normalization + validation documented, `/12` divisor confirmed, the un-ported widening recorded against `#148`, the attitude unit fix recorded |
| `README.md` | Units and confidence level on the published uncertainty fields; correct the field count (commit 3) |
| `cube_bathymetry/include/cube_bathymetry/detections_projector.h` | `ProjectionDiagnostics::default_beamwidth_beams` (commit 2b) |
| `cube_bathymetry/include/cube_bathymetry/projection_summary.h` | **New.** Header-only `cube::report_projection_summary()` + `ProjectionRunTotals`, replacing three verbatim copies of the run summary (commit 2b) |
| `cube_bathymetry/src/detections_projector.cpp` | Count the beams that take the device-default beamwidth; negate pitch and heave at the boundary (commits 2b, 2c) |
| `cube_bathymetry/src/detections_to_pointcloud.cpp` | Throttled warning for the default-beamwidth count; skip the callback unless active (commit 2b) |
| `cube_bathymetry/src/import_bag_main.cpp`, `src/batch_regen_main.cpp`, `src/bag_to_geotiff.cpp` | Accumulate into `ProjectionRunTotals` and call the shared summary; `bag_to_geotiff`'s stale "6-field" comment corrected (commits 2b, 3) |
| `cube_bathymetry/test/test_detections_projector.cpp` | Default-beamwidth counting (empty / short / over-long / range-gated) and the bow-up pitch-sign test (commits 2b, 2c) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | `cube_bathymetry/docs/divergences_from_calder.md` correction lands in this PR, alongside the code it describes, not deferred; the `marine_tools` consequence is flagged, not silently dropped, via the existing `cube_bathymetry#30` cross-reference |
| Test what breaks | Every existing test that zeroed `across_track_beamwidth` to avoid this term is left intact; new tests target exactly the branches and validation cases this fix changes (unit agreement, regression pin, empty/zero/non-finite/over-ceiling/real per-beam values) plus the attitude path, which no test exercised at all |
| Only what's needed / Improve incrementally | The angle-widening term is not required to fix the unit bug and its correct home is a device-specific decision, so it is out of this PR entirely and tracked as `#148`. The attitude fix (`#147`) *is* in, because it is the same bug class in the same call chain and this PR is what makes it the leading residual |
| Capture decisions, not just implementations | `cube_bathymetry/docs/divergences_from_calder.md` update closes the `/12` vs `/sqrt(12)` open question and corrects the backwards "normal path" claim, so the record matches the code going forward |
| Human control and transparency | No hidden behavior change — this is a correction toward documented intent (`rx_beamwidths` "in radians", `Device` "degrees"); the store re-measurement acceptance criterion the issue asked for is explicitly dropped by operator decision (see Out of scope), not silently omitted |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | No | Pure C++ math/logic fix and doc correction in an existing translation unit; no new packages, launch files, topics, or interfaces |
| Others | No | No ADR governs numerical/error-model conventions; `cube_bathymetry/docs/divergences_from_calder.md` is the informal record for this class of decision, and is exactly what this plan updates |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `swath_angle_error`'s beamwidth handling | `cube_bathymetry/docs/divergences_from_calder.md` "Angle error" material | Yes |
| `error_model.h`'s doc comments | Nothing else references the false "twice the nominal variance" claim (grepped; no other file quotes it) | Yes |
| The angular term's magnitude for every M3/norbit sounding | `depths/processed` store uncertainty (the issue's original motivation) | **No — dropped by operator decision** (see Out of scope); unit tests are the only proof for this PR |
| This repo's normalization boundary | `marine_tools#82` (populating `rx_beamwidths` for Kongsberg drivers) and `kongsberg_em_bridge/node.py:547-550`'s stale comment | Flagged only, not fixed here — both live in `marine_tools`, a different repo; cross-referenced via the existing `cube_bathymetry#30` issue |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `cube_bathymetry/docs/divergences_from_calder.md` (the "Angle error" material) — currently states the fallback is the rare path and claims "no
  behavioural change," both now false. Corrected above, alongside the code.
- **README** (must land in this PR): the `soundings` field list and the bold variance
  warning are user-facing statements of exactly what this PR changes the meaning of. Round 2
  found two of its own new claims wrong and both are corrected here — the "understate by its
  own square root" wording (true only below 1 m²; at the defaults `horizontal_error` carries
  `gps_drms²` = 4 m², where the same mistake overstates), and the `grid` output's
  `uncertainty` layer advertised two lines above under the OPPOSITE convention (a
  confidence-scaled standard deviation in metres, `node.cpp:266`) with nothing saying so.
- **Agent-instruction candidates**: None — this is a self-contained numerical bug fix with no
  new workflow, convention, or pitfall that generalizes beyond this file's own divergences
  doc (which already exists as the right place to record it).

## Open Questions

- None. Six decisions that would otherwise be open are settled: store re-measurement
  dropped, and the fix must be correct independent of `marine_tools#82`'s landing order (see
  "Out of scope"); `Device::across_track_beamwidth` stays in degrees, the error fields are
  one-sigma variances in m^2, the widening term is out of scope and tracked as `#148`, the
  attitude fix (`#147`) is in scope, and the per-beam validation gets a hard physical ceiling
  rather than a plausibility clamp (see "Settled unit decisions").
- Two more were settled by the operator at round 2, and are recorded in commit 2c: the pitch
  SIGN is corrected at the producer rather than by restating `Platform`'s convention, and
  Eqn. 3.49's pitch term is fixed to match Calder rather than recorded as a divergence.

## Estimated Scope

Single PR closing both `#144` and `#147`. Atomic commits, one logical change each, with every
commit's doc changes landing alongside its code. As implemented and reviewed, that came to
nine rather than the three this plan first sketched: the beamwidth unit fix and validation,
the attitude unit fix, the round-1 doc corrections, the constant-depth test rewrite, the
diagnostic surface and its round-2 correction, the pitch-sign fix, the Eqn. 3.49 fix, the
divergences-doc rewrite, and the round-2 wording and robustness fixes. No store re-measurement, no widening
(`#148`), no cross-repo changes (the `marine_tools` and `ros2sonic` consequences are flagged
only, as `marine_tools#82` and `rolker/ros2sonic#1`).
