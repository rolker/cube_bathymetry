# Plan: Persist GeoMapSheet draft tiles between sessions

## Issue

https://github.com/rolker/cube_bathymetry/issues/21

## Context

The live CUBE node currently uses `cube::MapSheet` (Cartesian, TF-projected), while
`GeoMapSheet` (GGGS geographic-index-based) is used only by the offline tools
(`import_bag`, `bag_to_geotiff`). `store_import.cpp` already provides
`mapSheetToEpochTiles(GeoMapSheet, ...)`, which is the correct save path.

To wire live persistence the node must migrate its accumulator from `cube::MapSheet`
to `cube::GeoMapSheet` — a prerequisite. After that, the save/load loop is a thin
wrapper around existing `mapSheetToEpochTiles` + `tile_io` APIs.

Decisions already made (owner comment 2026-06-15, issue review):
- Persist via `marine_bathymetry_store` tile_io format (`draft/<epoch>/` tiles).
- Atomic writes (temp-then-rename) per tile — costmap reads directly off disk.
- Recovery semantics: output-only + `Node::setPredictedDepth` prime on load.
- Full CUBE warm-start (Node hypothesis deserialization) is out of scope.
- Dirty-tile tracking in `GeoMapSheet`; periodic save timer in the node.
- Epoch label = ISO-8601 date at node start (fixed for the session; two sessions
  on the same day accumulate into the same LiveFused epoch — intended behavior).

## Approach

### Step 1 — Add dirty-grid tracking to `GeoMapSheet`

`GeoMapSheet::grids_` is a `std::map<gggs::GridIndex, shared_ptr<GeoGrid>>`. `GeoGrid`
has no dirty flag. Add a `std::set<gggs::GridIndex> dirty_grids_` member to
`GeoMapSheet`. In `addSoundings`, after calling `g->insert(soundings)` returns true
for a grid, record that grid's index in `dirty_grids_`. Add public methods:

```cpp
// Returns copy to avoid iterator invalidation during a save.
std::set<gggs::GridIndex> dirtyGridIndices() const;
void clearDirtyGrids();         // called after a successful periodic save
```

No change to `GeoGrid` itself — dirty lives at the sheet level, keyed by `GridIndex`.

### Step 2 — Migrate `cube_bathymetry_node` from `MapSheet` to `GeoMapSheet`

`MapSheet` takes Cartesian soundings in the map frame (post-TF transform).
`GeoMapSheet` takes `GeoSounding` (lat/lon/depth in the `earth` frame). The node must:

1. Change the TF lookup target from `map_frame_` to `"earth"` for sounding
   placement, matching `import_bag`'s per-ping `lookupTransform("earth", ...)` call.
2. Replace `cube::MapSounding` with `cube::GeoSounding` built from the earth-frame
   transformed PointCloud2 (x→longitude, y→latitude, z→depth).
3. Replace `std::shared_ptr<cube::MapSheet> map_sheet_` with
   `std::shared_ptr<cube::GeoMapSheet> geo_map_sheet_`.
4. Replace `publishGrid()` to iterate `geo_map_sheet_->grids()` (same cell iteration
   as before but using `CellAreaIterator` + GGGS cell positions instead of Cartesian
   origin + counts). The published `grid_map::GridMap` uses WGS84 lat/lon → map-frame
   position (via TF lookup to map_frame_ for display, or publish in earth frame and
   let rviz transform).

> Implementation note: `publishGrid()` currently positions cells in map-frame
> Cartesian. After migration, cells are GGGS geographic. The simplest approach: do a
> one-time `lookupTransform("map", "earth", ...)` at publish time, iterate grid cells,
> convert each GGGS cell center to map-frame position. Alternatively, publish the
> depth/uncertainty as a sensor_msgs/PointCloud2 in the earth frame and let consumers
> transform. Decision: keep grid_map output but look up map←earth at publish time.

### Step 3 — Add `store_import::loadEpochTiles()` (new reverse function)

Add to `store_import.h/.cpp` a new function:

```cpp
// Load depth+uncertainty from a BathymetryStore epoch into a GeoMapSheet,
// priming each touched cell's predicted depth via Node::setPredictedDepth.
// Only cells with finite depth are loaded (NaN no-data cells are skipped).
// Iterates the epoch's tiles; for each cell builds or retrieves the GeoGrid
// at the cell's GridIndex (via geo_map_sheet.getOrCreateGridsIn), finds or
// creates the Node at the matching CellIndex, calls setPredictedDepth(depth, variance).
// The GeoMapSheet starts fresh (no CUBE hypothesis state); only the predicted
// surface is seeded.
void loadEpochIntoSheet(
  const marine_bathymetry_store::BathymetryStore & store,
  marine_bathymetry_store::SourceLayer layer,
  const marine_bathymetry_store::Epoch & epoch,
  GeoMapSheet & map_sheet);
```

