---
issue: 154
---

# Issue #154 — The error model never reads detection flags — an invalid beam would become seafloor at the surface

## Issue Review
**Status**: complete
**When**: 2026-09-10 14:49 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: [rolker/cube_bathymetry#154](https://github.com/rolker/cube_bathymetry/issues/154)
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Claim verification (issue body is third-party text, verified against source)

- **`detection_flags` declared, never read** — CONFIRMED, and the finding is
  narrower than it looks. `grep -rn detection_flags include/ src/` (from the
  `cube_bathymetry/` package root) returns exactly one hit: the declaration at
  `include/cube_bathymetry/error_model.h:249`. But `cube::Ping` — the struct
  that field lives on — is itself dead: nothing in `src/` or `include/`
  constructs a `Ping` or passes one anywhere. `ErrorModel::compute()`
  (`src/error_model.cpp:440`) takes `const marine_acoustic_msgs::msg::
  SonarDetections &` directly and reads `detections.two_way_travel_times`,
  `detections.tx_angles`, `detections.rx_angles`, `detections.ping_info.*`
  straight off the ROS message — never through `Ping`. So the fix should read
  `detections.flags[i].flag` directly in `compute()`/`Sounding`, the same way
  it already reads `rx_angles`/`tx_angles`, not resurrect `Ping`.
- **`ErrorModel::compute` never consults `SonarDetections::flags`** —
  CONFIRMED by reading the full call graph: `compute()`, `swath_depth()`,
  `swath_horizontal()`, `horizontal_positioning_error()`, `swath_angle_error()`
  (`src/error_model.cpp`) — no reference to `detections.flags` anywhere.
- **"An invalid beam carries a zero two-way travel time in the drivers that
  feed this"** — PARTIALLY confirmed; the blanket framing needs a correction.
  Checked the two live drivers under `layers/main/sensors_ws/src/marine_tools`
  and `layers/main/underlay_ws/src/ros2sonic/r2sonic` (read-only reference,
  not touched):
  - `kongsberg_em_bridge` (`em_datagrams.py:120-136`) decodes `twtt` from the
    datagram unconditionally and flags validity from a separate bit
    (`det_info & 0x80`) — an invalid beam's raw `twtt` is whatever the sonar
    reported, **not necessarily zero**. `node.py`'s `_publish()` (`skip_invalid_beams`,
    default `True`) drops invalid beams **before** they are ever appended to
    `msg.flags`/`msg.two_way_travel_times` at all — the compensation the issue
    describes is a drop, not a zero-fill. So at today's default config, a
    flagged-bad Kongsberg beam never reaches `compute()` in the first place;
    the zero-twtt hazard is latent, not currently observed in the M3 path.
  - `r2sonic` (`conversions.cpp:60`) sets `two_way_travel_times[i]` from
    `getScaledRange(i)` and **never populates `flags` at all** — grepped for
    `DetectionFlag`/`DETECT_OK` across that driver, no hits. `garmin_sidescan`
    likewise sets no flags. So for these two drivers, `detections.flags` is an
    empty vector today, and any bounds-guard the fix adds must treat "no
    flags reported" as OK (matching the existing bounds-guard pattern on
    `rx_angles`/`tx_angles`/`rx_beamwidths`), not as a rejection.
  - Net: the issue is right that flags alone are not sufficient protection —
    two of three drivers checked publish no flags whatsoever, so a
    flag-only gate leaves r2sonic and garmin_sidescan beams completely
    unguarded against a genuine zero/no-detect range. The travel-time
    validation the issue also asks for is the one guard that actually covers
    every driver; the flag check is currently Kongsberg-only insurance.

### Precedent (PR #153 / #144, merge commit `54e891f`, 2026-09-10)

Read `ErrorModel::per_beam_beamwidth_usable` (`error_model.h`/`.cpp`),
`ProjectionDiagnostics::default_beamwidth_beams` /
`missing_rx_angle_beams` (`detections_projector.h`/`.cpp`), and
`cube::ProjectionRunTotals` / `report_projection_summary`
(`projection_summary.h`). The pattern this issue should follow:

