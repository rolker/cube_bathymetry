# Plan: Slope correction is disabled (commented out) in Node::insert — FULL FIX

## Issue

https://github.com/rolker/cube_bathymetry/issues/15

**This is a re-plan with expanded scope** (operator decision). The prior plan only
re-enabled the dormant block; `review-plan` rejected it (wrong formula + runtime-inert).
This plan fixes **both**: the correct slant-range offset analog AND the prior-surface
prediction pipeline that makes slope correction execute end-to-end.

## Context

### What the original CUBE actually does

Slope correction lets a sounding that touches down off-node be projected to the node
along the local seabed slope, instead of being treated as a vertical measurement at the
node. Two pieces are required and both exist in the original:

1. **The offset** (`cube_node.c:1846`):
   ```c
   if (snd->range != 0.0 && node->pred_depth != p->no_data_value)
       offset = node->pred_depth - snd->range;   // queued as snd->depth + offset
   ```
   `snd->range` is **not** a horizontal/slant distance — it is a *slant-projected
   signed depth*: `range = depth / cos(beam_angle)` (`sounding.c:1268`), computed
   **while `depth` is still positive** (depth is negated to negative-down only later,
   `sounding.c:1278`). So in final convention `snd->depth` is negative-down and
   `snd->range` is **positive**, magnitude `|depth|/cos(angle) ≥ |depth|`. At boresight
   (`angle=0`) `range = |depth|`; off-boresight `range > |depth|`.

2. **The prediction (`pred_depth`)** — produced two ways in the original:
   - **External prior surface**: `cube_grid_initialise` / `cube_grid_init_unct`
     (`cube_grid.c:1666,1773`) loop every node and call
     `cube_node_set_preddepth(node, depth, var)` from a supplied prior bathymetry
     array, then seed a base hypothesis via `cube_node_add_null_hypothesis`.
   - **Self-bootstrapped from current estimates**: `cube_grid_interpolate`
     (`cube_grid.c:2360`) does bilinear interpolation of the **4 nearest nodes'
     current `pred_depth`** to predict depth at an arbitrary point; returns `0.0`
     (no correction) unless all 4 corners have valid `pred_depth`.

The convention sentinels (`cube_node.c:1078`, `cube_node_set_preddepth` doc):
`pred_depth == NaN` ⇒ do not incorporate data; `pred_depth == INVALID` ⇒ no prediction
available, no slope correction (offset 0). The port already mirrors these in
`Node::insert` (`node.cpp:83,93`).

### The port today

