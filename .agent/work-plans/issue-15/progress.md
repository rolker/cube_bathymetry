---
issue: 15
---

# Issue #15 — Slope correction is disabled (commented out) in Node::insert

## Issue Review
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #15
**Comment**: https://github.com/rolker/cube_bathymetry/issues/15#issuecomment-4761051413
**Scope verdict**: well-scoped

### Actions
- [ ] Verify sign convention of `sonar_relative_position.z` matches `predicted_depth_` before implementing offset (`predicted_depth_ - sonar_relative_position.z`); sonar frame z is "down" while depth convention is "negative below surface".
- [ ] Add test in `test_node.cpp` covering the slope-correction path (non-zero `sonar_relative_position.z` with valid `predicted_depth_`).
- [ ] Add comment near the re-enabled code block explaining why slope correction was originally commented out (missing `range` field at port time).
- [ ] If a new `range` field is added to `Sounding` instead of reusing `sonar_relative_position.z`, update all `Sounding` constructors and document the field's sign convention.

## Plan Authored
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-15/plan.md` at `e3129be`
**Branch**: feature/issue-15 at `e3129be`
**Phases**: single

### Open questions
- [ ] Grid integration follow-on: should a new issue be filed now to track hooking `setPredictedDepth` into Grid/GeoGrid after a prior-surface interpolation pass, or leave for discovery when that pipeline is built?

## Plan Review
**Status**: complete
**When**: 2026-06-21 02:22 -0400
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-15/plan.md` at `e3129be`
**PR**: PR-less (--issue mode)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Slope-correction formula uses the wrong analog field AND the wrong sign. The plan equates the port's `sonar_relative_position.z` with the original's `snd->range`, but they are different quantities. Original (`sounding.c:1268`, `sounding_hips.c`, `sounding_gsf.c:481`): `range = depth / cos(angle)` — a *slant-projected signed depth*, magnitude **larger** than `|depth|`, and **negative-down** (since the original's `depth` is "always negative internally", `sounding.h:423`). The port's `sonar_relative_position.z = slant_range * cos(tx) * cos(rx)` is the *vertical component*, magnitude equal to `|depth|` (**smaller** than the slant), and **positive-down**. They differ by the obliquity factor `1/cos²`, not just a sign flip. Worked example (rx=60°, slant s): original final depth `= snd->depth + pred_depth - range = -0.5s + pred_depth + s = pred_depth + 0.5s`; plan's port final `= snd->depth + pred_depth + z = -0.5s + pred_depth + 0.5s = pred_depth`. These disagree by `0.5s` off-boresight, so the plan's correction silently flattens to `pred_depth` instead of reproducing CUBE's slope offset. The plan's sign-table claim ("`predicted_depth_ + sonar_relative_position.z`") is internally self-consistent with its own test but does **not** reproduce the original algorithm. — `plan.md:30-50`
- [ ] (must-fix) The plan's test (`SlopeCorrectionAppliesOffset`) only validates the plan's own (incorrect) formula, not the original CUBE behavior, so it would lock in the wrong result. A correct test must reconstruct the original `offset = pred_depth - depth/cos(angle)` for a genuinely off-boresight beam (non-zero rx/tx angle) and assert against that, otherwise the boresight-only case masks the error (at boresight both formulas coincide). — `plan.md:75-84`
- [ ] (must-fix / decision gate) Slope correction is inert at runtime as planned. `predicted_depth_` defaults to `INVALID_DATA` (= `float::max()`, `common.h:38`), is never NaN, and **no caller anywhere sets it** (`setPredictedDepth` has zero call sites; `grid.cpp:122` / `geo_grid.cpp:95` never supply a predicted depth). So at runtime the guard `predicted_depth_ != INVALID_DATA` is always false and the re-enabled block never executes. The plan honestly scopes the Grid/GeoGrid hook-up as a follow-on (`plan.md:53-59,117,122-126`), which prevents the PR from being outright misleading — but it means this PR re-enables a path that (a) cannot run in production and (b) would compute wrong depths if it did. Recommend: get the formula right first, and decide explicitly whether to land a dead-but-tested path now (with the follow-on issue filed and referenced) or fold the grid call-site into this PR so the feature is actually exercised. — `plan.md:53-59`
- [ ] (suggestion) Confirm `cos(tx_angle) * cos(rx_angles[i])` (the port's `z` construction, `sounding.h:50`) vs the original's single-angle `cos(angle)` (`sounding.c:1268`). The original projects with one beam angle; the port uses the product of two. Whichever field the corrected formula ends up using, the obliquity model must match the original's intent — re-derive `range`-equivalent from the port's geometry rather than reusing a field built for a different purpose. — `plan.md:25-28`
- [ ] (suggestion) Note guard mismatch in the commented code being replaced: it references `parameters.no_data_value` (`node.cpp:118-120`) while the live blunder check uses `INVALID_DATA` (`node.cpp:93`). The plan correctly specifies `predicted_depth_ != INVALID_DATA` for the new guard — just call out that the old sentinel is being deliberately corrected. — `plan.md:66-68`

### Evaluation summary
| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | 3 files, one method, self-contained; appropriate for a single PR. |
| Issue alignment | Good | Addresses the issue's "re-enable slope correction" ask. |
| File targeting | Good | `node.cpp`/`node.h`/`test_node.cpp` are the right files. |
| Consequences | Needs work | The follow-on (grid call-site) is named but the runtime-inert consequence is under-weighted given the formula bug. |
| Principle alignment | Concern | "Fix it completely" / "never document from assumptions": the sign-convention table is asserted as verified but the `range` analog is wrong on closer reading of `sounding.c:1268`. |
| ADR compliance | Good | No ADR triggered (pure algorithmic re-enable). |
| ROS conventions | N/A | No topics/params/QoS/lifecycle touched. |

The plan is well-organized and the runtime-inert risk is disclosed, but the core slope-correction formula is incorrect (wrong analog field + wrong sign/obliquity), so implementing as written would silently corrupt off-boresight depths. Fix the formula and its test against the original `pred_depth - depth/cos(angle)` semantics before implementing; resolve the land-dead-path-vs-wire-up-grid decision explicitly.
