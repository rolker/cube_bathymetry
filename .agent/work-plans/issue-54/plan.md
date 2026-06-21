# Plan: CUBE co-estimation of backscatter: per-beam {raw intensity, grazing angle} sufficient stats in Hypothesis + deferred-settled node-output correction (ADR-0007)

## Issue

https://github.com/rolker/cube_bathymetry/issues/54

## Context

ADR-0007 (ACCEPTED) specifies that backscatter intensity is co-estimated with depth
inside CUBE. Per **D3** (binding), the hypothesis carries, *per contributing beam*,
the **sufficient statistics needed to correct later** — `{ raw intensity, grazing
angle }` — **not** a prematurely-corrected or pre-averaged number. The GeoCoder
incidence/Lambert correction (D3) is applied **at node-output, per beam**, once the
winning hypothesis depth and local slope have settled; **then** the corrected per-beam
values are combined into a Welford mean + estimate-variance (D2/D4). The correction
itself is **deferred-settled** pending cube_bathymetry#15 (slope correction, currently
disabled at `node.cpp:118-124`) — so Phase B is a *documented no-op* that emits
uncorrected intensity, **but the per-beam `{raw, angle}` set is retained now** so the
value is re-correctable when #15 lands.

> **Design correction vs. the first-draft plan (review must-fix #1).** The earlier
> draft stored an O(1) Welford of *raw* intensity in the hypothesis. That deviates
> from ADR-0007 D3: raw intensity is angle-corrupted (Context fact 1) and a
> pre-averaged raw mean is **not re-correctable**. Roland's decision: conform to D3 —
> store per-beam `{raw intensity, grazing angle}` pairs (a small per-hypothesis
> container, bounded by hypothesis membership = `n_samples` beams, exactly as D3
> states), correct per beam at output, then combine. No ADR amendment is needed; this
> plan now matches the accepted ADR.

`sounding.h` already carries `float intensity = std::nan("")` (merged in #52). The
intensity field is NaN when the source omits intensities and must never corrupt the
accumulator.

### Grazing-angle availability (front-loaded risk — VERIFIED)

D3's per-beam design depends on the per-beam grazing/beam angle being available where
intensity is captured. **Verified**: `Sounding(detections, i, depth)` (sounding.h:40)
has direct access to `detections.rx_angles[i]` / `tx_angles[i]`, and the error model
already derives the per-beam beam angle from them (`ErrorModel::beam_angle()`,
error_model.cpp:194 = `-rx_angles[i] + static_roll`). The angle is computed at the
exact point intensity is read; #52 retained `intensity` on the `Sounding` but **not**
the angle. We add a #52-style carry: a `beam_angle` field on `Sounding`, populated in
the same constructor from `rx_angles[i]`.

**Semantic note (honest scope).** What is available now is the per-beam **beam /
incidence angle relative to nadir** (the transducer-referenced steering angle), *not*
a true grazing angle relative to the local seafloor — the latter needs local slope
(#15). This is exactly what D3 anticipates: "grazing angle … plus the per-beam
geometry already present". We store the beam angle now (re-correctable); the true
grazing angle is reconstructed at output by combining it with settled depth + slope
(#15) inside the deferred GeoCoder chain. Storing the beam angle is the correct,
re-derivable sufficient statistic; no information is faked or dropped.

### Data-flow path for {intensity, angle}

```
Sounding(detections, i, depth)              # intensity AND beam_angle captured here
  → Node::insert(distance, sounding, params)        # both available on sounding
    → queueEstimate(depth+offset, variance, intensity, beam_angle, params)
        → update(depth, variance, intensity, beam_angle, params)   # picks hypothesis
            → Hypothesis::recordBeam(intensity, beam_angle)        # per-beam append
```

`{depth, intensity, beam_angle}` must stay **bound to the same beam** through the
median pre-queue. We attach intensity + angle to the `DepthAndUncertainty` transport
struct so depth never mismatches its intensity/angle when the median sorts the queue.

**Key invariant (exclusion-on-intervention).** When `Hypothesis::update()` returns
`false` (W&H monitor triggers an intervention), the beam's depth was **rejected** from
the winning hypothesis; its intensity/angle MUST also be excluded from that
hypothesis's per-beam set and instead recorded on the **newly-seeded** hypothesis (the
one that actually accepted this beam). This is enforced by routing `recordBeam()` to
the correct hypothesis in each of `Node::update()`'s three paths (below).

## Approach

### Phase A — per-beam sufficient stats + enriched node record (unblocked, this PR)

1. **Add `beam_angle` to `Sounding` (`sounding.h`)** — `float beam_angle =
   std::nan("")`, populated in the `Sounding(detections, i, depth)` constructor from
   `detections.rx_angles[i]` (the same array the error model uses), NaN-guarded by the
   `i < rx_angles.size()` check, mirroring the existing `intensity` carry.

2. **Extend `DepthAndUncertainty` (`common.h`)** — add `float intensity =
   std::nan("")` and `float beam_angle = std::nan("")`. This is the transport vehicle
   through the median queue, keeping each beam's depth/intensity/angle bound together.
   `DepthAndUncertainty` is `#pragma pack(push,1)` (common.h:75-86); adding two more
   `float`s extends the packed struct from 8 to 16 bytes. **Verified no caller depends
   on `sizeof(DepthAndUncertainty)`** — the only `sizeof` hits (bag_to_geotiff.cpp:702/
   706) are `2 * sizeof(float)` raster-band strides, unrelated to this struct. Defaults
   keep all existing constructions valid.

3. **Add per-beam container + `recordBeam()` to `Hypothesis` (`hypothesis.h`,
   `hypothesis.cpp`)**:
   - A small POD `BeamIntensitySample { float raw_intensity; float grazing_angle; }`.
   - `std::vector<BeamIntensitySample> intensity_samples;` on `Hypothesis` — bounded by
     hypothesis membership (D3); a few bytes per contributing beam.
   - `void recordBeam(float raw_intensity, float grazing_angle);` — **skips NaN
     intensity** (NaN angle is allowed: a beam with valid intensity but unknown angle
     is still data; the output correction treats NaN angle as "no angle correction
     available for this beam" — uncorrected, consistent with the Phase B no-op). Only
     non-NaN-intensity beams are appended.
   - **No raw pre-averaging in the hypothesis** — only the per-beam set is stored. This
     is the D3-faithful change vs. the rejected O(1)-Welford-of-raw draft.

4. **Thread `{intensity, beam_angle}` through `Node`** (`node.h`, `node.cpp`):
   - `insert()` (node.cpp:81): pass `sounding.intensity`, `sounding.beam_angle` into
     `queueEstimate()`.
   - `queueEstimate(float depth, float variance, float intensity, float beam_angle,
     const Parameters&)` — store `DepthAndUncertainty{depth, variance, intensity,
     beam_angle}` in `queue_`. When a median is extracted (node.cpp:137) and pushed to
     `update()`, pass `mi->intensity`, `mi->beam_angle`.
   - `update(float depth, float variance, float intensity, float beam_angle, const
     Parameters&)` — record the beam on the **correct** hypothesis in all three paths:
     - **(1) no prior hypotheses** (`!best`, node.cpp:45-51 — review must-fix #2):
       `addHypothesis(depth, variance)` seeds the first hypothesis; then
       `depth_hypotheses_.back()->recordBeam(intensity, beam_angle)`. **Without this,
       every node silently drops its first beam.**
     - **(2) accepted by best** (`best->update()` returns `true`):
       `best->recordBeam(intensity, beam_angle)`.
     - **(3) intervention** (`best->update()` returns `false`): the beam was rejected
       from `best`; `best->resetMonitor(); addHypothesis(depth, variance);` then
       `depth_hypotheses_.back()->recordBeam(intensity, beam_angle)` — recorded on the
       NEWLY-SEEDED hypothesis, NOT on `best` (exclusion-on-intervention invariant).
   - `queueFlush()` (node.cpp:235-266 — review suggestion #6): the flush copies the
     queue to `std::vector<DepthAndUncertainty> q` (line 244) and calls
     `update(q[ex_pt].depth, q[ex_pt].uncertainty, ...)` (line 261). Update **this call
     site** to also pass `q[ex_pt].intensity, q[ex_pt].beam_angle`.

5. **Define `NodeRecord` (`node.h`)** — a plain struct emitted by the new
   `extractNodeRecord()`:
   ```cpp
   struct NodeRecord {
     float depth         = std::nan("");
     float depth_var     = std::nan("");
     float intensity     = std::nan("");  // corrected mean; NaN if no intensity beams
     float intensity_var = std::nan("");  // ESTIMATE variance; NaN if < 2 samples
     uint32_t n_samples  = 0;             // intensity-bearing beams on the hypothesis
   };
   ```
   `intensity_var` is the **variance of the estimate** (ADR-0007 D4, review suggestion
   #5): `sample_variance / n`, i.e. `M2 / (n * (n - 1))`, which **shrinks with n** and
   mirrors the bathy store's depth uncertainty — NOT the raw sample variance
   `M2 / (n - 1)`. NaN when `n < 2`.

6. **Add `extractNodeRecord()` to `Node`** (`node.h`, `node.cpp`) returning
   `NodeRecord`. It **honors `nominated_hypothesis_` exactly like
   `extractDepthAndUncertainty()`** (node.cpp:169-173 — review must-fix #3): if
   `nominated_hypothesis_` is set, build the record from it; else fall through to
   `chooseHypothesis()`. Both depth/uncertainty fields are computed the same way
   `extractDepthAndUncertainty()` does (so the two methods never disagree on the same
   node state). Intensity fields are computed from the chosen hypothesis's per-beam set:
   - **Phase B correction = documented no-op** (review issue-action + D3): for each
     per-beam `{raw, angle}`, apply the GeoCoder correction — currently the identity
     (uncorrected), with the comment below. Combine the corrected (== raw, for now)
     values into mean + estimate-variance.
   - `extractDepthAndUncertainty()` is **kept unchanged** (backward-compatible; used by
     grid.cpp:150 / geo_grid.cpp:122 and the bathy output path).
   ```cpp
   // TODO(#54-B / cube_bathymetry#15): apply the GeoCoder incidence/Lambert
   // correction per beam HERE, using the settled hypothesis depth + local seabed
   // slope (ADR-0007 D3). Slope is gated on cube_bathymetry#15 (disabled in
   // Node::insert today). Until #15 lands this is the identity (flat-geometry)
   // correction: the stored intensity is emitted UNCORRECTED. The per-beam
   // {raw_intensity, grazing_angle} set is retained on the hypothesis so the value
   // is fully re-derivable when #15 provides slope -- no information is lost.
   ```

### Phase B — GeoCoder correction (deferred-settled, NOT implemented here)

The per-beam output-stage incidence/Lambert correction is gated on
cube_bathymetry#15 (slope) and, per ADR-0007 D6/D9, ultimately moves to the shared
`marine_backscatter` GeoCoder chain. When #15 merges, the correction is wired into the
per-beam loop in `extractNodeRecord()` without touching hypothesis internals — the
`{raw, angle}` set is already there.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/sounding.h` | Add `float beam_angle = std::nan("")`; populate from `detections.rx_angles[i]` in the detections constructor (NaN-guarded) |
| `cube_bathymetry/include/cube_bathymetry/common.h` | Add `float intensity` + `float beam_angle` (NaN-default) to packed `DepthAndUncertainty`; constructor params |
| `cube_bathymetry/include/cube_bathymetry/hypothesis.h` | Add `BeamIntensitySample` POD + `std::vector<BeamIntensitySample> intensity_samples`; declare `recordBeam(float, float)` |
| `cube_bathymetry/src/hypothesis.cpp` | Implement `recordBeam()`: NaN-intensity skip + append `{raw, angle}` |
| `cube_bathymetry/include/cube_bathymetry/node.h` | Add `intensity, beam_angle` params to `update()` + `queueEstimate()`; declare `NodeRecord` + `extractNodeRecord()` |
| `cube_bathymetry/src/node.cpp` | Thread `{intensity, beam_angle}` through `insert()`→`queueEstimate()`→`update()` (all 3 paths + `queueFlush()` call site ~line 261)→`recordBeam()`; implement `extractNodeRecord()` (honors `nominated_hypothesis_`; Phase B no-op; estimate-variance) |
| `cube_bathymetry/test/test_hypothesis.cpp` | `recordBeam` append correctness; NaN-intensity skip |
| `cube_bathymetry/test/test_node.cpp` | aggregate mean + estimate-variance over an associated set; NaN-beam skip; **exclusion-on-intervention** (original hypothesis's set unchanged); **first-beam-initialization** intensity recorded; enriched `NodeRecord` emission incl. `nominated_hypothesis_` path |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Phase B no-op is explicit, comment cites #15 + ADR-0007 D3 — incompleteness is visible, not silent. Per-beam set retained so nothing is irreversibly lost. |
| Capture decisions | Now matches ADR-0007 D3 exactly (per-beam `{raw, angle}`); the D3 deviation in the first draft is corrected, not ratified by a side-comment. No ADR amendment needed. |
| A change includes its consequences | `DepthAndUncertainty`/`Sounding` extensions are NaN-default backward-compatible; `extractDepthAndUncertainty()` kept unchanged; `update()`/`queueEstimate()` signature changes audited (all internal to `Node`; `queueFlush()` call site named); `sizeof` checked. |
| Only what's needed | No store wiring, no #15 slope, no GeoCoder chain — strictly ADR-0007 Phase A + documented Phase B no-op. |
| Test what breaks | Exclusion-on-intervention, first-beam-init, NaN-skip, and estimate-variance correctness are all in scope. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0007 (MBES Backscatter Store) | Yes — direct | D2: per-beam set on Hypothesis, combined at output. D3: per-beam `{raw, angle}` retained; correction deferred to node-output, no-op cites #15. D4: `intensity_var` = estimate variance. D5: `NodeRecord` emitted. D9: in cube_bathymetry. |
| ADR-0002 (Bathymetric store) | Indirect | `extractDepthAndUncertainty()` unchanged; depth/uncertainty path unaffected. |
| ADR-0008 (ROS 2 conventions) | OK | No new packages; header + .cpp edits. |
| ADR-0001 (Adopt ADRs) | OK | Design decisions in ADR-0007; no silent deviation. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `Sounding` adds `beam_angle` | error-model constructor populates it; grid passes whole `Sounding` to `Node::insert` (no signature change) | Yes — step 1 |
| `DepthAndUncertainty` adds `intensity`+`beam_angle` | `queue_` entries carry them (NaN default); `sizeof` checked | Yes — step 2 |
| `queueEstimate()` signature | `insert()` caller | Yes — step 4 |
| `update()` signature | `queueEstimate()` median-extract call + `queueFlush()` call site (node.cpp ~261) | Yes — step 4 |
| `Hypothesis` gains per-beam vector | `recordBeam()` called on the correct hypothesis in all 3 `update()` paths | Yes — step 4 |
| `extractNodeRecord()` added | Store wiring (producer → `marine_mbes_backscatter_store`) | No — separate follow-on per settled scope |
| Phase B no-op | Wired when #15 merges (per-beam loop only) | No — follow-on |

## Open Questions

- [ ] None — scope is settled; grazing-angle availability verified (beam angle present
      at the Sounding constructor); D3 resolution decided (store per-beam pairs).

## Estimated Scope

Single PR. Phase A (per-beam sufficient stats + enriched record with estimate-variance)
is the complete implementation; Phase B is an annotated no-op. Both land in one branch.
