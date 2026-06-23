---
issue: 44
---

# Issue #44 — Chart prior importer: contour depth-below-surface → ellipsoidal Chart tiles (A2 of unh_marine_autonomy#163)

## Plan Authored
**Status**: complete
**When**: 2026-06-15 07:21 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-44/plan.md` at `03dbd69`
**PR**: https://github.com/rolker/cube_bathymetry/pull/45 (`[PLAN]` prefix)
**Phases**: cube_bathymetry importer PR + one small sibling mru_transform datum-config change

### Open questions
- [ ] Where `datum_polygons.massabesic.yaml` lives + who ships it (recommend mru_transform/config, sibling PR; importer takes --datum-config).
- [ ] Massabesic polygon ring coordinates — need a coarse lake outline for the entry.

## Plan Review
**Status**: complete
**When**: 2026-06-23 02:22 +0000
**By**: Claude Code Agent (Claude Opus) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-44/plan.md` at `c63b640`
**PR**: https://github.com/rolker/cube_bathymetry/pull/45 (`[PLAN]` prefix; reviewed locally, gh unauthenticated)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) `mru_transform`'s `resolve_datum` lives in the **`datum_config`** library, which mru_transform installs as a bare `install(TARGETS datum_config LIBRARY DESTINATION lib)` — **not** in an `EXPORT` set, **not** in `ament_export_targets`, and **not** linked into the exported `mru_transform` target. So plan step 4's "find_package(mru_transform) + link" will fail to resolve `resolve_datum`/`load_datum_config` symbols downstream. Resolution: a small sibling mru_transform CMake change to export the `datum_config` target (e.g. `EXPORT export_mru_transform` + `ament_export_targets`) — which contradicts the plan's "single PR, no sibling mru_transform change" scope claim and its Consequences/Estimated-Scope sections. Confirm the link path before implementation; update scope accordingly. — `plan.md:46-48,97`
- [ ] (suggestion) The output **Epoch** label is unspecified. `BathymetryStore::set(layer, epoch, cell, value)` and the on-disk layout `<dir>/Chart/<epoch>/` require an epoch; step 3's `set(Chart, …)` elides it. State the choice (e.g. the raw `<date>` subdir name) — it's visible to downstream loaders (sim#76, costmap). — `plan.md:42-43`
- [ ] (suggestion) The inline `resolve_datum(lat,lon,entries, nullopt)` call form is stale. Actual signature is `resolve_datum(lat, lon, lake_datum, lake_datum_mhhw, vdatum, entries)` — pass `nullopt, nullopt, nullopt, <loaded entries>` so the `override:true` polygon entry wins. — `plan.md:20,42`
- [ ] (suggestion) Chart cells are written with default `source_index=0` (unset) and `save(store, out_dir)` passes no `SourceRegistry`. Confirm Chart prior needs no registered provenance source (the existing `importGeoTiff` path does register one) — fine if deliberate, but note it. — `plan.md:43`
- [ ] (note) The plan correctly does **not** reuse `importGeoTiff` (its built-in datum conversion is VDatum-based, not lake-constant depth-below-surface), so the custom `loadTile` + per-cell-offset core is justified. Layering is sound: sensors_ws → platforms_ws/core_ws is downward per ARCHITECTURE.md.

## Implementation
**Status**: complete
**When**: 2026-06-23 07:24 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Branch**: feature/issue-44 at `a616a10` (+ plan-sync commits c63b640/837c772/34e1adf)
**Prerequisite**: sibling mru_transform export PR#29 (datum_config target) MERGED to jazzy (52cb640); main platforms_ws rebuilt — `mru_transform::datum_config` linkable.

### What landed
- `src/chart_import.{hpp,cpp}` — ROS-free/GDAL-free conversion core `chartCellFromContour`: `ellipsoidal = chart_datum_z + depth_below_surface`, uncertainty carried, legacy Float64-sec → int64-ns timestamp (0/NaN = unset), source_index 0, NaN band1 → no-data.
- `src/import_chart_prior.cpp` — executable `--contour-dir --store --datum-config [--level 11]`: GDAL-reads legacy 3-band Float64 tiles (loadTile rejects that format post-#178), resolves datum once/tile via 6-arg `resolve_datum` at tile center, GridIndex via `Level::gridIndex(center)`, `store.set(Chart, <date-epoch>, cell, value)` with chart_writable, `save(store, dir, nullptr)` (single-source, no registry — deliberate).
- `test/test_chart_import.cpp` — 10 gtests (core arithmetic, NaN, sec→ns, source_index).
- `test/fixtures/massabesic_datum_polygons.yaml` — self-contained datum config (override:true, chart_datum_z 48.88).
- `CMakeLists.txt`/`package.xml` — + marine_bathymetry_store + mru_transform deps; chart_import lib, import_chart_prior exe, test_chart_import gtest.

### Build / test / lint
- Build clean (10.3s); 264 tests, 0 failures, 39 skipped; test_chart_import 10/10 (independently re-confirmed by host).
- bare-jazzy ament_uncrustify + ament_cpplint clean on all 4 changed files.

### Row-order / GridIndex (flagged + resolved)
- Row order mirrors `marine_tiled_raster_store::saveTile`'s north-up flip inverse (GGGS row r ← GDAL row edge-1-r); verified by byte-identical geotransform round-trip vs input tiles.
- GridIndex from the GeoTIFF's real geotransform via `Level::gridIndex(center)` (not the private filename-nudge helper).
- Smoke test vs real prior (`chart/2026-06-12`): 142/142 tiles, 0 skipped, ~69.2M cells, depths 41.26–48.88 m ellipsoidal; split value/_time/_source tiles written.
