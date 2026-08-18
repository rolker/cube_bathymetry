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
     `Sounding::depth`. The header states this explicitly and cross-
     references `sounding.h`, with an explicit note that a future phase-2
     caller wiring a `RayTraceResult` into `Sounding::depth` must negate it.
   - Across-track offset: meters, positive-to-starboard (same sign as
     `rx_angles`, so a positive launch angle produces a positive offset for
     a non-turning ray — directly comparable to `Sounding::sonar_relative_position.y`,
     which uses the same sign).
   Mixing conventions within one module is worse than picking one
   non-matching-but-internally-consistent convention and documenting the
   mismatch loudly at the one place it matters (the header, and later the
   phase-2 wiring site).

2. **Profile type**: monotonic-by-depth `std::vector` of `{depth, sound_speed}`
   samples (strictly increasing depth; duplicate depths rejected as
   ambiguous-gradient input). A single-point profile is valid and
   degenerates to a straight ray at that point's sound speed — this is the
   uniform-profile parity test.

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
   - `enum class RayTraceStatus { kOk, kInvalidInput };` — `kInvalidInput`
     covers empty profile and zero/negative travel time (returns NaN-
     sentinel fields, matching this file's existing NaN-for-invalid
     convention, e.g. `Sounding::beam_angle`).
   - `struct RayTraceResult` — `RayTraceStatus status`,
     `float across_track_offset`, `float depth`, `float end_angle`,
     `float effective_sound_speed` (defined as straight-line distance from
     transducer to endpoint, divided by the one-way travel time — the
     constant speed that would reproduce the same slant range for the given
     time, directly comparable to today's `ping_info.sound_speed`),
     `bool turned` (a turning point — total internal refraction — was
     passed en route; endpoint is still valid), `bool extrapolated`
     (travel time ran past the shallowest/deepest profile sample; the
     boundary segment's gradient was extended).
   - `RayTraceResult traceRay(const SoundSpeedProfile & profile,
     float transducer_depth, float launch_angle, float array_sound_speed,
     float one_way_travel_time);`
     `array_sound_speed` (`ping_info.sound_speed`) is used only to compute
     the initial ray parameter when it differs from the profile's own
     sound speed at `transducer_depth` (the array-face Snell correction
     named in the issue) — i.e. the launch angle is defined relative to
     the array's assumed sound speed, and the ray parameter
     `p = sin(launch_angle) / array_sound_speed` is what's actually
     invariant through the water column, not
     `sin(launch_angle) / profile_sound_speed_at(transducer_depth)`.

2. **Implementation** `cube_bathymetry/src/ssp_ray_tracer.cpp`:
   - Per-layer constant-gradient (isogradient) integration: for a layer
     `(z0,c0)-(z1,c1)` with gradient `g = (c1-c0)/(z1-z0)`, the ray parameter
     `p = sin(theta)/c` is the Snell invariant (theta measured from nadir,
     same reference as the launch-angle convention — this is the geometric
     equivalent of the issue's "cos θ/c" invariant stated for the
     complementary from-horizontal angle; same physics). When `|g|` is
     below a small epsilon, integrate the segment as a straight ray at
     constant `c0`; otherwise the ray follows the closed-form circular arc
     of radius `R = 1/(p*g)`, standard for piecewise-linear-profile ray
     tracing.
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
   - Degenerate inputs: empty profile or `one_way_travel_time <= 0` return
     `kInvalidInput` immediately with NaN sentinel fields.

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
   - **Snell invariant across boundaries**: for a 3+ layer profile, verify
     `sin(theta_i)/c_i` computed from `end_angle` at each layer crossing
     matches the initial `p` to floating-point tolerance.
   - **Turning ray**: a profile/launch-angle/gradient combination chosen so
     the ray turns before exhausting travel time; assert `turned == true`
     and the endpoint is shallower than the turning depth (i.e. genuinely
     came back up), with an analytically expected endpoint.
   - **Profile-shallower-than-ray (extrapolation)**: travel time long enough
     to run past the deepest sample; assert `extrapolated == true` and the
     endpoint matches hand-computed extension of the last gradient.
   - **Degenerate inputs**: empty profile and zero/negative travel time each
     return `status == kInvalidInput`.

4. **CMakeLists.txt**: add the new target per decision 3, and register
   `ament_add_gtest(test_ssp_ray_tracer test/test_ssp_ray_tracer.cpp)`
   linking only `cube_bathymetry_ssp_ray_tracer` (no `cube_bathymetry`
   link — proves the isolation).

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/ssp_ray_tracer.h` | New: profile type, result type, `traceRay()` declaration, full sign/unit doc comments |
| `cube_bathymetry/src/ssp_ray_tracer.cpp` | New: constant-gradient ray integration |
| `cube_bathymetry/test/test_ssp_ray_tracer.cpp` | New: GTest suite (6 cases above) |
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
