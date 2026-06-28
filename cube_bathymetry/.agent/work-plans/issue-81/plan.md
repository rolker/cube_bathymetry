# Plan: Apply deferred M3 backscatter angle-correction in shared CUBE estimator

## Issue

https://github.com/rolker/cube_bathymetry/issues/81

## Context

`Node::extractNodeRecord()` (node.cpp:315-318) contains a Phase B no-op where
`corrected == raw` for every beam. The per-beam `{raw_intensity, grazing_angle}`
pairs are already stored on each hypothesis (`Hypothesis::intensity_samples`).
The gate — verifying `rx_angles` sign/zero convention against the M3 producer —
has been confirmed host-side: `kongsberg_em_bridge` negates the Kongsberg raw
pointing angle (`+PORT → -PORT`) so `rx_angles` is `+STARBOARD`, nadir=0,
radians; intensity is `reflectivity_db` in dB. Gate: **PASS**.

The `BeamIntensitySample::grazing_angle` field (hypothesis.h:49) is misnamed:
it stores the receive steering / incidence angle from nadir (same semantics as
`sounding.beam_angle`), NOT a true grazing angle (which would be 90° − θ). The
field doc already says "receive/steering (beam/incidence) angle" so the comment
is correct but the name is wrong. Plan renames it to `beam_angle` for consistency.

ADR-0007 has no document file in `docs/decisions/` yet; this PR adds the Phase B
transition note and files a follow-up task for the full ADR-0007 doc.

## Lambert Formula Derivation

`raw_intensity` is M3 `reflectivity_db` (dB), already TVG-compensated. The
remaining angular dependence is the receive-beam Lambert cosine:

```
BS(θ) = BS(0) + 10·log10(cos θ)    [one-way, TVG already handled]
```

where θ = incidence from nadir = `std::abs(sample.beam_angle)`.

To normalize to nadir (remove angular dependence):

```
corrected_dB = raw_dB − 10·log10(cos θ)
```

**Derivation rationale:**
- One-way (10·, not 20·): the transmit spreading loss is already in TVG; only
  the receive-beam incidence factor is uncompensated here.
- `cos θ` not `cos²θ`: cos²θ applies when both transmit and receive solid angles
  depend on θ; here only the receive steering angle is being corrected.
- At nadir (θ=0): `cos(0)=1`, `log10(1)=0` → correction=0 (identity). ✓
- At θ>0: `cos θ<1`, `log10(cos θ)<0` → correction is positive in dB (boosts
  off-nadir returns toward their nadir-equivalent). Physically correct for
  Lambert scattering where off-nadir returns are weaker.
- `cos θ → 0` as θ→90° guard: clamp `θ` to a maximum of `MAX_INCIDENCE_RAD =
  80° = 1.3963 rad` before computing. Beams beyond this are emitted uncorrected
  (identity) — the correction diverges and the Lambert model fails at near-grazing.
- NaN `beam_angle` → emit uncorrected (identity). Consistent with how `recordBeam`
  already retains NaN-angle beams as "no angle correction available".

