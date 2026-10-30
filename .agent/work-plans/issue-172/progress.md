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

## Plan Authored
**Status**: complete
**When**: 2026-10-30 13:43 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-172/plan.md` at `433e01f`
**Branch**: feature/issue-172 at `433e01f` (local, not pushed; `feature/issue-170` merged in as `2afec3e`)
**Phases**: 3 (core table/estimator; importer and live node; derive script, docs, Appledore validation)

### Open questions
- [ ] Reference mode default: most-sampled mode or a fixed one shared across campaigns?
- [ ] Is a 5 s adjacency window right for offset pairs? Confirm on Appledore pair counts.
- [ ] Confirm the M3's SonarInfo pulse/bandwidth values are stable under 1 us / 100 Hz rounding.
- [ ] Live node: accept the stale-mode window, or hold soundings a bounded delay?
- [ ] Declared-state mismatch: warn only, or add a strict flag to fail import_bag?
- [ ] Three PRs or one?

## Plan Review
**Status**: complete
**When**: 2026-10-30 13:52 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-172/plan.md` at `433e01f`
**PR**: PR-less (branch `feature/issue-172` at `bf42fdd`, local)
**Verdict**: changes-requested

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Needs work | Large; 129 lines vs plan-task's 30-80 guideline. The length is mostly design content that must be settled before code, and it points the same way as the split: two PRs, the science first (decision 6 below) |
| Issue alignment | Good | Follows the operator decisions (empirical per-mode residual, declared state only checked, Part B only). Acceptance is not restated for the narrowed scope |
| File targeting | Good | Missing: the node's `buildSession` (reset service) and import_bag's main-pass topic filter. Name nits listed below |
| Consequences | Needs work | The "old loader parses a table to empty" claim is false. The idx-0 = identity convention breaks producers that have no resolver |
| Documentation & instruction impact | Good | Non-silent. The candidate is framed as a proposal |
| Principle alignment | Needs work | Test what breaks: nothing checks the range functional form or the offset bias. Capture decisions: the ADR-0007 amendment is in Phase 3, after the behaviour it describes lands in Phases 1-2 |
| ADR compliance | Good | ADR-0003 key: `fromJson` must accept schema-2 files that lack it. ADR-0007 D3 amendment timing as above |
| ROS conventions | Good | The live SonarInfo reader needs depth > 1. Lifecycle reset path |

