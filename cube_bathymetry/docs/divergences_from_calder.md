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

- **Angle error** — kept as the generic `beamwidth/12` σ approximation, taking the
  beamwidth from the live per-ping `rx_beamwidths[i]` when the message reports a
  usable one and from the static `Device::across_track_beamwidth` otherwise
  (`error_model.cpp`, `swath_angle_error`). This is a generic stand-in for
  Calder's per-device `device_compute_angerr`, and can use live per-beam data
  Calder did not have. See the **Angle error: units, validation and the
  unconditional `/12` (#144)** section below for the unit normalization — that
  *is* a behavioural change, made after #47 — for the `1/cos(angle)` widening
  Calder applies to some devices and this port still does not, and for the
  `/12` divisor this port applies to every beam where Calder applies it only
  to amplitude detections.

## 2b. Angle error: units, validation and the unconditional `/12` (#144)

**Calder** (`original_cube/libsrc/ccom_core/device.c:807-820`, and the same shape
at `:895` and `:938`) forms the angular σ per device family, and — for the
families that widen — branches on the bottom-detection method. Verbatim, from
the `DEVICE_EM120` arm:

```c
        case DEVICE_EM120:
            bw = DEG2RAD(devices[device->type].across_width)/cos(angle);
            np = SOUNDING_GETWINDOWSZ(snd->flags);
            if (SOUNDING_ISAMPDET(snd->flags)) {
                rtn = bw/12.0;
            } else {
                if (np == 0) np = 1;
                rtn = 0.2*bw/sqrt(np);
            }
            rtn *= rtn;    /* Dealing with variances */
            break;
```

— i.e. degrees converted to radians at the point of use; for the flat-plate/FFT
families widened by `1/cos(angle)` for the beam's obliquity; then divided by 12
**for an amplitude detection**, or scaled by `0.2/√np` **for a phase
detection**; then squared into a variance. Other families differ again: `EM300`
uses `tan(K1·RBW)` / `tan(K2·RBW/√np)` and no `/12` at all.

**Port, before #144**: the fallback consumed `Device::across_track_beamwidth` raw,
in **degrees** (~57× too large, ~3,283× once squared into a variance), while the
per-beam path multiplied `rx_beamwidths[i]` by `π/180` even though
`marine_acoustic_msgs/msg/PingInfo` documents that field as **already radians**
(~57× too small). The `1/cos(angle)` widening was dropped entirely.

**Port, after #144**:

- **Boundary normalization.** `Device::across_track_beamwidth` is converted from
  degrees to radians exactly once, in the `ErrorModel` constructor, alongside the
  identical conversion already done for `along_track_beamwidth`. `Device` itself
  stays degrees-valued — every other angular field on `Device` and `Vessel` is
  degrees, and sonar datasheets quote beamwidths in degrees, so a lone radian
  field among them would be a fresh trap of exactly the kind this fixes.
  (`Platform` went the other way, to radians, in §2c: it is a measurement filled
  by our own projector, not human-entered configuration.)
  `rx_beamwidths[i]` is consumed as radians with **no** conversion. Both branches
  are therefore correct whether or not
  [`marine_tools#82`](https://github.com/rolker/marine_tools/issues/82) (populating
  `rx_beamwidths` for the Kongsberg bridge) has landed.
- **"Documented in degrees" is the whole of it — it is not *configured* at all.**
  `Device::across_track_beamwidth` is exposed by no ROS parameter, no YAML and no
  launch file, so in practice it is permanently the hardcoded `2.0` that belongs
  to no particular sonar. Since `kongsberg_em_bridge` leaves `rx_beamwidths`
  empty on every M3 ping, that single constant is — after this fix — the **sole
  driver of the angular term for every M3 sounding**. Correcting its units
  without being able to set it right is only half the job; giving the offline
  tools a device/vessel configuration is
  [`#145`](https://github.com/rolker/cube_bathymetry/issues/145), for which this
  issue is the stated prerequisite.
- **The device value itself is not validated, unlike the per-beam one.** A
  `Device::across_track_beamwidth` of `0.0`, negative, or NaN is accepted as
  given, while a per-beam `0.0` is rejected. Deliberate for now — several tests
  set it to `0.0` precisely to isolate other terms, so a constructor that
  refused to build would break them — but the asymmetry means a misconfigured
  device silently deletes the very term the per-beam path is guarded against
  deleting. Worth revisiting with `#145`, which is what would first make the
  field settable by a human.
- **Validation, not just a length check.** A per-beam value is used only when it is
  finite, strictly positive, and **below π radians**; otherwise the device value is
  used. This matters: `norbit_driver`'s `conversions.cpp` resizes `rx_beamwidths` to
  a **zero-filled** vector for a beamwidth it does not report, so the old
  length-only check took the per-beam branch and silently zeroed the angular term.
- **The π-radian bound is a HARD PHYSICAL ceiling, not a plausibility clamp — and
  the difference matters.** A beam cannot subtend half a turn or more, so a value at
  or above π rad is nonsense: a unit mix-up, a sentinel, or a corrupt field. The
  bound is deliberately no tighter than that, because a wide reported beamwidth is
  not automatically a wrong one. `garmin_sidescan` reports **55° across-track** for
  SideVu (46° for ClearVu), and that is correct data in the correct field — a
  sidescan does no across-track beamforming, so its receive fan genuinely is that
  wide. Any clamp tight enough to be a plausibility check would discard it.
  - The corollary, stated plainly so it is not mistaken for a solved problem: **this
    ceiling does not catch a misplaced transmit fan.** The R2Sonic driver stamps
    `TxBeamwidthHoriz` — the whole transmit sector, ~2.27 rad (130°), identical on
    every beam — into `rx_beamwidths`
    (`r2sonic/src/conversions.cpp:17-18`). That is a real, correctly-scaled
    measurement of the wrong quantity, and it passes the bound. Before #144 the
    spurious `π/180` laundered it into a small-looking number; with the conversion
    correctly gone, the value now arrives intact and would size the angular
    uncertainty from the transmit sector rather than the beam. The fix belongs in
    the driver — report the receive beamwidth, or leave the array empty as
    `PingInfo.msg` provides for — and is tracked in the driver's own repo, at
    [`rolker/ros2sonic#1`](https://github.com/rolker/ros2sonic/issues/1). It is an
    **upstream** bug, present on the USF-COMIT parent and on four SeawardScience
    branches, not a local divergence.
  - **The fallback is not silent.** `DetectionsProjector` counts the beams that
    *took* the generic `Device::across_track_beamwidth` into
    `ProjectionDiagnostics::default_beamwidth_beams`. That covers both a reported
    value the model refuses (`ErrorModel::per_beam_beamwidth_usable`, public so
    the two cannot drift) and -- the commoner case by far -- no reported value at
    all, from an empty or too-short `rx_beamwidths`. The count is taken over the
    beams (`two_way_travel_times`), with `ProjectionDiagnostics::total` as the
    denominator, so the tools report "N of M beams". Counting *rejections* instead
    would read 0 for every M3 ping, where `kongsberg_em_bridge` leaves the array
    empty and so 100% of beams run on the default while nothing is rejected. The
    live node emits a throttled warning; `bag_to_geotiff`, `import_bag` and
    `batch_regen_bag` fold the count into their run summary and warn when it is
    non-zero, so the operator knows the angular budget is being carried by a
    default.
- **The fallback was the live path, not the rare one.** The pre-#144 note in this
  document had this backwards. `kongsberg_em_bridge/node.py:547-550` (repo
  `marine_tools`) deliberately leaves `rx_beamwidths` **empty** to dodge this very
  mismatch, so every M3 sounding took the too-large fallback; norbit data took the
  per-beam branch with a zero. No sonar in service was on the "normal" path this
  document described. The `kongsberg_em_bridge` "leave empty" comment becomes stale
  once this normalization ships — flagged on `marine_tools#82`, not fixed here
  (different repo; see also `cube_bathymetry#30`).
- **`/12`, not `/√12`, is the right divisor — but Calder applies it only to
  amplitude detections, and this port applies it unconditionally.** The
  previously-open `/12` vs `/√12` question is closed against the source above:
  where he divides, he divides by `12.0`, uniform-distribution style. What the
  earlier note here over-claimed is the *scope*. `rtn = bw/12.0` sits inside
  `if (SOUNDING_ISAMPDET(snd->flags))`; a phase detection takes
  `rtn = 0.2*bw/sqrt(np)` instead, and `EM300` takes neither. **This port has no
  detection-method input at all** — `marine_acoustic_msgs/SonarDetections`
  carries no amplitude/phase flag and no bottom-detection window size (`np`) —
  so it applies `beamwidth/12` to every beam of every sonar. That is a real
  divergence, recorded here as one: for a phase-detected beam Calder's σ would
  be `0.2·bw/√np`, which for a typical `np` of a few tens is several times
  *smaller* than `bw/12`, so the port over-estimates the angular term on phase
  detections. Narrowing it needs the flag and the window size to reach the
  message, which is a driver-and-message-definition question, not an error-model
  one.
- **The `1/cos(angle)` widening is still NOT ported — deliberately, and this is
  the record of that gap.** It was restored during #144's implementation and then
  backed out before the PR, because it is not the error model's decision to make.
  Calder applies the widening at only **three of his eight** device families —
  `device_compute_angerr`'s switch has eight family arms plus a `default` —
  EM120, EM3000/D and SB8125, each annotated *"flat plate and FFT beamformer"* —
  and the other five arms do not widen. The term is therefore a property of the array
  and its beamformer, not a universal geometric truth: applying it unconditionally
  would assert that every sonar we use is a flat-plate FFT beamformer, and applying
  it on top of a driver-reported per-beam width risks double-counting a width the
  driver may already have broadened. The error model should not have to know
  whether an array is flat; that geometry belongs with the driver, which knows what
  hardware it is talking to. Where the widening belongs, and what contract
  `PingInfo::rx_beamwidths` carries (nominal-nadir width vs. width at that beam's
  angle), is tracked in
  [`#148`](https://github.com/rolker/cube_bathymetry/issues/148). Until that
  lands, the port's angular σ is Calder's un-widened `beamwidth/12` at every
  angle, which for an oblique beam on a flat array is an under-estimate.

Pinned by `ErrorModelTest.AngleErrorBranchesAgreeOnUnits`,
`AngleErrorFallbackPinnedAtNadir`, `AngleErrorFallsBackOnEmptyBeamwidths`,
`AngleErrorFallsBackOnZeroFilledBeamwidths`,
`AngleErrorFallsBackOnNonFiniteBeamwidth`,
`AngleErrorFallsBackOnNonPositiveBeamwidth`,
`AngleErrorFallsBackOnPhysicallyImpossibleBeamwidth`,
`AngleErrorAcceptsWideButLegitimateSidescanBeamwidth`,
`PerBeamBeamwidthUsablePredicateMatchesTheCeiling`, and
`AngleErrorUsesReportedBeamwidthAsRadians`.

### Consequence: CUBE's tuned constants were calibrated against the old budget

Recorded, not acted on. `hypothesis.cpp`'s decision constants —
`bayes_factor_threshold`, `variance_scale`, `blunder_scalar` and friends — were
tuned while the angular term was ~3,283× too large in variance, i.e. against a
budget that made almost every sounding look uncertain. With the term at its
correct magnitude the same constants sit against a much tighter budget, so
outlier rejection rate and the relative weighting of nadir versus swath-edge
soundings both shift. This is documentation of a known consequence, not the
store re-measurement the operator explicitly dropped for #144; it is what a
future re-tuning effort should start from.

## 2c. Platform attitude is held in radians, not degrees (#147)

**Calder** keeps `Platform`'s roll and pitch in **degrees** and converts them at
each use site (`errmod_full.c`), consistent with the rest of his human-entered
vessel configuration.

**Port, before #147**: the port kept the degrees labelling
(`error_model.h`, `/* Roll in degrees, +ve is port side up */`) and the
degree conversions (`error_model.cpp`, in `swath_depth` and in `compute`'s
per-ping trig), but the fields' one and only producer is our own
`DetectionsProjector`, which assigns `tf2::getEulerYPR`'s **radians** straight
in. One producer, one consumer, and nothing to reconcile them: a 10° roll was
stored as 0.1745 and then read as 0.1745 **degrees**, so attitude entered the
model 57.3× understated — effectively switched off. It was invisible while the
beamwidth fallback inflated the angular term by ~3,283× in variance; fixing that
(§2b) made this the leading residual.

**Port, after #147**: `Platform::roll` and `Platform::pitch` are **radians**, and
the six `* M_PI / 180.0` factors that converted them are gone (two in
`swath_depth`, four in `compute`'s per-ping trig). This is the same
boundary-normalization choice §2b made for the beamwidth, resolved the other way
for a good reason: `Device` and `Vessel` are human-entered configuration read off
datasheets and survey reports, so they stay in degrees and convert once at
construction; `Platform` is a *measurement*, filled by our own projector from TF,
with no degrees-facing configuration surface to preserve. Normalizing the type is
cheaper than converting at the boundary and cannot drift.

**Sign conventions stay Calder's; the producer converts (#144).** Units were
normalized at the type here, but *signs* are not: every ported equation was
derived under Calder's conventions, so `Platform` keeps them and
`DetectionsProjector` converts at the boundary, exactly as it does for the
beamwidth's degrees.

- **Roll** — `+ve port side up` — coincides with REP-103 already: a
  right-handed rotation about +x (forward) lifts +y (port). `beam_angle()`
  negates the starboard-positive `rx_angles`, so its `meas_angle` is
  port-positive too and the two add coherently — rolling port side up swings a
  port-side beam further from vertical. No conversion; verified twice.
- **Pitch** — `+ve bow up` — is the **opposite** of what `tf2::getEulerYPR`
  returns for an FLU rotation (`pitch = -asin(R[2][0])`, a right-handed
  rotation about +y/port, i.e. bow-down-positive), so the projector negates it.
  An earlier revision of this document and of the `Platform` comment asserted
  the two senses agreed; that claim was only half checked — the roll half was
  right, the pitch half was not.
- **Heave** — `+ve down` — is likewise opposite to the REP-103 `+up` TF
  translation the projector reads, and is likewise negated there. Numerically
  inert (heave enters only squared), but kept consistent deliberately: crossed
  conventions inside one struct are what produced the pitch defect.

The pitch sign is latent at the defaults: the only terms **odd** in
`sin(pitch)` — `swath_heave`'s IMU lever-arm term, and the heading and pitch
cross-terms of the static `horizontal_positioning_error` — are all multiplied
by IMU/GPS lever arms that default to zero. That is why the sign test has to
configure a non-zero lever arm.

Pinned by `ErrorModelTest.PlatformRollIsRadiansAndSteersTheBeam`,
`PlatformAttitudeEntersTheVerticalBudgetInRadians`, `PositiveRollIsPortSideUp`
— the first tests in the suite to use a non-zero attitude at all, which is why
the defect survived so long — and
`DetectionsProjectorTest.BowUpPitchFollowsCalderSignConvention`, which is the
one that would fail if the negation were dropped.

## 2d. Eqn. 3.49's pitch term: a porting error, FIXED — not a divergence (#144)

Recorded here because this document is where the port-versus-Calder comparison
lives, but this is a **fixed bug**, in the same category as the two under
[#46](https://github.com/rolker/cube_bathymetry/issues/46) — not something the
port keeps on purpose.

**Calder** (`original_cube/libsrc/errmod/errmod_full.c:376-377`):

```c
    pitch_err = snd->range*snd->range * cosT*cosT * ws->sinP*ws->sinP
                * ws->total_pitch_var;    /* Eqn. 3.49 */
```

where `cosT = cos(DEG2RAD(plat->roll) + meas_angle)` — the swath-geometry
factor shared with Eqns. 3.47 and 3.48 immediately above it.

**Port, before this fix**: `swath_depth` used `cos(pitch)²` in place of
`cosT²`. The two sibling terms in the same function were faithful, so this was
a transcription slip rather than a decision. Its effect is a factor of
`1/cos²T`: unity at nadir, ~4× at a 60° beam, i.e. an over-estimate biased to
the swath edge.

**Port, after this fix**: `cosT²`, matching Calder. The slip is older than
this branch, but it was numerically invisible while `sin(pitch)²` was ~3,283×
too small (§2c) — #147 is what turns the term on, so it is corrected in the
same pass. Pinned by
`ErrorModelTest.DepthPitchTermUsesSwathAngleNotPitchCosine`.

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

## 5. Touchdown interpolation ports depth only (no `var_pred`)

**Calder**: `cube_grid_interpolate` (`original_cube/libsrc/cube/cube_grid.c`)
optionally outputs an interpolated prediction variance (`var_pred` via
`cube_grid_est_interp_error`) alongside the bilinear predicted depth at the
sounding touchdown.

**Port** (#59, ADR-0008): `Grid::interpolatePredictedDepth` /
`GeoGrid::interpolatePredictedDepth` thread only the interpolated **depth** into
`Sounding::predicted_depth_at_touchdown` — `Node::insert`'s slope offset is a
pure depth delta and has no variance consumer. A future consumer of prediction
variance at the touchdown must port `cube_grid_est_interp_error`.

## 6. Nomination uncertainty source

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
- [#144](https://github.com/rolker/cube_bathymetry/issues/144) — the beamwidth
  unit mismatch and the per-beam validation of §2b.
- [#145](https://github.com/rolker/cube_bathymetry/issues/145) — offline
  processing has no device or vessel configuration, so
  `Device::across_track_beamwidth` cannot be set for the sonar in hand.
- [#147](https://github.com/rolker/cube_bathymetry/issues/147) — the `Platform`
  attitude unit mismatch of §2c, fixed in the same pull request as #144.
- [#148](https://github.com/rolker/cube_bathymetry/issues/148) — where Calder's
  `1/cos(angle)` beamwidth widening belongs, given that he gates it by device
  family. Still un-ported; see §2b.
- [`rolker/ros2sonic#1`](https://github.com/rolker/ros2sonic/issues/1) — the
  R2Sonic driver stamps the transmit horizontal fan into `rx_beamwidths`; an
  upstream driver fault (present on the USF-COMIT parent and four
  SeawardScience branches) that the §2b physical ceiling deliberately does not
  paper over.
