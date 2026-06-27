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
