# Plan: Shared forward constant-gradient SSP ray tracer

## Issue

https://github.com/rolker/cube_bathymetry/issues/126

## Context

Today's import path (`sounding.h:43`) computes range as a straight ray:
`range = twtt * ping_info.sound_speed / 2`. Two downstream consumers need a
refraction-aware forward model instead: `unh_marine_autonomy#300` phase-2
inversion (iterates candidate profiles through this forward model) and
`marine_perception_tools#28` (interactive re-projection with a candidate
cast). Both threads agreed the ray trace exists **once**, in
`cube_bathymetry`, and consume it rather than reimplementing it. This PR
adds only the forward model — no `Sounding` wiring, no file I/O, no
refraction-aware TPU.

The observable set was established by `#121` (merged, PR#125): per-beam
`(twtt, rx_angle)` + per-ping applied `ping_info.sound_speed`, with
`tx_angles` identically zero (no along-track launch-angle observable — zero
transmit tilt, not a data gap). This means the ray trace is a genuine 2D
problem confined to the across-track/depth plane: the beam's full launch
angle away from nadir is already `rx_angles[i]` alone, matching the issue's
scope (`launch angle (from rx_angles[i])`, no `tx_angle` parameter).

## Design decisions (settled here, per the review's recommendation)

These become the de facto contract both downstream repos build against, so
they are decided now rather than left implicit:

1. **Sign/unit conventions.** `sounding.h`'s existing conventions are:
   `rx_angles`/`beam_angle` positive-to-starboard; `Sounding::depth`
   negative-down (0 at surface, more negative deeper). The new module
   deliberately uses **one consistent convention throughout its own API**:
   - Angles (launch angle, end angle): radians from nadir (straight down =
     0), **positive to starboard** — same sign as `rx_angles`, so a caller
     passes `rx_angles[i]` in unmodified.
   - Depths (profile samples, transducer depth, result depth):
     **positive-down** (0 at surface, increasing with depth) — the standard
     oceanographic/CTD-cast convention, and the one a future `#299` cast
     loader will read naturally. This is the **opposite sign** of
     `Sounding::depth` (which is really an elevation: positive-up). Per the
     plan review, documentation alone is insufficient against this repo's
     known sign-bug risk, so the convention is **enforced in the names**:
     the result field is `depth_below_surface` (never bare `depth`), so a
     copy-paste into `Sounding::depth` reads wrong at the call site, and a
     dedicated sign-assertion test locks the direction. Supporting
     precedent cited in the header: positive-down matches
     `Sounding::sonar_relative_position.z`, and
     `original_cube/docs/CUBE_Development_Notes.md:53` documents the same
     positive-down-inside / positive-up-outside split in the original CUBE
     library. The header still cross-references `sounding.h` and notes that
     phase-2 wiring into `Sounding::depth` must negate.
   - **Depth semantics are absolute, not transducer-relative** (explicit
     divergence from the issue's "relative to the transducer" wording,
     chosen deliberately: profile lookups need absolute depths anyway):
     `depth_below_surface` and `transducer_depth_below_surface` are both
     measured below the **same surface datum as the profile's depths** —
     the caller owes all three on one shared datum. Across-track offset
     remains relative to the transducer.
   - Across-track offset: meters, positive-to-starboard (same sign as
     `rx_angles`, so a positive launch angle produces a positive offset for
     a non-turning ray — directly comparable to `Sounding::sonar_relative_position.y`,
     which uses the same sign).
   Mixing conventions within one module is worse than picking one
   non-matching-but-internally-consistent convention and documenting the
   mismatch loudly at the one place it matters (the header, and later the
   phase-2 wiring site).

2. **Profile type**: monotonic-by-depth `std::vector` of `{depth, sound_speed}`
   samples (strictly increasing depth; duplicate/non-monotonic depths are
   `kInvalidInput` — the rejection has an assigned status, not just a
   mention). A single-point profile is valid and degenerates to a straight
   ray at that point's sound speed — this is the uniform-profile parity
   test. **All API scalars are `double`** (params, profile fields, result
   fields) and the integration runs in `double` throughout — per the plan
   review's cancellation analysis (`R = 1/(p·g)` reaches ~77 km at
   g ≈ 0.02 s⁻¹ while offsets are metres; `float` arc differences
   catastrophically cancel). `float` inputs from `SonarDetections` promote
   losslessly.

