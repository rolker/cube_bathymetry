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

## Plan Authored
**Status**: complete
**When**: 2026-06-21 02:41 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-15/plan.md` at `560b52a` (re-plan, expanded scope — REPLACES the prior plan)
**Branch**: feature/issue-15 at `560b52a`
**Phases**: single PR if GeoGrid parity is tractable, else a 2-PR stack (Grid core first, GeoGrid prediction parity second)

This re-plan addresses both `## Plan Review` must-fixes: (1) the offset now reconstructs the original's slant-projected signed-depth analog `range = depth/cos(angle)` (via `depth * slant / |z|`) instead of the vertical `.z` component, with tests at off-boresight angles where the prior bug hid; (2) it builds the prior-surface prediction pipeline (faithful port of `cube_grid_interpolate` + `cube_node_set_preddepth`) and wires `setPredictedDepth` into `Grid::insert` (and `GeoGrid::insert`) so slope correction runs end-to-end rather than as dead code.

### Open questions
- [ ] GeoGrid bilinear geometry has no exact GGGS-cell analog: port nearest-estimated-neighbour prediction for GeoGrid + document the divergence, or require exact parity with the regular Grid before merge?
- [ ] PR shape: land Part A + Part B(Grid) + Part C as one PR with GeoGrid parity as a stacked follow-on if it balloons, or insist on a single PR covering both grids?

