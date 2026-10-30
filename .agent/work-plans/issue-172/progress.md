---
issue: 172
---

# Issue #172 — Per-pulse-mode backscatter normalisation with declared-TVG-aware transmission loss

## Issue Review
**Status**: complete
**When**: 2026-10-30 13:35 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #172
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-splitting

### Findings
- Split: Part A (declared-TVG residual TL) and Part B (per-mode curves + offsets) are separable; A ships first, B depends on A's per-ping correction state.
- Cross-repo: cube_bathymetry (estimator, importer, derive script, ADR-0007 amendment) and marine_tools kongsberg_em_bridge (set tvg_model, possibly publish the per-mode curve). Possibly unh_marine_autonomy if SonarInfo needs a CUSTOM TVG curve or a mode-offset field.
- Gap: curves are `db_relative_to_nadir`, so per-mode curves normalise away the very mode offset the issue wants removed. A separate absolute mode-offset term is required.
- Gap: existing tl_removed curves (full 40logR+2aR add-back) conflict with "UNKNOWN => TL identity". A curve must record the declared state it was derived under, and the estimator must check the ping's declared state against it.
- Evidence caveat: the issue's "raw nearly flat with range" holds near nadir for short ranges but the 1 ms FM mode falls ~22 dB over 8-43 m (mode_vs_range.txt), and 20 us CW rises ~7 dB over 0.5-4 m then falls. The law is not simply flat, and per-mode range regimes barely overlap, so mode offsets are only measurable in overlap ranges.
- Acceptance 2 (removes the range brightening) cannot be met by the mechanism alone while M3 stays UNKNOWN; it needs the research step or an operator declaration.
- Ping-to-SonarInfo association: SonarInfo is a separate topic; live ordering race and the importer's current pre-pass (curve only) must become a time-ordered merge. Sheet-level const Parameters must become per-ping state.
- SonarInfo carries one curve; per-mode curves need either the bridge publishing the matching curve per SonarInfo republish, or a message change.

### Actions
- [ ] Split into Part A and Part B issues (or keep #172 as umbrella); file the marine_tools TVG research/bridge issue separately.
- [ ] Define the curve/declaration consistency rule (curve records declared state; mismatch rejected or warned) and the fate of existing tl_removed curves; amend ADR-0007 D3 accordingly.
- [ ] Add an absolute mode-offset term and reference mode/range to the design; note offsets outside overlap ranges are extrapolation.
- [ ] Decide CUSTOM handling (no SonarInfo field for the curve today): operator-supplied file, or treat as UNKNOWN until the message gains it.
- [ ] Decide how acceptance 2 is met given UNKNOWN (research result, operator declaration, or reported-only trend) and restate it.
- [ ] Specify the mode key (canonicalised float pulse/bandwidth, ordered sector tuple, multi-sector sonars) and the SonarInfo-before-detections ordering guarantee live (udp_bridge) and offline.
- [ ] Note batch_regen (no SonarInfo source) needs a sidecar or stays unsupported for per-mode; drop source_level_db from the residual (constant offset only).
- [ ] Sequence A on top of open PR #171 (cube#170).
