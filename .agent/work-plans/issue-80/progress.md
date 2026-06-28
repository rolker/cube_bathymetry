---
issue: 80
---

# Issue #80 — Offline M3 import: add backscatter store layer

## Issue Review
**Status**: complete
**When**: 2026-06-27 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #80
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

The issue extends one existing tool (`import_bag_main.cpp` / the `import_bag` executable, written in
cube_bathymetry#63) to emit a second output — a `marine_mbes_backscatter_store` Draft tile set — from
the same CUBE pass that already produces bathy tiles. All infrastructure dependencies are confirmed
present in the current codebase:

- `marine_mbes_backscatter_store` package landed in unh_marine_autonomy#194 (package directory
  confirmed at `core_ws/src/unh_marine_autonomy/marine_mbes_backscatter_store/`).
- CUBE co-estimation with intensity (cube#52 / #54): `DepthAndUncertainty` already carries
  `intensity` and `beam_angle`; `Hypothesis::recordBeam()` already accumulates the per-beam
  intensity; `Node::value()` already emits intensity and intensity_variance.
- `marine_tiled_raster_store` float32 instantiation (ADR-0007 D6, #194): `MbesTile` already
  uses `TiledRasterTile<float>` for the value band.

The work fits in a single PR: threading intensity through the importer, adding a
`geoGridToBackscatterTile`/`mapSheetToBackscatterTiles` conversion, a new `--bs-store` flag on the
tool, and test coverage.

**Right repo?** Yes — `cube_bathymetry` is the correct placement per ADR-0007 D9: CUBE-coupled
accumulation lives here (sensors_ws), and `cube_bathymetry` already depends upward on `core_ws`
packages (`marine_bathymetry_store`, and now `marine_mbes_backscatter_store`).

**Dependencies**: No blocking prerequisites. All infrastructure already merged.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Offline tool; new flag is opt-in. CLI output should document what is written. |
| Enforce over documentation | OK | No new rule; code change only. |
| Capture decisions, not just implementations | OK | ADR-0007 D7 governs the draft/processed split — see Watch below. |
| A change includes its consequences | Watch | Tests for bathy import exist in `test_store_import.cpp`; a parallel test for the backscatter conversion is expected. CMakeLists.txt and package.xml must add the `marine_mbes_backscatter_store` dependency. |
| Only what's needed | OK | Reuses existing CUBE output path; first-cut correction (flat-bottom / current CUBE behavior) avoids premature GeoCoder work. |
| Improve incrementally | OK | Tightly scoped extension; both bathy and backscatter produced in one pass as ADR-0007 D5 intends. |
| Test what breaks | Watch | The intensity threading through `import_bag_main.cpp` is a new code path with no existing coverage; a regression test checking that intensity-bearing bags produce non-empty backscatter tiles would catch a future NaN-propagation bug. |
| Workspace vs. project separation | OK | Change is entirely in `cube_bathymetry` (project repo); no workspace content affected. |
| Workspace improvements cascade to projects | OK | N/A — project-repo change. |
| Primary framework first | OK | N/A — C++ implementation. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0007 (MBES Backscatter Store) | Yes — primary | D1 (single-tier; bags are the archive), D2 (intensity rides winning hypothesis), D3 (angle correction at node-output), D5 (fans out to both stores), D6 (float32 tile schema), D7 (draft/processed layers), D9 (package placement in cube_bathymetry). See Watch below on D7. |
| ADR-0002 (Bathymetric Store) | Watch | The offline importer writes to `BathymetryStore::Draft`. Backscatter should follow the same layer decision consistently — see D7 Watch. |
| ADR-0008 (ROS 2 Conventions) | OK | No new messages or services; extending an existing executable. |

### Consequences

The following should be part of the implementation or flagged as follow-up:

- `cube_bathymetry/CMakeLists.txt` and `package.xml`: add `marine_mbes_backscatter_store`
  as a new `ament_target_dependencies` / `<depend>` entry for the `import_bag` target.
- `test_store_import.cpp` (or a new test file): add backscatter tile conversion test mirroring the
  existing bathy tile conversion test.
- `import_bag` usage/help string: document the new `--bs-store` option.

### Actions
- [ ] Thread `intensity` and `beam_angle` from the projected `Sounding` to `GeoSounding` in `import_bag_main.cpp` (currently only `vertical_error`/`horizontal_error` are copied at lines ~507–511; without this, every beam has NaN intensity and CUBE accumulates no backscatter regardless of the input bag's intensity data).
- [ ] Decide draft vs. processed layer for the offline backscatter output. ADR-0007 D7 defines `processed` as "the durable product: the full deferred-settled correction (D3) over the offline CUBE re-run" and `draft` as "the live operator view". The offline importer is closer to `processed` semantically, but the full GeoCoder correction (cube#15) is not yet available, so the first-cut output is not fully durable. Explicitly document the chosen layer and why (matching the bathy store's pattern of using Draft is one defensible option; note it in the commit or issue).
- [ ] Add a bulk tile-insert path to the `MbesBackscatterStore` (e.g., `importTile()` or `importTiles()`) or explicitly document using cell-by-cell `set()` calls. The current `MbesBackscatterStore` has only `set()` per cell; the bathy import uses `BathymetryStore::importTiles()` for efficiency. The design choice (add bulk API vs. iterate cells) should be made consciously.
- [ ] Add test coverage for the backscatter conversion (parallel to `test_store_import.cpp`'s bathy conversion tests).

## Plan Authored
**Status**: complete
**When**: 2026-06-27 23:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-80/plan.md` at `05f281f`
**Branch**: feature/issue-80 at `05f281f`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-27 23:05 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-80/plan.md` at `05f281f`
**PR**: PR-less (`--issue` mode)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) Backscatter conversion omits timestamp + provenance — the bathy mirror `geoGridToTile(grid, timestamp_ns, source_index)` threads both into every `BathyCell` and `import_bag_main.cpp` feeds a `SourceRegistry` into `save()`; `MbesCell` has identical `timestamp`/`source_index` fields and mbes `save()` takes a `SourceRegistry*`. As written the Processed product ships timestamp=0/source_index=0/empty registry.json. Give `geoGridToBackscatterTile`/`mapSheetToBackscatterTiles` the same params, register an mbes source, pass the registry to `save()` — `plan.md:54-66`.
- [ ] (suggestion) State that `NodeRecord::intensity_var` maps to `MbesCell.intensity_variance` (ADR-0007 D6: variance is the quality band) so it isn't left NaN — `plan.md:55-57`.
- [ ] (suggestion) Document the same-pass layer split as intentional: bathy → Draft (`import_bag_main.cpp:666`), backscatter → Processed — `plan.md:26-29`.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-27 23:43 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-80 at `afcb106`
**Mode**: pre-push
**Depth**: Deep (reason: 10 files / 200+ changed lines; lifecycle-touching CUBE-node flush path)
**Must-fix**: 0 | **Suggestions**: 3
**Round**: 1 | **Ship**: recommended — no must-fix; only pre-existing-pattern suggestions, all plan-review findings resolved

### Findings
- [ ] (suggestion) `ament_export_dependencies` re-exports only `rclcpp`; the installed public header `store_import.h` now `#include`s `marine_mbes_backscatter_store/mbes_cell.hpp` (and `marine_bathymetry_store`). Pre-existing pattern — fix both deps together or leave — `cube_bathymetry/CMakeLists.txt:262`.
- [ ] (suggestion) `--bs-store` (and `-o`/`-d`/`--odom-topic`) read `*arg` after `arg++` with no bounds check → `vector::end()` deref (UB) if the flag is the last token. Pre-existing loop pattern — harden all or none — `cube_bathymetry/src/import_bag_main.cpp:290`.
- [ ] (suggestion) `BackscatterCellsMatchGridRecords` re-derives expected cells with the same `nodeRecords()`+`CellAreaIterator` walk as production, so it wouldn't independently catch a shared iterator-alignment bug; mitigated by absolute intensity/timestamp/source assertions. Optional — `cube_bathymetry/test/test_store_import.cpp:268`.

### Notes
- Deep review: 2 fresh-context Claude Adversarial passes (Lens A logic + Lens B systemic) + Static Analysis (ament_cpplint clean; cppcheck findings only on untouched context lines / GTest-macro false positive). Copilot off (default).
- Verified correct: `MbesCell` aggregate-init field order matches struct; `nodeRecords()`↔`CellAreaIterator` lockstep; double-flush idempotent (`queueFlush` early-returns + `clear()`s, extract is read-only); both stores share GGGS level via `fromCellSize(nominalCellSizeMeters())`.
- Plan adherence: full. Prior Plan-Review must-fix (timestamp + source_index provenance) is resolved — cells carry `cell_timestamp_ns` + non-zero `bs_source_index`, `&bs_registry` passed to `save()`.
- [ ] (suggestion) Line drift: threading insertion cited at 507–510; actual `gs.sounding.*` assignments at 508–509 — `plan.md:33-37`.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-28 00:00 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-80 at `fa13f94`
**Mode**: pre-push
**Depth**: Deep (reason: 10 files / 200+ changed lines; lifecycle-touching CUBE-node flush path)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 2 | **Ship**: recommended — no must-fix; round-1's CLI bounds-check suggestion resolved in `fa13f94`, only two pre-existing-pattern suggestions remain

### Findings
- [ ] (suggestion) `ament_export_dependencies` re-exports only `rclcpp`; installed public header `store_import.h` now `#include`s `marine_mbes_backscatter_store/mbes_cell.hpp` (and `marine_bathymetry_store`). Pre-existing latent pattern, propagates transitively via `ament_export_targets` — fix both deps or leave — `cube_bathymetry/CMakeLists.txt:262`.
- [ ] (suggestion) `BackscatterCellsMatchGridRecords` re-derives expected cells with the same `nodeRecords()`+`CellAreaIterator` walk as production, so it wouldn't independently catch a shared iterator-alignment bug; mitigated by absolute intensity/timestamp/source assertions. Optional — `cube_bathymetry/test/test_store_import.cpp:281`.

### Notes
- Round-2 re-review. Only code delta since round 1 (`afcb106`→`fa13f94`) is the CLI arg-parsing rewrite to a bounds-checked `next_value()` lambda — **resolves round-1 suggestion #2**: guards `std::next(arg) == end()` before advancing, `usage()` is `[[noreturn]]`, returned `const std::string&` consumed before any vector mutation. Verified correct.
- Deep review: 2 fresh-context Claude Adversarial passes (Lens A logic + Lens B systemic), both clean. Static Analysis: ament_cpplint clean; cppcheck findings only on untouched context lines / GTest-macro & cross-TU false positives. Copilot off (default).
- Re-verified: `MbesCell` aggregate-init field order matches struct (`intensity, intensity_variance, timestamp, source_index`); `nodeRecords()`↔`CellAreaIterator` lockstep; double-flush idempotent (`queueFlush` early-returns on empty queue); both stores share GGGS level via `fromCellSize(nominalCellSizeMeters())`.
- Tests **pass** in the latest build: `StoreImport.BackscatterCellsMatchGridRecords` + `StoreImport.BackscatterNaNPropagation` (test_store_import: 8 tests OK).
- Plan adherence: full. Prior Plan-Review provenance must-fix remains resolved.

## Integrated Review
**Status**: complete
**When**: 2026-06-27 20:30 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #82 at `63d180c`
**Sources**: 2 — Copilot @ `80eb6f3` (quota non-review, non-actionable), Local Review (Pre-Push) R2 @ `fa13f94`; CI rollup
**Cross-source confirmations**: 0
**CI**: all-pass (`ROS 2 Jazzy (industrial_ci)` success on `63d180c`)

The only GitHub review is a Copilot quota-limit non-review ("Copilot was unable to
review … reached their quota limit") with zero inline comments — classified as a
non-actionable non-review, not a finding. No human or conversation comments. The
single code-bearing source is the local round-2 pre-push review at `fa13f94`; the
only commits since (`80eb6f3` progress, `63d180c` ci.yml) touch no package code, so
both its open suggestions still apply verbatim at head and were re-verified against
the current files. No must-fix remains; the PR is mergeable with the two optional
suggestions tracked.

### Findings
- [ ] (suggestion, Local Review R2 @ `fa13f94`) `ament_export_dependencies(rclcpp)` re-exports only `rclcpp`, but the installed public header `store_import.h` now `#include`s `marine_bathymetry_store/*` and `marine_mbes_backscatter_store/mbes_cell.hpp` (confirmed lines 32–35). Downstream consumers of the exported target rely on transitive propagation via `ament_export_targets`. Pre-existing latent pattern (the `marine_bathymetry_store` include predates this PR) — fix both deps together or leave — `cube_bathymetry/CMakeLists.txt:262`.
- [ ] (suggestion, Local Review R2 @ `fa13f94`) `BackscatterCellsMatchGridRecords` re-derives expected cells with the same `nodeRecords()`+`CellAreaIterator` walk as production, so it would not independently catch a shared iterator-alignment bug; mitigated by absolute intensity/timestamp/source assertions. Optional test-robustness nit — `cube_bathymetry/test/test_store_import.cpp:281`.

### False positives
- (Copilot @ `80eb6f3`) No technical finding — the review body is a quota-limit notice with no inline comments, so there is nothing to dismiss; recorded only for source provenance.
