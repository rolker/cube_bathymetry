# Plan: CUBE co-estimation of backscatter: intensity-in-Hypothesis (Welford) + deferred-settled node-output correction (ADR-0007)

## Issue

https://github.com/rolker/cube_bathymetry/issues/54

## Context

ADR-0007 (ACCEPTED) specifies that backscatter intensity is co-estimated with depth
inside CUBE: each hypothesis carries a Welford running mean+variance over the raw
per-beam intensity of the beams it accepts (D2), and the best hypothesis emits an
enriched node record `{depth, depth_var, intensity, intensity_var, n_samples}` (D5).
The node-output GeoCoder incidence/Lambert correction (D3) is deferred-settled pending
cube_bathymetry#15 (slope correction, currently disabled at `node.cpp:118-124`).

`sounding.h` already carries `float intensity = std::nan("")` (merged in #52).
The intensity field is NaN when the source omits intensities and must never corrupt
the accumulator.

### Data-flow path for intensity

```
insert(distance, sounding, params)          # sounding.intensity available here
  → queueEstimate(depth+offset, variance, params)   # intensity currently dropped
      → update(depth, variance, params)     # picks winning hypothesis
          → Hypothesis::update()            # accumulates depth stats
```

Intensity must ride through this chain alongside depth. The simplest correct
approach: add `intensity` (NaN-default) to `DepthAndUncertainty`, propagate it
through `queueEstimate()` → `update()` → `Hypothesis::update()`. The Welford
accumulator lives in `Hypothesis`; NaN beams are skipped by the accumulator.

**Key invariant**: if `Hypothesis::update()` returns `false` (W&H monitor triggers
an intervention / new-hypothesis), the beam's depth was rejected; its intensity
MUST also be excluded from the winning hypothesis's accumulator. This is enforced
naturally because the Welford `updateIntensity()` is called only inside the `true`
branch of `Hypothesis::update()`.

## Approach

### Phase A — Welford accumulator + enriched node record (unblocked, this PR)

1. **Extend `DepthAndUncertainty` (`common.h`)** — add `float intensity =
   std::nan("")`. This is the transport vehicle through the median queue. All
   existing callsites default-initialize, so NaN propagation is automatic.

2. **Add Welford fields to `Hypothesis` (`hypothesis.h`)** — add:
   - `float intensity_mean = std::nan("")` — Welford running mean (NaN until first
     non-NaN beam)
   - `float intensity_M2 = 0.0f` — Welford running sum of squared differences
   - `uint32_t intensity_count = 0` — number of non-NaN intensity samples

   Add a helper method: `void updateIntensity(float raw_intensity)` that skips NaN,
   applies the Welford online update, and increments `intensity_count`. No grazing
   angle is stored in the hypothesis — ADR-0007 D2 says "sufficient stats"; the
   grazing angle computed at insert time is NOT per-hypothesis-acceptance (it comes
   from the beam geometry, not the filter decision), so we do not carry it in the
   hypothesis. The GeoCoder correction at output (Part B) will use seabed slope from
   #15, not per-beam grazing angle stored in the hypothesis.

3. **Thread intensity through `Node::insert()` → `queueEstimate()` → `update()` →
   `Hypothesis::update()`** (`node.h`, `node.cpp`):
   - `insert()`: pass `sounding.intensity` into `queueEstimate()`.
   - `queueEstimate(float depth, float variance, const Parameters&)` → add `float
     intensity = std::nan("")` param. Store `DepthAndUncertainty{depth, variance,
     intensity}` in `queue_`. When a median is extracted and pushed to `update()`,
     pass its intensity field.
   - `update(float depth, float variance, const Parameters&)` → add `float intensity
     = std::nan("")` param. After `best->update(depth, variance, parameters)` returns
     `true`, call `best->updateIntensity(intensity)`. After `addHypothesis()` is
     called (new hypothesis on intervention), call `new_hyp->updateIntensity(intensity)`
     — wait, on intervention the beam was REJECTED from the winning hypothesis and
     used to seed a NEW hypothesis. The new hypothesis IS the one that accepted this
     beam, so it should accumulate the intensity.
     
     **Intervention case more carefully**: `Node::update()` when
     `best->update()` returns `false`:
     ```
     best->resetMonitor();
     addHypothesis(depth, variance);   // new hypothesis seeded with this depth
     ```
     The new hypothesis's constructor sets `number_of_samples = 1`. That beam DID
     associate with the new hypothesis, so `updateIntensity(intensity)` should be
     called on the new hypothesis (the one just added). Retrieve it as
     `depth_hypotheses_.back()` after `addHypothesis()`.

   - `queueFlush()` similarly passes intensity from the copied queue entries.

4. **Define `NodeRecord` (`node.h` or a new `node_record.h`)** — a plain struct
   emitted by `extractDepthAndUncertainty()`:
   ```cpp
   struct NodeRecord {
     float depth       = std::nan("");
     float depth_var   = std::nan("");
     float intensity   = std::nan("");   // NaN if no intensity beams
     float intensity_var = std::nan(""); // NaN if fewer than 2 samples
     uint32_t n_samples = 0;
   };
   ```
   `intensity_var = intensity_M2 / (intensity_count - 1)` (sample variance; NaN
   when `intensity_count < 2`). Populated from `chooseHypothesis()`.

5. **Add `extractNodeRecord()` to `Node`** (`node.h`, `node.cpp`) returning
   `NodeRecord`. Keep `extractDepthAndUncertainty()` unchanged (backward-compatible;
   used throughout grid/map-sheet callers). The new method is the enriched output seam.

6. **Phase B stub in `extractNodeRecord()`** — emit uncorrected intensity (identity
   correction = no-op flat-geometry Lambert). Comment:
   ```cpp
   // TODO(#54-B/#15): Apply GeoCoder incidence/Lambert correction here.
   // Needs local seabed slope from cube_bathymetry#15 (ADR-0007 D3).
   // Until #15 lands, intensity is emitted uncorrected (flat-geometry identity).
   ```
   This is the explicit documented seam required by the settled scope.

### Phase B — GeoCoder correction (deferred-settled, NOT implemented here)

The output-stage incidence/Lambert correction is gated on cube_bathymetry#15 (slope).
See `TODO` comment above. When #15 merges, wire the correction in `extractNodeRecord()`
without touching the hypothesis internals.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/common.h` | Add `float intensity = std::nan("")` to `DepthAndUncertainty` |
| `cube_bathymetry/include/cube_bathymetry/hypothesis.h` | Add Welford fields (`intensity_mean`, `intensity_M2`, `intensity_count`); declare `updateIntensity(float)` |
| `cube_bathymetry/src/hypothesis.cpp` | Implement `updateIntensity()`: NaN-skip + Welford online update |
| `cube_bathymetry/include/cube_bathymetry/node.h` | Add `float intensity` param to `update()`, `queueEstimate()`; declare `extractNodeRecord()`; declare `NodeRecord` struct (or in a new `node_record.h`) |
| `cube_bathymetry/src/node.cpp` | Thread intensity through `insert()` → `queueEstimate()` → `update()` → `Hypothesis::updateIntensity()`; implement `extractNodeRecord()` with Phase B stub |
| `cube_bathymetry/test/test_hypothesis.cpp` | Add: Welford correctness test; NaN-skip test |
| `cube_bathymetry/test/test_node.cpp` | Add: exclusion-on-intervention test; NaN-beam propagation test; enriched-record emission test |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Phase B stub is explicit with a comment citing #15 and ADR-0007 D3 — incompleteness is visible, not silent. |
| Capture decisions | ADR-0007 is the design authority; the plan maps directly to D2/D3/D5. No new ADR needed. |
| A change includes its consequences | `DepthAndUncertainty` extension is backward-compatible (NaN default). `queueEstimate`/`update` signature changes require caller audit (covered in approach step 3). `extractDepthAndUncertainty()` is kept unchanged. |
| Only what's needed | No processed-build, no store wiring, no #15 slope — strictly ADR-0007 Phase A + stub. |
| Test what breaks | Exclusion-on-intervention and NaN-skip tests are mandatory and in scope. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0007 (MBES Backscatter Store) | Yes — direct | D2: Welford in Hypothesis. D3: stub at extractNodeRecord(), cites #15. D5: NodeRecord emitted. D9: implementation in cube_bathymetry. |
| ADR-0002 (Bathymetric store) | Indirect | `extractDepthAndUncertainty()` is kept unchanged; depth/uncertainty output path is unaffected. |
| ADR-0008 (ROS 2 conventions) | OK | No new packages; header-only changes + .cpp edits. |
| ADR-0001 (Adopt ADRs) | OK | Design decisions documented here and in ADR-0007; stub comment cites both. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `DepthAndUncertainty` adds `intensity` field | `queue_` entries in `Node` automatically carry it (NaN default) | Yes — automatic |
| `queueEstimate()` signature adds `intensity` | `insert()` caller in `node.cpp` | Yes — step 3 |
| `update()` signature adds `intensity` | `queueEstimate()` → `update()` call; `queueFlush()` passes entries | Yes — step 3 |
| `Hypothesis::update()` — new `updateIntensity()` call | Called inside `Node::update()` on both accept and intervention paths | Yes — step 3 |
| `extractNodeRecord()` added | Store wiring (producer→`marine_mbes_backscatter_store`) | No — separate follow-on issue per settled scope |
| Phase B stub | Will be wired when #15 merges | No — follow-on |

## Open Questions

- [ ] No open questions — scope is settled and approach is unambiguous.

## Estimated Scope

Single PR. Phase A (Welford + enriched record) is the complete implementation.
Phase B stub is an annotated no-op comment. Both land in one branch.