3. **Own CMake target, no new dependencies.** The module depends only on
   `<cmath>`/`<vector>` — no `marine_acoustic_msgs`, no Eigen, no tf2. Per
   this repo's existing isolation pattern (`cube_bathymetry_grid_projection`,
   `cube_bathymetry_quantize_tile`, each "own target so the dependency
   doesn't leak into the core library and stays unit-testable in isolation"),
   this ships as its own target, `cube_bathymetry_ssp_ray_tracer`, linking
   against nothing beyond the standard library. This matters here more than
   for those precedents: the two consumers are *separate repos*
   (`unh_marine_autonomy`, `marine_perception_tools`) that should be able to
   link the ray tracer alone without pulling in `rclcpp`/`tf2`/GDAL/
   `marine_acoustic_msgs` transitively through `cube_bathymetry`'s main
   library.

## Approach

1. **Header** `cube_bathymetry/include/cube_bathymetry/ssp_ray_tracer.h`:
   - `struct SoundSpeedProfilePoint { float depth; float sound_speed; };`
     and `using SoundSpeedProfile = std::vector<SoundSpeedProfilePoint>;`
     with the sign/ordering doc comments from decision 1–2 above.
   - `enum class RayTraceStatus { kOk, kInvalidInput, kEvanescentLaunch,
     kExtrapolationLimit };` — the full validity contract (per plan review:
     the phase-2 inversion iterates *perturbed* candidate profiles, so
     invalid input is the normal case, not the pathological one):
     - `kInvalidInput`: empty profile; non-strictly-increasing profile
       depths; any non-finite or ≤ 0 profile sound speed; non-finite
       `transducer_depth_below_surface`; non-finite launch angle or
       `|launch_angle| ≥ π/2`; non-finite or ≤ 0 `array_sound_speed`;
       non-finite or ≤ 0 `one_way_travel_time`. NaN-sentinel fields,
       matching this repo's existing NaN-for-invalid convention.
     - `kEvanescentLaunch`: the array-face Snell correction has no real
       solution — `p · c_profile(z_tx) ≥ 1` (the cast's sound speed at the
       transducer is high enough, relative to the applied
       `array_sound_speed`, that the steered beam cannot refract into the
       water column). This is reachable in practice: #121 found startup
       pings carry a stale applied sound speed. NaN sentinels; the
       inversion can penalize the candidate profile distinctly from
       malformed input.
     - `kExtrapolationLimit`: extrapolation past the profile's boundary
       gradient drove the local sound speed to ≤ `kMinSoundSpeed`
       (a documented floor, e.g. 1 m/s) before the travel time was
       exhausted; the result carries the endpoint at the floor point with
       `extrapolated = true` so a consumer sees how far the trace got.
   - `struct RayTraceResult` — `RayTraceStatus status`,
     `double across_track_offset`, `double depth_below_surface`,
     `double end_angle`, `double effective_sound_speed` (defined as
     straight-line distance from transducer to endpoint, divided by the
     one-way travel time — the constant speed that would reproduce the same
     **slant range** for the given time; the header states explicitly that
     substituting it into `range = twtt·c/2` with the original rx angle
     does NOT reproduce the endpoint position), `bool turned`,
     `bool extrapolated`.
   - **`end_angle` convention (settled per plan review)**: radians from
     nadir, positive-to-starboard, **continuous through a turning point**
     — it grows monotonically in magnitude along a turning arc, so an
     ascending ray has `|end_angle| > π/2`; the sign always gives the
     across-track direction of travel. Stated in the header with the
     turning-ray test asserting it.
   - `RayTraceResult traceRay(const SoundSpeedProfile & profile,
     double transducer_depth_below_surface, double launch_angle,
     double array_sound_speed, double one_way_travel_time);`
     `array_sound_speed` (`ping_info.sound_speed`) defines the invariant
     ray parameter `p = sin(launch_angle) / array_sound_speed` (the
     array-face Snell correction: the sonar steered the beam using its
     applied sound speed, so that pair defines `p`); the true initial
     water-column angle is `asin(p · c_profile(z_tx))`, with the
     no-real-solution case handled by `kEvanescentLaunch` above.

