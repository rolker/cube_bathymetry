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
3. **Calder's `1/cos(angle)` widening term** (`original_cube/libsrc/ccom_core/device.c:808-820`),
   ported alongside the unit fix since it is the same expression, but **as its own atomic
   commit** — it is a term the port dropped rather than part of the unit bug, so it stays
   independently reviewable and revertable (AGENTS.md atomic-commit rule; also
   settles the `/12` vs `/sqrt(12)` open question in `divergences_from_calder.md` — Calder
   uses `/12.0`, confirmed by the same source snippet).
4. **`docs/divergences_from_calder.md:73-91` correction** — currently claims the fallback is
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
Not switched to radians. Rationale: every angular field in the neighbouring `Vessel` and
`Platform` structs is degrees (roughly a dozen of them, each converted in the constructor),
so a lone radian field in `Device` would create a fresh trap of exactly the kind this issue
closes; sonar datasheets quote beamwidths in degrees, which is what a person configuring a
new device will type; and REP-103's radians rule binds ROS interfaces, not internal C++
config structs. The bug was never that the field is degrees — it was that the conversion
sat at the use site instead of the boundary, applied to one sibling field and not the
other. Moving all three structs to radians would be a legitimate separate change; doing it
to this one field alone would not.

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

**3. The `1/cos(angle)` widening is a RESTORATION, not a new enhancement.** Calder applies
it at `device.c:808`, `:895` and `:938`; the port dropped it. It still lands as its own
atomic commit for independent reviewability, but the commit message and the divergences doc
must describe it as restoring a term the port omitted, not as adding a term Calder lacked.

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
4. Correct `docs/divergences_from_calder.md:73-91` ("Angle error" subsection under "## 2.
   Device error budget is parameterized, not a per-device table"):
   - Remove the "no behavioural change" framing for the angle term — commit 1 (and commit 2,
     see below) **are** behavioral changes, not documentation-only, so this needs its own
     entry rather than a note inside the #47 section it currently lives in.
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
   change):
   - **Unit agreement**: drive the fallback and per-beam branches with equivalent inputs (a
     device beamwidth of `D` degrees vs. a single-beam `rx_beamwidths[0] = D * M_PI / 180.0`
     radians) at the same beam angle, and assert equal `ang_meas`/resulting horizontal or
     vertical error contribution.
   - **Regression pin**: fallback value for a 2° device beamwidth (the existing `Device`
     default) at nadir (angle = 0, so the widening term from commit 2 is 1.0 and doesn't
     interfere) — pin the exact `ang_meas` contribution.
   - **Validation — empty array**: `rx_beamwidths` empty (today's real M3 behavior) uses the
     device fallback.
   - **Validation — zero-filled array**: `rx_beamwidths = {0.0f, ...}` (today's real norbit
     behavior) is rejected and falls back to the device value, not silently zeroed.
   - **Validation — non-finite value**: `rx_beamwidths[i] = NaN` (or `-inf`) is rejected and
     falls back.
   - **Validation — real value accepted**: a positive finite `rx_beamwidths[i]` is used
     directly as radians (no conversion), distinguishing this from the old
     double-conversion bug.

**Commit 2 — restore Calder's `1/cos(angle)` widening term (separate commit):**

6. In `swath_angle_error`, after selecting the beamwidth (radians, from either source per
   commit 1), widen it by `1.0 / cos(meas_angle)` before forming `ang_meas` — `meas_angle` is
   already computed at `error_model.cpp:215` via `beam_angle()`, so no new geometry lookup is
   needed. Match Calder's order of operations (`device.c:808-820`): widen the beamwidth by
   `1/cos(angle)` *before* dividing by 12 and squaring, so the widening itself is also
   effectively squared in the final variance (consistent with Calder).
7. Test: an off-nadir beam (e.g. `meas_angle` such that `cos(meas_angle) = 0.5`) produces a
   `ang_meas` contribution `4x` the nadir value for the same beamwidth (since the widening
   factor of `2.0` is squared into the variance) — distinguishes the widening term from a
   no-op.
8. `docs/divergences_from_calder.md`: extend the same "Angle error" entry (or add a sub-bullet)
   to record that the port now includes the angle-dependent widening Calder applies, closing
   that specific divergence.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/error_model.h` | Add a private member for the boundary-normalized (radians) device beamwidth; delete/replace the false "twice the nominal variance" doc comment on **both** `horizontal_positioning_error` overloads (2-arg at :242-247, 5-arg at :310-317) |
| `cube_bathymetry/include/cube_bathymetry/sounding.h` | Document `vertical_error` / `horizontal_error` as variances in m^2 at one sigma, no confidence scaling; note the horizontal field is radial |
| `cube_bathymetry/src/error_model.cpp` | Constructor: convert `device.across_track_beamwidth` to radians once. `swath_angle_error`: validate + select beamwidth without a second conversion (commit 1); apply `1/cos(angle)` widening (commit 2) |
| `cube_bathymetry/test/test_error_model.cpp` | Add unit-agreement, regression-pin, and three validation-case tests (commit 1); add widening-term test (commit 2) |
| `docs/divergences_from_calder.md` | Correct the "Angle error" subsection: fallback-is-normal claim reversed, normalization + validation documented, `/12` divisor confirmed, widening term documented (split across commits 1 and 2 to match the code changes) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | `docs/divergences_from_calder.md` correction lands in this PR (commits 1 and 2), not deferred; the `marine_tools` consequence is flagged, not silently dropped, via the existing `cube_bathymetry#30` cross-reference |
| Test what breaks | Every existing test that zeroed `across_track_beamwidth` to avoid this term is left intact; new tests target exactly the branches and validation cases this fix changes (unit agreement, regression pin, empty/zero/non-finite/real per-beam values, widening) |
| Only what's needed / Improve incrementally | The angle-widening term is a restoration of something Calder had and the port dropped, not required to fix the unit bug — split into its own commit(s) so it is independently reviewable and revertable without touching the safety fix |
| Capture decisions, not just implementations | `divergences_from_calder.md` update closes the `/12` vs `/sqrt(12)` open question and corrects the backwards "normal path" claim, so the record matches the code going forward |
| Human control and transparency | No hidden behavior change — this is a correction toward documented intent (`rx_beamwidths` "in radians", `Device` "degrees"); the store re-measurement acceptance criterion the issue asked for is explicitly dropped by operator decision (see Out of scope), not silently omitted |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | No | Pure C++ math/logic fix and doc correction in an existing translation unit; no new packages, launch files, topics, or interfaces |
| Others | No | No ADR governs numerical/error-model conventions; `docs/divergences_from_calder.md` is the informal record for this class of decision, and is exactly what this plan updates |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `swath_angle_error`'s beamwidth handling | `docs/divergences_from_calder.md` "Angle error" subsection | Yes — commits 1 and 2 |
| `error_model.h`'s doc comments | Nothing else references the false "twice the nominal variance" claim (grepped; no other file quotes it) | Yes |
| The angular term's magnitude for every M3/norbit sounding | `depths/processed` store uncertainty (the issue's original motivation) | **No — dropped by operator decision** (see Out of scope); unit tests are the only proof for this PR |
| This repo's normalization boundary | `marine_tools#82` (populating `rx_beamwidths` for Kongsberg drivers) and `kongsberg_em_bridge/node.py:547-550`'s stale comment | Flagged only, not fixed here — both live in `marine_tools`, a different repo; cross-referenced via the existing `cube_bathymetry#30` issue |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `docs/divergences_from_calder.md:73-91` (the "Angle
  error" subsection) — currently states the fallback is the rare path and claims "no
  behavioural change," both now false. Corrected in commits 1 and 2 above.
- **Agent-instruction candidates**: None — this is a self-contained numerical bug fix with no
  new workflow, convention, or pitfall that generalizes beyond this file's own divergences
  doc (which already exists as the right place to record it).

## Open Questions

- None. Five decisions that would otherwise be open are settled: store re-measurement
  dropped, and the fix must be correct independent of `marine_tools#82`'s landing order (see
  "Out of scope"); `Device::across_track_beamwidth` stays in degrees, the error fields are
  one-sigma variances in m^2, and the widening term is a restoration (see "Settled unit
  decisions").

## Estimated Scope

Single PR, two atomic commits (unit-mismatch bug fix + validation; Calder angle-widening
enhancement), plus the doc correction split across both commits to match. No store
re-measurement, no cross-repo changes (the `marine_tools` consequence is flagged only).