**This formula is the highest-risk decision in this issue.** The review-plan step
must scrutinize it. Prior art: the sibling slope-correction (#15) had 2 wrong
formulas killed across 3 rounds.

## Approach

1. **Rename `BeamIntensitySample::grazing_angle` → `beam_angle`** — update the
   struct (hypothesis.h:49), `recordBeam` signature/impl (hypothesis.h:118,
   hypothesis.cpp:118,122,127), all call sites (node.cpp:308 comment), and all
   test references (test_hypothesis.cpp:216,218,227,230,243). Corrects the
   misleading name and aligns with `sounding.beam_angle`.

2. **Replace the Phase B no-op in `node.cpp:315-318`** — replace
   `const double corrected = sample.raw_intensity;` with the flat-bottom Lambert
   correction using `std::abs(sample.beam_angle)`. Add the inline comment
   capturing the rx_angles producer cross-check (Kongsberg +PORT→negate→+STARBOARD;
   nadir=0; radians; dB). Update/remove the stale `TODO(#54-B / #15)` block at
   node.cpp:298-312 to reflect Phase B completion; retain the follow-up note
   for the full GeoCoder correction (#15).

3. **Update `test_node.cpp` for corrected intensity values** — existing tests that
   pass `beam_angle = 0.0f` to `recordBeam` (or use nadir beams) continue to
   satisfy `corrected == raw` (identity at θ=0). Tests that use non-zero
   `beam_angle` (e.g., `NodeRecordMeanAndEstimateVariance` uses 0.1 rad beams)
   must now expect Lambert-corrected values. Add:
   - `LambertCorrectionNonNadir`: a non-nadir beam (e.g. θ=0.5 rad) produces
     `corrected ≠ raw`, and specifically `corrected = raw − 10·log10(cos(0.5))`.
   - `LambertCorrectionNadirIdentity`: θ≈0 → `corrected == raw` within float tol.
   - `LambertCorrectionPortStarboardSymmetry`: equal `|beam_angle|` on each side
     (e.g., +0.3 and −0.3 rad) produces equal corrected intensity.
   - `LambertCorrectionNaNAngle`: NaN `beam_angle` → identity (uncorrected).
   - `LambertCorrectionNearGrazing`: θ ≥ MAX_INCIDENCE_RAD → identity (clamped).

4. **Check `test_store_import.cpp`** — `BackscatterCellsMatchGridRecords` uses
   `kIntensity = 42.5f` with `beam_angle = 0.1f`. After the correction, the
   surfaced value will be `42.5 − 10·log10(cos(0.1)) ≈ 42.5 + 0.022 ≈ 42.52`,
   no longer exactly `kIntensity`. Fix: either change `beam_angle` to 0.0f in
   `makeSoundingsWithIntensity` (so nadir beams keep the identity), or update the
   assertion to expect the corrected value. Using 0.0f is simpler and keeps the
   test self-consistent.

5. **Add ADR-0007 transition note** — create
   `docs/decisions/0007-mbes-backscatter-store-phase-b-transition.md` containing:
   a minimal transition note recording: Phase B ended in this PR, first-cut
   flat-bottom Lambert applied at `extractNodeRecord()`, what stays deferred (full
   GeoCoder incidence via #15 + #59). File a separate GitHub issue to author the
   full ADR-0007 document.

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/hypothesis.h` | Rename `grazing_angle` → `beam_angle` in struct and `recordBeam` decl |
| `src/hypothesis.cpp` | Rename `grazing_angle` param in `recordBeam` impl |
| `src/node.cpp` | Replace no-op with Lambert correction; add producer cross-check comment; update TODO block |
| `test/test_node.cpp` | Update non-nadir intensity assertions; add 5 Lambert tests |
| `test/test_hypothesis.cpp` | Update `grazing_angle` field refs → `beam_angle` |
| `test/test_store_import.cpp` | Fix `makeSoundingsWithIntensity` to use `beam_angle = 0.0f` (nadir) |
| `docs/decisions/0007-mbes-backscatter-store-phase-b-transition.md` | New: transition note |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Enforcement over documentation | Add 5 targeted tests that would catch the wrong formula, wrong sign, and missing NaN/grazing guards — enforcement via test, not just comments |
| Capture decisions | ADR-0007 transition note records Phase B end date, formula chosen, what's deferred |
| A change includes its consequences | `test_store_import.cpp` fix included in same PR; both live (#78) and offline (#80) paths automatically corrected via the shared `extractNodeRecord()` |
| Only what's needed | Full GeoCoder correction (#15, #59) explicitly deferred; no per-path duplication |
| Improve incrementally | Phase B → first-cut Lambert → full GeoCoder (#15) — keeps each step reviewable |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0007 D3 (incidence correction at node-output) | Yes — primary | Implemented here at `extractNodeRecord()` |
| ADR-0007 D2/D4 (per-beam mean + estimate variance) | Yes — already in place | No change; `corrected` replaces `raw_intensity` in the sum |
| ADR-0001 (adopt ADRs) | Yes | Transition note in `docs/decisions/` |
| ADR-0013 (progress.md vocabulary) | Yes | `## Plan Authored` entry committed with plan |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `BeamIntensitySample::grazing_angle` name | `hypothesis.cpp`, `test_hypothesis.cpp`, `node.cpp` comment | Yes — Step 1 |
| `corrected` value in `extractNodeRecord()` | `test_node.cpp` non-nadir assertions | Yes — Step 3 |
| `corrected` value flows to `test_store_import.cpp` via `nodeRecords()` | `makeSoundingsWithIntensity` beam_angle=0.1 assertion | Yes — Step 4 |
| Phase B ends | ADR-0007 transition note | Yes — Step 5 |
| Full GeoCoder correction | cube#15, cube#59 | No — explicit follow-up |

## Open Questions

- [ ] Formula constant: this plan uses 10·log10(cos θ) (one-way Lambert, TVG
  handled). If the M3 `reflectivity_db` already includes the Lambert receive
  factor in its TVG (i.e., TVG corrects for cos θ), the correction double-counts.
  Confirm from Kongsberg EM Datagram Formats that `reflectivity_db` is raw
  backscatter without Lambert normalization before implementing.

## Estimated Scope

Single PR. All changes touch one package (`cube_bathymetry`). ~120 lines changed,
~80 new test lines.
