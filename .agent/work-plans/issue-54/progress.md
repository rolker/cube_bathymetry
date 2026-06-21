---
issue: 54
---

# Issue #54 — CUBE co-estimation of backscatter: intensity-in-Hypothesis (Welford) + deferred-settled node-output correction (ADR-0007)

## Issue Review
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Issue**: #54
**Comment**: https://github.com/rolker/cube_bathymetry/issues/54#issuecomment-4761028304
**Scope verdict**: well-scoped

### Actions
- [ ] Implement Phase A (Welford accumulation + enriched node record) and Phase B (output-stage GeoCoder correction stub) as separate, named phases in the PR description; Phase A is unblocked, Phase B stubs the correction pending cube_bathymetry#15.
- [ ] Do NOT fold cube_bathymetry#15 into this issue — keep them independent.
- [ ] Stub the output-stage incidence correction explicitly (not silent omission): emit uncorrected intensity with a comment citing #15 and ADR-0007 D3.
- [ ] Coordinate `MbesCell` shape with `marine_mbes_backscatter_store` write-API seam before implementing (confirm `{ depth, depth_var, intensity, intensity_var, n_samples }` matches).
- [ ] "Exclusion on intervention" test is mandatory and must not be deferred.
- [ ] Handle NaN intensity gracefully in the Welford accumulator (skip NaN beams; verify in tests).
- [ ] Audit all callers/constructors of `Hypothesis` after the struct grows per-beam state vector.

## Plan Authored
**Status**: complete
**When**: 2026-06-21 00:30 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-54/plan.md` at `6ae0394`
**Branch**: feature/issue-54 at `6ae0394`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-21 08:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Plan**: `.agent/work-plans/issue-54/plan.md` at `6ae0394`
**PR**: PR-less (no draft PR yet on feature/issue-54)
**Verdict**: changes-requested

### Findings
- [x] (must-fix) ADR-0007 D3 deviation unacknowledged: plan stores Welford mean/M2/count but NOT per-beam `{raw intensity, grazing angle}` pairs that D3 explicitly mandates as sufficient stats. The plan's rationale (lines 55-60) asserts grazing angle is "not per-hypothesis-acceptance" and says slope from #15 will substitute at output, but this directly contradicts D3 ("The hypothesis carries, per contributing beam, the sufficient statistics needed to correct later — `{raw intensity, grazing angle}`"). Either (a) conform to D3 and store per-beam pairs, or (b) amend ADR-0007 D3 to ratify the slope-only approach before implementing. A silent deviation from the accepted ADR is not acceptable — the justification must be captured in the ADR itself, not only in a plan comment. — `plan.md:55-60`, `plan.md:149`
- [x] (must-fix) Initialization path missing intensity call: `Node::update()` has three paths — (1) no prior hypotheses (`!best`, lines 45-51 of node.cpp), (2) accepted by best (`best->update()` returns `true`), (3) intervention (`best->update()` returns `false`). The plan describes paths (2) and (3) but omits path (1). When the first beam arrives and `best` is nullptr, `addHypothesis()` seeds a new hypothesis and that beam's intensity must be accumulated on it. The plan must explicitly include `depth_hypotheses_.back()->updateIntensity(intensity)` in the no-prior-hypothesis branch. — `plan.md:63-87`
- [x] (must-fix) `extractNodeRecord()` omits `nominated_hypothesis_` path: `extractDepthAndUncertainty()` (node.cpp:168-186) checks `nominated_hypothesis_` before falling through to `chooseHypothesis()`. The plan's step 5 defines `extractNodeRecord()` based only on `chooseHypothesis()` without mentioning the nominated path. If the plan's implementation diverges from `extractDepthAndUncertainty()`'s logic here, the two output methods will give different answers for the same node state. The plan should explicitly specify how `extractNodeRecord()` handles `nominated_hypothesis_`. — `plan.md:104-115`
- [ ] (suggestion) `#pragma pack(push, 1)` on `DepthAndUncertainty` — adding `float intensity` extends a packed struct. The current struct is two `float`s (8 bytes); adding a third is trivially layout-safe, but the plan should note this is extending a packed struct (common.h:75-86) and verify no caller depends on `sizeof(DepthAndUncertainty) == 8`. — `plan.md:127`
- [ ] (suggestion) Welford variance: the plan defines `intensity_var = intensity_M2 / (intensity_count - 1)` (sample variance). ADR-0007 D4 says `intensity_uncertainty` is "variance of the estimate" (i.e. `intensity_M2 / (intensity_count * (intensity_count - 1))` = sample variance / n). The plan should confirm which formula it emits in `NodeRecord.intensity_var` and whether it matches D4's "estimate variance" semantics. A discrepancy here will mismatch the store's quality band interpretation. — `plan.md:96-98`
- [ ] (suggestion) `queueFlush()` intensity transport: the plan mentions "queueFlush() similarly passes intensity from the copied queue entries" (step 3) but `queueFlush()` copies to a `std::vector<DepthAndUncertainty>` (node.cpp:244) and then calls `update(q[ex_pt].depth, q[ex_pt].uncertainty, ...)` (line 261). The intensity field will be in `q[ex_pt].intensity` but the call must be updated to pass it. The plan should list `queueFlush()` explicitly in the Files to Change table (currently only node.cpp is listed, which covers it implicitly, but the specific call site should be named). — `plan.md:88`, `plan.md:131`
- [ ] (suggestion) Test for init-path intensity: the mandatory test for "exclusion on intervention" covers the `false` branch; a complementary test for the initialization path (first beam → new hypothesis created → intensity accumulated) should be added to `test_node.cpp` to ensure path (1) above is exercised. — `plan.md:133`

