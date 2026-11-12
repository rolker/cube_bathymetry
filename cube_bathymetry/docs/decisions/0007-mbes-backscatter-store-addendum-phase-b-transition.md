# ADR-0007 (addendum): MBES backscatter — Phase B transition to empirical angular-response correction

## Status

Accepted (addendum).

**Amended 2026-07-16
([#102](https://github.com/rolker/cube_bathymetry/issues/102)):** the
correction's mode set and delivery changed. A third mode **`auto`** is now
the **default** (was `none`): a valid angular-response curve published in
`marine_interfaces/SonarInfo` beside the sonar stream — with TL provenance,
[uma#268](https://github.com/rolker/unh_marine_autonomy/issues/268), produced
per [marine_tools#71](https://github.com/rolker/marine_tools/issues/71) —
enables the empirical correction without configuration (decision: Roland,
2026-07-16). No curve = identity, so pre-SonarInfo deployments and bags are
unchanged. The `backscatter_curve_file` / `--backscatter-curve` CSV path
described in Decisions 2–3 below survives as the **explicit override**
(reprocessing with a better curve), no longer the primary delivery.
`batch_regen` deliberately rejects `auto` (it has no SonarInfo source);
bit-exact regeneration vs `import_bag` requires matching the effective
correction explicitly. This partially realizes the "SonarInfo (uma#240)"
future work in the Deferred section. Statements below reading "default
`none`" or describing CSV-only delivery are superseded accordingly.

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
4. **Fresh water only.** (Superseded by cube_bathymetry#170: the tool now
   computes the full Francois-Garrison absorption from `--salinity`/`--ph`, or
   takes an explicit `--absorption-db-per-m`, and records `absorption_source`,
   the model inputs and the source bags in the header. The C++ side is
   unchanged: it still applies the header's alpha verbatim.) As decided for
   cube_bathymetry#87: the salinity (boric-acid + MgSO₄) seawater absorption
   terms were out of scope, and the tool then rejected `--salinity > 0`.
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

## Per-pulse-mode addendum — the table replaces the curve for a mode-switching sonar (cube_bathymetry#172)

**Context.** The Appledore 2026 M3 product (2026-10-30) showed two things the
decision above cannot express. Within one pulse mode the M3's raw reflectivity
is nearly flat with slant range, so the tier-2 add-back of `40 log10 R + 2αR`
double-counts a range gain the sonar already applied; and the M3 switches
among five pulse modes hundreds of times per bag, with the modes 10 to 17 dB
apart at equal range — the pass-to-pass steps in the mosaic. One `|angle|`
curve corrects their mean and leaves each mode offset from it.

**Decision (amends D1, D3 and the tier-2 addendum).**

1. **A per-mode table is the correction for a mode-switching sonar.**
   `derive_angular_response.py --by-mode` writes one entry per pulse mode
   (`angular-response-table v1`): an angular curve `A(|θ|)` at a reference
   range R0, an EMPIRICAL range term `r_g(log10(R / R0))` per angle group of
   `|θ|` (binned log-range residual, measured, not modelled — it carries
   whatever range gain the sonar applied, absorption included), and the
   mode's level offset against one reference mode, measured at mode
   switches. The corrected beam is
   `raw − db_relative_to_nadir(|θ|) − r_g(log10(R/R0)) − offset`, every mode
   on the reference mode's nadir level at R0. No modelled transmission loss
   is added back to a table-corrected beam; the tier-2 add-back stays for
   single curves derived with `--remove-tl`.
2. **The mode key is shared.** `<TYPE>_<pulse_us>us_<bw_Hz>Hz` per transmit
   sector, `+`-joined, then `_<freq_kHz>kHz` from the ping's own frequency;
   one spec in the derive tool, the C++ `canonicalModeKey` tested on the same
   vectors (`test/data/mode_keys.json`). A ping's mode is the latest
   `SonarInfo` at or before its stamp plus its `PingInfo.frequency`; the live
   node reads the frequency from the `frequency` field
   `detections_to_pointcloud` writes on every point. The resolver keeps that
   SonarInfo history (same-stamp re-deliveries deduplicated) bounded — 60 s behind the newest
   resolved ping plus the one state in force at the cut, at most 4096
   states — since a mode-switching sonar republishes on every switch. A ping
   more than the window behind the newest one (time went backwards: a sim
   restart, a looped bag) re-anchors the window on itself. SonarInfo
   stamped behind the window is kept unpruned (and outside the 4096 cap,
   under a cap of its own) until such a ping arrives, and is then all the
   re-anchoring ping keeps; with none, it drops every stored state, since
   the old run's state in force can be stamped near the new run's start
   (SonarInfo is sent on change, and a heartbeat keeps the original stamp).
   Arrival order is the only signal: a clock-jump callback fires only for
   the ROS time source, which a replay without `--clock` and `import_bag`
   never use, and input delivered more than the window late is taken as a
   restart too. Its cost is unresolved pings until the next SonarInfo or
   heartbeat, never the old run's mode.
   `import_bag` holds the history from its oldest ping still waiting for the
   TF frontier, so a TF stall cannot cap away the states those pings need.
3. **Per-ping state in the estimator.** `Sounding::correction_idx` names the
   table entry (0 = unresolved → the legacy curve path, byte-identical to
   before; reserved identity and exclude values) and rides the sounding
   through the median queue to `Hypothesis::recordBeam`; the recon spill
   carries it so pass two corrects by pass one's mode.
4. **Declared state is a check, not an input.** The table records the
   `SonarInfo.tvg_model` / absorption each mode was seen under; the resolver
   compares every ping's declaration with it, warns once per (mode, state)
   and counts, and applies the table regardless (UNKNOWN matches only
   UNKNOWN; MIXED matches nothing).
5. **Uncorrectable pings and beams get a policy, never a plausible number.**
   A ping before the first SonarInfo, a mode with no entry or no measured
   offset (`offset_anchored=false`), an entry under the operator's ping
   floor, a beam in an angle bin whose variance the table could not write,
   a beam at or beyond 90°, a beam more than half a bin width beyond the
   outermost angle bin the table wrote (or short of the nadir-most one),
   or a beam with no finite angle or range, gets
   `identity` (recorded uncorrected, the default) or `exclude` (backscatter
   dropped, depth kept) — `--backscatter-mode-fallback`,
   `backscatter_mode_fallback`. Every case is counted per reason in the pass
   report: the ping-level cases per ping, the beam-level ones per beam and
   mode (counted as the beams are handed to the estimator, by the same test
   it applies). A table that does not load is an error, not an identity.
6. **Provenance.** The build fingerprint's `backscatter_correction` key
   records the mode, the curve/table kind and SHA-256, the fallback and the
   ping floor (ADR-0003); a store built under another table is stale.
   `batch_regen_bag` refuses a table (it reads no SonarInfo). A table run in
   which not one ping resolved to an entry is recorded as `kind:
   "table-unresolved"`, never `table`, and `import_bag` exits 4 with the
   reasons. Exit 4 means a table was given and no ping resolved a pulse
   mode; on a full run the store and its `table-unresolved` fingerprint
   were still written (exits 1 and 2 keep their code), and a
   `--level-plan-out` preflight, which writes neither, exits 4 too; the live node warns from the first unresolved
   cloud (once a minute while nothing has resolved, then once per new
   reason).

**Not done here, deliberately.** The inverse-variance beam weighting the
table's `var_db2` column was written for (plan decision 6) is not applied:
the persisted per-cell statistic is the unweighted Welford `{n, mean, M2}`
(the 3-band `MbesCell` reconstructs `n` from `sample_sd / standard_error`),
and a weighted Welford needs the weight sum and the sum of squared weights to
stay lossless across eviction, reload and the store round trip. On the M3 the
weights are flat (105–145 dB² in every bin) so the product is unchanged; the
loader uses the column only to tell an unweighable bin (decision 5). Applying
the weights is a follow-up with its own persisted-statistic change.

## Gain-state declaration addendum — an additive receiver-gain term before the table (cube_bathymetry#176)

**Context.** The per-mode table (previous addendum) cannot see a gain change the
sonar did not record. The Appledore M3 receiver gain stepped between
2026-08-26T20:17:30Z and the end of 2026-08-28: the reported level fell by 23 dB,
uniformly over the modes (CW 20/50 us +23.0 dB, 1 ms FM +22.7 dB, within the
quoted spread; measured from 528 overlapping 10 m cells). `SonarInfo` carries no
gain. A table derived across the step is fitted over two gain states, and every
import across it puts a 23 dB seam in the mosaic. Capturing the gain at the
source is rolker/marine_tools#97 and is not built here.

**Decision.**

1. **The operator declares the gain states.** A `gain-state-declaration v1`
   file names a reference gain state (a free label) and lists the deviations
   from it as half-open UTC intervals `[start, end)` with an additive offset in
   dB (the value applied is the canonical 6-decimal one the digest hashes), a
   mandatory provenance (`settings_file`, `measured`,
   `operator_reported`) and a mandatory note. The offset is the value ADDED to
   the reported level to reach the reference state: a segment that read 23 dB
   low is `+23.0`. Overlap is an error; a ping in no segment is AT the
   reference, offset zero, not unknown, and the run counts the pings per
   segment and in undeclared time so that a mistyped stamp shows as a wrong
   count. There is no fallback knob (decision 5 of the previous addendum is
   about modes, not gain). The file is campaign configuration: this repository
   ships the schema, the loaders and a test fixture only.
2. **The offset is applied to the raw intensity of the ping, before
   everything else** (`shiftSoundings`, at the three places that stamp
   `correction_idx`: `import_bag`, the node's `prepareCloudBackscatter`,
   `batch_regen`'s per-ping loop), for every ping in a segment whether or not a
   table is loaded or the ping's mode resolved. It is a receiver calibration,
   independent of the mode table. A ping whose mode fell back to `identity` is
   still shifted, an `exclude`d ping stays excluded, `--backscatter-correction
   none` and a single-curve run are shifted too, and a NaN intensity stays
   NaN. The corrected beam is therefore
   `(raw + offset) − db_relative_to_nadir − r_g − offset_db_vs_ref`: the table's
   terms are all additive dB, so the shift commutes with them and shifting
   first is correct provided the table was fitted from shifted data.
3. **Why not fold it into `correction_idx`.** That index is per mode and rides
   `Sounding`, the recon spill and the packed `ScatterRecord`; a mode × segment
   product would explode it and would leave a ping whose mode did not resolve
   uncorrected for gain, which is exactly the seam this addendum removes.
   Because the offset is baked into `Sounding::intensity` before the recon spill
   and the bucket write, nothing downstream applies it twice and no persisted
   record format changes.
4. **The derive tool fits in the reference state.** `derive_angular_response.py
   --gain-declaration` adds each ping's offset to its intensities before
   binning (single curve and `--by-mode`, and before the tier-2 add-back), so
   the angle bins, the range term, the switch offsets and the least-squares
   chain all see reference-state data and one table comes out, with no
   per-segment fitting. A table or curve derived under a declaration records
   `# gain_reference_state:` and `# gain_declaration_sha256:`.
5. **Declared inputs stay checked, not silently used.** `import_bag` and
   `batch_regen_bag` refuse, before any message is read, a curve or table whose
   recorded digest or reference state differs from the declaration supplied,
   one that records none while a declaration is supplied (a pre-#176 file
   carries none, and a zero-segment declaration is the escape hatch for a season
   that never crossed a step), and one that records a declaration while none is
   supplied. The record is both lines or neither, each once and non-empty, in
   the curve and table loaders alike; a malformed record is an error in all three
   tools (the node fails its configure), refused, not degraded to an uncorrected
   run. The `SonarInfo`-delivered curve (`auto`)
   has no header: a declaration with `auto` warns that the curve's gain state
   is unknown. The live node runs the same check at configure against its
   single curve or table (skipped under `none`), refuses a table or curve
   handed as the declaration and a declaration handed as the curve, and only
   warns, in every correcting mode, when the curve will come from `SonarInfo`. A
   source-side gain declaration (marine_tools#97) would be one more input on the
   same timeline — the loaders expose `offsetDbAt(stamp)` and nothing else, so
   such a term composes by sum — and, per the #172 addendum, would be checked
   against the declaration, not used by it.
6. **Provenance.** The build fingerprint (ADR-0003, schema 3) records the
   declaration's reference state and canonical digest; every store and
   per-mode table built before the declaration existed reads stale. The
   digest is of the canonical form (reference state, and each segment's start,
   end and offset), so editing a note does not stale anything.

**Not done here, deliberately.** `batch_regen_bag` cannot apply a per-mode
table (no `SonarInfo` source), so table + declaration parity with `import_bag`
waits for it to read `SonarInfo`; its supported paths (`none`, a single curve)
are bit-exact with `import_bag`. The live node reads the declaration at
`configure` only (no hot reload: a mid-run edit would split one store across
two declarations undetected; a runtime set while configured is rejected), checks
it against its curve or table at configure (a failed configure leaves no gain
state behind), and writes no fingerprint, so a node resuming
from a store built under another declaration is not detected. The Appledore
declaration file itself is campaign configuration, not part of this repository.

## Incidence-angle addendum — slope-aware correction as a separate, derived layer (cube_bathymetry#173)

**Context.** The correction above is indexed by the sonar-frame receive angle.
On a sloping seabed the angle the seabed sees differs from it by up to the slope,
so an upslope and a downslope pass over one ground read different levels. The
shared seabed-geometry module (`marine_backscatter::seabed`,
unh_marine_autonomy#304) gives the normal from a height surface and the
incidence against it. This addendum records how cube uses it. It does not claim
that slope explains any particular artefact; it makes the slope term
measurable.

**Decisions** (the operator's, 2026-11-10 and 2026-11-12, as recorded in the work
plan for #173).

1. **Two backscatter layers, never mixed.** The ordinary import stays as it is
   and writes the depths and the beam-angle layer. The slope-corrected layer is
   produced by a separate run (`import_bag --backscatter-angle-basis incidence`)
   that re-reads the bags, takes the slope from the finished depth layer and
   publishes a backscatter store of its own, with no depths. The status-quo layer
   always exists and the slope run can be re-run by itself. The default basis
   stays `beam` until an upslope and downslope comparison on real data shows the
   correction helps.
2. **The slope-corrected layer is a derived product, regenerated per tile.** Each
   tile records its contributing bags and the depth tiles it read (including the
   stencil margin), and the store records the inputs that must be the same for
   every tile (`derived_tiles.json`). A slope run rebuilds every tile it touches,
   refuses to rebuild a tile whose recorded contributors are not all among the
   bags it was given, and refuses a run whose inputs differ from the store's. It
   publishes only a complete run, crash-safely. Automatic detection of stale
   tiles and the lookup of bags are later work.
3. **The slope baseline is an angle.** The differencing baseline subtends a fixed
   angle at the sonar, so it scales with range (full baseline = slant range x
   angle, floored at one native cell either side). The parameter is in degrees,
   the run reports the baseline actually used and how often the floor decided,
   and the default (5 deg) is provisional until measured on Appledore.
4. **Where there is no surface the angle is the earth-frame angle from
   vertical**, not the sonar-frame angle, and the beam is counted per reason and
   never reported as slope-corrected.
5. **A table is only valid on the basis it was fitted on**, in both directions,
   and `batch_regen_bag` refuses incidence work. How the table says which basis
   it has is the implementation's: an incidence table has its own format line
   (`angular-response-table v2`) plus `angle_basis`, `slope_baseline_deg` and
   `slope_layers` records, so that an older binary refuses it instead of
   applying it to the beam angle.

**Left open.** The live node stays on the beam angle: it refuses an incidence
table at `configure` and has no incidence mode. The operator's earlier decision
(2026-11-10) for a flat-normal, earth-frame live mode predates the two-layer
design; with two layers it would put a third kind of value into the status-quo
layer, so it is not built, and whether it is still wanted is a question for the
operator.

**Consequences.**

- A slope-run backscatter store carries `derived_tiles.json` instead of a build
  fingerprint (ADR-0003). Ordinary runs and `batch_regen_bag` refuse such a store
  and the slope run refuses a beam-angle one.
- The per-beam table accounting is done on the incidence angle that is applied.
- The depths of the slope run's in-memory estimate are not compared with the
  ordinary run's: the slope run stays fixed-level and the depths are the ordinary
  import's.
- Cost: one normal is five height lookups; about 32 us per beam was measured
  over a real-sized processed store (README, *Slope-aware incidence correction*).

**Not done here.** Deriving an incidence table needs the slope run's per-beam
ledger (the beam index is not carried on the projector's output yet), which is
the second part of #173. The stale-tile tool, a live incidence mode, and a
sidescan use of the same layer record are not part of this addendum.

## Deferred (explicit follow-ups)

- **Full radiometric GeoCoder (tier-3)** — insonified-area, beam-pattern, TVG
  residual, and the depth/slope incidence term (ADR-0007 D3 / cube_bathymetry#15
  / #59). The empirical curve (tier-1) absorbs the aggregate angular falloff for a
  flat bottom; tier-2 makes it range/depth transferable; the per-mode table
  (#172 addendum above) absorbs the sonar's own range gain and mode offsets;
  the slope-aware incidence correction (cube_bathymetry#173, the shared
  seabed-geometry module of unh_marine_autonomy#304) is the incidence-angle
  addendum above (its table derivation is still to come); the area/beam-pattern
  terms remain future work. **Follow-up to file:** author the full,
  canonical `docs/decisions/0007-mbes-backscatter-store.md` document (this
  addendum folds into it).
- **Inverse-variance beam weighting** from the table's `var_db2` column
  (#172 addendum, "Not done here"): needs the intensity Welford to carry the
  weight sum and the sum of squared weights through the eviction spill and
  the `MbesCell` round trip.
- **TVG/absorption/frequency state in SonarInfo** (unh_marine_autonomy#240,
  marine_tools#95): the M3's gain law is still undeclared (`tvg_model`
  UNKNOWN); the per-mode table measures its effect instead of modelling it,
  and a declaration is checked against the table, not used by it.
- **Intensity domain (dB vs linear) as a first-class sonar property.** The
  correction assumes dB (a subtraction). Sonars reporting linear intensity would
  need a divide, or a domain conversion at ingest. **Follow-up to file:** model
  the intensity domain as a sonar property rather than assuming dB.

## Addendum (cube_bathymetry#96) — 3-band `MbesCell` write/reconstruct

unh_marine_autonomy#248 replaced the per-cell backscatter record
`{intensity, intensity_variance, timestamp, source_index}` with a **3-band
`Float32` Welford sufficient statistic** `{mean, standard_error, sample_sd}` (the
per-cell `timestamp`/`source_index` companions were dropped; coarse provenance is
now store-level `StoreMetadata`). The single layer is `survey` (draft/processed
collapsed). This addendum records how `cube_bathymetry` writes and reconstructs it;
the store round-trips the three floats bit-exactly and does no scaling itself.

### Write (`geoGridToBackscatterCells`)

From each node's `NodeRecord {intensity (running mean), intensity_var (estimate
variance = M2/(n−1)/n, NaN with <2 samples), n_samples}`:

- **`mean = intensity`** always.
- **n = 1 sentinel** (`intensity_var` is NaN — a single beam, no dispersion):
  `standard_error = 0`, `sample_sd = 0`. A finite `mean` with `sample_sd == 0`
  reconstructs to `n = 1`; distinct from no-data (`mean = NaN`).
- **n ≥ 2**: `sample_sd = sqrt(intensity_var · n_samples)` (the beams' sample
  standard deviation, `sqrt(M2/(n−1))`); `standard_error = kScale ·
  sqrt(intensity_var)` (the confidence-scaled standard error of the mean, true
  `SE = sqrt(intensity_var) = sample_sd/sqrt(n)`).

`kScale = kBackscatterConfidenceScale = 1.96f`, a single shared constant on both the
write and reconstruct sides (mirrors the bathy store's
`stddev_to_confidence_interval_scale` convention), so the round-trip is
self-consistent regardless of its numeric value.

### Reconstruct (`welfordFromCell`) — the exact inverse

Used to seed backscatter accumulation from a persisted `survey` tile on first tile
touch (seed precedence, ADR-0001 addendum) so an off-boat re-run blends with the
stored population rather than restarting it:

- `!hasData()` (`mean` NaN) → empty Welford `{n = 0}`.
- `sample_sd == 0 && isfinite(mean)` → `{n = 1, mean, M2 = 0}` (n=1 sentinel).
- else `SE = standard_error / kScale`; `n = round((sample_sd / SE)²)`;
  `M2 = sample_sd² · (n − 1)`.

`round()` absorbs float round-trip error in `n`. Round-trip guarantee: for a node
with ≥2 samples of genuine dispersion the reconstructed `(n, mean, M2)` — hence the
estimate variance `(M2/(n−1))/n` — reproduces the original. Enforced by
`test_store_import.WelfordFromCellInvertsEncode` and
`BackscatterWelfordRoundTripMultiSample`.

### Accepted limitation — n ≥ 2 identical samples

A cell whose n ≥ 2 samples are all **exactly** identical has `M2 = 0` →
`sample_sd = 0`, indistinguishable on reload from the n=1 sentinel: it collapses to
`n = 1`. For continuous corrected-dB data this is astronomically rare (exact float
equality across ≥2 beams), and the only consequence is a slightly under-counted `n`
on a subsequent re-survey blend of that one cell — the **mean stays exact**. It is
**not code-guarded**: a guard would need a separate "n but zero-variance" encoding
(a fourth band, or a reserved sentinel), which is not worth it for a
non-occurring case. Documented here as an accepted limitation.
