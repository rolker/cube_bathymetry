# Plan (v3): Slope correction is disabled (commented out) in Node::insert

## Issue

https://github.com/rolker/cube_bathymetry/issues/15

**Third plan.** Two prior plans got the CUBE slope-correction algorithm wrong and were
changes-requested. The latest `## Plan Review` (round 2) pinned the correct algorithm by
re-reading the original source line-by-line. This v3 ports *that* algorithm, verified
against `original_cube/` before writing (see Verification below). It **REPLACES** the v2
plan in full.

## The corrected algorithm (re-verified against `original_cube/` source)

Slope correction projects a sounding that touches down off-node onto the node along the
**predicted seabed surface**, not along the acoustic beam. The original needs two things,
and the prior plans misread both:

1. **The offset is a predicted-SURFACE slope delta — NOT `depth/cos(angle)`.**
   At the offset site (`cube_node.c:1846`):
   ```c
   if (snd->range != 0.0 && node->pred_depth != p->no_data_value)
       offset = node->pred_depth - snd->range;   // queued as snd->depth + offset
   ```
   `snd->range` here is **overwritten** before insert. In `mapsheet_cube.c:2434`:
   ```c
   data[snd].range = cube_grid_interpolate(tile, p->cube, de, dn, data[snd].dr, &pred_var);
   ```
   with the comment *"fill the result into the 'range' element of the sounding [bad!
   <smack>] for cube_node_insert() to use for slope corrections."* `cube_grid_interpolate`
   (`cube_grid.c:2360`) bilinearly interpolates the **4 surrounding nodes' `pred_depth`**
   (`cube_grid.c:2374-2377`) at the touchdown point `(de,dn)`. So:
   ```
   offset = pred_depth(node) − pred_depth(touchdown)
   queued = snd->depth + pred_depth(node) − pred_depth(touchdown)
   ```
   Both terms are negative-down predicted-surface depths in the *same* convention; the
   `depth/cos(angle)` "error-model range" (`sounding.c:1268`, computed while depth is still
   positive and negated at `sounding.c:1280`) is a **different quantity that is overwritten
   before the offset runs** — it is irrelevant at the offset site. No `1/cos²`, no obliquity
   factor: it is purely a surface-slope difference between node and touchdown.

2. **`pred_depth` does NOT self-bootstrap from running estimates.**
   `cube_grid_insert_depths` (`cube_grid.c:1877-1992`) iterates nodes and calls only
   `cube_node_insert` — it has **no** `cube_grid_interpolate` and **no**
   `cube_node_set_preddepth` call. `pred_depth` is seeded in exactly two places:
   - **External prior**: `cube_grid_initialise` (`cube_grid.c:1665`) loops every node, calls
     `cube_node_set_preddepth(node, depth, var)` (`:1712`) from a supplied prior bathymetry
     array `f32 *data`, and seeds a base hypothesis via `cube_node_add_null_hypothesis`
     (`:1730`).
   - **Touchdown interpolation** of that prior surface, written into `snd->range`
     (`mapsheet_cube.c:2434`, above).
   `cube_grid_interpolate` returns `0.0` (no correction) unless **all 4 corners** have valid
   `pred_depth` (`cube_grid.c:2379-2384`). Both prior plans invented an
   interpolate-from-running-estimates-during-insert path. **It does not exist in the
   original.**

### Verification performed for this plan (read, not assumed)

| Claim | Source verified | Result |
|---|---|---|
| `range` overwritten with interpolated touchdown pred_depth | `mapsheet_cube.c:2417-2444` | ✓ verbatim |
| offset = pred_depth − range (queued depth+offset) | `cube_node.c:1846-1862` | ✓ verbatim |
| `cube_grid_interpolate` reads `pred_depth`, returns 0 if any corner no-data | `cube_grid.c:2360,2374-2384` | ✓ |
| `cube_grid_insert_depths` never sets pred_depth / never interpolates | `cube_grid.c:1877-1992` | ✓ (only calls `cube_node_insert`) |
| `pred_depth` seeded from external prior array | `cube_grid.c:1665,1712,1730` | ✓ |
| `cube_node_set_preddepth` doc: INVALID ⇒ no slope correction | `cube_node.c:1066-1093` | ✓ |
| port `predicted_depth_` negative-down, blunder-checked vs `sounding.depth` | `node.cpp:93-102`, `node.h:198` | ✓ |
| port has no prior-load / no `setPredictedDepth` / no `Sounding.range` | `node.*`, `grid.cpp`, `geo_grid.cpp`, `sounding.h` | ✓ absent |