- **Reject, or something else? → NaN the sounding, let existing gates catch
  it — this is what the codebase already does, twice over.**
  `Sounding`'s constructor (`sounding.h`) already leaves `sonar_relative_position`
  NaN when `rx_angles` is missing/non-finite for a beam, and three independent
  downstream consumers already gate on `std::isfinite(sounding.depth)` before
  ingesting a sounding: `grid.cpp:53-56`, `geo_grid.cpp:55-58`, and
  `parameters.cpp:80-82`. `DetectionsProjector::project()`'s range gate
  (`detections_projector.cpp:200-206`) also drops a NaN-range sounding, since
  NaN fails both range-gate comparisons. **Important gap this review found**:
  the range gate alone does *not* catch a rejected beam whose *travel time* is
  the problem — with the shipped default `minimum_range = 0.0` (confirmed:
  grepped every `.yaml`/producer in the package, no config sets it above 0),
  a zero-range sounding has `range_sq == 0.0 >= minimum_range_sq_ (0.0)`, so
  it **passes** the range gate today. The fix must set `sounding.depth` (and
  ideally `vertical_error`/`horizontal_error`) to NaN directly for a rejected
  beam so the three depth-isfinite gates catch it regardless of the range
  gate's threshold — mirroring the `missing_rx_angle_beams` pattern (NaN the
  quantity, count it, let the existing consumers drop it) rather than adding
  a fourth, independent "skip this beam" branch inside `compute()`.
- **Diagnostics scope: warranted, not speculative generality.** #153
  established `ProjectionDiagnostics` as the place operator-visible per-beam
  rejection/fallback counts live, with a `total` denominator and matching
  entries in `ProjectionRunTotals`/`report_projection_summary` for the three
  offline tools plus throttled node warnings. A `flagged_beams` (or similarly
  named) counter, and ideally a separate one for the travel-time guard, is the
  same shape of gap #144 closed twice already — this is following an
  established, already-justified pattern, not adding one speculatively. Two
  siblings rather than one only if the two rejection reasons (bad flag vs.
  bad travel time) are operationally distinguishable and worth telling apart;
  worth deciding in planning, not assumed here.
- **Test naming/style precedent**: `test_error_model.cpp` and
  `test_detections_projector.cpp` already have the `AngleErrorFallsBackOn*`-
  style family for the beamwidth guard; a parallel family
  (`RejectsFlaggedBeam`, `RejectsZeroTravelTime`, `RejectsNonFiniteTravelTime`,
  `MixedPingRejectsOnlySomeBeams`, `TreatsMissingFlagsAsGood`) matches the
  issue's own "mixed ping" test ask and the codebase's existing convention.

### Scope Assessment

**Well-scoped?** Yes. Single, bounded fix inside `ErrorModel`/`Sounding` plus
a `ProjectionDiagnostics` counter and its two downstream reporting sites
(node warnings, offline-tool run summary) — the same surface #153 touched for
an analogous gap. No sub-issue split needed.

**Right repo?** Yes. This is `cube_bathymetry`'s own error model failing to
consult data its own message type carries; the companion driver-side issue
(`marine_tools#83`) is correctly filed in the other repo and is explicitly
gated on this one merging first — the ordering in the issue body is correct
and matches the precedent set by #144 → `marine_tools#82`.

