---
issue: 126
---

# Issue #126 — Shared forward constant-gradient SSP ray tracer (consumed by uma#300 phase-2 inversion + marine_perception_tools#28 CUBE lab)

## Issue Review
**Status**: complete
**When**: 2026-08-18 01:53 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #126
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

**Well-scoped?** Yes. A single dependency-light module (header + impl), a
concrete function signature spelled out in prose (profile, transducer depth,
launch angle from `rx_angles[i]`, `ping_info.sound_speed` for the array-face
Snell correction, one-way travel time → across-track offset + depth + end
angle + effective mean sound speed), an enumerated edge-case list (mid-layer
exhaustion, turning ray, shallow profile extrapolation, zero/negative time,
single-point profile), and an explicit GTest plan (uniform-profile parity
with today's straight ray, two-layer analytic Snell/arc case, Snell invariant,
turning-ray case, degenerate inputs). Three non-goals are named
(`Sounding` wiring, file I/O, refraction-aware TPU), which keeps the PR
boundary clean. Completable as a single PR — no splitting needed.

**Right repo?** Yes, and independently corroborated, not just asserted.
`rolker/unh_marine_autonomy#300`'s Architecture section states
"cube_bathymetry owns the inversion engine — it reads beam-level observables
... and owns ray tracing, where the correction is applied. Same species as
existing corrections (slope #15, ARA #81, TL #87)" — i.e. this follows an
established in-repo pattern of physically-motivated per-beam corrections
living in `cube_bathymetry`, not a new precedent. `rolker/marine_perception_tools#28`
independently confirms the single-shared-implementation decision: "the
forward ray-trace core should exist ONCE, somewhere shared (cube_bathymetry
or a small uma library) so the uma#300 inversion work and this lab consume
the same implementation." The placement decision itself is out of scope to
relitigate here per the dispatch context, and the source confirms it's
consistent with prior art.

**Dependencies**:
- Upstream: `rolker/cube_bathymetry#121` (merged, PR#125) established the
  actual observable set this consumes — confirmed by reading its findings:
  per-beam (twtt, rx_angle) + per-ping applied `sound_speed`, **no along-track
  launch-angle observable** (`tx_angles`/`tx_delays` are identically zero —
  zero transmit tilt, not an empty-sector artifact). #126's scope already
  matches this: it takes only `rx_angles[i]` as the launch angle, correctly
  omitting a `tx_angle` parameter the hardware doesn't supply. No mismatch
  found.
- Downstream (not blocking, but shapes the interface contract): `uma#300`
  phase-2 inversion and `marine_perception_tools#28`'s CUBE lab both consume
  this module once built. Neither blocks #126 starting.
- Explicitly deferred, not a dependency: cast file I/O → `uma#299` (open,
  "Spike: inventory historical sound speed casts..." — still a spike, not
  yet delivering a format). #126 correctly treats the in-memory profile type
  as the contract boundary and pushes loader-format decisions to #299's
  future recommendation rather than guessing a format now.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Only what's needed | OK | Module + tests only; non-goals explicitly fence out `Sounding` wiring, file I/O, and refraction-aware TPU rather than letting scope creep toward them. |
| Test what breaks | OK | The GTest list targets the actual failure modes of a ray tracer (turning rays, mid-layer exhaustion, degenerate profiles) rather than coverage-chasing; the uniform-profile parity test is a strong regression guard tying the new path to the existing `sounding.h:43` straight-ray formula. |
| Capture decisions, not just implementations | OK | The single-shared-implementation decision and its consumers are recorded on the uma#300/mpt#28 issue threads (verified above), not just in chat; this issue's body cross-links both. |
| A change includes its consequences | Watch | The issue's own scope is self-contained (no `Sounding`/importer changes to update in this PR). But two *downstream* repos will implement against whatever public function signature and edge-case semantics this PR ships — flagging as a Recommendation below since neither consumer PR exists yet to enforce it, and a signature change later would ripple into two repos. |
| Workspace vs. project separation | OK | Domain code (acoustic ray tracing) correctly lives in the project repo, not the workspace. |
| CCOM_BASE numerical-code review context (this repo's `AGENTS.md`) | Watch | This repo's own review guidance singles out "signs and units" (depths vs elevations, sign conventions) as a recurring bug source. #121's findings already pin down `rx_angles` as +ve-to-starboard and depth as negative-down elsewhere in `sounding.h`; the new module's launch-angle and across-track-offset sign conventions should be stated in the header doc comment and cross-checked against those existing conventions during implementation/review — not left implicit. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Workspace ADR-0002 (Worktree isolation) | Yes | Already satisfied — layer worktree exists at `layers/worktrees/issue-cube_bathymetry-126` on `feature/issue-126`. |
| Workspace ADR-0013 (progress.md entry-type vocabulary) | Yes | This entry satisfies it; downstream phases (plan-task, implementation) should continue the typed-entry chain in this file. |
| cube_bathymetry ADR-0001/0002/0003 (tile eviction, dirty-tile footprint, staleness fingerprint) | No | These govern the store's tile lifecycle; a standalone ray-tracer module with no store/tile interaction doesn't implicate them. |
| cube_bathymetry ADR-0007 (backscatter store addendum) | No | Different correction family (angular response / TL), not ray geometry. |
| cube_bathymetry ADR-0008 (predicted-surface interpolation geometry) | No | Governs `predicted_depth_at_touchdown` interpolation on the grid side, not the acoustic forward model. |

No new ADR appears necessary for this issue: it doesn't introduce a new
architectural pattern beyond what #300's Architecture section already
recorded (ray tracing owned by cube_bathymetry). If the eventual PR settles
open questions with lasting rationale (e.g., why circular-arc-per-layer over
an alternative integration scheme, or the extrapolation-beyond-profile
policy), that's a candidate for a `cube_bathymetry` ADR at plan/implementation
time — not required to open this issue.

### Consequences

Using the consequences map: this issue doesn't modify `Sounding` construction,
the importer, docs, or existing tests, so most map rows don't apply. The one
live consequence:

- **Two downstream consumers will code against this module's public
  interface** (uma#300 phase-2, mpt#28). The issue doesn't ask for a header
  doc contract as a deliverable; recommend the implementation's public
  header carry doc comments precise enough for both consumers to integrate
  without re-deriving edge-case behavior (sign conventions, what "extrapolate
  last gradient" means at the boundary, turning-ray return semantics) —
  see Recommendations.

### Recommendations

- Document the launch-angle and across-track-offset sign conventions in the
  new header, explicitly cross-referenced to `sounding.h`'s existing
  `rx_angles` (+ve to starboard) and `depth` (negative-down) conventions, so
  the two downstream consumers integrate against a stated contract rather
  than inferring it from the implementation.
- Since `uma#299` (cast inventory) is still an open spike and file I/O is
  explicitly deferred, the in-memory profile type's field types/units (e.g.
  depth in meters positive-down or positive-up, sound speed in m/s) should be
  chosen and documented now as the de facto contract both consumers will
  build against, since a later loader will have to conform to whatever this
  PR picks.

### Actions
- [ ] Document sign/unit conventions for launch angle, across-track offset,
  and the in-memory profile type in the new header's doc comments,
  cross-referenced against `sounding.h`'s existing conventions.
- [ ] Confirm during implementation/review that the "extrapolate last
  gradient" and turning-ray edge-case behaviors are unit-tested with
  explicit expected values, not just exercised.

## Plan Authored
**Status**: complete
**When**: 2026-08-18 01:58 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-126/plan.md` at `a77823a`
**Branch**: feature/issue-126 at `a77823a`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready. Both Watch
  recommendations from the Issue Review (sign/unit conventions;
  deliberate profile-type units/signs) are settled as concrete decisions
  in the plan itself: angles positive-to-starboard (matching
  `rx_angles`/`beam_angle`), depths positive-down throughout the new
  module (opposite `Sounding::depth`'s negative-down, explicitly
  cross-referenced and flagged for the future phase-2 wiring negation),
  and the module ships as its own dependency-light CMake target
  (`cube_bathymetry_ssp_ray_tracer`, no `marine_acoustic_msgs`/tf2/Eigen)
  so the two downstream repos (`unh_marine_autonomy#300`,
  `marine_perception_tools#28`) can link it without pulling in
  `cube_bathymetry`'s full dependency chain.

## Plan Review
**Status**: complete
**When**: 2026-08-18 02:02 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-126/plan.md` at `a77823a`
**PR**: PR-less (`--issue` mode, branch `feature/issue-126`)
**Verdict**: changes-requested

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | 4 files, one module, no call-site changes; non-goals mirror the issue's three exactly. |
| Issue alignment | Needs work | Covers the issue's enumerated edge cases, but silently changes `depth` from the issue's "relative to the transducer" to absolute-below-surface, and omits the array-face domain-failure case that #121's stale-applied-SS finding makes reachable. |
| File targeting | Needs work | Right four files, but the CMake row omits the `install(TARGETS ... EXPORT export_cube_bathymetry)` line without which neither external consumer can link the target. |
| Consequences | Good | Correctly identifies the public interface as the one live consequence; no `Sounding`/importer/doc rows apply. |
| Documentation & instruction impact | Good | Non-silent, both subsections explicit. One inaccurate verification claim (see finding 13). |
| Principle alignment | Needs work | "Enforcement over documentation" — the deliberately-inverted depth sign is mitigated by prose only. |
| ADR compliance | Good | Correctly reads 0001/0002/0003/0007/0008 as untriggered; new-ADR deferral is defensible but see finding 14. |
| ROS conventions | N/A | Pure `<cmath>`/`<vector>` geometry, no node/topic/QoS/parameter surface. |

### Findings
- [ ] (must-fix) Array-face Snell has an undefined domain failure: `asin(p * c_profile(z_tx)) > 1` when the applied SS is below the cast's true value at wide steering angles — reachable in real data per #121's stale-applied-SS finding. Define the behavior and test it — `plan.md:98-104`
- [ ] (must-fix) `end_angle` representation for turning/ascending rays is unspecified (does it exceed pi/2, or flip sign?) — both consumers integrate against this — `plan.md:86, 124-127`
- [ ] (must-fix) Validity contract covers only empty profile and `t <= 0`; add non-positive/non-finite sound speeds, `|launch_angle| >= pi/2`, NaN `transducer_depth`, the duplicate/non-monotonic-depth rejection status, and extrapolation that drives `c <= 0`. uma#300 phase-2 feeds *perturbed* candidate profiles, so invalid input is the normal case — `plan.md:80-83, 132-133`
- [ ] (must-fix) Sign mitigation is documentation-only. Rename the result field (e.g. `depth_below_surface`) so the mismatch with `Sounding::depth` — which is really an elevation — is visible at the call site, and add a sign-assertion test — `plan.md:39-45`
- [ ] (must-fix) Result `depth` is absolute-below-surface (per the parity formula) while the issue specifies "relative to the transducer"; state the divergence and the shared-datum requirement — `plan.md:139-141`
- [ ] (suggestion) Precision: `float` fields plus `R = 1/(p*g)` produce catastrophic cancellation in `R*(cos t0 - cos t1)` as `g -> 0`. Compute internally in `double`, state the straight-segment epsilon criterion in terms of that error, and name the closed forms used — `plan.md:106-122`
- [ ] (suggestion) Add a mirror-symmetry test (port beam = negated starboard beam); the six planned cases are all single-signed, and the turning test should be `|sin theta| >= 1` — `plan.md:123-127, 135-159`
- [ ] (suggestion) The Snell-invariant test cannot observe layer crossings through an endpoint-only API; specify analytically-computed boundary-crossing travel times, one call per boundary — `plan.md:148-150`
- [ ] (suggestion) Decision 3's cross-repo isolation rationale is overstated — `package.xml`'s dependency set and `ament_export_dependencies(rclcpp ...)` still resolve for any `find_package(cube_bathymetry)`; the target is link-isolated, not package-isolated — `plan.md:62-72`
- [ ] (suggestion) CMake row must add `install(TARGETS cube_bathymetry_ssp_ray_tracer EXPORT export_${PROJECT_NAME} ...)`; header install is already covered by the existing `install(DIRECTORY include/ ...)` — `plan.md:161-164, 173`
- [ ] (suggestion) Document that `effective_sound_speed` reproduces slant *range* only — substituting it into `range = twtt*c/2` with the original rx angle lands at the wrong point — `plan.md:87-90`
- [ ] (suggestion) Consider splitting `validateProfile()` from `traceRay()`, or document the per-call O(n) revalidation, given the inversion loop's call volume — `plan.md:94-96`
- [ ] (suggestion) Docs-impact verification claim is inaccurate: `README.md:132` maps `surf_sspeed`/`mean_speed`, and `original_cube/docs/CUBE_Development_Notes.md:53` documents the same positive-down/positive-up split. Conclusion (no doc updates) still holds; cite that precedent in the header — `plan.md:205-210`
- [ ] (suggestion) Reconsider a short ADR: review-issue flagged the extrapolation policy and integration-scheme choice as ADR candidates "at plan/implementation time", and both are now settled cross-repo contracts — `plan.md:192`

### Verified correct (no action)
- Constant-gradient arc radius `R = 1/(p*g)` with `p = sin(theta)/c`, theta from nadir — confirmed by differentiating Snell (`d theta/ds = p*g`, constant).
- Ray-parameter form of the array-face correction, `p = sin(launch_angle)/array_sound_speed`, is the standard beam-steering refraction correction (the applied pair defines the invariant); the integration start angle is then `asin(p * c_profile(z_tx))`, which the plan implies but never writes down.
- Uniform-profile parity formulas match `sounding.h:52-54` exactly at `tx_angle = 0` (`y = r sin(rx)`, `z = r cos(rx)`), and the across-track sign claim vs `sonar_relative_position.y` is correct.
- Turning condition (`c(z)` reaching `1/p`) is the correct total-internal-refraction criterion.
- Requiring analytic test expectations be derived independently of the code under test is the right call for this module.
