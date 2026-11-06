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

## Implementation
**Status**: complete
**When**: 2026-10-30 14:52 -04:00
**By**: Claude Code Agent (Claude Opus)

**Scope**: plan fold + PR 1 ("science first": derive `--by-mode`, table format, key spec, diagnostics on Appledore; no estimator change). Not pushed, no PR.
**Branch**: feature/issue-172 at `558486f` (local; contains the unmerged `feature/issue-170` merge, so do not push before PR #171 lands)

### Commits
- `e246fac` plan: fold the plan review and operator decisions into the #172 plan
- `1099b39` derive_angular_response.py: --by-mode per-pulse-mode table (PR 1 of #172)
- `34395c3` test: the single-curve C++ loader reads a mode table as an empty curve
- `4e7282c` README: document derive --by-mode, the table format and the key spec
- `558486f` plan: record the range-cell and misfit-bin sizes as implemented

### Plan fold (must-fixes)
- MF1 old-loader safety: keys start with a letter (`FMU_1000us_24000Hz_500kHz`), tables detected by `# format: angular-response-table v1`; the C++ loader (gtest) and the marine_tools mirror (pytest, read-only import) both return an empty curve on the table fixture. No C++ loader change was needed.
- MF2 `correction_idx` 0 = unresolved (use `*` if present, else identity), explicit identity a separate value (PR 2).
- MF3 angular intercepts are bin means of `L - r`, referred to R0; `db_relative_to_nadir` = A minus the nadir-most bin.
- MF4 range term = binned log-range residual per mode (operator decision), quarter-decade nodes of log10(R/R0), linear between them, signed-angle bins, specular < 10 deg excluded; misfit diagnostic with threshold, scalar slope reported beside it.
- MF5 offset bias stated; up- vs down-switch agreement per pair; 2 s window parameter.
- MF6 frequency in the key (3 significant figures, plain decimal).
- Plus: inverse-variance column in the format now, fixed reference mode with loud fallback, declared TVG per mode (column), `--by-mode --remove-tl` refused, two PRs, nits (`Node::insert`/`update`, `ScatterRecord`), acceptance restated.

### Implementation notes / deviations from the plan text
- Range term nodes are linear between nodes (no steps at bin edges); "bins" in the operator decision are realised as nodes every quarter decade of log10(R/R0), node 0 at R0.
- Samples are accumulated in (signed angle bin, 0.01-decade range cell) sufficient statistics; the first 0.05-decade cells biased edge nodes by up to 0.6 dB (caught by the 20logR test) and were refined before the real run.
- Misfit bins are eighth-decade (half the node spacing), n >= 200 per bin.
- A mode whose ranges never reach R0 has its range term anchored at its nearest node; its offset carries the range law between that node and R0 (consistent corrected output, but the offset column alone is not a pure "mode step").
- Switch events are stored sparse after closing, so a sonar switching every ping stays bounded.

### Tests
`./sensors_ws/test.sh cube_bathymetry`: `Summary: 1317 tests, 0 errors, 0 failures, 90 skipped`. New: 20 key vectors (`test/data/mode_keys.json`, float32-widening case included), 12 dB step recovered within 0.5 dB, chaining through an intermediate mode + unanchored flag, 0/-20/+10 dB/decade residual shapes, seawater 2*alpha*R curvature (binned misfit < 1.5 dB, scalar misfit > 1.5 dB), sloping bottom (signed fit right, folded fit biased > 1.5 dB), specular leak only without the exclusion, misfit flag on a step between nodes, flat single-depth bottom not identified, variance column, history/tracker units, end-to-end bags with and without `--box`, table format, fixture, both old loaders. Legacy CSV byte-identity tests unchanged and passing.

### Appledore diagnostic (cube#172 PR 1)
Run: `--by-mode --box 42.976 -70.628 43.002 -70.596`, 7 bags, `/bizzy/sensors/m3/detections`, 32 min over the NAS. Outputs in `/home/roland/project11/.agent/scratchpad/appledore_backscatter/by_mode/` (table, diagnostics JSON, `summary.md`, run log). Box kept 2641422 of 2822535 pings; 1880 SonarInfo stamps; 0 pings before the first SonarInfo; 6810 pings without usable beams. Reference FMU_1000us_24000Hz_500kHz (requested, present); R0 = 16.68 m (its median slant range). Declared TVG = UNKNOWN for every mode.

| mode | pings | median R m | offset vs ref dB (SE) | misfit max / rms dB | scalar-slope misfit dB |
|---|---|---|---|---|---|
| CW_20us_50000Hz_500kHz | 1457065 | 3.1 | +1.19 (0.08) | 5.03 / 0.74 | 10.26 |
| CW_50us_20000Hz_500kHz | 866161 | 7.7 | -0.67 (0.04) | 7.07 / 0.68 | 13.79 |
| FMU_1000us_24000Hz_500kHz (ref) | 317497 | 16.7 | 0 | 12.97 / 0.58 | 58.21 |
| FMU_5000us_24000Hz_500kHz | 169 | 5.8 | -15.03 (1.22) | 4.56 / 1.33 | 15.27 |
| FMU_10000us_12000Hz_500kHz | 530 | 10.2 | -22.95 (1.52) | 4.77 / 1.09 | 4.06 |

Range term r (dB) at log10(R/R0) nodes, well-supported nodes only:
- CW 20 us: -1.75: -11.6, -1.50: -7.7, -1.25: -9.0, -1.00: -6.1, -0.75: -2.9, -0.50: -0.4, -0.25: -6.1 (rises from 0.3 m to 5 m, then falls; same shape as mode_vs_range.txt)
- CW 50 us: -1.50: -16.2, -1.25: -14.7, -1.00: -9.9, -0.75: -12.3, -0.50: +2.7, -0.25: +4.3, 0: 0, +0.25: -0.7 (the 15 dB jump between 3 m and 5.3 m is the earlier "13 dB step")
- FMU 1 ms: -0.25: +5.4, 0: 0, +0.25: -7.4, +0.50: -17.5 (falls faster with range, -30 then -40 dB/decade, the way 2*alpha*R would)
- The two rare FM modes rest on a few hundred pings each; their nodes have SE 1-2 dB.

Switches at the 2 s window: 1806 used, 1 discarded for a gap, 23 with no beams. Pairs:
- 20 us <-> 50 us: 1023 switches, o(50us) - o(20us) = -1.85 +/- 0.07 dB; up -2.38 (504), down -1.34 (519): directions DISAGREE by 1.04 +/- 0.14 dB (flagged; the depth-correlated seabed bias the plan names).
- 50 us <-> 1 ms: 695 switches, +0.66 +/- 0.04 dB; directions agree (-0.18 +/- 0.07).
- Rare-mode pairs: 2-40 switches, mostly one direction only; 20 us <-> 1 ms (13) differs 11.8 +/- 5.2 dB between directions; the chain is loop-inconsistent (chi2 44.9 on 5 dof), driven by the rare modes.

### Findings for the operator (gate format v1 before PR 2)
- [ ] Separability: every mode exceeds the 1.5 dB misfit threshold. The pooled residuals are within about 2 dB, but well-populated angle-group bins are not: CW 20 us 10-30 deg at about 7 m is -5.0 dB (3.2M samples) and 50-90 deg runs +1.6 to +4 dB over 16.6M of its 36M samples; FMU 1 ms 10-30 deg at about 40 m is -3.5 dB (0.54M). The rms misfit is 0.6-0.7 dB for the main modes. Decide: accept the 1-D range term, or add an angle-group dimension to the `range` rows (a v1 format change; cheap now, expensive after PR 2). The max-over-bins metric is also sensitive to thin bins (n >= 200); a minimum share of samples could be added.
- [ ] After each mode's own range term is removed, the three main modes sit within about 2 dB of each other: most of the 10-17 dB mode difference is carried by the range terms (and R0 anchoring), not the offset column.
- [ ] The rare FM modes (5 ms: 169 pings, 10 ms: 530) have poorly determined offsets (SE 1.2-1.5 dB, inconsistent loops). PR 2 needs a policy (exclude backscatter for modes under N pings, or accept).
- [ ] Per-beam scatter is large (sigma about 11 dB, variance about 130 dB^2 per bin), so the inverse-variance weights will be nearly flat across angle for the CW modes.
- [ ] Not done here: the within-mode level-vs-range re-check on corrected data and the pass-to-pass step need the estimator (PR 2).

## Implementation

**Date:** 2026-11-02. **Scope:** PR 1 format decisions settled on the Appledore data (operator decisions of 2026-11-02: range term per angle group; offset = level above the fixed reference mode after `db_relative_to_nadir` and range term, kept absolute rather than flattened; rare modes keep their entries; variance weights kept).

**Changes (branch `feature/issue-172`, on top of the first implementation entry):**
- `derive_angular_response.py --by-mode`: range term `r_g` per `|angle|` group (`--range-angle-edges`, default 30,50; `none` = one group); range rows carry `angle_lo_deg`/`angle_hi_deg`; header gains `range_angle_groups_deg`, `range_term_lookup`, `offset_meaning`, one `range_term` line per mode and group. Misfit also per HALF group (the per-bin intercepts hide within-group angle-by-range interaction otherwise).
- End-node trimming: an end node under max(200, 0.1 %) of the group's beams is dropped; beams beyond the kept span are counted (`out_of_span`), corrected with the clamped end value at apply time, left out of the fit, intercepts and misfit; `r_g` is 0 at R0 if inside the span, else at the nearest end node. Misfit bins need the same share. Why: run 2 anchored the 20 µs term at an R0 node of 6e3 junk beams (offset +49.5 dB).
- `--accumulation-cache`: plain-data pickle of the accumulated builder; the 35-min bag pass runs once, refits take 0.3 s; identity-checked (bags, topics, box, bin width, exclusion, edges).
- `--range-node-spacing-decades` (default 0.25; misfit bins at half).
- Tests: 496 in `test_derive_angular_response.py` (edge parsing/rejection, bin snapping, non-separable stream, apply formula on the two-mode stream, trimming of a junk sliver, share floor, cache round trip, finer nodes on a sub-node step); fixture regenerated; C++ single-curve loader still reads the table as an empty curve (11 gtests). Full package suite: 1335 tests, 0 failures. flake8/pep257 clean.
- README by-mode section and plan decisions 2-7, 9 amended; plan Open Questions carry runs 1-3.

**Appledore results (scratchpad `appledore_backscatter/by_mode/summary_run3.md`):** run 3 offsets vs the 1 ms/24 kHz FM reference: 20 µs CW −14.31 (0.10), 50 µs CW −0.98 (0.06), 10 ms FM −36.92 (1.58), 5 ms FM −4.96 (1.26) dB; misfit rms 0.53/0.62/0.59 dB for the three main modes, max 3.2-3.8 dB from step-like structure inside a quarter decade (50 µs at 3-4 m slant range, 20 µs at 7-9 m); eighth-decade nodes do not remove it. 50 µs ↔ 20 µs offset differs by direction by 1.40 ± 0.15 dB (seabed-with-depth bias).

**Open for the operator:** whether the remaining 2-3 dB sub-node residual is acceptable for the product (the misfit flag stays on either way); PR 2 can start on this format.

**Next:** `/review-code` pre-push on the branch, then push and open PR 1.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-02 11:57 -05:00
**By**: Claude Code Agent (Claude Fable 5.1)
**Verdict**: changes-requested

**Branch**: feature/issue-172 at `acfef22`
**Mode**: pre-push
**Depth**: Deep (reason: 3477 lines over 10 files)
**Must-fix**: 7 | **Suggestions**: 14
**Round**: 1 | **Ship**: continue — seven contract/correctness must-fixes (all mechanical, all addressed in 98c03ac, 02498f0, 810eb9e before round 2)

Specialists: static analysis (flake8, pep257, cpplint, lint_cmake, xmllint: clean), governance (no must-fix), plan drift (minor drift), Claude Adversarial Lens A + Lens B, cross-model Gemini + Codex (both ran). Repo has no `.agents/README.md` (context).

### Findings
- [x] (must-fix) NaN within-bin spread when a fit bin's beams all lie beyond the span lets an unidentified range term through as fitted — `derive_angular_response.py:_fit_group` (Lens A + Gemini, cross-model confirmed)
- [x] (must-fix) Angular intercepts masked by group 0's span drop specular beams and whole specular-only modes — `fit_mode` (Gemini + Codex, cross-model confirmed)
- [x] (must-fix) `x` written with 1-2 decimals: 0.125 nodes become 0.12, sub-0.2 deg bins collide — `mode_table_lines` (Lens B + Codex + Lens A)
- [x] (must-fix) Unanchored mode written with offset 0.000; README apply rule gives no guard — writer + README "Applying it" (Lens B)
- [x] (must-fix) README weighting rule `1/var_db2` undefined for the empty var_db2 the writer emits at n < 2 — README (Lens B)
- [x] (must-fix) Cache identity keyed on bag basenames: same-named bag of another campaign or a replaced bag reuses stale statistics — `accumulation_cache_identity` (Codex; Lens A/B as suggestion)
- [x] (must-fix) `abs_bin < n_half` admits beams at/beyond 90 deg when the bin width does not divide 90 — `ping_beams` (Codex)
- [x] (suggestion) Trimmed junk cells re-counted onto the next end node via clamping — `_trim_span` (Lens A)
- [x] (suggestion) Huge finite angle overflows the int cast into a negative bin, ValueError at flush — `ping_beams` (Lens A)
- [x] (suggestion) `none` edges with coarse bins yields an empty first group without error — `range_angle_groups` (Lens A)
- [x] (suggestion) Pickle trust boundary documented but not enforced; narrow except — cache loader (Lens B + Lens A)
- [x] (suggestion) Cache write failure discards a 35-min bag pass; temp file left behind — `write_accumulation_cache` (Lens B)
- [x] (suggestion) Cache hit skips the bag-pass reports (dropped pings, corrupt messages) — `main_by_mode` (Lens B)
- [x] (suggestion) README does not list the nan/empty field cases — README (Lens B)
- [x] (suggestion) `list(x or ())` fails on numpy arrays — `sector_mode_key` (Gemini)
- [x] (suggestion) README cache paragraph lists the window as a refit setting although it is part of the identity — README (Gemini)
- [x] (suggestion) Plan "Step 0" text stale (PR #171 merged) — plan.md (Governance)
- [x] (suggestion) pytest TIMEOUT 120→300 without rationale — CMakeLists.txt (Governance)
- [x] (suggestion) Plan Files table and decisions 2/4/5/7 missing the node-spacing flag, share floor, half-group rule, third zero reason, offset-chain details, extra header lines and tests — plan.md (Governance + Plan Drift)
- [ ] (suggestion) marine_tools mirror follow-up issue not yet filed — plan Consequences (Governance; carried to PR 2 as planned)
- [ ] (suggestion) Instruction-update candidates for a future `.agents/README.md`: angle/range confounding needs within-bin fitting; letter-first mode keys keep old loaders rejecting rows; "every existing loader reads a new format as empty" test pattern (Governance; proposals only)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-02 12:07 -05:00
**By**: Claude Code Agent (Claude Fable 5.1)
**Verdict**: approved

**Branch**: feature/issue-172 at `c5bf5b7`
**Mode**: pre-push
**Depth**: Deep (reason: 3477-line diff; round 2 reviewed the round-1 fix commits 98c03ac..810eb9e plus the whole branch cross-model)
**Must-fix**: 2 (cross-model; both mechanical, both fixed in c5bf5b7) | **Suggestions**: 9
**Round**: 2 | **Ship**: recommended — Claude Lens A + Lens B found no must-fix and confirmed all seven round-1 items fixed; the two cross-model must-fixes (fstat on the opened descriptor; a variance printing as 0.000) and every suggestion are addressed in c5bf5b7; 506 derive tests pass, lint clean

Specialists: Claude Adversarial Lens A + Lens B on the fix commits (fresh context), cross-model Gemini + Codex on the branch (both ran; first launch failed outside the worktree and was re-run with --work-plans-dir). Static analysis re-run on the changed files: clean.

### Findings
- [x] (must-fix) Cache trust check stat-by-name then open: substitute window — `load_accumulation_cache` now fstat's the opened descriptor (Codex; Gemini as suggestion)
- [x] (must-fix) A positive variance under 0.0005 dB² printed as 0.000 breaks the 1/var_db2 rule — written EMPTY (unweighable), rule in README/header/plan/fixture; Codex's alternative (write the small value) declined: a near-zero variance is clipped/identical beams and would dominate a cell (Codex + Lens B)
- [x] (suggestion) Range-row se_db could print 'nan' — empty when not finite, documented (Lens B)
- [x] (suggestion) No unanchored-mode example in the shipped fixture — added, with a zero-variance bin (Lens B)
- [x] (suggestion) Corrupt-message report lost on the no-SonarInfo exit — printed first (Lens A + Lens B)
- [x] (suggestion) Default angle edges not validated in parse_args — validated like explicit ones (Lens A + Lens B)
- [x] (suggestion) Unanchored offset 0.0 in the fit dict/JSON/warning contradicts the empty CSV columns — NaN/null everywhere, warning reworded, README line fixed (Gemini)
- [x] (suggestion) _trim_span dropped nodes[0] first when both ends were under the floor — weaker end first (Gemini)
- [x] (suggestion) --sonar-info-topic equal to the detections topic silently swallowed the detections — refused (Gemini)
- [x] (suggestion) A bag with none of the wanted topics skipped silently under --box — refused (Gemini)
- [x] (suggestion) bag_identity mtime in whole seconds — mtime_ns (Gemini)
- [x] (suggestion) README said the trimming floor needs both thresholds and that out-of-span beams do not shape the intercepts — wording matches the code (Codex)
- [ ] (suggestion) README "Header provenance" key table still lists only the single-curve keys — cosmetic, the by-mode section describes its own header (Governance round 1; carried)

## Implementation
**Status**: complete
**When**: 2026-11-06 10:22 -05:00
**By**: Claude Code Agent (Claude Fable 5.1)

**Scope**: PR 2 of #172 — the per-pulse-mode table applied in the estimator, the offline importer and the live node (PR 1, the derive side, merged as PR #174). Not pushed, no PR.
**Branch**: feature/issue-172 at `6c8ad85` (six commits on jazzy `7437948`)

### Commits
- `32dfda8` backscatter_table: load the per-mode table and share the mode-key spec
- `d67cb73` estimator: carry a per-ping correction index to the record-time backscatter correction
- `6440bdc` backscatter_mode: resolve each ping's pulse mode against the table
- `3ccbc8f` build_fingerprint: record the backscatter correction beside the tiling (ADR-0003)
- `de9d930` import_bag + live node: apply a per-pulse-mode table per ping
- `6c8ad85` docs: per-pulse-mode addendum to ADR-0007, the fingerprint key in ADR-0003, applying the table

### What landed
- `backscatter_table.{h,cpp}`: `keyNumber` / `canonicalModeKey` (tested on every case of `test/data/mode_keys.json`), the strict `angular-response-table v1` loader (a table is read whole or refused with the line named; per-mode scalar disagreement, overlapping groups, missing header keys all refuse), `correctWithTable` = `raw - db_relative_to_nadir - r_g(log10 R/R0) - offset` with the angle-group lookup rule, node interpolation clamped beyond the ends, nearest-centre bin for the unweighable check, fallback identity|exclude.
- `backscatter_mode.{h,cpp}`: resolver (stamp-keyed deduplicated SonarInfo history, latest at or before the ping, declared-TVG check warned once per (mode, state) and counted per ping, fallback for NoSonarInfo / NoEntry / Unanchored / UnderPingFloor, late-SonarInfo stale count, report). `sonarInfoStateFrom` in `sonar_info_curve`.
- `Sounding::correction_idx` (0 unresolved → legacy curve path byte-identical, 0xFFFE exclude, 0xFFFF identity, 1..0xFFFD entries) through `DepthAndUncertainty` (22 bytes), `Node::update`/`queueEstimate`, `Hypothesis::recordBeam` (NaN back = skipped), the recon spill; `GeoMapSheet::setBackscatterTable`; `MultiLevelAccumulatorConfig`.
- import_bag: table via `--backscatter-curve` (format line), `--backscatter-mode-fallback`, `--backscatter-mode-min-pings`, SonarInfo topic in the main-pass read filter and resolved at drain time, resolver report, fingerprint record; main() refactored into five helpers (cpplint fn_size). Live node: `backscatter_mode_fallback`, `backscatter_mode_min_pings`, broken table fails configure, SonarInfo depth 10, table re-installed on reset, report at cleanup. `detections_to_pointcloud` writes a `frequency` field per point. batch_regen refuses a table.
- Fingerprint `backscatter_correction` {mode, kind, source, sha256 (OpenSSL, `libssl-dev`), fallback, min_pings}; a pre-#172 file is stale.
- marine_tools mirror follow-up filed: rolker/marine_tools#96.

### Deviations from the plan (recorded in plan.md + the ADR-0007 addendum)
- Decision 6: inverse-variance Welford weights are NOT applied — the persisted statistic is the unweighted `{n, mean, M2}` (eviction spill + the 3-band MbesCell round trip), and a weighted Welford needs the weight sums carried through both; the column serves only the unweighable-bin cut. Flat on the M3, so the product is unchanged; follow-up listed in the ADR's Deferred section.
- Decision 9: `nearest` fallback dropped (no honest metric; a neighbour's entry reintroduces the mode steps); `--backscatter-mode-min-pings` added as the apply-time answer for the 5 ms FM mode.
- Decision 10: the live node's clouds carried no frequency; `detections_to_pointcloud` now writes it (point_step 28 → 32), an older cloud keys as `?kHz` and gets the fallback.
- Per-beam uncorrectable cases (unweighable bin, no finite range) take the fallback but are not counted per beam (const parameters inside the estimator); the loader reports unweighable bins per mode at load time.

### Tests
Full package suite: 1395 tests, 0 failures (96 cppcheck skips, as before). New: `test_backscatter_table` (9), `test_backscatter_mode` (5), fingerprint key + SHA-256 vectors (2), `correctBeamIntensity` index paths, `recordBeam` exclude, `Node` queue carries the index, recon spill round trip, two live-node lifecycle tests. Lint (uncrustify, cpplint, xmllint, lint_cmake, cppcheck) clean. Not covered: import_bag's main-pass SonarInfo ordering end to end (no C++ synthetic bag with SonarInfo; the Appledore re-import is the integration run).

### Next
`/review-code` pre-push on the branch; after the PR, the Appledore re-import with the run-4 table (`--backscatter-mode-fallback exclude`) → candidate D for the operator's eyes.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-06 10:29 -05:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-172 at `e7e2022`
**Mode**: pre-push
**Depth**: Deep (reason: 3398 lines over 42 files, plus substantive ADR-0003/ADR-0007 amendments)
**Must-fix**: 8 | **Suggestions**: 18
**Round**: 1 (first review of PR 2; the two earlier entries reviewed PR 1, merged as PR #174) | **Ship**: continue — eight must-fixes. Two are correctness bugs: the range-term group lookup, and the false NO-OP warning. Two are design-level: an unbounded SonarInfo history in the live node, and a table run where nothing resolved still exiting 0 with a `kind:table` fingerprint. These warrant a second independent read after the fix.

Specialists: static analysis (cpplint, uncrustify, xmllint, lint_cmake: clean; cppcheck 2.13 skipped by ament on this host, as in the package tests), governance + plan drift, Claude Adversarial Lens A and Lens B (Deep horizon, fresh context), and cross-model Gemini and Codex (both ran, EXIT=0). Local Adversarial is off (the default). The repo has no `.agents/README.md` or `review-context.yaml`. Plan drift: the Files to Change table matches the diff, and the declared deviations are recorded in plan.md and ADR-0007, except that ADR item 5 still claims per-beam cases are counted.

### Findings
- [x] (must-fix) `groupFor`: a beam below the lowest PRESENT group's `angle_lo_deg` takes that group's range term. When the writer omits the first declared group ("zero:" status, no rows), beams in that group and in the specular band get the next group's r_g instead of the zero term the format specifies. Fix: honour `# range_angle_groups_deg` or the `# range_term:` lines, and add a test with an unfitted first group. — `cube_bathymetry/src/backscatter_table.cpp:375-388` (lead + Codex, cross-model confirmed) — fixed in `818ccb6` (+ load report `c2ed3cf`)
- [x] (must-fix) `cube_bathymetry_level_plan` is exported with a transitive `OpenSSL::Crypto` link, but `ament_export_dependencies` lacks OpenSSL. A downstream `find_package(cube_bathymetry)`, such as ui_ws `marine_perception_tools`, fails to resolve the target. — `cube_bathymetry/CMakeLists.txt:117,644` (Governance + Lens B) — fixed in `7874b06`
- [x] (must-fix) README says `soundings` carries seven float32 fields. `detections_to_pointcloud` now writes eight (`frequency`, point_step 32). — `README.md:16-17` (Governance) — fixed in `78ba0cc`
- [x] (must-fix) README says the beam "is weighted by `1 / var_db2`". Weights are not applied (decision 6 deviation). The "PR 2's loader" and "what PR 2 does" wording is stale too. — `README.md:~933,1062,1080` (Governance) — fixed in `62d75a1`
- [x] (must-fix) The live node logs "backscatter_angle_correction=empirical but no curve was loaded ... NO-OP" when a valid per-mode table IS loaded: the `bs_curve.points.empty()` branch lacks `!bs_file_is_table`. Add a lifecycle test with `empirical` + table. — `cube_bathymetry/src/cube_bathymetry_node.hpp:237` (Gemini) — fixed in `9b64de2`
- [x] (must-fix) `BackscatterModeResolver::history_` is never pruned. The M3 bridge republishes SonarInfo on every acquisition-signature change, which happens every ping under alternating multi-mode pinging, so a long-running live node grows without bound (roughly 250 MB a day at 10 Hz). Prune it behind a horizon without breaking the stale logic, and dedupe a new stamp whose state equals its predecessor's. — `cube_bathymetry/src/backscatter_mode.cpp:78-103` (Lens B; Lens A + Gemini as suggestion) — fixed in `83d3858`
- [x] (must-fix) With a table configured, a run in which no ping resolves still exits 0, prints only the report, and writes the fingerprint `kind:table` + sha256. That happens with a wrong `--sonar-info-topic`, with no SonarInfo in the bags, or with a live cloud without `frequency`. The result is uncorrected data with false provenance. Fail or warn loudly in import_bag when `sonar_info_seen == 0` or 0 pings resolved. In the node, warn once on a cloud lacking `frequency` and once when N pings have resolved nothing. — `cube_bathymetry/src/import_bag_main.cpp:2686`, `cube_bathymetry/src/cube_bathymetry_node.hpp:~2616` (Lens B) — fixed in `c54c922`
- [x] (must-fix) `setupBackscatter` builds the resolver with `&out.table`, then `return out;`. That relies on NRVO, which is not guaranteed; a move would leave `table_` dangling. Fix: make the resolver own or share the table, or build it at the call site, or delete the struct's copy/move. — `cube_bathymetry/src/import_bag_main.cpp:1095-1147` (Codex; Lens B as suggestion) — fixed in `75bafb4`
- [x] (suggestion) Beam-level fallbacks (NaN angle, no finite range, unweighable bin) are not counted per beam, yet ADR-0007 item 5 says "every case is counted per reason". Either count them or reword the ADR and the `--backscatter-mode-fallback` help. — `cube_bathymetry/src/backscatter_table.cpp:736-744`, ADR-0007 item 5 (Codex + Lens B + Governance) — fixed in `fdfb0e7` (counted, ADR item 5 now true)
- [x] (suggestion) A beam at |θ| >= 90 deg gets the clamped angle term but a zero range term. The writer drops such beams, so the C++ side could treat them as uncorrectable (fallback). — `cube_bathymetry/src/backscatter_table.cpp:375-388,747` (Lens A) — fixed in `e7aff1d`
- [x] (suggestion) An empty (width 0) cloud is still resolved with NaN frequency, so it counts as a `?kHz` NoEntry ping and may fire the TVG warning. Gate it on `!soundings.empty()`. — `cube_bathymetry/src/cube_bathymetry_node.hpp:~2701` (Gemini) — fixed in `4e62474`
- [x] (suggestion) The declared-TVG mismatch warning fires for Unanchored and UnderPingFloor entries and says "the table is applied as is", but the fallback applied instead. — `cube_bathymetry/src/backscatter_mode.cpp:158-177` (Gemini) — fixed in `e2a6fa3`
- [x] (suggestion) The loader does not check angle rows or group bounds against [0, 90]. A duplicate `# mode_summary:` silently overwrites, and a summary without `pings=` reads as 0, so it falls under any ping floor. — `cube_bathymetry/src/backscatter_table.cpp:513-639` (Gemini) — fixed in `4a7db64`
- [x] (suggestion) A near-miss format line (BOM, leading blank line, `v2`) is not detected as a table. It falls to the legacy loader, which yields an empty curve with only a WARN and a `kind:none` fingerprint. Fail loudly on a `# format: angular-response-table` prefix that is not exactly v1. — `cube_bathymetry/src/backscatter_table.cpp:412-426` (Lens B) — fixed in `55895b2`
- [x] (suggestion) The duplicate bin/node checks are O(n²) and have no row or line-length bound. An oversized operator file can stall `on_configure`. — `cube_bathymetry/src/backscatter_table.cpp:617-655` (Lens B) — fixed in `9acfbea`
- [x] (suggestion) `--backscatter-mode-fallback` / `--backscatter-mode-min-pings` (and the node parameters) are silently ignored without a table. Warn once. — `cube_bathymetry/src/import_bag_main.cpp:~1113` (Lens B) — fixed in `46ab062`
- [x] (suggestion) `~/reset` keeps the resolver (by design), and its counts and `warned_` keep accumulating, so the cleanup report mixes pre-reset and post-reset pings. Say so in the report or the README. — `cube_bathymetry/src/cube_bathymetry_node.hpp:~1903` (Gemini) — fixed in `33fb160`
- [x] (suggestion) `DeclaredTvgState` compares the float absorption exactly after a `%.6g` round trip. Use a small relative tolerance. — `cube_bathymetry/src/backscatter_table.cpp` operator== (Lens A) — fixed in `b3f62f8`
- [x] (suggestion) The README should say plainly that `identity` mixes raw beams into table-corrected means, so `exclude` is the safe fallback for a mode-switching sonar. Also state that an older cloud without `frequency` keys as `?kHz` and gets the fallback. — `README.md` by-mode / node parameters (Lens A + Governance) — fixed in `bec5b70`
- [x] (suggestion) `newest_stamp_ns_` / `have_newest_` are written but never read. — `cube_bathymetry/include/cube_bathymetry/backscatter_mode.h:174-175` (Gemini) — fixed in `83d3858` (with must-fix 6)
- [x] (suggestion) batch_regen refuses a table file but never reads `build_fingerprint.json`, so `--backscatter-correction none` can rebuild tiles in a table-built store. This gap predates #172; name it in the PR body. — `cube_bathymetry/src/batch_regen_main.cpp:918-927` (Lens B) — (deferred: PR-body text for the host, who opens the PR; the gap predates #172 and the wording is in the Implementation entry)
- [x] (suggestion) The `Parameters`/`Sounding`/`DepthAndUncertainty` layout changed (20 to 22 bytes), so ui_ws `marine_perception_tools` must be rebuilt with this. Note it in the PR body and the post-merge rebuild. (Lens B) — (deferred: PR-body text for the host, who opens the PR; the rebuild note is in the Implementation entry)
- [x] (suggestion) CMake comment "ADR-0003 schema v2, tiling only (#143)" is stale. — `cube_bathymetry/CMakeLists.txt:104` (Governance) — fixed in `a724a0b`
- [x] (suggestion) ADR-0003: the `tool_version` text runs on from "...keeps schema version 2 (additive)." with no paragraph break. — `cube_bathymetry/docs/decisions/0003-staleness-fingerprint.md` (Governance) — fixed in `77204a1`
- [x] (suggestion) README "Header provenance" key table still lists only the single-curve keys (carried from PR 1). — `README.md` (Governance) — fixed in `101ea6d`
- [x] (suggestion) Instruction-update candidates (proposals only): a `.agents/README.md` for cube_bathymetry recording the `soundings` field contract (count, point_step, fixtures); the pitfall that an exported target linking a new find_package'd library needs `ament_export_dependencies`. (Governance) — (deferred: proposals only; a cube_bathymetry `.agents/README.md` is a dedicated task with its own issue per AGENTS.md, recorded in the Implementation entry)


## Implementation
**Status**: complete
**When**: 2026-11-06 11:21 -05:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-172 at `8c34961`
**Addressed**: `## Local Review (Pre-Push)` of 2026-11-06 10:29 -05:00 (round 1 of PR 2, at `e7e2022`): 8 must-fix, 18 suggestions
**Commits**: `818ccb6` `7874b06` `78ba0cc` `62d75a1` `9b64de2` `83d3858` `75bafb4` `c54c922` `e7aff1d` `fdfb0e7` `4e62474` `e2a6fa3` `4a7db64` `55895b2` `9acfbea` `46ab062` `33fb160` `b3f62f8` `bec5b70` `a724a0b` `77204a1` `101ea6d` `c2ed3cf`; boxes checked in `8c34961`. Not pushed.

### Actions
- [x] (must-fix 1) Group lookup: the loader reads `# range_angle_groups_deg:` and creates every declared group per mode (an unfitted one with no nodes = zero term); the specular band takes the first DECLARED group; a range row naming an undeclared group, or a malformed declaration, fails the load; without the header line the written groups are used as before. Test with an unfitted first group (specular and 10-30 beams get zero, 30-90 gets its term) and the bare-table fallback. Load report now says "N of M groups fitted" — `818ccb6`, `c2ed3cf`
- [x] (must-fix 2) `ament_export_dependencies(... OpenSSL)`. A scratch downstream `find_package(cube_bathymetry)` + link of `cube_bathymetry_level_plan` configured before and after; before, OpenSSL::Crypto resolved only because another dependency already found OpenSSL — `7874b06`
- [x] (must-fix 3) README: `soundings` has eight fields (`frequency`, point_step 32); an older seven-field cloud keys as `?kHz` — `78ba0cc`
- [x] (must-fix 4) README: the estimator does not weight by `1/var_db2` (decision 6 deviation); stale "PR 2" wording replaced — `62d75a1`
- [x] (must-fix 5) Node: NO-OP warning guarded with `!bs_file_is_table`; lifecycle test captures the log (empirical + table: no NO-OP; empirical, no file: NO-OP) — `9b64de2`
- [x] (must-fix 6) Resolver history bounded as directed: on every `addSonarInfo`, entries before `newest resolved ping - 60 s` dropped except the newest at or before the cut; hard cap 4096 (oldest first); documented in the header, README, plan decision 10, ADR-0007 item 2. Also: a message whose state equals the one in force at its stamp is not stored; the stale count looks only at pings between the stamp and the next stored stamp and counts one only when the message changes its correction (each recent ping records its idx); dead `newest_stamp_ns_`/`have_newest_` removed. Tests: 2000 s switching every ping stays at the window size, late pings 59 s and 30 s behind resolve against the kept state, cap holds a ping-less stream, steady republish stores one state, out-of-order older message counts no ping that used a newer state — `83d3858`
- [x] (must-fix 7) Table run that resolves nothing: import_bag prints `error:` with the reason breakdown and likely causes, writes the store and the fingerprint with `kind: "table-unresolved"` (ADR-0003, `build_fingerprint.h`; `isTableKind()` keeps fallback/min_pings in the JSON), exits 4 (usage(): 1 and 2 outrank 4, 4 outranks 3; recon-only keeps its code). Node: WARN at the first unresolved cloud after each configure, throttled 60 s per configuration (member, not the macro's call-site static), with count + reasons; cleanup report kept. Tests: resolver `resolvedNothing`/`unresolvedSummary`, fingerprint round trip + staleness both ways, lifecycle log capture of the throttled warning, real-bag smoke case (fixture table over the excerpt, which has no SonarInfo: exit 4, error text, fingerprint kind) — `c54c922`
- [x] (must-fix 8) `setupBackscatter(BackscatterSetup & out, ...)` fills a caller-owned struct; copy and move deleted — `75bafb4`
- [x] (suggestion) Beam-level fallbacks counted: `countBeam()` applies the estimator's own `tableBeamStatus` to each sounding where the ping is stamped (import_bag + node); report lists uncorrectable beams per (mode, reason), as submitted; ADR item 5, `--backscatter-mode-fallback` help, README, plan decision 9 updated — `fdfb0e7`
- [x] (suggestion) `|θ| >= 90` deg is uncorrectable (fallback); `tableBeamStatus`/`beamAbsAngleDeg` added — `e7aff1d`
- [x] (suggestion) Empty cloud not resolved (`stampCloudMode` skips it); lifecycle test — `4e62474`
- [x] (suggestion) TVG mismatch warning names the reason and fallback when the entry is not applied — `e2a6fa3`
- [x] (suggestion) Loader refuses angle bins outside [0, 90), group bounds outside [0, 90], a duplicate `# mode_summary:`, and a summary without `pings=` — `4a7db64`
- [x] (suggestion) Near-miss format line: BOM / leading blank lines tolerated; any `# format: angular-response-table` prefix is a table, only v1 loads, another version is an "unsupported table format" error — `55895b2`
- [x] (suggestion) Loader bounds: file > 64 MiB, line > 4096 chars, > 1e6 rows refused; duplicate checks are set/map lookups (40,000-row table test) — `9acfbea`
- [x] (suggestion) Fallback/min-pings without a table warn once (import_bag and node; tested on both) — `46ab062`
- [x] (suggestion) Cleanup report says it spans every `~/reset`; README reset section lists the table and resolver as kept — `33fb160`
- [x] (suggestion) Declared absorption compared within a relative 1e-5 (the `%.6g` round trip) — `b3f62f8`
- [x] (suggestion) README: `exclude` is the safe fallback for a mode-switching sonar; older clouds without `frequency` get the fallback — `bec5b70`
- [x] (suggestion) Dead `newest_stamp_ns_`/`have_newest_` removed — `83d3858`
- [x] (suggestion) batch_regen never reads `build_fingerprint.json` — (deferred: PR-body text for the host; wording below)
- [x] (suggestion) Layout change needs a ui_ws rebuild — (deferred: PR-body text for the host; wording below)
- [x] (suggestion) CMake "tiling only" comment — `a724a0b`
- [x] (suggestion) ADR-0003 paragraph break — `77204a1`
- [x] (suggestion) README "Header provenance": per-mode header keys table, read vs record — `101ea6d`
- [x] (suggestion) Instruction-update candidates — (deferred: proposals only; a cube_bathymetry `.agents/README.md` is a dedicated task with its own issue per AGENTS.md. Candidates kept: the `soundings` field contract (count, point_step, fixtures); an exported target linking a new find_package'd library needs `ament_export_dependencies`; RCLCPP_*_THROTTLE state is a per-call-site static shared by every node in the process, so a per-configuration throttle needs a member)

### For the PR body (host)
- **Known gap, predates #172:** `batch_regen_bag` refuses a per-mode table file, but it never reads `build_fingerprint.json`, so `--backscatter-correction none` (or another curve) can rebuild tiles inside a table-built store without any check. The fingerprint now records the correction; a consumer that compares it is the ADR-0003 `--incremental` follow-up.
- **Rebuild:** `Sounding`/`DepthAndUncertainty`/`Parameters` changed layout (`DepthAndUncertainty` 20 → 22 bytes, `Parameters` carries the table), so ui_ws `marine_perception_tools` must be rebuilt with this PR, and the main tree's sensors_ws and ui_ws rebuilt after merge.
- `import_bag` has a new exit code 4 (table loaded, no ping resolved; fingerprint kind `table-unresolved`).

### Tests
Full package suite (`./sensors_ws/test.sh cube_bathymetry`): **1410 tests, 0 errors, 0 failures, 96 skipped** (the same 96 cppcheck skips as before; was 1395). The real-bag smoke cases ran (excerpt reachable on this host). uncrustify and cpplint clean on every touched file, lint_cmake clean; import_bag `main()` at 492 of cpplint's 500 lines.

### Left open
- Nothing from round 1 besides the three deferrals above.
- Behaviour to know (documented in `backscatter_mode.h`): a SonarInfo deduplicated because it equals the state in force is not stored, so a later out-of-order message stamped between it and its predecessor governs pings after the dropped stamp too. Needs out-of-order delivery from one publisher; accepted.
- Not a defect, noticed: the lifecycle tests' synthetic SonarInfo leaves `tvg_absorption_db_per_km` at 0 (the message says producers must set NaN), so it reads as a declared `UNKNOWN/0 dB/km` mismatch; real bridge data sets NaN.

### Next
`/review-code` pre-push (round 2) on the branch, fresh context.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-06 11:31 -05:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-172 at `36b5d26`
**Mode**: pre-push
**Depth**: Deep (reason: +4751 / -138 lines over 43 files, plus ADR-0003/ADR-0007 amendments; round 2 reviewed the round-1 fix commits 8f5a52f..c2ed3cf, with the whole branch read cross-model)
**Must-fix**: 3 | **Suggestions**: 13
**Round**: 2 of PR 2 (the earlier rounds 1-2 of 11-02 reviewed PR 1, merged as PR #174) | **Ship**: continue — must-fixes fell from 8 to 3, and each is a precise fix. Two of them were introduced by round-1 fixes in the resolver and the exit-4 path, so a short round 3 on just the new fix commits is worth it; a full re-dispatch is not.

Specialists:
- Static analysis on every changed C++/CMake/XML file: cpplint, uncrustify, lint_cmake, xmllint, plus cppcheck 2.13 run directly (ament skips it on this host). All clean.
- Governance and plan drift: plan.md, README, ADR-0003 and ADR-0007 are in sync with the fixes. The repo has no `.agents/README.md` or `review-context.yaml`.
- Claude Adversarial Lens A and Lens B ran on the fix commits (fresh context, Deep horizon).
- Cross-model: Gemini and Codex ran on the whole branch, both EXIT=0.
- Local Adversarial is off (the default).

Round-1 closure: all 8 must-fixes and the 15 fixed suggestions are verified closed, and the 3 deferrals are still owed as PR-body text. Two round-1 fixes introduced new defects: must-fix 6 (bounded history) led to must-fix 2 below, and must-fix 7 (exit 4) led to must-fix 3 below.

Dropped after checking:
- Gemini, locale-dependent `snprintf`/`strtod`: nothing in the package calls `setlocale`.
- Gemini, the SonarInfo curve path under a table: an explicit file returns early, and that warning is throttled.
- Gemini, packed 22-byte `DepthAndUncertainty`: `pack(1)` was already there before this branch.
- Gemini, `auto` vs `empirical` fingerprint mode: errs on the safe side (a false stale) and applies to single curves too.

### Findings
- [ ] (must-fix) The loader refuses an angle bin centred at or past 90 deg, but `derive_angular_response.py` `fit_mode` writes `(j + 0.5) * bin_width` up to `ceil(90 / width)`. At `--bin-width-deg` 4, 8, 12, 20, 40 or 60 (not divisors of 90), one beam in the last bin makes the whole table fail to load. Accept the last bin when its lower edge is under 90 (beams at 90 deg or more are already uncorrectable), and add a round-trip test at width 4. — `cube_bathymetry/src/backscatter_table.cpp:760` (Lens A; from `4a7db64`)
- [ ] (must-fix) The history-window anchor `newest_resolved_ns_` is a running maximum and never moves back. After a backward stamp jump (sim restart, `ros2 bag play --loop`; `~/reset` keeps the resolver), each `addSonarInfo` prunes to the newest entry before the old maximum. A ping whose successor SonarInfo arrived first then gets no state and falls back, until time passes the old maximum. Before the bound, the unbounded history kept those states. The node's member throttle `(now - last).seconds() >= 60` is negative after the jump, so the unresolved warning is muted too. Fix: re-anchor (and clear `recent_resolved_`) when a ping lands more than the window behind the anchor, treat a negative elapsed time as "warn now", and add a test. Offline, a far-future ping blocks the head of the pending queue until the flush, so that case is weaker than Lens B states. — `cube_bathymetry/src/backscatter_mode.cpp:210-213,136-140`, `cube_bathymetry/src/cube_bathymetry_node.hpp:973-974` (Lens B + Gemini, cross-model confirmed; from `83d3858`)
- [ ] (must-fix) The "nothing resolved" error prints "The store is otherwise complete; its fingerprint records kind 'table-unresolved' and the run exits 4" before the finish step runs. That is false in three cases: a recon-only `--level-plan-out` preflight (no store, no fingerprint, exits 0, so a preflight gate passes a wrong `--sonar-info-topic`), a finish that returns 1 (store may be incomplete), and a finish that returns 2 (no fingerprint). Keep the diagnosis in `reportModeResolution`, print the outcome sentence from `withTableResolution` once the code is known, and make the recon-only preflight exit 4, or say plainly that it failed. — `cube_bathymetry/src/import_bag_main.cpp:1196-1204,1213-1216` (Lens B must-fix, Lens A suggestion, cross-pass confirmed; from `c54c922`)
- [ ] (suggestion) Deduplicating equal states across different stamps breaks out-of-order association. Example: A@100 is stored, A@300 is dropped as a duplicate, then a late B@200 arrives, and a ping at 350 gets B. The Python derive keeps distinct stamps. This was documented and accepted, and it needs non-monotonic stamps from one producer; Codex rates it must-fix. The 60 s window alone already bounds the history, so the dedup could go, but that would load the cap in import_bag (next item). — `cube_bathymetry/src/backscatter_mode.cpp:85-93` (Codex)
- [ ] (suggestion) The `kMaxHistory` cap ignores pings still pending in import_bag. SonarInfo is fed at read time, while pings wait for the TF frontier. If TF stalls through more than 4096 state changes, the states those pings need are dropped, and the pings are reported as "before the first SonarInfo". — `cube_bathymetry/src/backscatter_mode.cpp:146-148`, `cube_bathymetry/src/import_bag_main.cpp:2723-2736` (Gemini)
- [ ] (suggestion) The stale count can count one ping twice and never takes a count back. Example: a ping at 20 resolves under A@0, then a late B@10 arrives, then a late C@15, and the count is 2 for one ping. Count each ping at most once. — `cube_bathymetry/src/backscatter_mode.cpp:101-118` (Lens A)
- [ ] (suggestion) A beam past the last written angle bin, or in a gap between bins, gets the nearest bin's clamped value, which the table never measured. This is the same reasoning as the 90 deg rule. Add a status for "no bin within half `angle_bin_width_deg`". — `cube_bathymetry/src/backscatter_table.cpp:460-471,442` (Lens A)
- [ ] (suggestion) The live node's unresolved warning fires every minute on normal unresolved pings: a rare mode with no entry, the ping floor, pings before the first SonarInfo. import_bag treats only "nothing resolved" as an error, so the two disagree. Gate the warning on `resolvedNothing()`, or warn once per new reason. — `cube_bathymetry/src/cube_bathymetry_node.hpp:969` (Lens A + lead)
- [ ] (suggestion) The loader's limits can be bypassed by a non-regular file such as a FIFO or `/dev/zero`:
  - a `file_size` error skips the 64 MiB check;
  - `getline` buffers the whole line before the 4096-character check;
  - comment lines are not counted against the row limit;
  - `isAngularResponseTable` has no bound and consumes the start of a FIFO;
  - `sha256File` reads the file a second time, so an in-place rewrite between the reads fingerprints a table that was not applied.

  Refuse a non-regular file, read at most the cap once, then probe, parse and hash that one buffer. — `cube_bathymetry/src/backscatter_table.cpp:214-219,516-547,567-571`, `cube_bathymetry/src/import_bag_main.cpp:1010` (Lens B)
- [ ] (suggestion) `warned_` and `declared_mismatch` are keyed by the absorption text (6 significant digits). A sonar whose declared absorption drifts gets a new WARN and a new map key for each value, without bound. Key on (mode, TVG model), or round or cap. — `cube_bathymetry/src/backscatter_mode.cpp:234-236` (Lens B)
- [ ] (suggestion) The declared absorption accepts ±inf from the table (no `isfinite` check) and from SonarInfo. `operator==` then treats inf as equal to any finite value, and inf is not equal to itself. Reject non-finite values in the loader and guard the comparison. — `cube_bathymetry/src/backscatter_table.cpp:739,395` (Gemini)
- [ ] (suggestion) Under `identity`, `fallbackIdx()` returns `kCorrectionUnresolved` (the legacy single-curve path), not `kCorrectionIdentity`. This is correct today only because a table run carries no legacy curve. Return `kCorrectionIdentity` so the invariant is explicit. — `cube_bathymetry/src/backscatter_mode.cpp:160-164` (Gemini)
- [ ] (suggestion) Written groups are bucketed by exact bounds but matched to declared groups within 1e-9. Two near-identical written groups (only possible in a hand-edited table) fill one slot, and the second silently overwrites the first. Refuse a second fill. — `cube_bathymetry/src/backscatter_table.cpp:~747` (Gemini)
- [ ] (suggestion) The duplicate `# mode_summary:` and `pings=` errors name the column line's number, not the summary line's. — `cube_bathymetry/src/backscatter_table.cpp:652-661` (Lens A)
- [ ] (suggestion) The comment "ADR-0003 schema 2, tiling only -- cube#143" sits on the `writeFingerprint` call that now records the backscatter correction. It is the same staleness as the CMake comment fixed in `a724a0b`. — `cube_bathymetry/src/import_bag_main.cpp:1746` (lead)
