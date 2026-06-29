---
issue: 87
---

# Issue #87 — Tier-2 TL-removed (depth-transferable) backscatter angular-response curve

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-28 20:15 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-87 at `24a0187`
**Mode**: pre-push
**Depth**: Deep (reason: substantive ADR-0007 addendum + cross-cutting estimator change threading a new field through a pack(1) binary-raster struct)
**Must-fix**: 2 | **Suggestions**: 3
**Round**: 1 | **Ship**: continue — one genuine, cross-pass-confirmed correctness concern (live-path R ≠ calibration R) warrants a fix or explicit bounding before the transferability claim holds.

### Findings
- [ ] (must-fix) Stale inverted-sign formula in the `range` field doc comment (`corrected = raw - (40*log10(R)+2*alpha*R) - residual`); validated code/ADR use `raw + TL(R) - residual`. Commit 24a0187 fixed the sign everywhere except this header — `include/cube_bathymetry/hypothesis.h:55`
- [ ] (must-fix) Live-path `slant_range = sqrt(x²+y²+z²)` = `R·√(1+sin²tx·sin²rx)` ≠ offline/Python-calibration `R = twtt·c/2` when tx_angle≠0 (cross-pass confirmed, Lens A+B); silently biases the live correction and breaks live/offline equivalence. Comment "norm IS the slant range" is inaccurate. Reconcile R, or bound+document the limitation — `src/cube_bathymetry_node.cpp:973-975` (cf. `sounding.h:52-54`, `import_bag_main.cpp:604`)
- [ ] (suggestion) `freqs` accumulated unconditionally even in tier-1 (unused); guard on `remove_tl` — `scripts/derive_angular_response.py:168-170`
- [ ] (suggestion) Python drops non-positive/NaN-range beams from bins entirely while C++ retains them (TL-skipped); note the intentional asymmetry — `scripts/derive_angular_response.py:189-196` / `src/node.cpp:397`
- [ ] (suggestion) `parse_args` missing a docstring (flake8-docstrings D103) — `scripts/derive_angular_response.py:58`

### Verified correct (both adversarial passes, no action)
- TL sign (added-back/TVG-style) consistent across Python, CSV, and `node.cpp:401`.
- Francois-Garrison freshwater A3/P3 coefficients + T>20/≤20 split are standard; 500 kHz/24 °C → ~0.049 dB/m verified by hand. Units (Hz→kHz, dB/m) consistent.
- Absorption (linear, per-bin via mean R) vs spreading (non-linear log, per-beam) aggregation is identical Python↔C++ by construction; α read verbatim in C++.
- `DepthAndUncertainty` 16→20 byte growth is layout-safe (only `sizeof()` uses are bag_to_geotiff RasterIO strides that auto-track; all else by-name).
- `range` threaded through every update/queueEstimate/recordBeam/queueFlush path; no silent drop.
- Tier-1 backward compatibility preserved (`apply_tl = apply_ara && tl_removed`; defaulted params/args; old loader API retained). Header parser tolerant; tests cover edge cases.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-29 03:56 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-87 at `03cf523`
**Mode**: pre-push
**Depth**: Deep (reason: substantive ADR-0007 addendum + cross-cutting estimator change threading a new field through a pack(1) binary-raster struct)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 2 | **Ship**: recommended — both round-1 must-fixes resolved (sign comment fixed; live-R bounded+documented for the M3 tx=0 case, the resolution round-1 explicitly permitted); two independent adversarial lenses + static analysis surfaced 0 new must-fix.

### Findings
- [ ] (suggestion) Optional producer-frame assert on live-path `R = sqrt(x²+y²+z²)` — verified correct for the M3 (sensor-frame norm, tx=0) and honestly documented; an assert would harden against a future source emitting the cloud in a vehicle/base frame — `src/cube_bathymetry_node.cpp:977`
- [ ] (suggestion) Optional one-shot warn when tier-2 is active but the range-present beam fraction is low; the documented retain-vs-drop asymmetry (C++ keeps NaN/non-positive-range beams as residual-only; Python drops them) is then load-bearing — `src/node.cpp:393-403`

### Verified resolved (round-1 must-fixes)
- Stale inverted-sign formula in `hypothesis.h` range-field doc now matches the validated `raw + (40*log10(R) + 2*alpha*R) - residual` model — `include/cube_bathymetry/hypothesis.h:54`.
- Live-path R vs offline-R discrepancy: the inaccurate "norm IS the slant range" comment (present at `24a0187`) replaced with a bounded, M3-tx=0-honest derivation; offline import uses `twtt*c/2` so live/offline match for the M3 — `src/cube_bathymetry_node.cpp:969-977`.

### Verified correct (no action, round 2)
- Round-1 suggestions also addressed: `freqs` accumulation now guarded on `--remove-tl`; `parse_args` docstring added; retain-vs-drop asymmetry documented.
- flake8 (ament profile) clean; `py_compile` OK. `pack(1)` 16→20 B growth layout-safe — `bag_to_geotiff.cpp:707` strides by `sizeof(DepthAndUncertainty)`; no whole-struct persistence / on-disk format break. No governance concerns (ADR-0007 addendum extended coherently; consequences handled; tests cover new tier-2 paths).