2. **Implementation** `cube_bathymetry/src/ssp_ray_tracer.cpp`:
   - Per-layer constant-gradient (isogradient) integration: for a layer
     `(z0,c0)-(z1,c1)` with gradient `g = (c1-c0)/(z1-z0)`, the ray parameter
     `p = sin(theta)/c` is the Snell invariant (theta measured from nadir,
     same reference as the launch-angle convention — this is the geometric
     equivalent of the issue's "cos θ/c" invariant stated for the
     complementary from-horizontal angle; same physics). When `|g|` is
     below `kStraightGradientEpsilon`, integrate the segment as a straight
     ray at constant `c0` — the epsilon is chosen and documented **in terms
     of the resulting position error** (arc-vs-chord sagitta ≤ ~1 µm over a
     100 m segment), not as an arbitrary small number; otherwise the ray
     follows the closed-form circular arc of radius `R = 1/(p*g)`, standard
     for piecewise-linear-profile ray tracing, computed in `double`
     throughout (decision 2's cancellation rationale).
   - Walk layers from the transducer's starting depth, accumulating
     horizontal offset and elapsed travel time per layer/partial-layer,
     until the accumulated time reaches `one_way_travel_time` (exhaustion
     may happen mid-layer — solve the partial-arc/partial-segment analytically
     for the fraction of the layer that consumes the remaining time, not by
     stepping).
   - Turning-ray handling: if the local ray parameter would require
     `sin(theta) > 1` to keep descending in a layer (i.e. `c(z)` would reach
     `1/p` before the layer's far edge), the ray turns within that layer;
     continue integrating the same arc past the turning point (now
     ascending) and set `turned = true`.
   - Profile-boundary extrapolation: if the ray reaches the shallowest or
     deepest profile sample with travel time remaining, continue using that
     boundary segment's gradient (or constant speed, if the boundary is the
     single-point/edge case) and set `extrapolated = true`.
   - Degenerate/invalid inputs: the full `kInvalidInput` /
     `kEvanescentLaunch` / `kExtrapolationLimit` contract from Approach 1 —
     validation runs before any integration; NaN sentinel fields on every
     non-`kOk`/non-`kExtrapolationLimit` status.

3. **Tests** `cube_bathymetry/test/test_ssp_ray_tracer.cpp` (GTest), per the
   issue's explicit plan:
   - **Uniform-profile parity**: single-point (or constant-`c`) profile
     reproduces today's straight-ray geometry exactly —
     `depth = transducer_depth + one_way_time * c * cos(launch_angle)`,
     `across_track_offset = one_way_time * c * sin(launch_angle)` — matching
     `sounding.h:43`'s `range = twtt*c/2` formula at zero launch angle as a
     sub-case.
   - **Two-layer analytic case**: hand-computed circular-arc geometry for a
     specific `(z0,c0)-(z1,c1)` gradient, launch angle, and travel time —
     expected `across_track_offset`/`depth`/`end_angle` computed
     independently (by hand/spreadsheet, not by calling the code under
     test) and compared with a tight tolerance.
   - **Snell invariant across boundaries** (redesigned per plan review —
     the API returns only an endpoint, so the naive form can't be built):
     for a 3-layer profile, analytically compute the travel time to each
     layer boundary, call `traceRay` once **per boundary-crossing time**,
     and verify each call's `sin(end_angle)/c_at_that_boundary` matches the
     initial `p` to tolerance.
   - **Port/starboard mirror symmetry**: identical profile and travel time
     traced at `+launch_angle` and `-launch_angle` must give mirrored
     `across_track_offset`/`end_angle` and identical `depth_below_surface`
     — one test covering the entire negative-`p`/negative-`R` sign family
     (half the real swath).
   - **Sign-assertion test** (decision 1 enforcement): a steep downward ray
     from a shallow transducer ends **deeper** than the transducer with
     `depth_below_surface > transducer_depth_below_surface > 0` — locks the
     positive-down convention against a silent flip.
   - **Evanescent launch**: `array_sound_speed` well below the cast's
     surface value at a wide launch angle so `p·c(z_tx) ≥ 1`; assert
     `kEvanescentLaunch` + NaN fields (the #121 stale-startup-SS case).
   - **Turning ray**: a profile/launch-angle/gradient combination chosen so
     the ray turns before exhausting travel time; assert `turned == true`
     and the endpoint is shallower than the turning depth (i.e. genuinely
     came back up), with an analytically expected endpoint.
   - **Profile-shallower-than-ray (extrapolation)**: travel time long enough
     to run past the deepest sample; assert `extrapolated == true` and the
     endpoint matches hand-computed extension of the last gradient.
   - **Degenerate inputs**: empty profile and zero/negative travel time each
     return `status == kInvalidInput`.

4. **CMakeLists.txt**: add the new target per decision 3, register
   `ament_add_gtest(test_ssp_ray_tracer test/test_ssp_ray_tracer.cpp)`
   linking only `cube_bathymetry_ssp_ray_tracer` (no `cube_bathymetry`
   link — proves the isolation), **and add
   `install(TARGETS cube_bathymetry_ssp_ray_tracer EXPORT …)`** following
   the repo's existing exported-target pattern — without the install/export
   neither external consumer can link it (plan-review catch). Header
   install is covered by the existing `install(DIRECTORY include/ …)`.
   Scope honesty (decision 3 amendment): the isolation is **link-level**,
   not package-level — `find_package(cube_bathymetry)` still resolves the
   package's full `<depend>` set; true package isolation would need a
   separate package and is not warranted for two consumers that already
   depend on this package.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/ssp_ray_tracer.h` | New: profile type, result type, `traceRay()` declaration, full sign/unit doc comments |
| `cube_bathymetry/src/ssp_ray_tracer.cpp` | New: constant-gradient ray integration |
| `cube_bathymetry/test/test_ssp_ray_tracer.cpp` | New: GTest suite (9 cases above) |
| `cube_bathymetry/CMakeLists.txt` | New `cube_bathymetry_ssp_ray_tracer` library target + gtest registration |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | No `Sounding` wiring, no file I/O, no TPU — matches the issue's stated non-goals exactly. |
| Test what breaks | The 6 GTest cases target the tracer's actual failure modes (turning, extrapolation, degenerate input) plus a strong regression tie to the existing straight-ray formula, not coverage-chasing. |
| Capture decisions, not just implementations | The sign/unit/target-isolation decisions above are recorded in this plan (durable, committed) rather than only in code comments. |
| A change includes its consequences | No existing call site changes in this PR (non-goal), so no dependent references need updating yet; the header doc comments are the consequence-bearing artifact for the two downstream repos. |
| Numerical/statistical code — watch signs and units (this repo's `AGENTS.md`) | Directly addressed by design decision 1 (explicit, cross-referenced, single internal convention) and by the Snell-invariant and analytic-case tests, which would catch a sign error in the gradient/turning logic. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| cube_bathymetry ADR-0001/0002/0003 (tile eviction, dirty-tile footprint, staleness fingerprint) | No | No tile/store interaction — pure in-memory geometry function. |
| cube_bathymetry ADR-0007 (backscatter store addendum) | No | Different correction family (angular response/TL), not ray geometry. |
| cube_bathymetry ADR-0008 (predicted-surface interpolation geometry) | No | Governs grid-side interpolation, not the acoustic forward model. |
| New ADR? | No | No new architectural pattern beyond what `#300`'s Architecture section already recorded (ray tracing owned by `cube_bathymetry`). If a later PR settles the arc-integration-vs-alternative choice with lasting rationale worth recording for future maintainers, that's a candidate ADR at that time — not required here. |

## Consequences

Using the consequences map: this PR touches no existing `Sounding`
construction, importer, docs, or existing tests, so most rows don't apply.
The one live consequence — two downstream repos will code against this
module's public interface — is addressed by making the header doc comments
the authoritative, cross-referenced contract (decision 1), not left to be
inferred from the implementation.

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): None — no existing README/docs
  reference ray tracing or sound-speed profiles yet (verified: no hits for
  "ray", "snell", "sound speed", or "ssp" in `cube_bathymetry/README.md`).
  The new header's doc comments are the documentation for this feature;
  no separate doc file is warranted for a single internal module at this
  stage.
- **Agent-instruction candidates** (proposals only): None — the
  isolation-per-target pattern and the sign-convention discipline are
  already established/documented in this repo's `CMakeLists.txt` comments
  and `AGENTS.md` ("Numerical/statistical code — watch signs and units");
  this PR follows existing guidance rather than surfacing a new one.

## Open Questions

- None — the sign/unit/target-isolation decisions above were settled in
  this plan per the review's recommendation, rather than deferred to
  implementation or to the user.

## Estimated Scope

Single PR (4 new/changed files, no existing call sites touched).
