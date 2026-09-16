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
nullopt)` (= 52.3 m inside the `massabesic` polygon).

## Approach (single PR, cube_bathymetry; + one sibling mru_transform change — see OQ1)
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
   `CMakeLists.txt` (find_package + link); new `add_executable`.
5. **`massabesic` datum entry** (`chart_datum_z: 52.3`, polygon ring around the
   lake) — see OQ1 for where it lands.

## Files to Change
| File | Change |
|------|--------|
| `cube_bathymetry/src/import_chart_prior.cpp` | new importer executable |
| `cube_bathymetry/src/chart_import.{hpp,cpp}` | ROS-free per-tile conversion core |
| `cube_bathymetry/CMakeLists.txt` / `package.xml` | + `marine_bathymetry_store`, `mru_transform`; new exe |
| `cube_bathymetry/test/test_chart_import.cpp` | conversion-core gtests |
| *(sibling)* `mru_transform/config/datum_polygons.massabesic.yaml` | `massabesic` entry (OQ1) |

## Principles Self-Check
| Principle | Consideration |
|---|---|
| Reuse over duplication | Reuses `loadTile`/`save` (store) + `resolve_datum` (mru_transform); no new tile or datum code. |
| Layering | cube (sensors_ws) legitimately links core + platforms below it; store core stays datum-agnostic. |
| Do it right | Pure conversion core with gtests; single source of truth for 52.3 (datum config, not hardcoded). |

## ADR Compliance
| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0002 §D7 (convert at import) | Yes | Datum offset applied here; store stores ellipsoidal. |
| ADR-0002 §D3 / §D4 | Yes | Writes the read-only Chart layer via A1's chart_writable; lake = constant offset (off-VDatum). |

## Consequences
| If we change... | Also update... | Included? |
|---|---|---|
| New importer writes the Chart store | sim harness (sim#76) builds both stores from it; costmap (autonomy#164) consumes it | Downstream, separate issues |
| Add `massabesic` datum entry | the deployment's `chart_datum_path` should point at the same file (single source) | OQ1 |

## Open Questions
1. **Where does `datum_polygons.massabesic.yaml` live + who ships it?** Recommend
   `mru_transform/config/` (where `resolve_datum` + the example live, and where the
   runtime `chart_datum_node` `datum_config_path` would point — single source of
   truth) as a **small sibling PR**. The importer just takes `--datum-config <path>`.
2. **Massabesic polygon ring coordinates** — need a real (even coarse) lake outline
   for the `ring`; a generous bounding polygon around 42.99 N / -71.39 W suffices for
   a constant offset. Source the vertices before implementing the config.

## Estimated Scope
One cube_bathymetry PR (importer + tests) + one small mru_transform PR (datum
config). Depends on A1 (merged); independent of the open #148 Phase-2 importer.
