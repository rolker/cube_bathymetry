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

## Plan Authored
**Status**: complete
**When**: 2026-06-21 09:30 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-15/plan.md` (v3 — REPLACES the v2 plan in full)
**Branch**: feature/issue-15
**Phases**: single small PR (node-level math + primitive + tests); prior-surface subsystem is a referenced follow-on

This is the **third** plan, re-authored after the round-2 changes-requested pinned the
correct algorithm. Every claim in v3 was re-verified line-by-line against `original_cube/`
before writing (see plan's Verification table):

- **Offset corrected** to the predicted-SURFACE slope delta `offset = pred_depth(node) −
  pred_depth(touchdown)`. Confirmed `snd->range` is OVERWRITTEN at `mapsheet_cube.c:2434`
  with `cube_grid_interpolate(... de, dn ...)` (the bilinear interpolation of the 4
  surrounding nodes' `pred_depth` at the touchdown point), so the `depth/cos(angle)`
  error-model range (`sounding.c:1268`) is irrelevant at the offset site. No `1/cos²`
  obliquity factor — both terms are same-convention negative-down predicted depths.
- **No self-bootstrap.** Confirmed `cube_grid_insert_depths` (`cube_grid.c:1877-1992`)
  never sets `pred_depth` / never interpolates — it only calls `cube_node_insert`.
  `pred_depth` is seeded from an EXTERNAL prior via `cube_grid_initialise`
  (`cube_grid.c:1665,1712`) + base hypothesis (`:1730`). Both prior plans invented the
  interpolate-from-running-estimates path; it does not exist in the original.

**Crux resolution**: the port has **no** predicted-surface mechanism (no prior-load, no
`setPredictedDepth`, no `Sounding.range`; `predicted_depth_` is `INVALID_DATA` everywhere
and nothing sets it). A faithful *active* correction needs the whole `cube_grid_initialise`
+ touchdown-interpolation subsystem — too large to port correctly now. v3 therefore ports
the node-level math **faithfully and correctly** (corrected offset + a 1:1
`cube_node_set_preddepth` port) and **explicitly defers** active correction behind the
external-prior subsystem, FILED as a referenced follow-on. With no producer wired,
`predicted_depth_at_touchdown` stays at its sentinel → offset 0 → the defined,
correct-but-uncorrected behaviour the grids have today. The block is exercised end-to-end
by an integration test that supplies a synthetic prediction directly, so it is neither
non-physical nor silently-dead — it activates the instant a prior surface is supplied.

**Scope**: Grid-only per the review; in fact neither grid is wired to a producer in this
PR, so both run at offset 0 unchanged. GeoGrid (and the GGGS bilinear-vs-regular geometry
question) belongs to the prior-surface follow-on. Field named `predicted_depth_at_touchdown`
(NOT `range`) to avoid resurrecting the misread.

**Size/risk after this third pass**: low. 3 production edits + focused tests; the large,
risky prediction subsystem is correctly out-of-scope and tracked. Remaining open question
is the no-correction sentinel choice (`INVALID_DATA` vs literal-original `0.0`), settled at
review.

### Open questions
- [ ] Sentinel for `predicted_depth_at_touchdown`: `INVALID_DATA` (preferred — a real
  interpolated touchdown depth could legitimately be `0.0`) vs the literal original
  `range != 0.0` test. Flagged for review.
- [ ] Follow-on shape: one umbrella issue for external-prior load + touchdown interpolation
  + Grid/GeoGrid geometry, or split prior-load from interpolation. (Recommend one umbrella —
  they only deliver value together.)

## Plan Review
**Status**: complete
**When**: 2026-06-21 10:05 -04:00
**By**: Claude Code Agent (Claude Opus)  <!-- independent fresh-context dispatch (#490); not the plan author's self-review -->

**Plan**: `.agent/work-plans/issue-15/plan.md` at `7abd050`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

This is the **third** plan review. v1 was changes-requested (used the vertical `.z`
component — an obliquity error). v2 was changes-requested (used `depth/cos(angle)` plus a
self-bootstrap-from-running-estimates path that does not exist in the original). v3 was
re-verified line-by-line against `original_cube/` for this review; **every load-bearing
claim holds**, and the formula is finally correct.

### Source verification (independently re-read, not assumed)

| v3 claim | Source | Result |
|---|---|---|
| Offset site `offset = node->pred_depth − snd->range`, queued `snd->depth+offset` | `cube_node.c:1846,1847,1864` | ✓ verbatim |
| `snd->range` OVERWRITTEN before insert with interpolated touchdown pred_depth | `mapsheet_cube.c:2418-2444` (comment + `data[snd].range = cube_grid_interpolate(...de,dn...)`) | ✓ verbatim |
| Interpolation is at TOUCHDOWN `(de,dn)` from sounding east/north, not the node | `mapsheet_cube.c:2426,2434-2435` | ✓ |
| `cube_grid_interpolate` bilinearly blends 4 corners' `pred_depth`, returns `0.0f` if any corner no-data | `cube_grid.c:2360,2374-2395,2379-2384` | ✓ |
| `depth/cos(angle)` is the *error-model* range, computed at file-load while depth still positive, negated after — a different quantity, overwritten before the offset runs | `sounding.c:1268,1280` | ✓ irrelevant at offset site; **no `1/cos²`** |
| `cube_grid_insert_depths` never interpolates / never sets pred_depth — only calls `cube_node_insert` (NO self-bootstrap) | `cube_grid.c:1877-1992` (only `cube_node_insert` at :1981) | ✓ — v1/v2's invented path confirmed absent |
| `pred_depth` seeded ONLY from external prior array via `cube_grid_initialise` + base-hypothesis | `cube_grid.c:1665,1712,1730` | ✓ |
| `cube_node_set_preddepth` is a trivial 1:1 setter; doc: INVALID ⇒ no slope correction, NaN ⇒ don't incorporate | `cube_node.c:1066-1093` | ✓ |
| Port `predicted_depth_` is negative-down, blunder-checked vs `sounding.depth`, defaults `INVALID_DATA`, set NOWHERE | `node.cpp:93-104`, `node.h:198,202`, `common.h:38` | ✓ — confirmed inert |
| Port has no `Sounding.range`; `.z` (`sonar_relative_position.z = range*cos(tx)*cos(rx)`) is the v1 vertical-component trap | `sounding.h:50,33-73` | ✓ — new field correctly named `predicted_depth_at_touchdown`, NOT `range` |
| Commented block's stale `parameters.no_data_value` sentinel vs live `INVALID_DATA` guard | `node.cpp:118` vs `node.cpp:93`; `parameters.h:81` (`no_data_value = quiet_NaN()`) | ✓ — the old sentinel was doubly wrong (NaN, not the live `INVALID_DATA`); plan's correction is right |
| Both `Grid::insert` and `GeoGrid::insert` call the SAME `Node::insert` and supply no predicted depth | `grid.cpp:122`, `geo_grid.cpp:95` | ✓ — offset-0 safety identical for dense Grid and sparse GeoGrid |

### Findings

- [ ] (suggestion) **Off-boresight test discrimination — tighten the framing.** The corrected
  offset is **angle-independent**: it depends only on `predicted_depth_` (node) and the
  `Sounding.predicted_depth_at_touchdown` field, NOT on beam geometry. So the real
  discriminator against the two prior wrong formulas is *touchdown-position* offset along a
  known synthetic prior surface, not *beam angle*. The plan's `SlopeCorrectionSurfaceSlopeDelta`
  (`pred_node=-10`, `pred_touchdown=-10.6`, `depth=-10.5` ⇒ expect `-10.1`) does discriminate —
  v1 (`.z`) and v2 (`depth/cos`) both depend on beam angle and cannot hit `depth + (pred_node −
  pred_touchdown)` for a genuinely off-node touchdown — **provided the test reconstructs the
  expected value independently from `pred_node − pred_touchdown` (the plan says it will) and does
  NOT echo the implementation**. Just ensure the test prose/asserts key off touchdown *position*,
  not "off-boresight beam angle"; the plan slightly conflates the two (`plan.md:159,170-171`). —
  `plan.md:164-185`

- [ ] (suggestion) **`Sounding` default-member-init suffices; "set in both ctors" is redundant.**
  Both ctors (`sounding.h:35-38,40-59`) only set `depth`; a default member initializer
  `float predicted_depth_at_touchdown = INVALID_DATA;` covers both with no per-ctor edit. The
  plan's "set sentinel in both ctors" (`plan.md:137,191`) is harmless but unnecessary — prefer the
  default initializer (matches how `depth`/`intensity` already default). — `plan.md:132-137`

### Open-question recommendations

- **Sentinel for `predicted_depth_at_touchdown`: use `INVALID_DATA`, NOT the literal `0.0`.**
  In the original, `range == 0.0` is the no-correction sentinel only because
  `cube_grid_interpolate` *returns `0.0f`* on no-data corners (`cube_grid.c:2383`) — `0.0` is an
  artifact of that return convention, not a deliberate semantic choice, and is safe there only
  because a real seabed `pred_depth` is never exactly `0.0` m. In the port a legitimately
  interpolated touchdown depth at the shoreline could be `0.0`, which a literal `!= 0.0` guard
  would wrongly skip. `INVALID_DATA` is the faithful-*intent* port ("no interpolation result ⇒
  skip") without the value collision. Recommend the plan adopt `INVALID_DATA` (it already prefers
  this) and add a one-line comment citing `cube_grid.c:2383` so the deviation from the literal
  `0.0` is documented as intentional, not accidental.
- **Follow-on shape: one umbrella issue.** External-prior load (`cube_grid_initialise` analog)
  and per-sounding touchdown interpolation (`cube_grid_interpolate` + the `mapsheet_cube.c` range
  overwrite) only deliver value together — neither is useful alone — and the Grid-vs-GeoGrid
  geometry decision belongs to the same producer subsystem. Agree with the plan: one umbrella
  issue. File it before/with the PR and reference it.

### Producer-deferral judgment (faithful increment vs inert-path-in-disguise)

**This is a faithful, safe increment — not a re-skinned inert-path defect.** Three reasons the
v2 inert-path objection does not recur here:

1. **The math being landed is now CORRECT**, where v1/v2 landed a *wrong* formula behind the
   inert guard. v2's defect was "re-enables a path that, if it ran, would corrupt off-boresight
   depths." v3's path, if it runs, computes the verified `pred_depth(node) − pred_depth(touchdown)`.
   Landing a correct-but-dormant primitive is categorically different from landing a wrong one.
2. **The deferred piece is a genuinely separate subsystem**, not a call-site bolt-on. The producer
   is the entire external-prior pipeline: a prior-bathymetry loader (`cube_grid_initialise`:
   load array, seed `pred_depth` + base hypothesis per node) plus per-sounding touchdown
   interpolation (`cube_grid_interpolate` + the `mapsheet_cube.c` `range` overwrite). That is real,
   sizeable infrastructure the port wholly lacks — deferring it is honest scoping, not a dodge.
3. **The path is exercised, not silently-dead.** The integration test
   (`SlopeCorrectionAppliesOnSeededSurface`) seeds `setPredictedDepth` directly and supplies a
   synthetic `predicted_depth_at_touchdown`, driving the offset end-to-end; the paired cold-grid
   case proves offset-0. So the block has live test coverage at exactly the cases the prior bugs hid.

**Offset-0-when-unwired is safe for BOTH grids.** `Grid` and `GeoGrid` share the identical
`Node::insert` (grid.cpp:122 / geo_grid.cpp:95) with the same default-sentinel `predicted_depth_`;
there is no separate sparse-GeoGrid node path that could diverge. With no producer, both run at
offset 0 — the defined correct-but-uncorrected behaviour they have today, not a regression.

The one residual reservation (a suggestion, not a blocker): once merged, production CUBE still does
**no** slope correction until the follow-on lands, so the issue's user-visible "slope correction is
disabled" symptom persists in deployed behaviour. The plan is explicit and honest about this, files
the follow-on, and the PR description must say so plainly so a reader doesn't mistake "re-enabled"
for "active in production." With that framing, the increment is the right call under the Quality
Standard's "fix it completely at the achievable boundary."

### Evaluation summary
| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | 3 small production edits + focused tests; prediction subsystem correctly out-of-scope and tracked. |
| Issue alignment | Good | Re-enables the offset with the correct algorithm; remaining production-activation gap honestly scoped + filed. |
| File targeting | Good | `sounding.h`/`node.{h,cpp}`/`test_node.cpp`/`test_grid.cpp` are exactly right; no `grid.cpp`/`geo_grid.cpp` churn (no producer). |
| Consequences | Good | Both ctors, `setPredictedDepth` primitive, tests, and the follow-on dependency are all captured. |
| Principle alignment | Good | "Replicate don't invent" (formula + primitive are 1:1 ports; the rejected self-bootstrap is explicitly NOT implemented); "never document from assumptions" (every claim cited and re-verified here); "fix it completely at the achievable boundary." |
| ADR compliance | Good | No ADR triggered — pure algorithmic faithful-port; `predicted_depth_at_touchdown` is an internal field, not a ROS message change. |
| ROS conventions | N/A | No topics/params/QoS/lifecycle touched. |

The slope-correction formula is **finally correct**: `offset = predicted_depth_(node) −
predicted_depth_at_touchdown`, both negative-down predicted-surface depths, with no obliquity
factor — exactly the original's `node->pred_depth − snd->range` once you account for the
`mapsheet_cube.c:2434` overwrite. The producer deferral is a faithful, tested, safe increment, not
the v2 inert-path problem renamed. Approve with two minor suggestions (test-framing precision,
default-member-init) and the two open-question recommendations above (use `INVALID_DATA`; one
umbrella follow-on). Ready for implementation.

### Findings checklist (for downstream parsers)
- [ ] (suggestion) Tighten off-boresight test framing to touchdown-position, reconstruct expected independently — `plan.md:164-185`
- [ ] (suggestion) Prefer a default member initializer for `predicted_depth_at_touchdown` over per-ctor sets — `plan.md:132-137`
- [ ] (recommendation) Adopt `INVALID_DATA` sentinel (not literal `0.0`); comment-cite `cube_grid.c:2383` — `plan.md:227-231`
- [ ] (recommendation) One umbrella follow-on for the external-prior + touchdown-interpolation producer — `plan.md:232-234`

## Implementation
**Status**: complete
**When**: 2026-06-21 10:30 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-15
**Commits**: `db05fc3` (production), `f5831d8` (tests), `9dd4df7` (plan sync) — atomic, agent identity, hooks passed, NOT pushed.

### What changed (3 production edits + tests, per the approved v3 plan)
- **`sounding.h`**: added `float predicted_depth_at_touchdown = INVALID_DATA;` (default member initializer, per review suggestion 2 — no per-ctor edit). Negative-down predicted-surface depth at the sounding touchdown; the port's analog of the *overwritten* `snd->range` (`mapsheet_cube.c:2434`), deliberately NOT named `range`. Doc cites `cube_grid.c:2383` for why the literal-original `0.0` sentinel is port-unsafe (a real shoreline touchdown depth can be `0.0`) and `INVALID_DATA` carries the faithful intent without the value collision (review suggestion 1 applied).
- **`node.h` / `node.cpp`**: added `setPredictedDepth(float, float)` — a 1:1 port of `cube_node_set_preddepth` (`cube_node.c:1084`) — plus a `predictedDepth()` const accessor for the future producer + tests.
- **`node.cpp` `Node::insert`**: re-enabled the previously-commented offset block with the corrected formula `offset = predicted_depth_ - sounding.predicted_depth_at_touchdown`, queued as `sounding.depth + offset`. Guard reproduces the original `range != 0.0 && pred_depth != no_data_value`, both sentinels as `INVALID_DATA` (the old comment's `parameters.no_data_value` was `quiet_NaN()` — doubly wrong; corrected). Comment block cites `cube_node.c:1846` + `mapsheet_cube.c:2434`, states why it was disabled at port time (no predicted-surface field), and that no `1/cos^2` obliquity applies.
- **Producer NOT built** (deferred follow-on): nothing sets `predicted_depth_` or `predicted_depth_at_touchdown` in production, so both stay at `INVALID_DATA` => offset 0 => unchanged correct-but-uncorrected behaviour for both `Grid` and `GeoGrid`.

### Tests (`test_node.cpp`, +4)
- `SlopeCorrectionSurfaceSlopeDelta` — synthetic-injection end-to-end (seed `setPredictedDepth(-10.0)` + touchdown `-10.6`, raw depth `-10.5`; drive insert→queueFlush→extractDepthAndUncertainty; assert converged `-9.9` reconstructed **independently** from `depth + (pred_node - pred_touchdown)`, and that it does NOT collapse to `pred_node` or raw depth — so an offset-0 regression fails). Review suggestion 1 (independent reconstruction) applied.
- `SlopeCorrectionZeroOnFlatSurface` — `pred_touchdown == pred_node` => offset 0.
- `NoSlopeCorrectionWhenTouchdownSentinel` — sounding's touchdown is `INVALID_DATA` => offset 0.
- `NoSlopeCorrectionWhenPredictedDepthInvalid` — node has no prediction (default `INVALID_DATA`) => offset 0 (the production-unwired path).

### Deviation from plan (recorded in plan.md `9dd4df7`)
Plan named `test_grid.cpp` for the integration test. `Grid`/`GeoGrid` keep `nodes_` private with **no per-node seed API**, so `predicted_depth_` cannot be set through the grid public surface. The end-to-end synthetic-injection test therefore lives at the **`Node`** level — the shared integration point both `Grid::insert` and `GeoGrid::insert` call (`grid.cpp:122`, `geo_grid.cpp:95`) — driving the identical pipeline. Adding a node-seed grid accessor is the deferred producer's concern, not this PR's. No `test_grid.cpp` change.

### Build + test results
- Build: **clean** (`cube_bathymetry` built; the only stderr is pre-existing GDAL `-Wunused-result` warnings in `bag_to_geotiff.cpp`, unrelated to this change).
- gtest: **269 tests, 0 errors, 0 failures, 38 skipped** across the package. `test_node.gtest.xml`: **16 tests (was 12, +4 new slope tests), 0 failures, 0 errors** — all 4 new slope tests `result="completed"`. `test_grid.gtest.xml`: 9, 0/0. No uncrustify local-drift noise this run (uncrustify.xunit.xml: 38, 0 failures). Judged green by gtest XML.

### Suggested producer follow-on issue (for the host to file at the publish checkpoint)
**Title**: Port the CUBE predicted-surface producer (external-prior load + per-sounding touchdown interpolation) to make slope correction active

**Body** (suggested):
> Part of #15. Issue #15 ported the node-level slope-correction math (`Node::setPredictedDepth` + the corrected offset in `Node::insert`) but explicitly deferred the predicted-surface *producer* that drives it. With no producer, `predicted_depth_`/`Sounding::predicted_depth_at_touchdown` stay at `INVALID_DATA` so the offset is 0 (correct-but-uncorrected) in production.
>
> This issue ports the producer subsystem the port wholly lacks, as one umbrella (the two pieces only deliver value together):
> 1. **External-prior load** (`cube_grid_initialise` analog, `cube_grid.c:1665,1712,1730`): load a prior bathymetry array, seed each node's `predicted_depth_`/variance via `setPredictedDepth`, and seed a base null hypothesis.
> 2. **Per-sounding touchdown interpolation** (`cube_grid_interpolate` + the `mapsheet_cube.c:2434` `range` overwrite): bilinearly blend the 4 surrounding nodes' `predicted_depth_` at each sounding's touchdown `(de,dn)` and set `Sounding::predicted_depth_at_touchdown` (returns no-correction sentinel if any corner is no-data).
>
> The Grid-vs-GeoGrid interpolation geometry decision (regular bilinear vs the GGGS-cell analog) belongs here, with a per-node seed API on `Grid`/`GeoGrid`. Once landed, slope correction is active end-to-end; until then, deployed behaviour remains uncorrected (#15 is honest about this).

**Status**: complete
**By**: Claude Code Agent (Claude Opus)
