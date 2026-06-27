# Plan: Offline M3 import — add backscatter store layer

## Issue

https://github.com/rolker/cube_bathymetry/issues/80

## Context

The `import_bag` tool (`import_bag_main.cpp`) replays M3 bags through the CUBE
estimator and writes a `marine_bathymetry_store` bathy tile set. The CUBE node
already co-estimates backscatter via `Node::extractNodeRecord()` (intensity +
intensity_var), but the offline import path currently:

1. Does NOT thread `intensity`/`beam_angle` from the projected `Sounding` to
   `GeoSounding` (lines 507–510 of `import_bag_main.cpp` copy only
   `vertical_error`/`horizontal_error`), so every beam has NaN intensity and CUBE
   accumulates no backscatter.
2. Has no backscatter tile conversion or output path.

The fix surfaces the existing uncorrected co-estimated backscatter into a
`marine_mbes_backscatter_store` **Processed** layer tile set, written by a new
opt-in `--bs-store` flag. `node.cpp` is untouched (no beam-angle correction in
this PR — that is cube#81).

**Layer decision (operator-confirmed):** write to `Processed`, not `Draft`. The
offline CUBE re-run is the authoritative off-boat product; cube#81 will update
corrected values in place when it lands. Rationale deviates from ADR-0007 D7's
literal "full deferred-settled correction" definition — operator accepted the
pragmatic reading ("authoritative offline re-run") and the plan documents it.

## Approach

1. **Thread `intensity`/`beam_angle` in `import_bag_main.cpp`** — at lines
   507–510, add `gs.sounding.intensity = s.intensity;` and
   `gs.sounding.beam_angle = s.beam_angle;`. `projection.soundings` is
   `std::vector<cube::Sounding>` and `Sounding` already populates both fields
   from the `SonarDetections` message.

2. **Add `GeoGrid::nodeRecords()`** — parallel to `values()`, iterates `nodes_`
   in `CellAreaIterator` order and calls `node->queueFlush()` +
   `node->extractNodeRecord(parameters_)` for each present node; returns
   `std::vector<NodeRecord>`. Absent nodes push a default `NodeRecord{}` (NaN
   intensity, matching the `values()` / `DepthAndUncertainty` sentinel). Declare
   in `geo_grid.h`, implement in `geo_grid.cpp`.

3. **Add `MbesBackscatterStore::importTiles()`** in the
   `marine_mbes_backscatter_store` package — takes `SourceLayer` + `std::map<
   gggs::GridIndex, MbesTile>` (move), inserts each tile into the store's layer
   map directly. Mirrors `BathymetryStore::importTiles()`. Rationale: keeps
   `store_import.cpp` as a pure CUBE→tile converter and gives the store a clean
   bulk-import API matching the bathy precedent. Declared in `mbes_store.hpp`,
   implemented in `mbes_store.cpp` (or inlined — small method).

4. **Add `geoGridToBackscatterTile()` + `mapSheetToBackscatterTiles()`** in
   `store_import.h`/`.cpp` — mirrors `geoGridToTile`/`mapSheetToTiles` but
   calls `grid.nodeRecords()`, builds an `MbesTile` from finite-intensity cells,
   and returns `std::map<gggs::GridIndex, MbesTile>`. Skip NaN-intensity cells.
   Call `values()` (bathy) then `nodeRecords()` (backscatter) in sequence on the
   same map sheet — the second `queueFlush()` is a no-op after the first.

5. **Add `--bs-store <dir>` CLI flag** in `import_bag_main.cpp` — opt-in; when
   provided, after `mapSheetToTiles()`, call `mapSheetToBackscatterTiles()`,
   construct an `MbesBackscatterStore` at the same GGGS level, import the tiles
   into the `Processed` layer, and call `marine_mbes_backscatter_store::save()`.
   Log tile count and output path, matching the bathy path's log style.

6. **Update `cube_bathymetry` build files** — `CMakeLists.txt`: add
   `find_package(marine_mbes_backscatter_store REQUIRED)`, link it into
   `cube_bathymetry_store_import` (and `import_bag` if it also uses the store API
   directly). `package.xml`: add `<depend>marine_mbes_backscatter_store</depend>`.

7. **Tests** — add `test_store_import.cpp` tests for backscatter conversion:
   - `BackscatterTileCellsMatchGridRecords`: intensity-bearing soundings (set
     `sounding.intensity` in `makeSoundings()`) → non-empty backscatter tile with
     finite intensity cells matching `nodeRecords()` values.
   - `BackscatterNaNPropagation`: soundings with NaN intensity produce an empty
     (all-no-data) backscatter tile — NaN guard against a future threading bug.
   Update `test_store_import`'s `target_link_libraries` in `CMakeLists.txt` to
   add `marine_mbes_backscatter_store::marine_mbes_backscatter_store`.

## Files to Change

| File | Change |
|------|--------|
| `core_ws/src/unh_marine_autonomy/marine_mbes_backscatter_store/include/marine_mbes_backscatter_store/mbes_store.hpp` | Add `importTiles(SourceLayer, std::map<gggs::GridIndex, MbesTile>)` declaration |
| `core_ws/src/unh_marine_autonomy/marine_mbes_backscatter_store/src/mbes_store.cpp` | Implement `importTiles()` |
| `sensors_ws/src/cube_bathymetry/cube_bathymetry/include/cube_bathymetry/geo_grid.h` | Add `nodeRecords()` declaration |
| `sensors_ws/src/cube_bathymetry/cube_bathymetry/src/geo_grid.cpp` | Implement `nodeRecords()` |
| `sensors_ws/src/cube_bathymetry/cube_bathymetry/include/cube_bathymetry/store_import.h` | Add `geoGridToBackscatterTile()` + `mapSheetToBackscatterTiles()` declarations + `#include` for backscatter store types |
| `sensors_ws/src/cube_bathymetry/cube_bathymetry/src/store_import.cpp` | Implement both backscatter conversion functions |
| `sensors_ws/src/cube_bathymetry/cube_bathymetry/src/import_bag_main.cpp` | Thread `intensity`/`beam_angle` at lines 507–510; add `--bs-store` flag and backscatter output path |
| `sensors_ws/src/cube_bathymetry/cube_bathymetry/CMakeLists.txt` | `find_package` + link `marine_mbes_backscatter_store` into `cube_bathymetry_store_import` and test target |
| `sensors_ws/src/cube_bathymetry/cube_bathymetry/package.xml` | Add `<depend>marine_mbes_backscatter_store</depend>` |
| `sensors_ws/src/cube_bathymetry/cube_bathymetry/test/test_store_import.cpp` | Add two backscatter conversion tests |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | `--bs-store` is opt-in; existing bathy-only workflow unchanged. CLI logs tile count and output path. |
| Only what's needed | No correction logic, no new node fields, `node.cpp` untouched. Surfaces existing `extractNodeRecord()` output. |
| A change includes its consequences | Tests added in same PR; CMakeLists.txt + package.xml updated; layer decision documented in commit message. |
| Test what breaks | `BackscatterNaNPropagation` guards against silent NaN-through-threading regression. |
| Capture decisions, not just implementations | Layer choice (Processed), API choice (importTiles vs cell-by-cell set), and beam_angle threading rationale documented here and in commit. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0007 (MBES Backscatter Store) | Yes — primary | D2: intensity rides winning hypothesis (via `extractNodeRecord()`). D3: deferred correction (cube#81, not here). D5: offline import writes both bathy + backscatter from same pass. D6: float32 two-band `MbesTile` used. D7: Processed layer — see "Layer decision" above for ADR-0007 D7 nuance and operator rationale. D9: package placement in `cube_bathymetry` correct. |
| ADR-0002 / ADR-0008 (workspace) | No | No workspace content affected; C++ only. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `mbes_store.hpp` (add importTiles) | `mbes_store.cpp` implementation | Yes — step 3 |
| `store_import.h` (add backscatter functions) | `store_import.cpp`, `cube_bathymetry_node.cpp` if it ever uses them | Step 4 covers .cpp; node does not need backscatter conversion (live path is #78) |
| `import_bag_main.cpp` (new flag + output) | Usage string in `printHelp()` | Yes — step 5 |
| `CMakeLists.txt` (`cube_bathymetry_store_import` deps) | `test_store_import` target link, `test_persistence`/`test_tile_eviction_rss` if they use backscatter types | Test target covered in step 7; other test targets don't need the backscatter store |

## Open Questions

- None — all decisions confirmed by operator (Processed layer, no correction, plan independent of #78).

## Estimated Scope

Single PR. Two packages touched (`marine_mbes_backscatter_store` + `cube_bathymetry`), all in the same workspace.
