# Plan: Per-pulse-mode backscatter normalisation (Part B, empirical range residual)

## Issue

https://github.com/rolker/cube_bathymetry/issues/172 (scope narrowed by the operator decisions in its second comment: Part B only; range term is an empirical per-mode residual, the declared gain state is a recorded check).

## Context

Backscatter is corrected at record time (`Hypothesis::recordBeam` -> `correctBeamIntensity`) from a single sheet-level `Parameters` curve (`angular_response_curve`, `backscatter_tl_removed`, `backscatter_absorption_db_per_m`), latched first-wins from SonarInfo (node `sonarInfoCallback`) or read once in an `import_bag` pre-pass (`loadCurveFromBagSonarInfo`). The curve is `db_relative_to_nadir`, binned by `|angle|`, so it cannot remove the 10-17 dB offsets between the M3's five pulse modes, and the cube#87 tier-2 add-back (`40 log10 R + 2 alpha R`) double-counts a range gain the sonar already applied.

Target: a per-mode table (angular curve + log-range slope + absolute offset against a reference mode), a per-ping mode carried to the estimator, and a time-ordered SonarInfo/detections association offline and live.

**Step 0 (already done, local only):** `feature/issue-170` (open PR #171) was merged into `feature/issue-172` (merge commit `2afec3e`) because this plan builds on its `derive_angular_response.py` (`--box`, TF-positioned pings, seawater absorption, provenance header, `test_derive_angular_response.py`). Do not push `feature/issue-172` before PR #171 is merged; rebase onto the merged result at that point.

## Design decisions

1. **Mode key.** From the latest SonarInfo, per transmit sector in sector order: `<pulse_us>us_<bw_hz>Hz_<type>`, pulse rounded to the nearest 1 us, bandwidth to the nearest 100 Hz, type one of `CW|FMU|FMD|UNK`; sectors joined with `+`. Example: `1000us_24000Hz_FMU`. A NaN, non-positive or missing element becomes `?`; empty arrays give the key `unknown`. Missing `tx_signal_types` means `UNK` for every sector (SonarInfo.msg: never assume CW). `SonarDetections` has no beam-to-sector map, so a multi-sector ping gets ONE key for the whole ping and the table cannot tell sectors apart within a ping (documented limit; the M3 is single-sector). A key containing `?`/`unknown` is valid and only matches an identical key or the `*` entry.
2. **Table file: one long-format CSV, mode as the first column.** Header row `mode,abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir,range_slope_db_per_decade,offset_db_vs_ref`; the two scalar columns repeat on every row of a mode (the loader rejects the table if rows of a mode disagree). Global `# key: value` comments: `# format: mode_table 1`, `# reference_mode`, `# range_ref_m` (R0, default 10), `# declared_tvg_model` (NONE|20LOGR|40LOGR|CUSTOM|UNKNOWN), `# declared_tvg_absorption_db_per_km` (number or `nan`), `# source_bags`, `# box`, `# derived_utc`. Why one file: one thing to hash for the fingerprint, one `--backscatter-curve` argument unchanged, one atomic write; per-mode files plus an index would add a second thing to keep consistent. Why mode first: the existing single-curve loader reads columns 0 and 3, so on a table file it fails to parse and yields an empty curve (a loud no-op warning) instead of silently merging all modes. A table may use the mode `*` ("every mode"); a legacy single-curve CSV loads as a table with exactly one `*` entry, with `tl_removed` behaviour exactly as today. A table that carries `# tl_removed: true` is rejected (the measured residual replaces the modelled add-back for new tables).
3. **Residual model: separable, per mode.** `L_m(theta, R) = c_m + g_m(|theta|) + s_m * log10(R / R0)`; the estimator subtracts `g_m(|theta|) + s_m*log10(R/R0) + o_m` where `o_m = c_m - c_ref`. `g_m` is the existing 2-degree-bin curve (nadir bin 0). `s_m` is the pooled WITHIN-angle-bin regression slope of intensity on log10(R) (per-bin intercepts as fixed effects). Within-bin matters: on a flat bottom R = h/cos(theta), so angle and range are confounded across bins and only depth variation within a bin identifies the range slope. Separable is chosen over a 2-D table because the data identify a 2-D surface poorly (range spread inside one bin comes only from depth change), a 2-D table needs far more samples per cell than cube_lab's 200-per-bin rule, and the live hot path stays a 1-D lookup plus one `log10`. Check on separability: the derive tool also fits the slope per angle third (inner/mid/outer) and warns when they differ by more than 6 dB/decade, recording the spread in the header; if real data show non-separability, a 2-D table is a follow-up. A mode whose range span is under 0.25 decade or has under 200 samples gets `s_m = 0` and a stated reason (and a curve only if the nadir bin has >= 200 samples, else "no curve", reported).
4. **Absolute offset.** `o_m` is measured against the declared reference mode (`--reference-mode`, default = the mode with the most samples) using time-adjacent pairs: pings of mode m within 5 s of a ping of the reference mode, same nadir-most angle band, compared at the median range of the range overlap after removing each mode's fitted `s` (so seabed change between passes cannot masquerade as an offset). Modes not directly overlapping the reference are chained through intermediate modes by least squares over the overlap graph. A mode in a component with no overlap path to the reference gets `offset 0` and `anchored: false` in a `# unanchored: <modes>` header, and the estimator counts it separately. Offsets outside the measured overlap range are extrapolation through `s_m`; recorded as `# offset_range_m: <mode> lo hi`.
5. **What the residual does to a real gradient (accepted cost).** `s_m` absorbs any genuine within-mode dependence of seabed reflectivity on depth/range (e.g. fine sediment in deeper water): that gradient is flattened together with the artefact and cannot be recovered from the store. The slope and its sample span are in the table, the import report prints the slope applied per mode, and the ADR amendment states it.
6. **Declared state is a check, not an input to the maths.** The table records the SonarInfo gain state it was derived under. At each SonarInfo the resolver compares `tvg_model` / `tvg_absorption_db_per_km` with the table; on mismatch it logs one WARN per (mode, state) pair (node: throttled) and counts the pings affected, but still applies the table. `UNKNOWN` is a valid recorded state and matches only `UNKNOWN`; a table recorded under `UNKNOWN` applied to a ping declaring `40LOGR` warns. Tables with no declared-state header are treated as `UNKNOWN`. Legacy single curves (`*` entry) skip the check (they carry no declared state) except their existing TL provenance validation.
7. **No-curve modes.** A ping whose key matches neither an exact entry nor `*` is identity and counted per key. `--backscatter-mode-fallback identity|nearest` (node param `backscatter_mode_fallback`, default `identity`): `nearest` picks the entry with the same signal-type tuple minimising `|log10(pulse ratio)| + |log10(bandwidth ratio)|`; none with the same type means identity. The fallback is recorded in the fingerprint and printed in the import report with counts per key.
8. **Per-ping state.** `Parameters` holds a `BackscatterCurveTable` (entries with precomputed curve, `s`, `o`, `log10 R0`) instead of the single curve; the legacy `angular_response_curve` / `backscatter_tl_removed` / absorption fields remain only to build the `*` entry. `Sounding` gains `uint16_t correction_idx` (0 = identity, k = table entry k-1), threaded `Sounding` -> `Node::addSounding` -> `Hypothesis::recordBeam` -> `correctBeamIntensity(raw, angle, range, params, idx)`. The resolver (mode key -> idx, fallback, counters) lives in a new `backscatter_mode.{h,cpp}`, outside the hot path. The `recon` projected-sounding spill (depth-adaptive two-phase path) carries `correction_idx` in its record; it is a same-run scratch file so no compatibility is needed, but its record size and any `static_assert`/test on it change. The raster `DepthAndUncertainty` layout is untouched (the correction happens before the Welford). Wherever a sheet/accumulator is built (`GeoMapSheet::setBackscatterCorrection`, `MultiLevelAccumulatorConfig`) the table replaces the curve fields.
9. **Time-ordered association.** Rule everywhere: a ping uses the latest SonarInfo whose `header.stamp` is at or before the ping stamp (SonarInfo.msg contract). Offline: `import_bag` reads `sonar_info` in the same bag-time-ordered main loop as detections/TF; the SonarInfo history (stamp -> key + declared state) is kept, and the ping's `correction_idx` is resolved at DRAIN time (inside the existing TF-frontier pending queue, which already holds a ping until the bag clock is `kGuardNs` past it), so a SonarInfo recorded slightly after the ping it describes is still seen. Pings with no SonarInfo at or before them are identity and counted ("before first SonarInfo"). The pre-pass `loadCurveFromBagSonarInfo` stays only for the legacy single-curve `auto` delivery. Live node: subscribes as today; the resolver holds a short history; each `/soundings` message takes the idx for its header stamp at callback time. A SonarInfo that arrives late applies to pings processed after it, never retroactively (the record-time Welford cannot be redone); pings that arrive before the first SonarInfo, or before the SonarInfo for a newly selected mode, take the previous mode's idx for the transport-latency window (bounded by the change-republish, not the 10 s heartbeat) and a counter `pings_with_stale_mode` is logged when a SonarInfo newer than a processed ping's stamp shows its mode differed. Table delivery in the live node is by file only (`backscatter_curve_file`); SonarInfo carries one curve, so `auto` keeps its latch-first single-curve behaviour as the `*` entry, and the file wins as today.
10. **`batch_regen`: documented unsupported for mode tables.** It has no SonarInfo source and its bit-exact incremental design would also need the timeline in its fingerprint. A table with any entry other than `*` is rejected with a clear message; `*`-only tables work as today. The resolver/timeline is written as a library so a follow-up can adopt it. A sidecar was rejected: it would be a second artefact to keep consistent with the bags.
11. **Fingerprint (ADR-0003).** The `backscatter_correction` key listed in ADR-0003 is not implemented yet (`build_fingerprint.h` says so). Implement it, additively within schema 2, for the stores `import_bag` writes: `{mode, curve_sha256, declared_tvg_model, declared_tvg_absorption_db_per_km, reference_mode, mode_fallback}`; any change is a full regen per the staleness table. Amend ADR-0003 and its "Implementation status" paragraph.
12. **Python mirror.** `kongsberg_em_bridge/angular_response.py` (marine_tools) mirrors the loader. Because a table file now parses to an empty curve in the old loader (safe failure), the bridge keeps working with single-curve files, but must gain `load_mode_table` to publish per-mode curves; that is a marine_tools PR, a consequence of this change, not part of it.

## Approach

Phased as three PRs on this branch lineage (the whole is too large for one review); each phase builds and passes tests on its own.

**Phase 1 — table, key, estimator (C++ core)**
1. `backscatter_mode.{h,cpp}`: `canonicalModeKey(SonarInfo)`, `BackscatterCurveTable` + `loadBackscatterTable(path)` (long-format parser, per-mode consistency check, `*` legacy wrap), `ModeResolver` (history by stamp, declared-state check, fallback, per-key counters).
2. `angular_response_curve.{h,cpp}`: `correctBeamIntensity(..., idx)` with the new term and offset; legacy `*`+`tl_removed` path stays byte-identical (golden test).
3. `Parameters`, `GeoMapSheet::setBackscatterCorrection`, `MultiLevelAccumulator` config take the table; `Sounding.correction_idx` threaded to `recordBeam` (node.cpp, hypothesis.cpp, recon.cpp spill record).
4. `build_fingerprint` `backscatter_correction` key.

**Phase 2 — importer and live node**
5. `import_bag_main.cpp`: read `sonar_info` in the main loop, history, drain-time resolution, `--backscatter-mode-fallback`, report (modes seen with counts, entry used, identity counts, warnings), remove the first-wins pre-pass for tables.
6. `cube_bathymetry_node.hpp`: resolver wiring, `backscatter_mode_fallback` param, counters; lifecycle: table reloaded on configure; late-SonarInfo rule above.
7. `batch_regen_main.cpp`: reject non-`*` tables; help text.

**Phase 3 — derivation, docs, validation**
8. `derive_angular_response.py` (on the cube#170 version): `--by-mode` (default for SonarInfo-bearing bags; needs `--sonar-info-topic`, default sibling `sonar_info`) groups pings by the same canonical key (Python port of 1, tested against shared vectors), bins by mode, fits within-bin slope, separability check, overlap offsets with chaining, writes the table with provenance; without `--by-mode` it writes today's single-curve CSV unchanged. SonarInfo is read through the same time-ordered pass the `--box` code already has.
9. ADR-0007: D3 amendment paragraph (empirical per-mode residual replaces the modelled add-back for new tables, declared state is a recorded check, accepted gradient cost, tier-2 curves still load); ADR-0003 amendment; README backscatter section and the parameter docs.
10. Appledore re-import (read-only on `~/data`, stores and tables under `/home/roland/project11/.agent/scratchpad/appledore_backscatter/`): derive the table from the five survey bags, re-import per bag with the table and with the old single curve, compare pass-to-pass differences on cells shared by bag stores; success = the step between passes gone (median overlap difference near 0, spread reduced against the single-curve baseline). Report results in the PR; no real data enters the repo.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/backscatter_mode.h`, `src/backscatter_mode.cpp` (new) | key canonicalisation, table loader, resolver |
| `include/.../angular_response_curve.h`, `src/angular_response_curve.cpp` | per-mode `correctBeamIntensity`, legacy path preserved |
| `include/.../parameters.h`, `src/parameters.cpp` | table replaces sheet-level curve fields |
| `include/.../sounding.h`, `src/node.cpp`, `src/hypothesis.cpp`, `src/recon.cpp`, `include/.../recon.h` | `correction_idx` threaded; spill record |
| `src/geo_map_sheet.cpp`, `include/.../geo_map_sheet.h`, `src/multi_level_accumulator.cpp`, `include/.../multi_level_accumulator.h` | table setter/config |
| `src/sonar_info_curve.cpp` | unchanged semantics; adds declared-state extraction helper |
| `src/import_bag_main.cpp` | time-ordered SonarInfo, drain-time resolution, flags, report |
| `include/.../cube_bathymetry_node.hpp` | live resolver, param, counters |
| `src/batch_regen_main.cpp` | reject mode tables |
| `src/build_fingerprint.cpp`, `include/.../build_fingerprint.h` | `backscatter_correction` key |
| `scripts/derive_angular_response.py` | per-mode derivation |
| `docs/decisions/0007-*.md`, `docs/decisions/0003-*.md`, `README.md` | amendments and usage |
| `test/test_backscatter_mode.cpp` (new), `test_angular_response_curve.cpp`, `test_import_*`, `test_node_lifecycle.cpp`, `test_build_fingerprint.cpp`, `test/test_derive_angular_response.py`, `CMakeLists.txt` | tests below |
| marine_tools `kongsberg_em_bridge/angular_response.py` (separate PR) | table loader mirror |

## Tests

- Synthetic two-mode stream with a 12 dB step at the mode switch: corrected levels have no step; the same stream with the single-curve `*` table keeps the step (proves the table does the work).
- Range residual removal on two synthetic streams, one with 20 log R and one with 40 log R already compensated (plus a flat one): the derived slope matches the injected slope within tolerance and the corrected level is flat vs range; generic over sonar behaviour (no law assumed).
- Within-bin identification: a stream where angle and range are confounded (flat bottom) but depth varies across pings recovers the slope; a purely flat-bottom single-depth stream yields `s = 0` with the "span too short" reason.
- Backward compatibility: the existing single-curve CSV (tier-1 and `tl_removed`) gives bit-identical corrected values to the pre-change code (golden values), and loads as one `*` entry; a table file read by the old column-0/3 rule parses empty.
- Key canonicalisation vectors (rounding, NaN, multi-sector, missing types) shared between C++ and Python tests.
- Declared-state mismatch warning (once per pair, counted) and `UNKNOWN`-matches-only-`UNKNOWN`.
- Fallback: identity counted vs nearest; no same-type candidate -> identity.
- Importer ordering: SonarInfo recorded after the first ping of its mode, SonarInfo before first ping, pings before any SonarInfo, interleaved TF; asserts per-ping idx via a recording hook.
- Fingerprint changes with table bytes, declared state, fallback.
- Depth-adaptive spill round-trip preserves `correction_idx`.
- Derive script: two-mode synthetic bag -> table with expected slopes/offsets, chained offsets, unanchored mode flagged, `--box` still applies per mode.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Capture decisions | ADR-0007 D3 and ADR-0003 amended in the same PR series; open questions recorded |
| A change includes its consequences | Python mirror, batch_regen, fingerprint, spill, README, derive script all listed |
| Human control and transparency | Declared state recorded and checked, UNKNOWN never assumed NONE, every identity fallback counted and reported |
| Improve incrementally | Three phases; Part A machinery and CUSTOM deferred |
| Test what breaks | Synthetic generic-over-sonar tests, golden legacy test, ordering tests |
| Only what is needed | One separable model, no 2-D table, no sidecar |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| cube ADR-0007 addendum (D3, tier-2, B.2) | Yes | D3 amendment; tier-2 curves still load; reprocessing is a bag re-import (B.2) |
| cube ADR-0003 fingerprint | Yes | `backscatter_correction` key implemented with table + declared state |
| uma ADR-0009 SonarInfo | Semantics only | no message change in this plan (CUSTOM field and curve publication are follow-ups) |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `correctBeamIntensity` signature | hypothesis, node, tests, any caller | Yes |
| curve file format | marine_tools `angular_response.py` mirror, bridge | Separate marine_tools PR (noted) |
| Sounding / spill record | recon, batch_regen record conversion (`SoundingRecord`) | Yes (batch_regen: carries idx 0 only) |
| fingerprint keys | ADR-0003 text, `isStale` | Yes |
| derive script output | README, tests, existing 170 header keys | Yes |
| latch-first node logic | lifecycle/reconfigure tests | Yes |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): README backscatter section and parameter list (`backscatter_mode_fallback`, table format, derive `--by-mode`), `--help` text of `import_bag` and `batch_regen`, ADR-0007 D3 and Deferred text on TVG, ADR-0003 key list and implementation status, header comments in `angular_response_curve.h`. The repo has no `.agents/README.md` parameter table (the workspace guidance does not apply here); if one is added meanwhile, update it.
- **Agent-instruction candidates** (proposals only): a note that angle and range are confounded for flat-bottom MBES data, so a range slope must be fitted within angle bins; belongs in a future `.agents/README.md`.

## Open Questions

- Reference mode default: most-sampled mode, or a fixed choice (1 ms FM) so tables from different campaigns share a reference?
- Is 5 s the right adjacency window for offset pairs given real switch cadence (hundreds per bag)? The derive tool will print the pair counts; confirm on Appledore.
- Pulse rounding to 1 us / bandwidth to 100 Hz: confirm the M3 reports stable values (check on the bag SonarInfo before coding Phase 1).
- Live node: accept the stale-mode window, or hold soundings for a short bounded delay (e.g. 200 ms) to let a change-republish arrive? The plan accepts the window and counts it.
- Should a mismatch of declared state be fatal for import_bag under a `--strict-declared-state` flag? Plan: warn only.
- Phase boundaries: three PRs as above, or one?

## Estimated Scope

Large: three PRs (core, importer/node, derive/docs/validation). Follow-ups, listed not planned: Part A declared-residual machinery; marine_tools research into the M3's gain law and setting `tvg_model`; the bridge publishing per-mode curves and the marine_tools mirror; CUSTOM curve field in SonarInfo; outer-angle cut; slope-aware incidence correction via a shared geometry module in `marine_backscatter`; `batch_regen` adopting the mode timeline; a 2-D range-angle table if separability fails.