### Findings
- [ ] (must-fix) The safety claim that the old loader reads a table as an empty curve is false for digit-leading keys. `std::stof` and the bridge mirror's `_leading_float` both parse a numeric prefix, so `1000us_24000Hz_FMU,...` loads as angle = 1000 with column 3 (= `n`) as the dB value. Neither checks the angle range, so old binaries apply that garbage silently and the bridge would publish it in SonarInfo. Fix: start the key with a letter (type first, e.g. `FMU_1000us_24000Hz`) and detect tables by `# format:`. Run both loaders on a table file in tests — `plan.md:17-18`
- [ ] (must-fix) `correction_idx` 0 = identity breaks every Sounding producer that has no resolver. batch_regen's packed `ScatterRecord` round-trip ("carries idx 0 only"), the `Sounding(depth)` constructor, existing tests, and the live `auto` single-curve path would all silently drop a legacy curve. Make 0 mean "unresolved: use the table's `*` entry if any, else identity", with a separate explicit identity value. Add a batch_regen golden test with a legacy curve — `plan.md:24,108`
- [ ] (must-fix) g_m must be the within-bin fixed-effect intercepts referred to R0 (bin mean minus s_m times the bin mean of log10(R/R0)), not "the existing 2-degree-bin curve". On a flat bottom the raw bin means already hold the angle-correlated range term (log10(1/cos θ)), so subtracting s_m·log10(R/R0) as well removes it twice: about 9 dB at 60° for s = -30 dB/decade. Also state what the `mean_bs_db` and `db_relative_to_nadir` columns hold — `plan.md:19`
- [ ] (must-fix) Nothing checks the range functional form. The separability check tests only the angle × range interaction. `appledore_backscatter/mode_vs_range.txt` shows within-mode level vs range that one slope cannot fit. 20 µs CW at nadir rises 9 dB from 0.75 to 3.5 m, then falls 11 dB. 50 µs CW steps 13 dB between the 3-4 m and 4-6 m bins. The 1 ms FM fall accelerates the way an uncompensated 2αR would. These numbers cannot tell an instrument effect from substrate, and either way the scalar slope leaves it. Fix: record a per-mode binned residual-vs-log10 R diagnostic in the header, with a threshold. Have the operator choose a scalar slope or a binned log-range term before format v1 is frozen — `plan.md:19,47`
- [ ] (must-fix) The residual bias of the offset estimator is not stated. Removing the slope first is right. But the switch happens because the depth changed, so the shallower mode sees the shallower patch on both up- and down-switches. Wherever substrate correlates with local depth (rocky highs at Appledore) that bias does not cancel. There is also slope error × extrapolation distance, and the specular nadir band. State this and add an up-switch vs down-switch offset agreement diagnostic. Consider a shorter window (about 2 s at 22 Hz ping rate) — `plan.md:20`
- [ ] (must-fix) The mode key leaves out `PingInfo.frequency`. It is per-ping in SonarDetections (no association needed) and switches on multi-frequency sonars (EM2040, R2Sonic). A key frozen into a file format without it either breaks the "generic over sonar" goal or needs a format bump later. Include it, rounded, or document the limit explicitly — `plan.md:17`
- [ ] (suggestion) Sloping-bottom bias in s_m: folding port and starboard into |θ| bins on a cross-track slope pairs shorter range with smaller incidence, which biases s. Fit with signed-angle bins (side intercepts), keep the specular bins (<~10°) out of the slope fit, and state the residual — `plan.md:19`
- [ ] (suggestion) The 0.25-decade span test must use the within-bin spread of log10 R. Overall span is about 0.3 decade at 60° from angle alone, even on a single-depth flat bottom. Record each slope's SE, and judge the angle-third disagreement against the SE rather than a fixed 6 dB/decade — `plan.md:19`
- [ ] (suggestion) Add a `--backscatter-mode-fallback exclude` option (drop the beam's backscatter, keep its depth). Identity mixes values 10-20 dB off into the same Welford, which is worse than a gap — `plan.md:23`
- [ ] (suggestion) Use relative rounding for the key (3 significant figures) rather than absolute 1 µs / 100 Hz, so sub-µs pulses on other sonars do not collapse. Say whether the non-positive test runs before or after rounding — `plan.md:17`
- [ ] (suggestion) Record the declared TVG state per mode (a column) rather than as one global header. The derive tool should refuse or split when the declared state changes within a mode across the input bags — `plan.md:18,22`
- [ ] (suggestion) import_bag: add the sonar_info topic to the main-pass read filter (cube#107 `filter_topics`). Key the history by stamp and dedupe, because the bridge's heartbeats repeat the change stamp — `plan.md:25,41`
- [ ] (suggestion) Live node: reader QoS depth > 1 (the writer stays transient_local depth 1), so a rapid mode alternation cannot drop a change between callbacks. Keep the resolver and table across the reset service's `buildSession`, because the subscription does not redeliver — `plan.md:25,42`
- [ ] (suggestion) Fingerprint: `fromJson` must accept a schema-2 file without `backscatter_correction` (absent → null). Define the curve hash for `auto` (a canonical serialisation of the adopted curve). Note that `isStale` has no consumer yet — `plan.md:27`
- [ ] (suggestion) Tests to add: (1) a shared table fixture written by the Python test and read by the C++ loader test (issue acceptance 3 round-trip); (2) a linear-in-R absorption synthetic that reports the misfit; (3) a sloping-bottom synthetic; (4) the live node with SonarInfo arriving after soundings, checking the stale counter; (5) the fingerprint reading an old file — `plan.md:69-81`
- [ ] (suggestion) Appledore validation: (1) a numeric pass criterion, e.g. |median pass-to-pass step| < 2 dB on every bag pair with ≥ N shared cells, with the baseline measured first; (2) re-run the within-mode corrected level vs range table (issue acceptance 2); (3) a leave-one-bag-out check, because the table is fitted on the same five bags it is validated on — `plan.md:48`
- [ ] (suggestion) Offset: also estimate from the full-swath residual after removing g_m and s_m (more samples, away from the specular nadir) and report any disagreement — `plan.md:20`
- [ ] (suggestion) Restate acceptance for the narrowed scope (issue acceptance 1-2 were written for Part A). File the Part A and marine_tools law-research issues before implementation starts — `plan.md:5,129`
- [ ] (suggestion) Unkeyed gain state: Kongsberg runtime parameters (transmit power re max, receiver fixed gain) change levels and are not in the key. The derive tool should report within-mode level steps over time, and the plan should list this as a known limit — `plan.md:17`
- [ ] (suggestion) Plan accuracy nits. `Node::addSounding` does not exist; the path is `Node::insert` → `Node::update(..., intensity, beam_angle, range)`. The batch_regen record is the packed `ScatterRecord`, not `SoundingRecord`. derive `--by-mode --remove-tl` must be refused, since tables reject `tl_removed` — `plan.md:24,108,46`

### Open decisions (recommended answers)
- [ ] Reference mode: use a fixed reference per sonar (`--reference-mode`, documented M3 default `FMU_1000us_24000Hz`). When that mode is absent, fall back to the most-sampled mode with a loud "not comparable across campaigns" warning. A most-sampled default changes between campaigns, which recreates the step between mosaics.
- [ ] Adjacency window: make it a parameter with a 2 s default, and print pair counts and the up/down-switch agreement on Appledore before freezing it.
- [ ] Rounding: keep it, but make it relative (3 significant figures). Count the distinct raw pulse/bandwidth floats on one Appledore bag before Phase 1 (the analysis scripts saw 5 clean modes).
- [ ] Live stale window: accept it and count it, with reader depth > 1. The bridge publishes SonarInfo before the detections, and soundings take an extra hop through detections_to_pointcloud, so the window is rare in this topology.
- [ ] Strict declared-state flag: warn only for now. Print the per-(mode, state) summary in the import report.
- [ ] Phases: two PRs. PR 1: derive `--by-mode`, the table format, shared key vectors and fit diagnostics on Appledore, with no estimator change; it gates the format on real fits. PR 2: C++ loader, resolver, estimator, importer, node, fingerprint and the ADR amendments, plus the Appledore re-import.