## The crux: the port has NO predicted-surface mechanism — resolution

A faithful port of *active* slope correction requires infrastructure the port **entirely
lacks**: (a) a prior-surface load path (`cube_grid_initialise` analog: load prior bathymetry,
seed `predicted_depth_` + base hypothesis per node), and (b) the per-sounding touchdown
interpolation of that prior surface (`cube_grid_interpolate` + the `mapsheet_cube.c` `range`
overwrite). Today `predicted_depth_` is `INVALID_DATA` everywhere and nothing sets it.

The two rejected paths are off the table:
- **Self-bootstrap from running estimates** — this is what v1/v2 reached for. It is *not*
  the original (`cube_grid_insert_depths` sets no pred_depth; `cube_grid_interpolate` reads
  `pred_depth`, not estimates). Shipping it would be inventing a new estimator and
  mislabeling it a port. **Rejected — deviation from CUBE with no correctness argument.**
- **A non-physical offset or a silently-dead block** — also rejected by the review.

**Decision (documented deviation: none — faithful subset + scoped deferral):**
This PR ports the **offset shape correctly and faithfully**, and **explicitly defers the
active-correction behaviour** behind the missing external-prior subsystem, which is too
large to build correctly under current pressure and is filed as a referenced follow-on.
Concretely:

- Add `Node::setPredictedDepth(depth, var)` — a faithful 1:1 port of
  `cube_node_set_preddepth` (`cube_node.c:1084`). This is the real, named primitive; it is
  not a stand-in for the prior pipeline.
- Re-enable the offset with the **correct formula**: `offset = predicted_depth_(node) −
  predicted_depth_at_touchdown`, where `predicted_depth_at_touchdown` is supplied **per
  sounding** as a field on `Sounding` (the port's analog of the overwritten `snd->range` —
  named `predicted_depth_at_touchdown`, NOT `range`, to avoid resurrecting the misread).
  Guard exactly as the original: skip (offset 0) when the touchdown prediction is the
  no-correction sentinel **or** `predicted_depth_` is `INVALID_DATA`.
- **Do not** wire a predicted-surface producer in this PR. With no prior loaded,
  `predicted_depth_` stays `INVALID_DATA` and `Sounding.predicted_depth_at_touchdown` stays
  at its no-correction sentinel, so the offset is 0 — the **defined, safe, correct-but-
  uncorrected** behaviour the grids already have today. This is *not* a dead block: it is a
  correct, tested, fully-wired code path that activates the instant a predicted surface is
  supplied. The block is exercised end-to-end by the integration test (which supplies a
  synthetic prediction directly), so it is not silently-dead either.
- **File the follow-on now** (referenced in the PR): "Port `cube_grid_initialise`
  external-prior load + base-hypothesis seed, and the `mapsheet_cube.c` per-sounding
  touchdown interpolation (`cube_grid_interpolate`) of `predicted_depth_at_touchdown`, to
  make slope correction active in production." That issue is the home for the prior-surface
  subsystem; this PR delivers the node-level math it will drive.

This satisfies "fix it completely" at the achievable boundary: the part that *can* be
ported faithfully and correctly now (the node math + primitive) is, with tests at the
off-boresight cases where the prior bugs hid; the part that needs a whole subsystem is
honestly scoped out and tracked, not faked.

## Scope decision: Grid-only (and here, neither grid is wired — both stay at offset 0)

Per the review: defer GeoGrid (and the GGGS bilinear analog) to a referenced follow-on.
GeoGrid at offset-0 is **safe (correct-but-uncorrected), not dead**. In this PR the
node-level offset is identical for both grids (it lives in `Node::insert`); since neither
grid supplies a predicted surface yet, both run at offset 0 unchanged. The follow-on that
builds the prior-surface producer is where Grid-vs-GeoGrid interpolation geometry gets
decided — that is the right place for the GGGS divergence question, not here.

## Approach

### Part A — Correct, faithful offset in `Node::insert`