This is the only new production function. No changes to `BathymetryTile` or
`BathymetryStore`.

### Step 4 — Wire persistence into `cube_bathymetry_node` lifecycle

In `on_configure`:

```cpp
// New parameters (declare before use, ADR-0008):
draft_dir_ = declare_parameter("draft_dir", "");   // empty = persistence disabled
save_interval_s_ = declare_parameter("save_interval", 30.0);
```

Load existing tiles at startup (if `draft_dir_` is set and non-empty):

```cpp
marine_bathymetry_store::BathymetryStore store =
  marine_bathymetry_store::BathymetryStore::fromCellSize(cell_size_);
marine_bathymetry_store::load(store, draft_dir_);
// Get today's epoch (newest in the draft layer):
auto & epochs = store.epochs(marine_bathymetry_store::SourceLayer::Draft);
if (!epochs.empty()) {
  loadEpochIntoSheet(store, SourceLayer::Draft, epochs.rbegin()->first, *geo_map_sheet_);
}
```

Epoch label for new live writes = node-start UTC date, fixed for the session:

```cpp
// Set once in on_configure; format: "YYYY-MM-DD".
epoch_ = currentUtcDateString();   // free function using std::chrono + strftime
```

Periodic save timer (created in `on_configure`, runs in `on_activate`):

```cpp
save_timer_ = create_wall_timer(
  std::chrono::duration<double>(save_interval_s_),
  std::bind(&CubeBathymetry::saveDirtyTiles, this));
```

`saveDirtyTiles()`:

```cpp
void saveDirtyTiles() {
  if (draft_dir_.empty() || !geo_map_sheet_) return;
  auto dirty = geo_map_sheet_->dirtyGridIndices();
  if (dirty.empty()) return;
  // Convert dirty grids to tiles via geoGridToTile, inject into a temporary store,
  // persist via tile_io::save. Pattern: BathymetryStore::set() per cell, or
  // use BathymetryStore::importEpoch with just the dirty tiles.
  // Chosen: iterate dirty GridIndex -> get GeoGrid -> geoGridToTile -> saveTile
  // directly (atomic write via tile_io). This avoids creating a full BathymetryStore
  // just to save N tiles.
  auto ts_ns = current_ros_time_ns();
  for (const auto & idx : dirty) {
    auto grid_ptr = geo_map_sheet_->gridAt(idx);   // new const accessor on GeoMapSheet
    if (!grid_ptr) continue;
    auto tile = cube::geoGridToTile(*grid_ptr, ts_ns, 0 /*source_index*/);
    auto path = draftTilePath(draft_dir_, epoch_, idx);
    marine_bathymetry_store::saveTile(tile, path);  // atomic: temp-then-rename
  }
  geo_map_sheet_->clearDirtyGrids();
}
```

`on_cleanup` / `on_shutdown`: call `saveDirtyTiles()` for a final save before teardown.

### Step 5 — Add `GeoMapSheet::gridAt()` accessor

The save loop needs access to a specific GeoGrid by index without creating it:

```cpp
// Returns nullptr if the grid does not exist (not yet populated).
std::shared_ptr<GeoGrid> gridAt(const gggs::GridIndex & index) const;
```

### Step 6 — Add `draftTilePath()` helper (free function or static)

```cpp
// Returns the path string: "<dir>/draft/<epoch>/<level>_<row>_<col>.tif"
// Creates the directory tree if it does not exist.
std::string draftTilePath(
  const std::string & dir, const std::string & epoch, const gggs::GridIndex & idx);
```

Uses `marine_bathymetry_store::tileFilename(idx)` and `layerDirName(SourceLayer::Draft)`.

### Step 7 — Tests

- `test_geo_map_sheet.cpp`: add `DirtyTracking` test — insert soundings, verify
  `dirtyGridIndices()` non-empty; call `clearDirtyGrids()`, verify empty.
- `test_store_import.cpp`: add `LoadEpochIntoSheet` round-trip test — build a
  GeoMapSheet, save via `mapSheetToEpochTiles` + `tile_io::saveTile`, reload into a
  fresh GeoMapSheet via `loadEpochIntoSheet`, verify `Node::predictedDepth()` for a
  known cell is finite and close to the saved depth.
- Node migration integration test (if an existing node launch test exists): verify
  earth-frame TF path. (Check existing test infrastructure first; if none, leave as
  manual validation per issue review.)