## Plan Review
**Status**: complete
**When**: 2026-06-21 03:05 -04:00
**By**: Claude Code Agent (Claude Opus)  <!-- independent fresh-context dispatch (#490); not the plan author's self-review -->

**Plan**: `.agent/work-plans/issue-15/plan.md` at `560b52a`
**PR**: PR-less (--issue mode)
**Verdict**: changes-requested

This is the **second** plan review (re-plan after the first was changes-requested). The v2
plan correctly diagnoses the prior `.z`-vs-slant-range bug and the runtime-inert defect,
and the **scope-honesty, risk section, and interpolation 4-NN/guard description are good**.
But independent re-derivation from the original source shows the v2 plan **still has the
slope-correction geometry wrong** — for a deeper reason than v1 — and **mis-ports the
prediction pipeline**. Both prior reviewers (v1 author, v1 Opus reviewer) and this plan all
misread what `snd->range` *is* at the offset site. Verified line-by-line below.

### Findings

- [ ] (must-fix) **`snd->range` is OVERWRITTEN before the offset runs — it is NOT
  `depth/cos(angle)` at the slope-correction site.** The `range = depth/cos(angle)` value
  (`sounding.c:1268`, `sounding_gsf.c:481`, `sounding_hips.c:1263`) is only the
  *error-model* range. In the slope-correction path, `mapsheet_cube.c:2424-2444` explicitly
  does `data[snd].range = cube_grid_interpolate(... de, dn ...)` — the comment reads
  *"fill the result into the 'range' element of the sounding [bad! <smack>] for
  cube_node_insert() to use for slope corrections."* So at `cube_node.c:1847`,
  `offset = node->pred_depth − snd->range` is **`pred_depth_node − pred_depth_interpolated_at_touchdown`**:
  the difference of two negative-down predicted-surface depths (node vs touchdown point),
  i.e. the predicted slope between them. The plan's Part A formula
  (`range_analog = sounding.depth / cos(angle)`, `plan.md:74-96,102-115`) reconstructs the
  *error-model* range, which is the wrong quantity. Verified numerically: with the plan's own
  worked example (rx=60°, depth=−9, pred=−10) the plan queues `−1 m`; the literal original
  error-model range queues `−37 m`; the *actual* slope-corrected value is a small slope
  delta near `−9 m`. The plan's "reproduces CUBE off-boresight" claim (`plan.md:87-93`) is
  not borne out. — `plan.md:74-115`

- [ ] (must-fix) **Part B is not a faithful port — it invents a self-bootstrap path the
  original does not have, and assigns the interpolation to the wrong point.** The original's
  insert path (`cube_grid_insert_depths`, `cube_grid.c:1877-1990`) never calls
  `cube_grid_interpolate` or `set_preddepth`; it only iterates nodes and calls
  `cube_node_insert`. `pred_depth` is set in exactly two places: the **external-prior**
  init (`cube_grid_initialise`/`init_unct`, `cube_grid.c:1712,1812`) from a supplied prior
  bathymetry array, and the per-sounding `range` overwrite in `mapsheet_cube.c` (above).
  There is **no "interpolate from current running estimates during insert" mechanism** —
  the plan's claim that `cube_grid_interpolate` "needs no external prior file … is the
  production-relevant path" (`plan.md:117-120`) is incorrect: that routine interpolates the
  *prior-surface* `pred_depth`, and is invoked at surface-finalization, interpolated at the
  **sounding touchdown** `(de,dn)`, not at the node. The plan interpolates at the **node
  position** from neighbours' running estimates (`plan.md:127-129,140-141`) — wrong source
  field and wrong location. As written, Part B builds a new, unfaithful estimator and labels
  it a port. — `plan.md:117-152,194-198`

- [ ] (must-fix) **`predicted_depth_` must be seeded from a prior surface for the original's
  slope correction to be meaningful at all.** The original applies slope correction only when
  a prior bathymetry was loaded via `cube_grid_initialise` (which also seeds a base
  hypothesis, `cube_node_add_null_hypothesis`). The port has `Hypothesis::generateNullHypothesis`
  (`plan.md:57`) but no prior-load entry point, and `Grid::insert`/`GeoGrid::insert` never
  set `predicted_depth_` (confirmed: `grid.cpp:122`, `geo_grid.cpp:95` — no pred set). The
  honest faithful-port options are: (a) port `cube_grid_initialise` (external-prior load +
  base-hypothesis seed) and the `mapsheet_cube.c` per-sounding touchdown-interpolation
  `range` overwrite, or (b) explicitly decide the port will *not* support slope correction
  until a prior-surface subsystem exists, and keep the block disabled with a referenced
  follow-on. The current plan's middle path (self-bootstrap from estimates) is neither
  faithful nor clearly-scoped. — `plan.md:32-41,117-145`

- [ ] (suggestion) The Part C tests assert against the plan's own (incorrect) reconstruction
  `pred − depth/cos(angle)` (`plan.md:156-164`). Like the v1 test, they would lock in the
  wrong formula. Any test must assert against the *touchdown-vs-node predicted-surface
  difference* (`pred_node − pred_touchdown`), reconstructed independently in the test from a
  known synthetic prior surface — otherwise off-boresight passes for the wrong reason. —
  `plan.md:156-164`

- [ ] (suggestion) `INVALID_DATA`-vs-`no_data_value` sentinel correction (`plan.md:115`) and
  the `|z|>0` div-guard are fine and worth keeping regardless of the formula rework. The
  comment-history note (why it was disabled at port time) is good practice. — `plan.md:111-115`

### Open-question recommendations (the two the plan defers)

- **GeoGrid now vs follow-on**: Recommend **defer GeoGrid entirely** to a tracked follow-on
  regardless — but only *after* the Grid-side geometry is corrected. A Grid-only landing is
  acceptable *only* if GeoGrid is not a silently-dead path. It currently isn't dead: both
  grids today run with offset 0 (no correction), which is a defined, safe behaviour. So
  shipping Grid-only correction + filing a GeoGrid issue leaves GeoGrid working-without-
  correction (acceptable, tracked), not broken. Do **not** attempt GGGS bilinear in this PR.
- **PR shape**: Recommend the plan be **re-scoped before any implementation**, not split as-is.
  The blocking problem is *correctness of the offset and the prediction source*, which Part A
  and Part B share. A 2-PR stack of a wrong formula is still wrong. Sequence: (1) re-derive
  the offset as `pred_depth_node − pred_depth_at_touchdown` from a seeded prior surface;
  (2) decide whether this PR ports the external-prior load path (`cube_grid_initialise`) or
  defers slope-correction-active behaviour behind a referenced follow-on; (3) only then split
  GeoGrid off.

### Evaluation summary
| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Needs work | Honestly sized and risk-flagged, but built on a wrong correction model; re-scope before implementing. |
| Issue alignment | Good | Targets the issue's "re-enable slope correction" ask. |
| File targeting | Good | `sounding.h`/`node.*`/`grid.*`/`geo_grid.*`/tests are the right files. |
| Consequences | Needs work | Consequences table is complete *for the planned (incorrect) design*; misses the external-prior / touchdown-interpolation dependency. |
| Principle alignment | Concern | "Never document from assumptions" / "Replicate, don't invent": the `range` semantics and the insert-time interpolation are asserted as verified-from-source but contradict `cube_grid_insert_depths`, `mapsheet_cube.c:2424`, and `cube_grid_initialise`. |
| ADR compliance | Good | No ADR triggered (algorithmic faithful-port). |
| ROS conventions | N/A | No topics/params/QoS/lifecycle touched. |

The v2 plan is well-written and improves on v1's disclosure and risk-honesty, but the corrected
formula does **not** hold up: `snd->range` at the offset site is the interpolated predicted
depth at touchdown (overwritten in `mapsheet_cube.c`), not `depth/cos(angle)`, so the real
correction is `pred_depth_node − pred_depth_touchdown`. Part B mis-ports the prediction pipeline
(self-bootstrap from running estimates ≠ the original's external-prior + touchdown-interpolation).
Re-derive the offset and the prediction source from `mapsheet_cube.c:2424` + `cube_grid_initialise`
before implementation; defer GeoGrid to a tracked follow-on once Grid is correct.
