# Plan: cube_bathymetry: drop epoch dimension, adapt to single-fused-grid store API

## Issue

https://github.com/rolker/cube_bathymetry/issues/69

## Context

`marine_bathymetry_store` (#221) removed the per-day epoch subdirectory from its
on-disk layout and API. The store now exposes a single fused grid per layer
(`store.tiles(layer)` → `std::map<GridIndex, BathymetryTile>`) rather than a
map-of-epochs. `epoch.hpp` was deleted. The path layout is now
`<dir>/<layer>/<level>_<row>_<col>{,_time,_source}.tif` with no `<epoch>` segment.

`cube_bathymetry` still uses the old API: `store.epochs(Draft)`, epoch-keyed map
access, `epoch.hpp`, `currentUtcDateString()`, and per-epoch dir construction in
`saveDirtyTiles()`. These break against the new headers.

## Approach

1. **Drop `epoch.hpp` include and `Epoch` type references** — remove the include
   from every file that uses it; the type `marine_bathymetry_store::Epoch` is gone.

2. **Update `store_import.h` / `store_import.cpp`** — rename `loadEpochIntoSheet`
   → `loadIntoSheet` (drop the `epoch` parameter) and replace the body with a
   direct iteration over `store.tiles(layer)`. Rename `mapSheetToEpochTiles` →
   `mapSheetToTiles` for consistency with the store's new naming. Fix docstring
   reference from `BathymetryStore::importEpoch` → `importTiles`.

3. **Update `cube_bathymetry_node.cpp`** — three sites:
   a. On-configure prime: call `cube::loadIntoSheet(store, Draft, sheet)`
      instead of finding the newest epoch and passing it.
      Remove the `draft_epochs` / `newest_epoch` local variables.
   b. `saveDirtyTiles()`: drop `currentUtcDateString()`, build dir as
      `draft_dir_ + "/" + layerDirName(Draft)` (no epoch segment). Update log
      messages that mention "epoch = UTC date".
   c. Remove the `currentUtcDateString()` static method entirely.
   d. Remove `#include "marine_bathymetry_store/epoch.hpp"`.

4. **Update `test_persistence.cpp`** — two test helpers/tests:
   - `saveDirty()`: remove the `epoch` param and drop the epoch subdir from the
     output path (write to `dir/draft/` directly).
   - `DraftLayerDirNameIsDraft`: no change needed.
   - `SaveDirtyThenStoreLoadRoundTrips`: replace `loaded.epochs(Draft)` + epoch
     lookup with `loaded.tiles(Draft)` iteration; remove `Epoch` local variable.
   - `PeriodicSaveEqualsEndOfSessionExport`: same epoch→tiles migration.
   - Update `mapSheetToEpochTiles` → `mapSheetToTiles` call sites.
   - Remove `#include "marine_bathymetry_store/epoch.hpp"`.

5. **Update `test_store_import.cpp`** — update `mapSheetToEpochTiles` →
   `mapSheetToTiles` calls; update `store.importEpoch(layer, epoch, tiles, ...)` →
   `store.importTiles(layer, std::move(tiles))`; update `loadEpochIntoSheet` →
   `loadIntoSheet` calls (drop epoch arg); remove `Epoch` local variables;
   remove `#include "marine_bathymetry_store/epoch.hpp"`.

6. **Update stale comments** — remove "LiveFused" and "epoch" references in
   block comments in `store_import.h` and `cube_bathymetry_node.cpp` that describe
   the old epoch-based provenance model.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/store_import.h` | Remove `epoch.hpp` include; rename `loadEpochIntoSheet` → `loadIntoSheet` (drop epoch arg); rename `mapSheetToEpochTiles` → `mapSheetToTiles`; fix `importEpoch` → `importTiles` in docstring |
| `cube_bathymetry/src/store_import.cpp` | Update function bodies: `loadIntoSheet` iterates `store.tiles(layer)` directly; rename `mapSheetToEpochTiles` → `mapSheetToTiles` |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | Remove `epoch.hpp`; remove `currentUtcDateString()`; update `on_configure` prime to call `loadIntoSheet`; update `saveDirtyTiles()` save path |
| `cube_bathymetry/test/test_persistence.cpp` | Remove `epoch.hpp`; update `saveDirty()` helper, `mapSheetToEpochTiles` calls, and both epoch-using tests |
| `cube_bathymetry/test/test_store_import.cpp` | Remove `epoch.hpp`; rename `mapSheetToEpochTiles` → `mapSheetToTiles`; update `importEpoch` → `importTiles`; update `loadEpochIntoSheet` → `loadIntoSheet` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Tests are updated in the same PR; stale comments updated; all build-breaking call sites covered |
| Only what's needed | No new abstraction added — only the minimal removal of the epoch dimension from each call site |
| Improve incrementally | Single focused PR; no unrelated cleanups (pre-seeding from `chart`/`processed` is explicitly out of scope) |
| Test what breaks | `test_persistence.cpp` round-trip tests are updated to exercise the new flat-directory layout |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | Yes | No ROS message / node interface changes; purely internal persistence path |
| ADR-0002 §D5 (tile persistence layout) | Yes | New layout `<dir>/<layer>/<tile>.tif` matches the amended store contract (no epoch subdir) |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `loadEpochIntoSheet` → `loadIntoSheet` | Call site in `cube_bathymetry_node.cpp` and `test_store_import.cpp` | Yes |
| `mapSheetToEpochTiles` → `mapSheetToTiles` | Call sites in `test_persistence.cpp` and `test_store_import.cpp` | Yes |
| `saveDirtyTiles()` dir path | On-disk layout assumptions in `test_persistence.cpp` | Yes |
| `store.importEpoch()` | `test_store_import.cpp` uses it — replace with `store.importTiles()` | Yes |
| Remove `currentUtcDateString()` | No external callers (private static method) | Yes |
| Remove `epoch.hpp` include | All four files that included it | Yes |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR. All changes are mechanical adaptations to the new store API; no design decisions required.