### Step 8 — Design note (in lieu of a full ADR)

Add a short comment block to `store_import.h` documenting the design decision:
"Live CUBE draft tiles are persisted via `marine_bathymetry_store` tile_io
(`draft/<epoch>/` layout, `LiveFused` provenance) so `bathymetry_layer` (#164) and
the sim live loop (#77) read exactly what CUBE writes. The `tile_io` atomic
temp-then-rename write ensures readers never observe a half-written tile. Full Node
hypothesis deserialization is out of scope; recovery primes `setPredictedDepth` only."
This records the decision inline with the code that implements it, keeping it
proportionate (a prose ADR would be warranted if this format were debated across
repos; here the decision is a direct constraint from the owner comment).

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/geo_map_sheet.h` | Add `dirty_grids_` member + `dirtyGridIndices()`, `clearDirtyGrids()`, `gridAt()` declarations |
| `src/geo_map_sheet.cpp` | Implement dirty tracking in `addSoundings()`; implement `clearDirtyGrids()`, `gridAt()` |
| `include/cube_bathymetry/store_import.h` | Add `loadEpochIntoSheet()` declaration + design-decision comment |
| `src/store_import.cpp` | Implement `loadEpochIntoSheet()` |
| `src/cube_bathymetry_node.cpp` | Migrate `MapSheet` → `GeoMapSheet`; add `draft_dir_`, `save_interval_s_`, `epoch_` params; add `save_timer_`, `saveDirtyTiles()`, `draftTilePath()`; wire load in `on_configure`, final save in `on_cleanup` |
| `test/test_geo_map_sheet.cpp` | Add `DirtyTracking` test |
| `test/test_store_import.cpp` | Add `LoadEpochIntoSheet` round-trip test |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | No new file format or manifest. Reuses tile_io exactly. `loadEpochIntoSheet` is the only new function beyond node wiring. |
| Capture decisions, not just implementations | Design decision recorded inline in `store_import.h` (proportionate; owner comment is the authority). |
| A change includes its consequences | Tests included for both new code paths. Node migration changes the TF target — existing soundings integration tests must still pass. |
| Human control and transparency | `draft_dir_` defaults empty (persistence disabled). Operators opt in per deployment. Save interval is a ROS param. |
| Test what breaks | Round-trip test covers the critical save→load→prime path. Dirty-tracking test covers the incremental-save invariant. |
| Improve incrementally | Single PR. Node migration and persistence are co-committed (they are inseparable since `mapSheetToEpochTiles` requires `GeoMapSheet`). |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | Yes | New params (`draft_dir`, `save_interval`) declared via `declare_parameter` in `on_configure`, before use. |
| ADR-0001 (Adopt ADRs) | Watch | Design decision recorded inline in `store_import.h` comment block. A separate ADR is not warranted here since the decision is a direct owner constraint, not a debated architectural choice; the comment makes the rationale durable. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Node migrates from MapSheet to GeoMapSheet | TF lookup changes from `map_frame_←sensor` to `earth←sensor`; `publishGrid()` needs map←earth at display time | Yes — step 2 addresses both |
| `clearGrid()` service resets the map sheet | `geo_map_sheet_` replaces `map_sheet_` throughout, including in `clearGrid()` | Yes — step 2 replaces all `map_sheet_` refs |
| Dirty tracking added to GeoMapSheet | `addSoundings` must record dirty grids even when `insert()` returns false for some grids (only record when true) | Yes — step 1 guards on `insert()` return value |
| `geoGridToTile` called periodically (not just at batch end) | `values()` mutates node state (flushes pre-filter). Periodic calls may affect CUBE convergence on very-fresh data. | Acceptable: same flush behavior as end-of-session export; the pre-filter queue refills after the flush. Documented in `saveDirtyTiles()` comment. |

## Open Questions

- [ ] `publishGrid()` after migration: publish earth-frame PointCloud2 or continue
  using grid_map with a one-time map←earth TF? (Recommendation: grid_map + TF at
  publish time, matching existing consumers. If earth-frame TF unavailable at publish,
  fall back to skipping publish for that cycle — not a hard failure.)
- [ ] Source index for live tiles: use 0 (no registry) or wire a `SourceRegistry` in
  the node? (Recommendation: 0 for this issue; registry wiring is a follow-on once
  `marine_control` device-control is in place.)

## Estimated Scope

Single PR. Steps 1–2 are prerequisites for steps 3–6. All steps are in
`cube_bathymetry` package only; no changes to `marine_bathymetry_store`.
