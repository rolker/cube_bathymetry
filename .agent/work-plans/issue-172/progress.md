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