1. **Add `predicted_depth_at_touchdown` to `Sounding`** (`sounding.h`): a `float`
   defaulting to a no-correction sentinel (`INVALID_DATA`, mirroring the original's
   `range != 0.0` guard meaning "no interpolation result"). Document it as the port's analog
   of the *overwritten* `snd->range` (interpolated prior-surface depth at the sounding
   touchdown, negative-down) — explicitly **not** `depth/cos(angle)`. Leave both existing
   constructors setting it to the sentinel (no producer yet).

2. **Re-enable the offset** (`node.cpp:118-125`): replace the commented block with
   ```cpp
   if (sounding.predicted_depth_at_touchdown != INVALID_DATA &&
       predicted_depth_ != INVALID_DATA) {
     offset = predicted_depth_ - sounding.predicted_depth_at_touchdown;
   }
   ```
   queued as `sounding.depth + offset` (unchanged). Add a comment block deriving the offset
   as `pred_depth(node) − pred_depth(touchdown)` with the `cube_node.c:1846` +
   `mapsheet_cube.c:2434` citations, stating why it was disabled at port time (no
   `range`/predicted-surface field existed), and **correcting the stale
   `parameters.no_data_value` sentinel** in the old comment to `INVALID_DATA` (matching the
   live guard at `node.cpp:93`).

