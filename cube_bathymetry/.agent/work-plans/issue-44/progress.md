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
