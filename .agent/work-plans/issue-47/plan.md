# Plan: Error-model fidelity: datum-aware tide terms + parameterized device errors + divergences doc

## Issue

https://github.com/rolker/cube_bathymetry/issues/47

## Context

`cube_bathymetry`'s `ErrorModel` carries two correctness warts from its original
Calder port and one missing documentation artefact (the deliverable of #30):

1. **Tide terms are orphaned.** `Vessel::tide_measured_sdev` / `tide_predicted_sdev`
   are declared (defaulted to Calder's 0.02 m) but never used.
   The grid is ellipsoid-referenced — no tidal reduction is applied — so omitting
   them is correct, but the intent is invisible. An `ellipsoidal_referenced` flag
   (default `true`) makes it explicit and enables tidal-datum mode without recompile.

2. **Range error is 10× too pessimistic.**
   `error_model.cpp:234-239` hardcodes `(depth * 0.05)²` — 5% of depth — marked
   `// todo: replace with device specific info`. Calder's deep-water reference
   devices were 0.3–0.6% with a 0.15–0.6 m floor. Two new `Vessel` fields
   (`range_error_percent` default 0.005, `range_error_floor_m` default 0.05) replace
   the literal, exposed as ROS parameters on `detections_to_pointcloud`.

3. **No divergences-from-Calder document.** The intentional differences from
   Calder's original (tide flag, parameterized device errors, IHO f(z) model not
   ported, CONF_99PC = 2.576 vs 2.56, nominated-input_sample_variance choice)
   are unrecorded. This is the explicit deliverable of #30.

**IHO validation of 0.5% default (computed):** at 2–20 m survey depths the
0.5%/0.05 m range error is well inside IHO Order 1a budget (0.1 m at 20 m depth
vs 0.288 m 1σ budget). The old 5% value reached 1.0 m at 20 m — larger than the
entire allowed vertical budget.

## Approach

### Step 1 — Add `ellipsoidal_referenced` flag to `Vessel` and wire it into `ErrorModel`

- Add `bool ellipsoidal_referenced = true;` to `struct Vessel` in `error_model.h`.
- In `ErrorModel::ErrorModel()` (constructor, lines 32–35): **keep the existing
  computation unchanged** — `vertical_reduction` already omits tide terms, which
  is correct when `ellipsoidal_referenced = true`.  No arithmetic change needed.
- Add a new private method or inline conditional in `swath_vertical` (or store the
  tide variance in `static_error_sources_.vertical_reduction` during construction):
  when `vessel_.ellipsoidal_referenced == false`, add
  `vessel_.tide_measured_sdev² + vessel_.tide_predicted_sdev²` to
  `static_error_sources_.vertical_reduction` at construction time. This keeps
  `swath_vertical()` unchanged.
- Add a comment at the relevant constructor lines explaining the choice (per review
  recommendation): `// No tide terms: grid is ellipsoid-referenced (#47); see
  divergences doc`.

### Step 2 — Expose `ellipsoidal_referenced` as a ROS parameter in `detections_to_pointcloud`

- In `DetectionsToPointCloud::on_configure()`, after the existing frame-name
  declarations: declare and read `ellipsoidal_referenced` (default `true`).
- Build the `cube::Vessel` with that value before constructing `ErrorModel`.
- Also declare and read `range_error_percent` (default 0.005) and
  `range_error_floor_m` (default 0.05) in the same block.

### Step 3 — Parameterize `range_error()` in `Device` (not `Vessel`)

After reviewing the struct layout: range error is sonar-device-specific (a sonar
spec, not a vessel spec). Add to `struct Device` in `error_model.h`:

```cpp
double range_error_percent = 0.005;   // fraction of depth (0.5%)
double range_error_floor_m = 0.05;    // absolute floor, m
```

Update `ErrorModel::range_error(double depth)` in `error_model.cpp`:

```cpp
double ErrorModel::range_error(double depth) const
{
  auto error = std::max(std::abs(depth) * device_.range_error_percent,
                        device_.range_error_floor_m);
  return error * error;
}
```

(`std::abs(depth)` because depth is negative by convention; the floor prevents
an error of zero at the surface.)

Wire these into `detections_to_pointcloud.cpp`: declare/read `range_error_percent`
and `range_error_floor_m` ROS parameters, set them on the `cube::Device` struct
before constructing `ErrorModel`.

### Step 4 — Confirm angle error fallback is a parameter (no-change verification)

- Verify `device_.across_track_beamwidth` is the static fallback at line 214 in
  `error_model.cpp`. It is (the field exists on `Device` at `error_model.h:38`).
- No behavioral change needed. Document it in the divergences doc.

### Step 5 — Add/extend tests in `test_error_model.cpp`

New test cases (in the `ErrorModelTest` fixture):

1. **`TideTermsOffByDefaultEllipsoidReferenced`** — construct with default Vessel
   (ellipsoidal_referenced=true), and a custom Vessel with
   ellipsoidal_referenced=false but same other fields. Verify:
   - default `vertical_error` == ellipsoidal mode output
   - tidal-datum mode produces a *larger* `vertical_error` (tide variances add)

2. **`RangeErrorParameterized`** — construct Device with range_error_percent=0.01
   and floor=0.0, compute at a shallow and deep beam; verify vertical_error scales
   correctly (i.e., changing percent changes the result).

3. **`RangeErrorFloorDominatesAtShallowDepth`** — set percent=0.005, floor=0.2;
   at depth ≈0.02 m the floor should dominate; verify `range_error(0.02)` ≈ 0.04
   (floor² = 0.04).

4. **`DefaultsPreserveExistingBehavior`** — run all existing tests implicitly pass
   (they already use default Vessel/Device, so the defaults must match prior
   behavior). No code change, just note this constraint.

5. **`IHOBudgetCheck_Order1a`** — at depths 2, 10, 20 m with default vessel/device,
   verify `vertical_error < maxVarianceAllowed(depth)` for IHO Order 1a. This
   pins the defaults against the acceptance criterion from the issue.

### Step 6 — Create divergences doc

Create `cube_bathymetry/docs/divergences_from_calder.md`:

Document the five intentional divergences:
1. **Tide terms / referencing flag** — why and how (this issue)
2. **Device error parameterized, not per-device table** — range/angle, with Calder
   reference values noted
3. **IHO f(z) model NOT ported** — only the FULL MBES model is ported; rationale:
   the f(z) interpolation model in Calder is for legacy depth sounders; this port
   targets MBES only
4. **CONF_99PC = 2.576 vs Calder's 2.56** — source is the standard Normal table
   (more accurate); Calder used a rounded value
5. **Nomination uncertainty: `nominated->input_sample_variance`** — more
   self-consistent than Calder's list-head variance; rationale explained

Reference the CUBE_Development_Notes.md and the original Calder source for context.

### Step 7 — Link divergences doc from README and reference #30

- Add a `## Design Notes` or `## Divergences from Calder` subsection to `README.md`
  linking to `docs/divergences_from_calder.md`.
- Update `docs/divergences_from_calder.md` to note it closes #30.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/error_model.h` | Add `ellipsoidal_referenced` to `Vessel`; add `range_error_percent` + `range_error_floor_m` to `Device` |
| `cube_bathymetry/src/error_model.cpp` | Constructor: compute tide-term contribution based on flag; `range_error()`: replace hardcoded 5% with device params |
| `cube_bathymetry/src/detections_to_pointcloud.cpp` | Declare/read 3 new ROS params (`ellipsoidal_referenced`, `range_error_percent`, `range_error_floor_m`); pass to `Vessel`/`Device` before constructing `ErrorModel` |
| `cube_bathymetry/test/test_error_model.cpp` | Add 5 new test cases (tide mode toggle, range parameterization, floor dominance, IHO budget check) |
| `cube_bathymetry/docs/divergences_from_calder.md` | New file: 5 intentional divergences with rationale |
| `README.md` | Add link to divergences doc; reference #30 closure |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | `ellipsoidal_referenced` is a ROS parameter — operator can toggle tide mode in YAML without recompile. `range_error_percent` / `range_error_floor_m` are likewise operator-tunable for platform-specific calibration. |
| Capture decisions, not just implementations | Divergences doc records the five intentional deviations with rationale, making them discoverable to future maintainers and auditors. |
| A change includes its consequences | Tests cover the new modes; README links the doc; inline comments at the previously-silent constructor lines explain the "why". `detections_to_pointcloud` is the only ROS-layer consumer of `ErrorModel` — both the library structs and the node are updated in the same PR. |
| Test what breaks | Existing tests pass unchanged (defaults preserved). New tests cover both flag states and parameterized range error. An IHO budget check pins the defaults against acceptance criteria. |
| Only what's needed | Angle error model: no behavioral change, documentation only. Predicted-surface (#48) and node ratio/tie-break (#49) are explicitly deferred. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 — Adopt ADRs | Yes (light) | Divergences doc is not an ADR but a package-level design record; the README links it as required. No new ADR needed. |
| ADR-0008 — ROS 2 Conventions | Yes | New ROS parameters use `snake_case`, `declare_parameter` / `get_parameter` pattern, grouped in `on_configure()` adjacent to existing parameter declarations. |
| ADR-0013 — progress.md vocabulary | Yes (meta) | progress.md entries will be written per this plan. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `Vessel` fields | `detections_to_pointcloud.cpp` (constructs Vessel with defaults) | Yes — Step 2 |
| `Device` fields | `detections_to_pointcloud.cpp` (constructs Device with defaults) | Yes — Step 3 |
| `range_error()` behavior | All existing error tests (they exercise range implicitly) | Yes — defaults unchanged; Step 5 confirms |
| `static_error_sources_.vertical_reduction` constructor | `swath_vertical()` — no change; `vertical_reduction` is pre-summed | Yes — no downstream code change |
| README | Cross-reference #30 and new divergences doc | Yes — Step 7 |

## Open Questions

- [ ] No open questions — plan is review-plan-ready. (All decisions from #30 triage
  and Issue Review checkpoint are incorporated; the `Device` vs `Vessel` placement
  of range-error fields is resolved: `Device` is the right owner.)

## Estimated Scope

Single PR. All changes are within `cube_bathymetry` package (~6 files).
Estimated: 3 header/source edits, 1 test file extension, 1 new doc, 1 README update.
