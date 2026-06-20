# Divergences from Calder's original CUBE

This document records where `cube_bathymetry` **intentionally** differs from
Brian Calder's original CUBE C implementation (vendored under `original_cube/`).
It is the deliverable of
[#30](https://github.com/rolker/cube_bathymetry/issues/30) (validate the port
against Calder's reference) and closes it.

A line-by-line static comparison established that the CUBE **estimator** — the
West & Harrison depth update, the Bayes-factor monitoring/intervention, the
hypothesis lifecycle, and the grid distance-weighting/capture logic — is a
faithful port and produces the same results as Calder's reference for the same
inputs. Two outright **bugs** found in that comparison (a `profile_err` variance
double-square and a `max_variance_allowed` operator-precedence error) were fixed
under [#46](https://github.com/rolker/cube_bathymetry/issues/46) and are **not**
divergences — the port now matches Calder there.

What follows are the differences we keep **on purpose**. Each notes the rationale
and, where relevant, the reference location in `original_cube/`.

---

## 1. Datum-aware tide terms (referencing flag)

**Calder**: the vertical reduction (Eqn 3.63,
`original_cube/libsrc/errmod/errmod_full.c:250-252`) always includes
`tide_measured_sdev² + tide_predicted_sdev²`.

**Port**: those two terms are omitted **by default**. The grid here is
**ellipsoid-referenced** (see the README "Data flow & pose sourcing" section): no
tidal-datum reduction is applied, so there is no tide uncertainty to propagate.
The seabed's ellipsoid height is tide-independent and is the stable
representation; chart-datum reduction is a downstream spatial transform.

To make the assumption explicit and operator-controllable, `Vessel` carries a
`bool ellipsoidal_referenced` (default `true`), exposed as the
`ellipsoidal_referenced` ROS parameter on `detections_to_pointcloud`. When set
`false` (the survey is reduced to a tidal datum), the `ErrorModel` constructor
adds `tide_measured_sdev² + tide_predicted_sdev²` back into
`static_error_sources_.vertical_reduction`, matching Calder. Default behaviour is
numerically unchanged from the prior port.

Rationale: keep the (currently costmap-grade) tide/datum chain out of the
bathymetry critical path while leaving a clean, recompile-free switch for the day
a tidal-datum product is wanted.

## 2. Device error budget is parameterized, not a per-device table

**Calder**: `device_compute_rangeerr` / `device_compute_angerr`
(`original_cube/libsrc/ccom_core/device.c`) are a `switch` over specific 1990s-era
sonars (Simrad EM300/EM120, Atlas Hydrosweep, Seabeam 2112, …), each with
hardcoded constants.

**Port**: we do not maintain a device table (none of the sonars we run — e.g. the
Kongsberg M3 — are in Calder's switch anyway). Instead the device error terms are
parameters with reasonable defaults:

- **Range error** — `Device::range_error_percent` (default `0.005` = 0.5%) and
  `Device::range_error_floor_m` (default `0.05`), both ROS parameters. The error
  is `max(range_error_percent·|depth|, range_error_floor_m)`, then squared.
  - For reference, Calder's deep-water devices used **0.3–0.6 %** of depth with a
    **0.15–0.6 m** floor (`device.c`: EM300/EM120 0.3 %, Seabeam 2112 0.5 %, Atlas
    Hydrosweep 0.6 %). The pre-#47 port hardcoded **5 %** with no floor — roughly
    8–17× more pessimistic than any of Calder's values and far too large for a
    shallow high-frequency head. The 0.5 % default sits inside Calder's range; the
    0.05 m floor is appropriate for shallow survey (Calder's 0.15 m floors are
    deep-water values). Both should be refined per-sonar against datasheets.
  - At 2–20 m survey depths the default 0.5 %/0.05 m budget stays well inside the
    IHO Order 1a vertical allowance (≈0.1 m at 20 m vs a ~0.29 m 1σ budget); the
    old 5 % reached ~1.0 m at 20 m, larger than the entire allowance. Pinned by
    `ErrorModelTest.DefaultsInsideIHOOrder1aBudget`.

- **Angle error** — kept as the generic `beamwidth/√12` (uniform-distribution σ
  over the beamwidth) using the **live per-ping** `rx_beamwidths[i]` from the
  `SonarDetections` message, with the static `Device::across_track_beamwidth` as
  fallback (`error_model.cpp`, `swath_angle_error`). This is a reasonable generic
  equivalent to Calder's per-device `device_compute_angerr`, and uses live
  per-beam data Calder did not have. No behavioural change in #47 — documented
  here only.

  > **Known pre-existing inconsistency (not fixed in #47):** the static fallback
  > `across_track_beamwidth / 12` consumes `across_track_beamwidth` in **degrees**,
  > whereas the live `rx_beamwidths[i]` path converts to **radians** before the
  > `/12`. The two paths therefore disagree by a factor of `(π/180)` when the
  > fallback is taken. The live path is the normal one (real pings carry
  > `rx_beamwidths`); the fallback only fires when the message omits them. Tracked
  > as a follow-up, not addressed here to keep #47 scoped.

## 3. IHO f(z) error model is not ported

**Calder** ships two TPU models: the detailed **FULL** MBES model
(`errmod_full.c`) and a simpler **IHO** `dz = √(a² + (b·z)²)` order-based model
(`errmod_iho.c`).

**Port**: only the FULL MBES model is ported. The IHO f(z) model is a coarse
fallback for config-light / legacy single-beam use; with a real vessel/device
config and an MBES, the FULL model is the correct and richer choice. Not porting
it is a deliberate scope decision; revisit only if a config-light quick-look
fallback is ever needed.

## 4. `CONF_99PC` = 2.576 vs Calder's 2.56

**Calder**: `#define CONF_99PC 2.56f` (`original_cube/libsrc/cube/cube.c:110`).

**Port**: `CONF_99PC = 2.576` (`common.h`). The exact two-sided 99 % z-score is
2.5758; 2.576 is the more accurate rounding (Calder's own code comments cite
2.5758 while the macro rounds to 2.56). The constant feeds the grid effect-radius
clamp and the median-queue outlier bounds; the ~0.6 % difference is numerically
negligible. We keep the more-correct value rather than reproduce Calder's rounded
one. Regression tests assert 2.576, not Calder's constant.

## 5. Nomination uncertainty source

**Calder**: when a hypothesis is nominated, `cube_node_extract_depth_unct`
(`original_cube/libsrc/cube/cube_node.c:1916`) reports the nominated hypothesis's
**depth** but pairs it with the **list head's** `cur_variance` — a different
hypothesis, almost certainly a latent bug.

**Port**: `Node::extractDepthAndUncertainty` (`node.cpp:172`) reports the
nominated hypothesis's own `input_sample_variance`, so the reported depth and
uncertainty come from the **same** hypothesis, and the value is the same
`input_sample_variance` the non-nominated path already reports. This is
internally self-consistent and is the behaviour we keep, even though it differs
from the number Calder's code emits. Only observable when an operator actually
nominates a hypothesis (a manual disambiguation override).

---

## Related issues

- [#30](https://github.com/rolker/cube_bathymetry/issues/30) — the validation
  effort this document closes.
- [#46](https://github.com/rolker/cube_bathymetry/issues/46) — the two genuine
  bugs fixed (not divergences).
- [#48](https://github.com/rolker/cube_bathymetry/issues/48) — Calder's
  predicted-surface / guided-disambiguation subsystem (CUBE_PRIOR / CUBE_POSTERIOR
  and prior-depth propagation) is not yet ported; the port currently runs pure
  data-driven CUBE. Tracked separately, not a permanent divergence.
- [#49](https://github.com/rolker/cube_bathymetry/issues/49) — node extraction
  quality (hypothesis-strength ratio, deterministic tie-break).
