# Plan: Chart prior importer — contour depth-below-surface → ellipsoidal Chart tiles (A2)

## Issue
https://github.com/rolker/cube_bathymetry/issues/44  (A2 of, and closes, rolker/unh_marine_autonomy#163)

## Context
A1 (merged) added `SourceLayer::Chart` + the `chart_writable` flag to
`marine_bathymetry_store`. A2 is the importer that turns the existing raw contour
prior into ellipsoidal Chart tiles the store can load. `cube_bathymetry`
(sensors_ws) is the right home: it can link both `marine_bathymetry_store`
(core_ws) and `mru_transform`'s `datum_config` (platforms_ws) — keeping the store
core datum-agnostic (ADR-0002 §D7, convert at import). The package already uses
GDAL + GGGS (`bag_to_geotiff.cpp` is the model) but does **not** yet depend on the
store or mru_transform.

Raw input (`~/data/bathymetry/massabesic/store/chart/<date>/`, 142 level-11
GeoTIFFs): band1 = depth **below lake surface, negative-down, m**; band2 = const
3.0 uncertainty; band3 = timestamp. Conversion: `ellipsoidal = chart_datum_z +
depth_below_surface`, with `chart_datum_z` from `resolve_datum(lat,lon,entries,
nullopt)` (= **48.88 m** full-pool inside the `Lake Massabesic` polygon).

**Datum resolved (2026-06-22, both OQs closed by merged work — see Resolved
Decisions below).** The corrected full-pool lake datum is **48.88 m** WGS84
ellipsoidal (the earlier `52.3` was the boat on the trailer; corrected
2026-06-16, echoboats#278). The polygon datum config already exists, merged:
`bizzyboat_project11/config/massabesic_datum_polygons.yaml` (echoboats#285/PR#286)
— `chart_datum_z: 48.88` + a real raster-derived ring. Using 48.88 here also
satisfies the `#163` bullet of **unh_marine_autonomy#170** (datum-propagation
tracker). No new mru_transform config file and no invented polygon vertices.

## Approach (single PR, cube_bathymetry — no sibling config PR; config already merged)
1. **New executable `import_chart_prior`** (`src/import_chart_prior.cpp`), CLI:
   `--contour-dir <raw tiles> --store <out dir> --datum-config <yaml> [--level 11]`.
2. **Pure conversion core** (`src/chart_import.{hpp,cpp}` or in-lib, ROS-free): given
   a `BathymetryTile` (raw, depth-below-surface) + a resolved `chart_datum_z`,
   produce the ellipsoidal tile — so the arithmetic is unit-testable without GDAL.
3. **Importer flow:** `load_datum_config(path)`; for each `*.tif` in the contour dir
   `BathymetryTile raw = loadTile(path, level)` (reads by path — epoch subdir is
   fine); per data cell compute lat/lon from its `CellIndex`, `resolve_datum(...)`
   → `chart_datum_z` (warn+skip cells outside any polygon), set
   `{chart_datum_z + raw.depth, raw.uncertainty, raw.timestamp}` into a
   `BathymetryStore(level, /*chart_writable=*/true)` via `set(Chart, …)`; then
   `save(store, out_dir)`. Resolve the datum **once per tile** (constant within the
   `massabesic` polygon) — not per cell.
4. **Deps:** add `marine_bathymetry_store` + `mru_transform` to `package.xml` /
   `CMakeLists.txt` (find_package + link); new `add_executable`. The
   `mru_transform` dep is for the `resolve_datum` **code**, not a config file —
   the datum config is supplied at run time via `--datum-config` (the merged
   `bizzyboat_project11/config/massabesic_datum_polygons.yaml`).
5. **Test fixture (self-contained gtests):** ship a tiny `massabesic` datum-config
   fixture under `test/` (single `override:true` entry, `chart_datum_z: 48.88`,
   coarse ring) so the conversion gtests run without reaching into `platforms_ws`.
   Real runs point `--datum-config` at the merged bizzyboat config. **No sibling
   mru_transform config PR** — the production config already exists and is merged.

## Files to Change
| File | Change |
|------|--------|
| `cube_bathymetry/src/import_chart_prior.cpp` | new importer executable |
| `cube_bathymetry/src/chart_import.{hpp,cpp}` | ROS-free per-tile conversion core |
| `cube_bathymetry/CMakeLists.txt` / `package.xml` | + `marine_bathymetry_store`, `mru_transform`; new exe |
| `cube_bathymetry/test/test_chart_import.cpp` | conversion-core gtests |
| `cube_bathymetry/test/fixtures/massabesic_datum_polygons.yaml` | self-contained test datum config (`chart_datum_z: 48.88`) |

## Principles Self-Check
| Principle | Consideration |
|---|---|
| Reuse over duplication | Reuses `loadTile`/`save` (store) + `resolve_datum` (mru_transform); no new tile or datum code. |
| Layering | cube (sensors_ws) legitimately links core + platforms below it; store core stays datum-agnostic. |
| Do it right | Pure conversion core with gtests; single source of truth for 48.88 (datum config, not hardcoded). |

## ADR Compliance
| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 §D7 (convert at import) | Yes | Datum offset applied here; store stores ellipsoidal. |
| ADR-0002 §D3 / §D4 | Yes | Writes the read-only Chart layer via A1's chart_writable; lake = constant offset (off-VDatum). |

## Consequences
| If we change... | Also update... | Included? |
|---|---|---|
| New importer writes the Chart store | sim harness (sim#76) builds both stores from it; costmap (autonomy#164, MERGED PR#210) consumes it | Downstream, separate issues |
| Datum value used | unh_marine_autonomy#170 tracks 48.88 propagation to #163/#148/#164; using 48.88 here closes its #163 bullet | Yes (48.88) |

## Resolved Decisions (2026-06-22 — were Open Questions)
1. **Datum config location / who ships it — RESOLVED.** Already merged:
   `bizzyboat_project11/config/massabesic_datum_polygons.yaml` (echoboats#285/PR#286).
   No new mru_transform config file. Real runs pass it via `--datum-config`; gtests
   use a self-contained `test/fixtures/` copy.
2. **Massabesic polygon ring — RESOLVED.** Real raster-derived ring already in the
   merged config (4 corners from `massabesic_bathy.tif` extent, padded ~0.5 km).
   No vertices to invent.
3. **Datum value — RESOLVED to 48.88 m** (full-pool, WGS84 ellipsoidal; the plan's
   prior `52.3` was the on-trailer reading, corrected echoboats#278). Per
   unh_marine_autonomy#170.

## Estimated Scope
One cube_bathymetry PR (importer + tests + test fixture). No sibling config PR
(production datum config already merged). Depends on A1 (merged); independent of
the open #148 Phase-2 importer.
