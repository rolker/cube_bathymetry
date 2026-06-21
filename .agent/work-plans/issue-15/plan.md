# Plan: Slope correction is disabled (commented out) in Node::insert

## Issue

https://github.com/rolker/cube_bathymetry/issues/15

## Context

The original CUBE C code (`original_cube/libsrc/cube/cube_node.c:1846-1862`) applies a
slope correction offset when a predicted depth is available and the sounding's boresight
range is non-zero:

```c
if (snd->range != 0.0 && node->pred_depth != p->no_data_value) {
    offset = node->pred_depth - snd->range;
}
```

In the ROS 2 port (`cube_bathymetry/src/node.cpp:117-124`) this entire block is
commented out; `offset` is always 0.0. The commented code referenced `sounding.range`,
a field that was never added to the port's `Sounding` struct — this is why it was
left disabled at port time.

The port already carries the equivalent data in
`Sounding::sonar_relative_position.z`, which is computed as
`range * cos(tx_angle) * cos(rx_angles[i])` — the vertical component of the slant
range from the sonar to the detection (positive = down). This is the correct analog
for the original's `snd->range` field.

### Sign convention (verified against source)

| Quantity | Sign convention |
|---|---|
| `Sounding::depth` | Negative below surface (per header comment: "Positive is up above sea surface and negative is down below") |
| `predicted_depth_` | Same as `depth` — negative below surface (used as `target_depth` in blunder comparisons against `sounding.depth`) |
| `sonar_relative_position.z` | Positive down (computed as `range * cos(…) * cos(…)`, always positive for a downward sonar) |
| Original `snd->range` | Positive down (hydrographic positive-below-surface convention) |
| Original `node->pred_depth` | Positive down (original CUBE hydrographic convention) |

The original offset is `pred_depth_orig - range_orig` where both are positive-down.
In the port, `predicted_depth_` is negative-down and `sonar_relative_position.z` is
positive-down, so:

```
offset_port = predicted_depth_ - (-sonar_relative_position.z)
            = predicted_depth_ + sonar_relative_position.z
```

The commented-out formula `predicted_depth_ - sounding.range` has the wrong sign for
the port's depth convention.

### Missing setter

`predicted_depth_` has no public setter in the port. The original sets it via
`cube_node_set_preddepth()` called from the map sheet after interpolating a prior
surface. Without a setter (and call site in Grid/GeoGrid), slope correction will
never fire even after re-enabling. Adding the setter is in scope for this issue;
hooking it into a per-node interpolation pass in Grid/GeoGrid is a larger follow-on
(the port doesn't yet have a prior-surface interpolation pipeline).

## Approach

1. **Fix the formula and re-enable slope correction in `Node::insert`** — replace the
   commented block with the corrected expression using `sonar_relative_position.z`.
   Guard condition: `sonar_relative_position.z != 0.0` (matches original's
   `snd->range != 0.0`) and `predicted_depth_ != INVALID_DATA`. Add a comment
   explaining (a) the sign-convention derivation and (b) why this was originally
   commented out (no `range` field at port time; the `sonar_relative_position.z`
   field was the fix).

2. **Add `setPredictedDepth()` to `Node`** — expose a public method so grid-level
   code can supply the interpolated predicted surface depth and variance before
   calling `insert`. Without this, slope correction remains unreachable.

3. **Add slope-correction test in `test_node.cpp`** — add a
   `SlopeCorrectionAppliesOffset` test:
   - Set `predicted_depth_` via `setPredictedDepth()` to a known value (e.g., -10.0)
   - Construct a `Sounding` with `sonar_relative_position.z = 9.0` (node is 1m
     shallower than boresight) and `depth = -9.0`
   - Insert with distance small enough to pass capture and blunder checks
   - Flush the queue and extract depth
   - Assert the extracted depth reflects the corrected value
     (`-9.0 + (-10.0 + 9.0) = -10.0`)
   - Add a zero-z `NoSlopeCorrectionWhenZero` test confirming offset stays 0.0

4. **Comment: why originally disabled** — add an inline comment block at the
   re-enabled slope correction explaining that `sounding.range` was the missing
   field at port time and that `sonar_relative_position.z` is now the equivalent.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/node.cpp` | Re-enable slope correction with corrected sign; add explanatory comments |
| `cube_bathymetry/include/cube_bathymetry/node.h` | Add `setPredictedDepth(float depth, float variance)` declaration |
| `cube_bathymetry/test/test_node.cpp` | Add `SlopeCorrectionAppliesOffset` and `NoSlopeCorrectionWhenZero` tests |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Fix it completely | The formula sign bug is fixed, the setter is added, and tests cover both the corrected and the zero-z paths. The grid-integration follow-on is called out explicitly rather than left silent. |
| Never document from assumptions | Sign convention derived from reading `sounding.h` (header comment), `node.cpp` blunder comparisons, `original_cube/libsrc/cube/cube_node.c`, and `original_cube/libsrc/sounding/sounding.h`. |
| Robustness | Guard conditions match the original (`z != 0.0` and `predicted_depth_ != INVALID_DATA`). |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| None directly triggered | — | Change is a pure algorithmic re-enable within an existing file; no new topics, parameters, or architecture changes. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `Node::insert` slope correction | `Node::setPredictedDepth` (new, must be added) | Yes — step 2 |
| `setPredictedDepth` API | Grid/GeoGrid callers that will eventually supply predicted surface | No — follow-on; noted as open question |
| Slope correction enabled | Tests | Yes — step 3 |

## Open Questions

- **Grid integration follow-on**: Should a new issue be filed now to track hooking
  `setPredictedDepth` into Grid/GeoGrid after a prior-surface interpolation pass, or
  is that left for discovery when the interpolation pipeline is built? This plan does
  not implement the grid-level call site — slope correction will compile and have a
  test, but will remain inactive in production until a caller sets the predicted depth.

## Estimated Scope

Single PR. Three files changed; no new dependencies; no API surface beyond the one new
public method. Test additions are self-contained.