## Implementation
**Status**: complete
**When**: 2026-06-21 +00:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan revised at**: `a151e67` (D3-faithful per-beam design)
**Implementation at**: `bdfa467`
**Branch**: feature/issue-54

### Grazing-angle availability (Step 0.5) — CLEARED
The per-beam beam/grazing angle IS available where intensity is captured.
`Sounding(detections, i, depth)` (sounding.h) has direct access to
`detections.rx_angles[i]`/`tx_angles[i]`, and the error model already derives
the per-beam beam angle from them (`ErrorModel::beam_angle()` = `-rx_angles[i] +
static_roll`). #52 retained `intensity` on the `Sounding` but not the angle, so a
#52-style carry was added: a `beam_angle` field on `Sounding`, populated in the
same constructor from `rx_angles[i]` (NaN-guarded). Semantic note recorded in the
plan: what is stored now is the per-beam **beam/incidence angle relative to
nadir**, NOT a true seafloor grazing angle (that needs local slope, deferred to
#15). This is exactly what ADR-0007 D3 anticipates ("grazing angle ... plus the
per-beam geometry already present"); the true grazing angle is reconstructed at
output by combining beam angle + settled depth + slope (#15) in the deferred
GeoCoder chain. No information faked or dropped. Not blocked.

### What changed (per the revised plan, all 7 review findings addressed)
- **sounding.h**: `float beam_angle` carried from `detections.rx_angles[i]`.
- **common.h**: `DepthAndUncertainty` carries `{intensity, beam_angle}` so each
  beam's depth/intensity/angle stay bound through the median pre-queue.
- **hypothesis.h/.cpp**: `BeamIntensitySample{raw_intensity, grazing_angle}` POD +
  `std::vector<BeamIntensitySample> intensity_samples` (bounded by membership, D3)
  + `recordBeam()` (NaN-intensity skip; NaN-angle retained). No raw pre-averaging
  in the hypothesis (must-fix #1).
- **node.cpp `Node::update()`**: records the beam on the correct hypothesis in ALL
  three paths — first-beam `!best` init (must-fix #2), accept, and intervention
  (the NEWLY-SEEDED hypothesis, not the rejected `best` — exclusion-on-intervention).
  Threaded through `insert()`→`queueEstimate()`→`update()` + the `queueFlush()` call
  site (~line 261, suggestion #6).
- **node.h/.cpp `NodeRecord` + `extractNodeRecord()`**: honors `nominated_hypothesis_`
  exactly like `extractDepthAndUncertainty()` (must-fix #3); `intensity_var` =
  estimate variance `sample_var/n` (D4 / suggestion #5), shrinks with n, NaN for n<2;
  Phase B correction = documented no-op citing #15 + ADR-0007 D3 (emit uncorrected,
  per-beam raw set retained → re-derivable). `extractDepthAndUncertainty()` unchanged
  (bathy backward compat).
- **bag_to_geotiff.cpp** (suggestion #4 — went further than plan): the RasterIO pixel
  stride was a hardcoded `2*sizeof(float)` walking consecutive `DepthAndUncertainty`
  elements. The struct grew 8→16 bytes, so that stride would have read the wrong
  field. Fixed to `sizeof(cube::DepthAndUncertainty)`. (The plan's "no caller depends
  on sizeof" was imprecise — this call did, as an array stride; corrected here.)

### Tests (gtest, all green)
Added 9 tests:
- test_hypothesis (15 total, +3): `RecordBeamAppendsRawAndAngle`,
  `RecordBeamSkipsNanIntensity`, `RecordBeamRetainsNanAngle`.
- test_node (18 total, +6): `FirstBeamInitializationRecordsIntensity`,
  `NodeRecordMeanAndEstimateVariance` (mean + estimate-variance correctness),
  `NodeRecordSkipsNanIntensityBeam`, `ExclusionOnInterventionLeavesOriginalIntensityUnchanged`,
  `NodeRecordHonorsNominatedHypothesis` (via a test-only friend
  `NodeNominationTestAccess`, since `nominated_hypothesis_` has no production
  setter), `NodeRecordNoData`.

### Build / test result
- Build: clean (`./sensors_ws/build.sh cube_bathymetry`), only pre-existing GDAL
  `warn_unused_result` warnings (unrelated).
- Tests: `test_node.gtest.xml` 18 tests / 0 failures; `test_hypothesis.gtest.xml`
  15 tests / 0 failures. All cube_bathymetry gtest, cpplint, cppcheck, flake8 suites
  pass. The only 3 colcon-test failures are **uncrustify 0.78.1 local version-drift**
  (whole-namespace de-indent on files incl. lines never touched — the documented
  CI-divergent noise, ignored per workspace instructions).

### Left / follow-on (out of scope, per settled scope)
- `extractNodeRecord()` store wiring (producer → `marine_mbes_backscatter_store`) is
  a separate phase (ADR-0007 D9 phase 3).
- Phase B GeoCoder correction wires in at the per-beam loop in `extractNodeRecord()`
  when cube_bathymetry#15 (slope) lands — hypothesis internals untouched.
- Did NOT push (per handoff contract). PR not opened.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-21 10:30 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-54 at `86e0524`
**Mode**: pre-push
**Depth**: Deep (reason: correctness-critical CUBE hypothesis core + struct-growth blast radius + cross-layer ADR-0007)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 1 | **Ship**: recommended — no must-fix; struct-growth blast radius fully contained, all 3 update() paths + median binding correct, faithful to ADR-0007 D2/D3/D4/D5

### Findings
- [ ] (suggestion) Estimate-variance uses sum-of-squares form `(sum_sq - sum*sum/n)/(n-1)`; for O(-30 dB) means over many beams, float rounding can yield a slightly NEGATIVE sample_variance, emitted as negative `intensity_var` (no sqrt guard). Clamp to >=0 or use Welford/two-pass — `cube_bathymetry/src/node.cpp:264`
- [ ] (suggestion) `Hypothesis::intensity_samples` is never cleared; grows 8 bytes/beam per node for the survey lifetime (same growth class as existing `number_of_samples`, but stores bytes not a counter). Document the worst-case memory budget or reduce to sufficient stats once #15's correction model settles — `cube_bathymetry/include/cube_bathymetry/hypothesis.h:165`
- [ ] (suggestion) No test exercises intensity/angle binding THROUGH the median pre-queue (`insert`→`queueEstimate`→`queueFlush`); all node tests call `update()` directly. The D3 "no depth/intensity mismatch on median sort" invariant is correct by inspection but unproven end-to-end. Add one `insert()`-driven test with a reordering median — `cube_bathymetry/test/test_node.cpp`
- [x] (suggestion) `beam_angle = rx_angles[i]` sign/convention ("positive to starboard") should be cross-checked against the marine_acoustic_msgs producer before #15 consumes it for grazing-angle reconstruction; a sign error would bias the deferred radiometric correction (no current consumer affected) — `cube_bathymetry/include/cube_bathymetry/sounding.h:81`

## Implementation (Pre-Push Review Fold)
**Status**: complete
**When**: 2026-06-21 14:00 +00:00
**By**: Claude Code Agent (Claude Sonnet 4.6)

**Commit**: `309f42f` on `feature/issue-54`

### Folds applied

**Fold #1 — negative-variance clamp** (`node.cpp:264`):
- Added `std::max(0.0, ...)` around the sum-of-squares sample_variance
  before dividing by n. Float rounding at O(-30 dB) means over many
  beams can produce a tiny negative intermediate; the clamp enforces the
  `intensity_var >= 0` contract without changing the formula for
  well-behaved inputs.
- Extended tests: added `IntensityVarNonNegativeAfterClamp` — 10 identical
  intensity beams via `update()`, asserts `intensity_var >= 0.0` and not NaN.

**Fold #3 — insert()-driven median-queue test** (`test_node.cpp`):
- Added `InsertDrivenMedianQueuePreservesIntensityBinding`: 4 beams via
  `Node::insert()` with `median_length=3` (fires mid-stream), depths
  arriving out of depth-sort order ({10, 8, 12, 10} m), each with a
  distinct intensity (-30, -20, -40, -30 dB). After `queueFlush()`,
  asserts `n_samples > 0` and `intensity` mean ≈ -30.0 (±1 dB), proving
  {depth, intensity, beam_angle} stay bound through `queueEstimate`/
  `queueFlush` reordering — the D3 invariant, end-to-end.

**Note #2 — memory budget comment** (`hypothesis.h:intensity_samples`):
- Added inline doc: ~8 bytes/beam/node for survey lifetime; same growth
  class as `number_of_samples`; reducible to sufficient statistics once
  cube_bathymetry#15 correction model settles.

**Note #4 — beam_angle sign convention warning** (`sounding.h:beam_angle`):
- Extended existing comment: flagged that the `rx_angles[i]` sign
  convention MUST be cross-checked against the `marine_acoustic_msgs`
  producer before cube_bathymetry#15 consumes it for grazing-angle
  reconstruction; a sign error would bias the deferred radiometric
  correction.

### Build / test result
- Build: clean (`sensors_ws/build.sh cube_bathymetry`); only pre-existing
  GDAL `warn_unused_result` warnings (unrelated to this change).
- `test_node.gtest.xml`: **20 tests / 0 failures** (+2 new tests).
- `test_hypothesis.gtest.xml`: **15 tests / 0 failures** (unchanged).
- Uncrustify: 4 failures — known local 0.78.1 version-drift noise
  (documented, ignored per workspace instructions; CI uses a different version).
- Did NOT push (per handoff contract).