3. **`Node::setPredictedDepth(float depth, float variance)`** (`node.h`/`node.cpp`):
   faithful 1:1 port of `cube_node_set_preddepth` (`cube_node.c:1084`) — sets
   `predicted_depth_` / `predicted_depth_variance_`. Public, so the future prior-load path
   (and the tests) can seed a predicted surface. Port the doc-comment sentinel conventions
   verbatim (NaN ⇒ don't incorporate; INVALID ⇒ no prediction / no slope correction).

### Part B — Tests (the bug hid off-boresight and at the offset semantics)

Tests reconstruct the **original** `pred_depth(node) − pred_depth(touchdown)` independently
from a known synthetic prior surface — never the rejected `pred` or `pred − depth/cos`.

4. **`test_node.cpp`:**
   - `SlopeCorrectionSurfaceSlopeDelta`: `setPredictedDepth(pred_node, var)`; a `Sounding`
     with `predicted_depth_at_touchdown = pred_touchdown` for a **genuinely off-boresight**
     touchdown on a known slope (e.g. `pred_node = -10`, `pred_touchdown = -10.6`,
     `depth = -10.5`); assert the queued/extracted depth equals
     `depth + (pred_node − pred_touchdown)` reconstructed independently in the test — the
     surface-slope-delta result, **not** `pred` and **not** `pred − depth/cos(angle)`. This
     is the test that catches all three prior-formula errors.
   - `SlopeCorrectionZeroOnFlatSurface`: `pred_touchdown == pred_node` ⇒ offset 0 ⇒
     queued `= depth`.
   - `NoSlopeCorrectionWhenTouchdownSentinel` and `…WhenPredictedDepthInvalid`: offset stays
     0 (queued `= depth`).

5. **Synthetic-injection integration test** (`SlopeCorrectionSurfaceSlopeDelta`, in
   `test_node.cpp`): seed a `Node`'s predicted surface via `setPredictedDepth(pred_node, var)`,
   construct off-node soundings whose `predicted_depth_at_touchdown` is set (in the test) to
   the surface value at their touchdown, drive the **full** `Node::insert` → `queueFlush` →
   `extractDepthAndUncertainty` pipeline, and assert the converged estimate reflects
   `depth + (pred_node − pred_touchdown)` — i.e. the wired path executes end-to-end. The
   paired `NoSlopeCorrectionWhenTouchdownSentinel` / `…WhenPredictedDepthInvalid` cases assert
   offset-0 (cold / no-prior behaviour), proving the safe default. Together these demonstrate
   the block is live-and-correct when fed a prediction, and safe (offset 0) when not —
   directly answering "not a silently-dead block."

   **Deviation from earlier draft (this file → `test_grid.cpp`):** `Grid` and `GeoGrid` keep
   `nodes_` private and expose **no** per-node seed API, so `predicted_depth_` cannot be set
   through the grid public surface. The end-to-end injection test therefore lives at the
   **`Node`** level — the shared integration point both `Grid::insert` and `GeoGrid::insert`
   call (`grid.cpp:122`, `geo_grid.cpp:95`) — driving the identical pipeline. Adding a
   node-seed API to the grids is the deferred predicted-surface producer's concern (it is what
   `cube_grid_initialise` does), not this PR's; introducing a test-only grid accessor now would
   add production surface area ahead of that subsystem. No `test_grid.cpp` change in this PR.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/sounding.h` | Add `predicted_depth_at_touchdown` field (sentinel default) + doc; set sentinel in both ctors |
| `cube_bathymetry/include/cube_bathymetry/node.h` | Declare `setPredictedDepth(float,float)` |
| `cube_bathymetry/src/node.cpp` | Re-enable corrected offset (`pred_depth − touchdown`); implement `setPredictedDepth`; comments + sentinel fix |
| `cube_bathymetry/test/test_node.cpp` | Surface-slope-delta + flat + sentinel + invalid offset tests, AND the synthetic-injection end-to-end test (seed `setPredictedDepth` + touchdown → insert→flush→extract) |
| ~~`cube_bathymetry/test/test_grid.cpp`~~ | **No change** — Grid/GeoGrid expose no node-seed API; the end-to-end injection test lives in `test_node.cpp` at the shared `Node::insert` integration point (see Part B.5 deviation) |

No change to `grid.cpp` / `geo_grid.cpp` in this PR (no predicted-surface producer is
wired — see crux). They continue to run at offset 0 as today.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Replicate, don't invent | Offset is the verified `pred_depth(node) − pred_depth(touchdown)` surface delta; `setPredictedDepth` is a 1:1 port of `cube_node_set_preddepth`. The rejected self-bootstrap path is explicitly not implemented. |
| Never document from assumptions | Every formula/sign claim cited to `mapsheet_cube.c:2434`, `cube_node.c:1846`, `cube_grid.c:2360/1665/1712`, `cube_node.c:1084`, re-read for this plan (Verification table). |
| Fix it completely | The faithfully-portable part (node math + primitive) ships fully tested at off-boresight/slope cases; the part needing a whole prior-surface subsystem is scoped out and FILED, not faked or half-built. |
| Robustness / no silent corruption | No `1/cos²` obliquity error (the v1/v2 bug); guards (`INVALID_DATA` on both pred + touchdown) reproduce the original's `range != 0.0 && pred != no_data` exactly; default behaviour is offset-0 (safe), not a non-physical correction. |
| Human control / transparency | Re-enabled path is inert-but-correct until a prior surface is supplied; the follow-on issue documents the remaining subsystem so no hidden half-feature lands. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| None | No | Pure algorithmic faithful-port; no ROS topics/params/QoS/lifecycle. `predicted_depth_at_touchdown` is an internal `Sounding` field, not a ROS message change. |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `Sounding` adds `predicted_depth_at_touchdown` | both ctors set the sentinel | Yes — A.1 |
| `Node::insert` offset re-enabled | `setPredictedDepth` primitive (new) | Yes — A.3 |
| Offset re-enabled but no producer | follow-on issue for `cube_grid_initialise` prior-load + `cube_grid_interpolate` touchdown interp (Grid + GeoGrid geometry) | Yes — filed + referenced in PR |
| New offset/primitive | `test_node.cpp` + `test_grid.cpp` | Yes — Part B |

## Open Questions

- [ ] **Sentinel for `predicted_depth_at_touchdown`**: plan uses `INVALID_DATA` to mirror the
  original's `range != 0.0` guard. Confirm `INVALID_DATA` (= `float::max`) is the right
  "no-correction" sentinel here vs. `0.0` (the literal original test). `INVALID_DATA` is
  preferred because a *legitimately interpolated* touchdown depth could be `0.0`; flag for
  review.
- [ ] **Follow-on title/scope**: confirm the prior-surface subsystem (external-prior load +
  touchdown interpolation, Grid + GeoGrid) is one follow-on issue, or split prior-load from
  interpolation. Recommend one umbrella issue, since they only deliver value together.

## Estimated Scope

**Single small PR.** Three production edits (one `Sounding` field, the offset re-enable, the
`setPredictedDepth` primitive) plus focused tests. The large, risky part — the prior-surface
prediction subsystem the v2 plan tried to fold in — is correctly *out* of this PR and filed
as a referenced follow-on. Size/risk read after three passes: **low**. The remaining risk is
the sentinel-convention choice (Open Question 1), settled at review.
