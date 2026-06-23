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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-23 11:33 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-44 at `ae035a4`
**Mode**: pre-push
**Depth**: Deep (reason: 843 added lines ≥ 200; new cross-layer importer linking core_ws + platforms_ws)
**Must-fix**: 1 | **Suggestions**: 6
**Round**: 1 | **Ship**: continue — one mechanical must-fix (trailing-slash epoch abort) plus a re-import data-correctness suggestion worth resolving before push.

Static analysis: ament_cpplint + ament_uncrustify clean on all 4 changed C++ files. Claude Adversarial: 2 disjoint-lens passes (Lens A logic, Lens B systemic). API usage verified against real dependency headers (BathyCell, BathymetryStore::set/save, resolve_datum 6-arg, GGGS CellIndex row-from-south, validateEpochLabel); GGGS row-flip and conversion arithmetic correct; fixture YAML schema matches load_datum_config.

### Findings
- [ ] (must-fix) Trailing slash on `--contour-dir` derives an empty/`.` epoch → tool aborts with a misleading "not a usable epoch label" error; the `has_filename() ? dir : dir+"/.."` ternary handles trailing slashes backwards (verified empirically; the plan's own documented path form has a trailing slash) — `src/import_chart_prior.cpp:141-143`
- [ ] (suggestion) Per-cell `set()` live path never sets `supersedes_disk`, so `save()` won't clear a pre-existing `chart/<epoch>/`; a re-import with a smaller footprint resurrects stale tiles → silently wrong store. Use `importEpoch(Replayed)` or require an empty output epoch dir. Promote to must-fix if re-imports into a populated store are expected — `src/import_chart_prior.cpp:303,322`
- [ ] (suggestion) Band-1 no-data assumes NaN only; ignores `GDALRasterBand::GetNoDataValue()`. Real Massabesic tiles use NaN (smoke test skipped ~47% of cells, so not broken today) but a legacy generator using a numeric sentinel would import garbage — `src/chart_import.cpp` / `src/import_chart_prior.cpp:~298`
- [ ] (suggestion) `std::stoi(--level)` uncaught → `std::terminate` on `--level abc` instead of the clean usage message every other parse error emits — `src/import_chart_prior.cpp:116`
- [ ] (suggestion) Mid-tile `RasterIO` error sets `tile_cells=0` and breaks, but cells already `set()` in earlier rows stay in the store and are saved; tile is then counted as skipped (counters under-report, partial tile reaches disk) — `src/import_chart_prior.cpp:285-290`
- [ ] (suggestion) `save()` is non-atomic: a GDAL failure mid-save leaves a partial `chart/<epoch>/` indistinguishable from a complete epoch on `load()`; note that the operator must clear the dir on failure — `src/import_chart_prior.cpp:319-326`
- [ ] (suggestion) Tests cover only the pure core; the GDAL/filesystem path (epoch derivation, no-data, filename parse) is untested — exactly where the above findings live. No negative `chart_datum_z` case either — `test/test_chart_import.cpp`

### Host scope note for address-findings (Roland 2026-06-23: full robustness pass)
Fix scope for this round (operator-approved):
- **MF (trailing slash)** — fix; normalize `--contour-dir` so a trailing slash still yields the `<date>` epoch. Add a test.
- **#1 (re-import resurrects stale tiles) — PROMOTED to must-fix.** Fix by switching the executable from the per-cell `set()` live path to the **wholesale import path**: build one fully-populated `BathymetryTile` per input file into a `std::map<gggs::GridIndex, BathymetryTile>`, then call `store.importEpoch(SourceLayer::Chart, epoch, std::move(tiles), Provenance::Replayed)`. This wholesale-replaces the epoch (no stale tile resurrection) AND means a mid-tile `RasterIO` error drops that whole tile rather than leaving a partial one in the store — so **#4 is fixed by the same change**. Verify importEpoch honors chart_writable for the Chart layer.
- **#2 (no-data)** — also honor `GDALRasterBand::GetNoDataValue()` in addition to NaN.
- **#3 (stoi)** — wrap `--level` parse; clean usage error, not std::terminate.
- **#6 (tests)** — add tests for epoch derivation (incl. trailing slash) + GridIndex-from-filename/geotransform parse + a negative `chart_datum_z` core case.
- **#5 (save() non-atomicity) — DEFER to UMA#189 (atomic writes, already open).** Do NOT implement atomic save here; add an in-code comment citing #189 and note in --help/log that a failed save leaves a partial epoch the operator must clear.