- `Sounding` (`sounding.h`) has **no `range` field**. It has
  `sonar_relative_position.z = range * cos(tx) * cos(rx)` — the **vertical component**
  of the slant range (= `|depth|`, positive-down), which is the *wrong* analog for
  `snd->range` (the prior plan's bug: differs by `1/cos²`, not a sign flip).
- `predicted_depth_` defaults to `INVALID_DATA` (`node.h:198`); **no caller sets it**
  (`setPredictedDepth` does not exist; `Grid::insert`/`GeoGrid::insert` never supply
  one). So the guard at `node.cpp:93` is always false → slope correction is inert.
- `Hypothesis::generateNullHypothesis(depth, var)` already exists (`hypothesis.h:43`)
  — the port has the base-hypothesis primitive the original seeds from a prior.
- `Grid` (`grid.cpp`) is a flat regular `x×y` array (`origin_`, `sizes_`, `counts_`)
  → directly supports bilinear 4-NN interpolation. `GeoGrid` (`geo_grid.cpp`) is a
  sparse `map<GridIndex,Node>` keyed by GGGS cell, iterated via `CellAreaIterator`.

### Verified sign/quantity table (read from source, not assumed)

| Quantity | Source | Convention / magnitude |
|---|---|---|
| original `snd->depth` | `sounding.c:1278`, `sounding.h:423` | negative-down |
| original `snd->range` | `sounding.c:1268` (pre-negation) | **positive**, `= \|depth\|/cos(angle)` |
| original `node->pred_depth` | `cube_node.c` | negative-down (same as depth) |
| original offset | `cube_node.c:1847` | `pred_depth − range` (both as above) |
| port `sounding.depth` | `sounding.h` header | negative-down ("negative is down") |
| port `sonar_relative_position.z` | `sounding.h:50` | `range·cos(tx)·cos(rx)` = **vertical comp**, positive-down, `= \|depth\|` |
| port `predicted_depth_` | `node.h:198` | negative-down (compared against `sounding.depth` in blunder check) |

**Correct port offset** (reproduces the original at all angles): reconstruct the
slant-projected *signed* depth analog of `snd->range`, keeping the port's negative-down
sign:

```
slant = |sonar_relative_position|              (full slant range, ≥ |z|)
cos(angle) = |z| / slant                        (z = vertical comp)
range_analog = sounding.depth / cos(angle)      (= sounding.depth * slant / |z|),
                                                 negative-down, magnitude ≥ |depth|
offset = predicted_depth_ - range_analog
queued depth = sounding.depth + offset
```

Sanity vs original (boresight, rx=tx=0): `slant=|z|`, `cos=1`, `range_analog =
sounding.depth`, `offset = pred − depth`, queued `= pred`. Off-boresight (e.g. rx=60°):
`cos=0.5`, `range_analog = 2·depth` (still negative-down), `offset = pred − 2·depth`,
queued `= depth + pred − 2·depth = pred − depth` — i.e. the node sees the prediction
shifted by the slope-projected residual, **matching CUBE's `pred + (depth − range)`
once both are in the same (negative-down) convention**. The prior plan's
`pred + z` collapsed to exactly `pred` off-boresight, erasing the correction.

Guard `|z| > 0` (avoids div-by-zero; equivalent to the original's `range != 0.0`) and
`predicted_depth_ != INVALID_DATA`.

## Approach

### Part A — Correct offset in `Node::insert`

1. **Add a `range` field to `Sounding`** (`sounding.h`): a signed slant-projected
   depth `= depth / cos(beam_angle)`, populated in the `SonarDetections` constructor
   from the already-computed geometry (`range_analog = depth * slant / |z|`, with
   `slant = hypot(x,y,z)`); leave it `0.0` (the "no correction" sentinel, matching the
   original's `range != 0.0` guard) when `|z|` is 0 or the explicit-`depth`-only
   constructor is used. Document the sign convention in a header comment and note this
   field is the port's analog of the original `snd->range`.

2. **Re-enable slope correction** (`node.cpp:117-124`): replace the commented block with
   `if (sounding.range != 0.0 && predicted_depth_ != INVALID_DATA) offset =
   predicted_depth_ - sounding.range;`. Add a comment block citing the sign derivation,
   `sounding.c:1268`/`cube_node.c:1847`, and why it was originally disabled (missing
   `range` field at port time). Correct the stale `parameters.no_data_value` sentinel in
   the old comment to `INVALID_DATA` (matching the live guard at `node.cpp:93`).

### Part B — Prior-surface prediction pipeline (makes correction execute)

Replicate the original's **self-bootstrapped** mechanism (`cube_grid_interpolate`), the
one that needs no external prior file — it is the production-relevant path:

3. **`Node::setPredictedDepth(float depth, float variance)`** (`node.h`/`node.cpp`):
   faithful port of `cube_node_set_preddepth` — set `predicted_depth_` /
   `predicted_depth_variance_`. Public so grid code can call it.

4. **`Node` current-estimate accessor**: expose the node's current best-estimate
   depth+variance (reuse `extractDepthAndUncertainty`) so the grid can read neighbour
   estimates to interpolate from. (Original interpolates from `pred_depth`; the port's
   equivalent stored prediction is the running best estimate.)

5. **`Grid::interpolatePredictedDepth(MapPosition) -> optional<DepthAndUncertainty>`**
   (`grid.cpp`): faithful port of `cube_grid_interpolate` — locate the 4-NN cell
   (`floor(x/dx), floor(y/dy)`), read the 4 corner nodes' current estimates, return
   `nullopt` if any corner lacks a valid estimate (mirrors original's "return 0.0 / no
   correction"), else bilinear-interpolate depth and propagate variance (port the
   `cube_grid_est_interp_error` weighting; if that proves heavy, document a simpler
   variance-of-the-mean and flag as a follow-up — see Risks).

6. **Wire prediction into the insert path** (`Grid::insert`): before calling
   `node->insert(...)` for each affected node, compute the predicted depth at that
   **node's** position from its neighbours and `node->setPredictedDepth(...)`. Match the
   original's ordering caveat: prediction is interpolated from *already-estimated*
   neighbours, so on a cold grid the first soundings get no prediction (offset 0, exactly
   as the original behaves until a surface forms) and correction strengthens as the
   surface fills — verify this is the original's behaviour, do not invent a seeding rule.

7. **`GeoGrid::insert` parity** (`geo_grid.cpp`): provide the same prediction step for
   the sparse geographic grid. The 4-NN concept maps onto the GGGS cell neighbourhood;
   if exact bilinear over GGGS cells is non-trivial, port the nearest-estimated-neighbour
   prediction faithfully and document any geometry divergence from the regular-grid case.
   If this is genuinely large, it is the one sub-part that may warrant splitting into a
   stacked follow-on PR (see Estimated Scope) — `Grid` is the testable core.

### Part C — Tests

8. **Off-boresight offset unit tests** (`test_node.cpp`):
   - `SlopeCorrectionOffBoresight`: `setPredictedDepth(-10, var)`; a `Sounding` with a
     genuinely off-boresight beam (e.g. rx=60° so `range_analog = 2·depth`),
     `depth=-9`; assert the queued/extracted depth equals the **original CUBE**
     reconstruction `pred − depth_offset` (computed independently in the test from
     `pred_depth − depth/cos(angle)`), **not** the prior plan's `pred`. This is the
     test that would have caught the bug.
   - `SlopeCorrectionBoresightMatchesOriginal`: boresight beam → queued depth `= pred`.
   - `NoSlopeCorrectionWhenRangeZero` and `…WhenPredInvalid`: offset stays 0.

9. **Interpolation unit test** (`test_grid.cpp`): build a `Grid`, seed 4 corner nodes
   with a known synthetic slope, assert `interpolatePredictedDepth` at the centre equals
   the bilinear value and returns `nullopt` when a corner is unset.

10. **End-to-end integration test** (`test_grid.cpp`, new
    `SlopeCorrectionImprovesEstimateOnSlope`): on a known synthetic planar slope, insert
    off-node soundings through `Grid::insert` (prediction → corrected insert) and assert
    the corrected node estimate is closer to ground truth than the same run with
    correction disabled (predicted depth withheld). This proves the pipeline executes
    and helps, not just compiles.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/sounding.h` | Add signed `range` field + populate in `SonarDetections` ctor; doc sign convention |
| `cube_bathymetry/include/cube_bathymetry/node.h` | Declare `setPredictedDepth`, current-estimate accessor |
| `cube_bathymetry/src/node.cpp` | Re-enable corrected slope offset; implement `setPredictedDepth`/accessor; comments + sentinel fix |
| `cube_bathymetry/include/cube_bathymetry/grid.h` | Declare `interpolatePredictedDepth` |
| `cube_bathymetry/src/grid.cpp` | Implement bilinear prediction; wire into `insert` before `node->insert` |
| `cube_bathymetry/include/cube_bathymetry/geo_grid.h` | Declare prediction helper (GeoGrid parity) |
| `cube_bathymetry/src/geo_grid.cpp` | Prediction step in `insert` (or document split to follow-on) |
| `cube_bathymetry/test/test_node.cpp` | Off-boresight + boresight + zero/invalid offset tests |
| `cube_bathymetry/test/test_grid.cpp` | Interpolation unit test + end-to-end slope integration test |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Fix it completely | Both review-plan must-fixes addressed: correct slant-range analog (not `.z`) and a working prediction pipeline, not dead code. Tests hit off-boresight where the bug hid. |
| Never document from assumptions | Offset/sign derived from `sounding.c:1268/1278`, `cube_node.c:1846`, `cube_grid.c:2360/1666`; port types read from `sounding.h`, `node.h`, `grid.cpp`, `geo_grid.cpp`, `hypothesis.h`. |
| Robustness | Guards: `range != 0.0`, `predicted_depth_ != INVALID_DATA`, `nullopt` when 4-NN incomplete, div-by-zero guard on `|z|`. Faithful to original cold-grid behaviour. |
| Replicate, don't invent | Prediction = port of `cube_grid_interpolate` + `cube_node_set_preddepth`; not a new scheme. Divergences (variance model, GeoGrid geometry) called out, not hidden. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| None | No | Pure algorithmic faithful-port; no new topics/params/QoS/lifecycle. The `range` field on the internal `Sounding` struct is not a ROS message change. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `Sounding` adds `range` | both ctors (`SonarDetections` + explicit-depth) | Yes — Part A.1 |
| `Node::insert` offset | `setPredictedDepth` accessor (new) | Yes — Part B |
| `Grid::insert` prediction step | `GeoGrid::insert` parity | Yes — Part B.7 (or split, see scope) |
| New offset/pipeline | `test_node.cpp` + `test_grid.cpp` | Yes — Part C |
| Variance interpolation model | `cube_grid_est_interp_error` fidelity | Partial — port faithfully or flag simplification (Risks) |

## Open Questions

- [ ] **GeoGrid bilinear geometry**: regular-grid bilinear has no exact GGGS-cell analog.
  Acceptable to port nearest-estimated-neighbour prediction for `GeoGrid` and document
  the divergence, or must GeoGrid match `Grid` exactly before merge?
- [ ] **PR shape**: land Part A+B(Grid)+C as one PR and stack GeoGrid parity as a
  follow-on if it proves large, or insist on a single PR covering both grids?

## Risks (honest read — this is larger than the issue implied)

- **The interpolation pipeline (Part B) is the bulk of the work and the real risk.** The
  one-line offset (Part A) is small; building faithful prior-surface prediction and
  wiring it into two different grid representations (regular `Grid` + sparse GGGS
  `GeoGrid`) is a non-trivial port. The original's variance-propagation
  (`cube_grid_est_interp_error`, `cube_grid.c:2306`) is fiddly and may need its own
  careful port or a documented simplification.
- **Cold-grid / ordering coupling**: prediction reads already-estimated neighbours, so
  results depend on sounding insert order until the surface fills. This matches the
  original but makes the integration test sensitive to construction; the test must use a
  surface that is pre-seeded or inserted in an order that exercises the corrected path
  deterministically.
- **Mitigation / fallback**: if GeoGrid parity balloons under the ~2-day deployment
  pressure, the defensible minimum is Part A + Part B for `Grid` + all of Part C
  (off-boresight correctness proven end-to-end on the regular grid), with GeoGrid parity
  as an explicitly-filed stacked follow-on issue — **not** a silently-dead path.

## Estimated Scope

**Single PR if GeoGrid parity is tractable; otherwise a 2-PR stack** (Part A + Part B
for `Grid` + Part C first; GeoGrid prediction parity second). This is materially larger
than a "re-enable one block" change — it builds a prediction subsystem. The plan flags
the split point rather than understating the size.