**Dependencies**: `marine_tools#83` (driver stops dropping flagged beams)
is blocked on this issue, as the issue body states. No open cube_bathymetry
issue blocks this one.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Diagnostics counter (per #153 precedent) keeps the rejection operator-visible rather than a silent drop |
| A change includes its consequences | Watch | See Consequences below — this reopens the "two uncertainty/validity scales in existing stores" question #144 already surfaced, once more |
| Only what's needed | OK, with one judgment call flagged | The single new counter mirrors #153's precedent; whether to split it into two counters (flag vs. travel-time) is a real scope question for plan-task, not creep — see Recommendations |
| Test what breaks | Action needed (for plan-task/implementation) | Issue body's own ask (flagged-bad beam, zero-twtt-good-flag, mixed ping) is correct and should be to the letter; this review adds "treats absent flags as good" as a fourth required case, since r2sonic/garmin_sidescan publish no flags at all |
| Improve incrementally | OK | Same shape and size as #144/#153 |
| Workspace vs. project separation | N/A | Project-repo domain issue |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0013 — progress.md entry-type vocabulary | Yes | This entry itself; plan-task/implementation must continue the vocabulary |
| Others (0001-0010) | No | No workspace-infra, tooling, or Python-packaging surface touched |

### Consequences

- **Existing stores**: do **not** propose reprocessing/regeneration. The
  operator explicitly declined store re-measurement for the analogous #144
  gap (`docs/divergences_from_calder.md` "Consequence: CUBE's tuned constants
  were calibrated against the old budget" section) — the same
  document-not-reprocess disposition applies here. What to document, if
  anything: unlike #144's uniform "budget changed" case, this fix mostly
  makes previously-*silent* bad soundings visible/rejected rather than
  rescaling everything, so the store impact is narrower — the shallowest-water
  blunders this issue names (a full-depth zero-depth spike) would already be
  conspicuous outliers CUBE's own blunder rejection or an operator's QC pass
  would likely have caught, and at today's Kongsberg default
  (`skip_invalid_beams=True`) the hazard hasn't actually been firing for M3
  data. Net: worth a one-line note in `docs/divergences_from_calder.md`
  alongside #144's entry (a real gap existed, is now closed, no
  re-measurement performed), not a new discontinuity claim — there is no
  second uncertainty *scale* here, only a correctness gate.
- **`.agents/review-context.yaml` / package docs**: none found in this repo
  for `ProjectionDiagnostics` fields specifically (no project `.agents/
  README.md` exists in `cube_bathymetry` — checked, absent). `README.md`'s
  "two projection warnings" section (added by #144) should gain a third
  paragraph for the new warning, matching the existing pattern.
- **`marine_tools#83`**: still correctly gated on this issue; no action here.

### Recommendations

- Bounds-guard `detections.flags` exactly like `rx_angles`/`tx_angles` are
  bounds-guarded (`sounding.h`) — an empty or short-per-beam `flags` array
  must read as OK, not as a rejection, or every r2sonic/garmin_sidescan beam
  starts getting silently dropped.
- Validate the travel time independent of the flag path (the issue already
  asks for this) — it is the only guard that covers r2sonic/garmin_sidescan
  today, since neither driver populates `flags`.
- Set `sounding.depth` (and consider `vertical_error`/`horizontal_error`) to
  NaN for a rejected beam rather than skipping the push into the output
  vector, so the fix rides the three existing `isfinite(depth)` gates
  (`grid.cpp`, `geo_grid.cpp`, `parameters.cpp`) instead of relying solely on
  the range gate, which — at the shipped default `minimum_range = 0.0` —
  does **not** catch a zero-range sounding today.
- Decide during plan-task whether one `ProjectionDiagnostics` counter
  (e.g. `rejected_beams`) or two (flag vs. travel-time, mirroring the
  `default_beamwidth_beams`/`missing_rx_angle_beams` split) better serves
  operator diagnosis; either is a legitimate reading of the #153 precedent,
  this review does not have a strong preference.
- Add a one-line entry to `docs/divergences_from_calder.md` and a third
  warning paragraph to `README.md`'s existing "projection warnings" section,
  matching #144's documentation pattern.

### Actions
- [ ] Bounds-guard `detections.flags` per-beam (absent/short array = treat as good), matching the `rx_angles`/`tx_angles` pattern
- [ ] Validate travel time independent of flags (issue's own ask; confirmed the only guard covering r2sonic/garmin_sidescan, which publish no flags at all)
- [ ] NaN `sounding.depth` (not just position) for a rejected beam so the existing `isfinite(depth)` gates in `grid.cpp`/`geo_grid.cpp`/`parameters.cpp` catch it — the range gate alone does not, at the shipped `minimum_range = 0.0` default
- [ ] Decide one vs. two `ProjectionDiagnostics` counters for the two rejection reasons during plan-task
- [ ] Add doc lines to `docs/divergences_from_calder.md` and `README.md`'s projection-warnings section, matching #144's pattern; no store reprocessing (operator already declined that for #144)
- [ ] Tests: flagged-bad beam, zero travel time with a good/absent flag, non-finite travel time, mixed ping (only some beams rejected), and a beam with no flags reported at all treated as good
