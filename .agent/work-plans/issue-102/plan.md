# Plan: read the angular-response curve from SonarInfo (auto-enable)

## Issue

https://github.com/rolker/cube_bathymetry/issues/102

## Context

Curve-delivery arc, consumer half. Wire format: uma#268 (merged, PR uma#269)
added `angular_response_tl` (UNKNOWN/TL_IN/TL_REMOVED) +
`angular_response_absorption_db_per_m` (NaN = unknown) to
`marine_interfaces/SonarInfo`. Producer: marine_tools#71 (PR mtools#72) —
`kongsberg_em_bridge` publishes the curve + provenance, latched
(`transient_local` depth 1), republished on change + 10 s heartbeat.

**Decisions (Roland, 2026-07-16)**: curve presence in SonarInfo auto-enables
the correction; explicit file/flag stays as the override for reprocessing.

## Behavior spec

- **Mode param** (`backscatter_angle_correction` / `--backscatter-correction`)
  gains `auto` and it becomes the **default** (was `none`):
  - `auto`: a valid non-empty SonarInfo curve enables the empirical
    correction; no curve → identity (same as today's default in practice).
  - `none`: hard off — SonarInfo curves ignored (log once).
  - `empirical`: as today; a curve is expected from file or SonarInfo, warn
    no-op when absent.
- **Curve source precedence**: explicit `backscatter_curve_file` /
  `--backscatter-curve` (non-empty) wins and SonarInfo curves are ignored
  (log once, throttled); else first valid SonarInfo curve; else none.
- **Provenance validation** (both consumers): non-empty curve with
  `angular_response_tl == UNKNOWN` → reject with warning (never guess the TL
  model). `TL_REMOVED` + NaN absorption → reject with warning too (alpha
  unknown ⇒ TL add-back impossible; producer publishes NaN only when the CSV
  header was missing/unparseable). `TL_REMOVED` + finite alpha → tier-2;
  `TL_IN` → tier-1 (alpha 0).
- **Latch-first**: apply the first accepted SonarInfo curve for the
  run/import; log and ignore later curves that differ (record-time Welford
  correction must not mix curves within one grid). Identical re-publishes
  (heartbeats) are silently fine.

## Implementation

1. **`angular_response_curve.h/.cpp`**: extend
   `parseBackscatterAngleCorrection` with `auto` →
   `BackscatterAngleCorrection::Auto` (new enum value in `parameters.h`;
   `correctBeamIntensity` treats Auto like Empirical — gate is a non-empty
   curve either way). New helper
   `curveFromSonarInfo(const marine_interfaces::msg::SonarInfo &,
   AngularResponseCurve & out, std::string & reject_reason)` → bool, doing
   the provenance validation + parallel-array length check + ascending sort
   (mirror of the file loader's postcondition). Add the back-reference
   comment pointing at the Python mirror
   (`kongsberg_em_bridge/angular_response.py`) — carried from mtools#71
   review. NOTE: this adds a marine_interfaces dep to the cube_bathymetry
   lib target (already a package dep for TileIndex etc. — verify
   CMakeLists links it for the lib, not only the node).
2. **Live node (`cube_bathymetry_node.cpp`)**: default of
   `backscatter_angle_correction` → `"auto"`. New param `sonar_info_topic`
   (default `sonar_info`, relative — resolves beside the M3 chain in the
   same namespace; on DeltaT platforms the topic never fires). Subscription
   QoS reliable + `transient_local`, depth 1. Callback: skip if mode none,
   or explicit file was configured, or a curve already latched (log
   differing curves, throttled); validate via `curveFromSonarInfo`; on
   accept, `geo_map_sheet_->setBackscatterCorrection(Empirical-or-Auto,
   points, tl_removed, alpha)` + INFO log (mirror the file-load logs).
   Config-time behavior: mode empirical/auto + file → load file as today
   (auto+file acts like empirical+file); mode auto without file → wait for
   SonarInfo. The empirical-no-curve warning stays for `empirical`; `auto`
   without curve is quiet (that is its point).
   Threading: callback runs on the same executor as soundings — check the
   node's callback groups; default single-threaded executor ⇒ no race with
   recordBeam. Verify before assuming.
3. **`import_bag_main.cpp`**: `--backscatter-correction` accepts/defaults
   `auto`. New optional `--sonar-info-topic <topic>` (default: derive from
   `--detections-topic` by replacing the last path element with
   `sonar_info`; document). Pre-pass: a second SequentialReader with a
   StorageFilter on the sonar_info topic scans for the FIRST valid curve
   (latch-first), before the main pass; then proceeds exactly like a file
   curve. Precedence identical to the node. Legacy bags (no such topic):
   pre-pass finds nothing, auto = identity, flag path unchanged.
4. **Tests** (`test_angular_response_curve.cpp` + a new
   `test_sonar_info_curve.cpp` if cleaner): parse `auto`;
   `curveFromSonarInfo` accept/reject matrix (TL_UNKNOWN reject, TL_REMOVED
   + NaN reject, TL_REMOVED + alpha accept tier-2, TL_IN accept tier-1,
   parallel-length mismatch reject, empty curve → no-op false, unsorted
   input sorted); Auto behaves as Empirical in `correctBeamIntensity`.
5. **Docs**: README (cube_bathymetry) `--backscatter-correction` /
  `backscatter_angle_correction` sections + the new auto default + SonarInfo
  source; ADR-0007 addendum? — check whether ADR-0007 D3 text names the CSV
  delivery; if so add an amendment note (auto-enable decision + SonarInfo
  supersedes CSV as primary delivery).

## Out of scope

- Retiring the CSV mechanisms (they remain the explicit override).
- Producer-side anything (done in mtools#71/#72).

## Verification

- Package unit tests green.
- Offline: build a tiny synthetic bag (or reuse test fixtures if present)
  with a SonarInfo + detections; `import_bag` with default auto picks up the
  curve (log line) and with `--backscatter-correction none` ignores it.
- Live: smoke — run the node, publish a latched SonarInfo with a tier-2
  curve, verify the accept log and that a later differing curve is ignored.

## Behavior-change note (for PR body)

Default flips from `none` to `auto`: deployments recording SonarInfo curves
start getting corrected backscatter without config changes — that is the
point of the decision; platforms without SonarInfo see no change.
