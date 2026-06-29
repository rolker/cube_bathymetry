# ADR-0007 (addendum): MBES backscatter — Phase B transition to empirical angular-response correction

## Status

Accepted (addendum).

This is an **addendum**, not the canonical ADR-0007 document. The full
`0007-mbes-backscatter-store.md` (the per-beam co-estimation design and the
"D1–D5" decision points referenced throughout the `cube_bathymetry` sources) has
not yet been authored as a committed file in `docs/decisions/`; the design lives
in the source comments and the earlier `unh_marine_autonomy` ADR set. This file
records one concrete transition — the end of "Phase B" (the identity no-op) and
the introduction of an **empirical angular-response (ARA) correction** — so the
decision is captured now rather than lost. A follow-up will author the full
ADR-0007 document and renumber/fold this addendum into it. It deliberately does
**not** claim the canonical `0007` slot's content.

## Context

`Node::extractNodeRecord()` retains, per winning depth hypothesis, the per-beam
`{raw_intensity, beam_angle}` sufficient statistics (ADR-0007 D2/D3/D4). Until
now the node-output correction was a **Phase B no-op**: `corrected == raw` for
every beam, with the per-beam pairs kept so the surfaced backscatter stays
re-derivable once a real correction lands.

> **Superseded (cube#92/#93):** the correction was later moved to record time and
> the per-beam raw set is no longer retained — see the *Phase B.2 addendum* below.

An empirical study of the `m3_dryrun` Portsmouth Harbor survey (`/detections`,
2025 pings, ~445k beams) showed the earlier candidate model — a cos²θ Lambert
correction — is wrong for this data: it explains only ~¼ of the observed
nadir-to-edge falloff (~24 dB). The remainder is dominated by sonar-equation
terms cos²θ ignores (insonified area, beam-pattern, TVG residual). A
closed-form per-beam model is therefore not justified by the data on hand.

The angular dependence *is*, however, well captured empirically: binning real
beams by `|rx_angle|` produces a smooth, monotonic angular-response curve per
sonar. The `rx_angles` sign/zero convention was verified against
`kongsberg_em_bridge`: the producer negates the Kongsberg pointing angle
(+PORT → +STARBOARD), nadir = 0, radians, intensity in dB.

## Decision

1. **Replace the Phase B no-op with an empirical-ARA correction** at
   `Node::extractNodeRecord()`:
   `corrected_dB = raw_dB − curveRel(|beam_angle|)`, where `curveRel` is a
   per-sonar angular-response curve's `db_relative_to_nadir` column, **linearly
   interpolated between bin centres** by `|beam_angle|` in **degrees**. Nadir →
   `curveRel ≈ 0` → identity. Beyond the curve's max angle, a NaN `beam_angle`,
   mode `None`, or an empty curve all fall back to identity (raw). The curve is
   keyed on `|angle|`, so port/starboard beams of equal magnitude get the same
   correction.

2. **Configured curve, default OFF, sonar-agnostic.** No correction model is
   baked into the estimator (which serves M3, Norbit, R2Sonic, sim, …). The mode
   (`none` | `empirical`) and the curve file are operator-supplied at startup;
   the default `none` preserves the prior identity behavior on every existing
   deployment. An `empirical` mode with an empty/missing/unparseable curve is a
   **loud no-op** — a WARNING is logged (node `RCLCPP_WARN`, offline `std::cerr`)
   so the misconfiguration is visible rather than silent.

3. **Plumbed through both estimator entry points** — the live node
   (`backscatter_angle_correction` / `backscatter_curve_file` ROS params) and the
   offline importer (`--backscatter-correction` / `--backscatter-curve`). Both
   run the same estimator, so one mechanism corrects both the live and offline
   paths. The mode + curve are written into the sheet's private `Parameters` via
   `GeoMapSheet::setBackscatterCorrection()`; grids hold a `const Parameters&`,
   so the setting reaches every grid (call after construction, before/at
   processing).

4. **Offline derivation tool.** `scripts/derive_angular_response.py` reads a
   `SonarDetections` bag, bins `intensities` by `|rx_angles|` (2° bins by
   default), and writes the curve CSV
   (`abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir`). This is how an
   operator produces a curve for their sonar.

5. **Rename** `BeamIntensitySample::grazing_angle` → `beam_angle`. The field
   stores the receive/steering (beam/incidence) angle from nadir, not a true
   grazing angle (90° − θ).

## Consequences

- The surfaced backscatter (live `grid`/`~/tiles` and the offline `--bs-store`
  layer) is **uncorrected by default**; it becomes angle-corrected only when an
  operator opts in and supplies a curve. Documentation and the importer `usage()`
  reflect "uncorrected by default".
- The per-beam `{raw_intensity, beam_angle}` retention is unchanged, so the node
  value stays fully re-derivable when a richer correction lands.
  **(Superseded by the Phase B.2 addendum, cube#92/#93: raw samples are no longer
  retained — only an O(1) corrected Welford — so a richer correction now requires
  re-importing the bag rather than in-place re-derivation.)**
- Tests: 6 ARA cases at `extractNodeRecord()` (interpolation, nadir identity,
  beyond-max identity, NaN-angle identity, mode-None identity, port/starboard
  symmetry) plus curve-loader/mode-parse tests; the 3 pre-existing default-path
  intensity assertions are unchanged (default `None` ⇒ `corrected == raw`).

## Tier-2 addendum — TL-removed, depth-transferable curve (cube_bathymetry#87)

The tier-1 curve above bins backscatter by `|rx_angle|` only, so per-beam range
(spreading + absorption) is baked into the curve and it is **depth-regime
specific** (must be re-derived per survey). Tier-2 removes the per-beam **2-way
transmission loss** by **compensating (adding it back)** before the angular
response is characterized and applied — a distant return lost more energy, so it
is boosted to recover range-independent backscatter (TVG-style) — so the residual
curve becomes **range/depth transferable**:

`corrected = raw + TL(R) − residualCurve(|beam_angle|)`,
`TL(R) = 40·log₁₀(R) + 2·α·R` (R = per-beam slant range `twtt·c/2`, m).

> Sign note: TL is **added back**, not subtracted. An earlier draft subtracted it
> (making far beams dimmer → steeper curve + larger cross-bag spread); the
> validated fix compensates the loss, which flattens the curve and shrinks the
> spread.

Design decisions:

1. **The curve file is self-describing — one correction mode, not two.** The
   `Empirical` mode is unchanged; the estimator applies TL **iff the loaded curve
   says so**. The curve CSV header gains `# tl_removed: true` and
   `# absorption_db_per_m: <α>` (plus `# water_temp_c` / `# tl_model` provenance).
   Tier-1 curves carry `tl_removed: false` (or omit the lines) and keep working
   unchanged — fully backward compatible.
2. **α lives only in Python.** The Francois-Garrison freshwater absorption is
   computed once by `derive_angular_response.py` (`--remove-tl --water-temp-c`)
   and written into the header as a scalar. The C++ estimator reads that scalar
   verbatim and never recomputes α, so Python and C++ apply an **identical** TL by
   construction (a divergence would silently corrupt the correction). At 500 kHz /
   24 °C / fresh water α ≈ 0.049 dB/m.
3. **Per-beam slant range R is a new sufficient statistic** (Superseded by the
   Phase B.2 addendum, cube#92/#93: R is now consumed by `correctBeamIntensity` at
   record time and folded into the Welford, not retained per beam), threaded
   exactly like
   `beam_angle`: `Sounding::slant_range` → `DepthAndUncertainty::range` (the
   pack(1) raster struct grows to 20 bytes; layout-safe because every raster read
   strides by `sizeof(DepthAndUncertainty)`) → `BeamIntensitySample::range`,
   recorded on the winning depth hypothesis. The offline importer threads R from
   the `Sounding` detections ctor; the live node recovers R as the norm of the
   sensor-frame `/soundings` point (no new cloud field). A NaN / non-positive R
   skips the TL term (no log of a non-positive range).
4. **Fresh water only.** The salinity (boric-acid + MgSO₄) seawater absorption
   terms are out of scope for this issue; `--salinity > 0` is rejected by the tool.
5. **Multi-bag derivation.** `derive_angular_response.py` now accepts multiple
   bags (`nargs='+'`) and merges the per-bin sums — a real survey calibration
   spans many bags.

This still does **not** implement the full GeoCoder (insonified-area +
beam-pattern), which is the deferred tier-3. The TVG/absorption/frequency state
ultimately belongs in `SonarInfo` (unh_marine_autonomy#240); α is computed
locally meanwhile.

## Phase B.2 addendum — record-time correction + streaming Welford (cube_bathymetry#92/#93)

cube_bathymetry#93 moves the per-beam correction from **extract** to **record**
time and replaces the per-beam `{raw_intensity, beam_angle, range}` retention with
a streaming **Welford** `(n, mean, M2)` of the *corrected* intensity, kept on the
winning depth hypothesis. cube_bathymetry#92 then makes tile eviction **lossless**
by spilling and restoring that triplet.

This **reverses** two earlier decisions recorded above:

1. **Correction timing.** Decision 1 located the empirical-ARA (and the tier-2 TL)
   correction at `Node::extractNodeRecord()`. It now runs at record
   (`Hypothesis::recordBeam` → `correctBeamIntensity`, the per-beam math moved
   verbatim to `angular_response_curve.cpp`); extract only reads the triplet. The
   math is unchanged, so the node-output mean + estimate variance are **identical**
   to the old correct-at-extract path — only the timing moved.

2. **Raw-sample retention / re-derivability.** The Context and Consequences above
   state the per-beam `{raw_intensity, beam_angle}` set is retained so the surfaced
   backscatter "stays fully re-derivable when a richer correction lands." That is
   **no longer true**: only the O(1) corrected Welford is kept (the cube#93
   Massabesic OOM fix — per-cell intensity memory used to grow with total beam
   count). A future richer correction (the deferred tier-3 GeoCoder /
   cube_bathymetry#15) therefore requires **re-importing the source bag**, not
   in-place re-derivation from stored raw samples. Per-beam slant range `R`
   (Tier-2) is likewise folded into the correction at record and not retained.

Why this is acceptable: the empirical ARA + tier-2 TL correction the store needs
*today* is applied before the Welford folds each beam, so the persisted value is
the final corrected backscatter; applying a *different* correction was always going
to be a re-processing operation, and bounding live/offline RAM (the cube#92/#93
motivation) is the harder constraint. The Welford triplet is a perfect sufficient
statistic for the mean + estimate variance, so eviction spill/reload is
bit-identical to never-evicting.

This addendum **supersedes** the "retention is unchanged / re-derivable" statements
in the Context, Consequences, and Tier-2 sections above, and the backscatter
"out of scope; re-derivable per cube#15" trade-off note in
`0001-tile-eviction-and-incremental-publish.md` (§ Negative / trade-offs).

## Deferred (explicit follow-ups)

- **Full radiometric GeoCoder (tier-3)** — insonified-area, beam-pattern, TVG
  residual, and the depth/slope incidence term (ADR-0007 D3 / cube_bathymetry#15
  / #59). The empirical curve (tier-1) absorbs the aggregate angular falloff for a
  flat bottom; tier-2 makes it range/depth transferable; the slope-aware incidence
  correction and the area/beam-pattern terms remain future work, gated on the
  predicted-surface producer (#59). **Follow-up to file:** author the full,
  canonical `docs/decisions/0007-mbes-backscatter-store.md` document (this
  addendum folds into it).
- **TVG/absorption/frequency state in SonarInfo** (unh_marine_autonomy#240): the
  M3's already-applied TVG is the one genuine remaining unknown; the tier-1-vs-2
  comparison resolves it empirically. Until then α is computed locally from the
  per-ping frequency + water temperature.
- **Intensity domain (dB vs linear) as a first-class sonar property.** The
  correction assumes dB (a subtraction). Sonars reporting linear intensity would
  need a divide, or a domain conversion at ingest. **Follow-up to file:** model
  the intensity domain as a sonar property rather than assuming dB.
