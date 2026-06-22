# Plan: Persist GeoMapSheet draft tiles between sessions

## Issue

https://github.com/rolker/cube_bathymetry/issues/21

## Context

The live `cube_bathymetry_node` currently accumulates detections into a Cartesian
`cube::MapSheet` and publishes `grid_map::GridMap` in `map_frame`. The offline tools
(`bag_to_geotiff`, `import_bag`) already use `cube::GeoMapSheet` + `marine_bathymetry_store`
for persistence.

**Owner decision (2026-06-15, re-confirmed in handoff brief):** Issue #21 will migrate
the node's internal accumulator from `MapSheet` to `GeoMapSheet` so that
`mapSheetToEpochTiles` / `tile_io` can persist live data directly, without a lossy
Cartesian→geographic resample. Persistence is then built on top of the migrated node.

**Critical safety constraint:** The node's published `grid_map::GridMap` MUST stay in
`map_frame` and keep feeding collision avoidance / costmap / camp / rviz unchanged.
The migration changes the INTERNAL accumulation only. `publishGrid()` projects the
geographic GeoMapSheet back to Cartesian `map_frame` at publish time (see Step 2).
A TF failure at publish time MUST NOT drop the grid — it must degrade gracefully by
reusing the last good transform (the `lookupAtOrLatest` pattern already in the node).

This is a sizable re-architecture (GeoMapSheet ingestion path, map←earth publish
projection, load/prime API addition, persistence loop) but lands in a single PR
because the parts are inseparable: `mapSheetToEpochTiles` requires `GeoMapSheet`.

---

## Approach

### Step 1 — Migrate ingestion: MapSheet → GeoMapSheet

**What changes in `pingCallback`:**

Today the node calls `lookupAtOrLatest(map_frame_, msg->header.frame_id, ...)`, transforms
the cloud to map frame, iterates `MapSounding(x, y, z)`, and calls `map_sheet_->addSoundings()`.

After migration:

1. Change TF target to `"earth"` — matching `bag_to_geotiff.cpp:554`'s
   `lookupTransform("earth", sensor_frame, ...)`.
2. For each point in the earth-frame cloud, convert ECEF→lat/lon using
   `gz4d::GeoPointECEF` → `gz4d::GeoPointLatLongDegrees` — exact same chain as
   `bag_to_geotiff.cpp:556–559`.
3. Build `cube::GeoSounding(ll)`, copy vertical/horizontal error fields.
4. Call `geo_map_sheet_->addSoundings(soundings, timestamp)`.

The `map_sheet_` member and `clearGrid()` service both become `geo_map_sheet_` of type
`std::shared_ptr<cube::GeoMapSheet>`. Constructor: `GeoMapSheet(cell_size_)` — the float
`cell_size_` parameter maps directly (GeoMapSheet resolves GGGS level from cell size in
meters at construction). The `grid_cell_count_` parameter is no longer used by the
GeoMapSheet (GGGS grid sizes are fixed at 960×960); remove it or leave as a no-op param
with a deprecation note.

**TF chain for `"earth"`:** sensor → earth is navigated via ROS TF the same way
bag_to_geotiff does: `lookupAtOrLatest("earth", msg->header.frame_id, stamp, transform)`,
then `tf2::doTransform(cloud, earth_cloud, transform)` or per-point
`tf2::doTransform(PointStamped, PointStamped, transform)` (the bag_to_geotiff per-point
form is cleaner for extracting individual lat/lon). Both patterns are valid; the
per-point form avoids repacking the whole cloud.

**Fallback on TF miss:** same as today — log a throttled WARN and return (drop the ping).
The `lookupAtOrLatest` helper is already in the node and handles
`ExtrapolationException` → `TimePointZero` fallback.

### Step 2 — Publish path: GeoMapSheet → map_frame grid_map (the crux)

`publishGrid()` today iterates `map_sheet_->grids()`, reads Cartesian `origin()` + cell
positions, and fills a `grid_map::GridMap` in `map_frame_`. After migration the cells
are geographic (GGGS lat/lon). The published contract — `grid_map::GridMap` in
`map_frame_`, `elevation` + `uncertainty` layers — MUST NOT change.

**Implementation:**

At publish time, look up the `map` ← `earth` transform once (cached per publish call):

```cpp
geometry_msgs::msg::TransformStamped map_from_earth;
bool have_tf = lookupAtOrLatest("map", "earth", now(), map_from_earth);
if (!have_tf) {
  // Degrade gracefully: publish using the last good cached transform.
  // If no cached transform exists yet (startup), skip this publish cycle only.
  if (!have_publish_tf_) { return; }
  map_from_earth = last_publish_tf_;
}
last_publish_tf_ = map_from_earth;
have_publish_tf_ = true;
```

Then for each GGGS grid cell with a finite depth:
1. Get the cell's center lat/lon from `gggs::CellAreaIterator` / `gggs::Level::cellCenter(CellIndex)`.
2. Convert lat/lon → ECEF (`gz4d::GeoPointECEF ecef(gz4d::GeoPointLatLongDegrees(lat, lon, 0))`) 
   → `geometry_msgs::msg::PointStamped` with `"earth"` frame.
3. Apply `tf2::doTransform` with `map_from_earth` → point in map frame.
4. Call `map.getIndex(p, index)` and fill `elevation` / `uncertainty` layers exactly as today.

The published `grid_map::GridMap` geometry (center, length, resolution) is derived from
the geographic bounds of the GeoMapSheet, projected to map frame — equivalent to today's
Cartesian bounds from `map_sheet_->gridBounds()`.

**Cached TF semantics:** `last_publish_tf_` is a `geometry_msgs::msg::TransformStamped`
member; `have_publish_tf_` is a `bool` member. On first publish after `on_configure`,
`have_publish_tf_` is false — if the TF is unavailable then, skip one cycle. After the
first successful lookup, the cached value is reused on any subsequent TF miss.
Log a throttled WARN when falling back to the cached transform.

This guarantees the collision-avoidance grid is never silently starved: a TF outage of
arbitrary length reuses the last good map←earth alignment rather than emitting nothing.

### Step 3 — Consumer regression assertion

**Consumers of the published `/grid` (or `<namespace>/grid`) topic:**
- `bathymetry_layer` costmap plugin (unh_marine_perception#164) — reads `grid_map::GridMap`
  in `map_frame`, elevation + uncertainty layers.
- CAMP bathy overlay (camp#64 / camp#77 area) — reads the same topic.
- rviz `GridMapDisplay` — reads from the published `grid_map_msgs/GridMap`.
- sim live loop (unh_marine_simulation#77) — exercises the node in simulation.
- `clear_grid` service — clears the accumulator; must be updated to reset `geo_map_sheet_`.

**Published representation contract (must stay unchanged):**
- Topic: `grid` (namespaced via launch)
- Message type: `grid_map_msgs/msg/GridMap`
- Frame: `map_frame_` (configurable, default `"map"`)
- Layers: `elevation`, `uncertainty`
- Resolution: `cell_size_` (unchanged)

The publish path above preserves all of these. No launch file changes needed.

**Regression check:** Add an integration test fixture (Step 7) that feeds the same
synthetic soundings through both the Cartesian (MapSheet, pre-migration) and geographic
(GeoMapSheet + publish projection) paths and asserts equivalent elevation/uncertainty
grid output within floating-point tolerance. This pins the equivalence claim.

### Step 4 — Add `GeoGrid::setPredictedDepthAt()` + `GeoMapSheet` pass-through (new API)

`GeoGrid::nodes_` is `std::map<gggs::CellIndex, std::shared_ptr<Node>>` (private, no
accessor). `Node::setPredictedDepth(float depth, float variance)` exists (node.h:269)
but is unreachable from outside without a new GeoGrid method.

Add to `GeoGrid`:

```cpp
/// Seed the predicted depth at @p cell (lazy-create the Node if absent).
/// Used by the on-startup load path to warm-start slope correction from
/// a persisted tile. Does not insert a CUBE hypothesis — CUBE continues
/// accumulating from scratch on top of the seeded prediction surface.
void setPredictedDepthAt(const gggs::CellIndex & cell, float depth, float variance);
```

Add a pass-through to `GeoMapSheet`:

```cpp
/// Seed predicted depths in every grid cell that falls within @p tile's
/// non-NaN cells. Iterates the tile's finite depth cells, finds or creates
/// the corresponding GeoGrid/Node, calls Node::setPredictedDepth.
void primeFromTile(const marine_bathymetry_store::BathymetryTile & tile);
```

(Or equivalently, implement the prime loop in `loadEpochIntoSheet` in store_import —
see Step 5. The pass-through on GeoMapSheet is the cleaner interface because it keeps
GGGS cell iteration inside the geo layer.)

### Step 5 — Add `loadEpochIntoSheet()` to store_import

Add to `store_import.h`/`store_import.cpp`:

```cpp
/// Load depth+uncertainty from every tile of one @p epoch in @p store into
/// @p map_sheet, seeding each touched cell's predicted depth via
/// GeoGrid::setPredictedDepthAt (warm-start for slope correction).
/// Only finite-depth cells are loaded. Does not insert CUBE hypotheses —
/// CUBE accumulates from scratch on top of the seeded prediction surface.
/// Full Node hypothesis deserialization is out of scope.
void loadEpochIntoSheet(
  const marine_bathymetry_store::BathymetryStore & store,
  marine_bathymetry_store::SourceLayer layer,
  const marine_bathymetry_store::Epoch & epoch,
  GeoMapSheet & map_sheet);
```

Implementation: iterate the epoch's tiles (via `store.epochs(layer)` → find epoch →
iterate `EpochTiles::tiles`), call `map_sheet.primeFromTile(tile)` for each.

### Step 6 — Add dirty-grid tracking to `GeoMapSheet`

`GeoMapSheet::grids_` is `std::map<gggs::GridIndex, shared_ptr<GeoGrid>>`. No dirty
flag today. Add:

```cpp
// In GeoMapSheet private section:
std::set<gggs::GridIndex> dirty_grids_;

// Public:
std::set<gggs::GridIndex> dirtyGridIndices() const;  // copy — safe during save iteration
void clearDirtyGrids();
std::shared_ptr<const GeoGrid> gridAt(const gggs::GridIndex & index) const;  // nullptr if absent
```

In `addSoundings`, after `g->insert(soundings)` returns `true` for a grid, record
`dirty_grids_.insert(g->index())`.

### Step 7 — Wire persistence into `cube_bathymetry_node` lifecycle

**New ROS parameters** (declared in `on_configure`, ADR-0008):

```cpp
draft_dir_ = declare_parameter("draft_dir", std::string(""));  // empty = disabled
save_interval_s_ = declare_parameter("save_interval", 30.0);
```

**On-configure load** (if `draft_dir_` non-empty):

```cpp
marine_bathymetry_store::BathymetryStore store =
  marine_bathymetry_store::BathymetryStore::fromCellSize(cell_size_);
marine_bathymetry_store::load(store, draft_dir_);
const auto & draft_epochs = store.epochs(marine_bathymetry_store::SourceLayer::Draft);
if (!draft_epochs.empty()) {
  const auto & newest_epoch = draft_epochs.rbegin()->first;  // ISO date, newest last
  cube::loadEpochIntoSheet(store, marine_bathymetry_store::SourceLayer::Draft,
    newest_epoch, *geo_map_sheet_);
  RCLCPP_INFO(get_logger(), "Loaded draft epoch '%s' from %s",
    newest_epoch.c_str(), draft_dir_.c_str());
}
```

**Epoch label** — set once at configure time, ISO-8601 UTC date:

```cpp
// Free function (or inline lambda) using std::chrono + std::gmtime + strftime:
epoch_ = currentUtcDateString();  // e.g. "2026-06-21"
```

Two sessions on the same day accumulate into the same epoch — the `LiveFused` `set()`
path handles this correctly (newest value wins per cell on periodic save).

**Periodic save timer** (created in `on_configure`, fires while active):

```cpp
save_timer_ = create_wall_timer(
  std::chrono::duration<double>(save_interval_s_),
  std::bind(&CubeBathymetry::saveDirtyTiles, this));
```

**`saveDirtyTiles()`:**

```cpp
void saveDirtyTiles() {
  if (draft_dir_.empty() || !geo_map_sheet_) { return; }
  auto dirty = geo_map_sheet_->dirtyGridIndices();
  if (dirty.empty()) { return; }
  auto ts_ns = get_clock()->now().nanoseconds();
  for (const auto & idx : dirty) {
    auto grid_ptr = geo_map_sheet_->gridAt(idx);
    if (!grid_ptr) { continue; }
    // geoGridToTile calls values() which flushes the pre-filter.
    // Same flush behavior as end-of-session export; queue refills after flush.
    auto tile = cube::geoGridToTile(*grid_ptr, ts_ns, /*source_index=*/0);
    if (!tile.dirty()) { continue; }  // all NaN — skip
    auto dir = draft_dir_ + "/draft/" + epoch_;
    std::filesystem::create_directories(dir);
    auto path = dir + "/" + marine_bathymetry_store::tileFilename(idx);
    marine_bathymetry_store::saveTile(tile, path);  // atomic: temp-then-rename
  }
  geo_map_sheet_->clearDirtyGrids();
}
```

**`on_cleanup` / `on_shutdown`:** call `saveDirtyTiles()` for a final save.

**`clearGrid()` service:** reset to a fresh `GeoMapSheet(cell_size_)` and clear
`dirty_grids_`. Same semantics as today but for the new accumulator type.

### Step 8 — Tests

1. **`test_geo_map_sheet.cpp` — `DirtyTracking`:** insert soundings → verify
   `dirtyGridIndices()` non-empty; call `clearDirtyGrids()` → verify empty.
   Also test `gridAt()`: existing index returns non-null; absent index returns null.

2. **`test_geo_map_sheet.cpp` — `PrimeFromTile`:** build a sheet, add soundings, call
   `geoGridToTile` + `primeFromTile` on a fresh sheet, verify that `predictedDepth()` on
   touched nodes is finite and close to the tile's depth.

3. **`test_store_import.cpp` — `LoadEpochIntoSheet`:** build a GeoMapSheet, call
   `mapSheetToEpochTiles` → inject tiles into a BathymetryStore epoch →
   call `loadEpochIntoSheet` into a fresh GeoMapSheet → verify `predictedDepth()` for a
   known cell is finite and within tolerance of the saved depth.

4. **`test_geo_grid.cpp` — `SetPredictedDepthAt`:** call `setPredictedDepthAt` on a fresh
   GeoGrid → verify the node is created (via a subsequent insert that uses slope
   correction) and `predictedDepth()` returns the seeded value. Accessing the node
   externally is not required; test via the Node accessor on a locally constructed Node
   as proof of the lazy-create path. (Alternatively add a test-only accessor in geo_grid
   as `friend` — match the pattern in `node.h`'s `NodeNominationTestAccess`.)

5. **Regression (publish equivalence):** a unit-level test that feeds the same synthetic
   soundings through both the old MapSheet path and the new GeoMapSheet + publish-
   projection path, and asserts that populated elevation cells in the resulting
   grid_map agree within a tolerance of `~cell_size / 2` (position) and `0.1 m`
   (depth). This is a sim-free integration test; it does not require a live TF tree —
   use a fixed identity `map←earth` transform (e.g. at a known test latitude).

### Step 9 — Design note (inline documentation)

Add a comment block to `store_import.h` (or `cube_bathymetry_node.cpp` near the
parameters) documenting the design decision:

> Live CUBE draft tiles are persisted via `marine_bathymetry_store` `tile_io`
> (`draft/<epoch>/` layout, `LiveFused` provenance, atomic temp-then-rename) so that
> `bathymetry_layer` (#164) and the sim live loop (#77) read exactly what CUBE writes.
> The node migrates its accumulator from `MapSheet` to `GeoMapSheet` (geographic) so
> that `mapSheetToEpochTiles` / `geoGridToTile` can save directly with no lossy
> Cartesian→geographic resample. The published `grid_map::GridMap` in `map_frame` is
> preserved by projecting each GeoMapSheet cell center back to `map_frame` at publish
> time via the `map←earth` TF. Full Node hypothesis deserialization is out of scope;
> restart-recovery primes `setPredictedDepth` only (warm-start slope correction).

---

## Commit sequence (recommended, keeps build green at each step)

1. **Step 4 only** — `GeoGrid::setPredictedDepthAt` + `GeoMapSheet::primeFromTile` +
   test. No node change. Build green.
2. **Steps 1+2+3** — node migration (ingestion + publish path + clearGrid + cached TF
   member). Build green; sim verification gate: run the node in simulation and confirm
   `/grid` still populates rviz with equivalent depths. This is the highest-risk commit.
3. **Steps 5+6** — `loadEpochIntoSheet` + dirty-tracking + `gridAt` + tests.
4. **Steps 7+8+9** — persistence wiring + `saveDirtyTiles` + params + remaining tests.

---

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/geo_grid.h` | Add `setPredictedDepthAt(CellIndex, depth, variance)` declaration |
| `src/geo_grid.cpp` | Implement `setPredictedDepthAt` (lazy-create node, call `node->setPredictedDepth`) |
| `include/cube_bathymetry/geo_map_sheet.h` | Add `dirty_grids_`, `dirtyGridIndices()`, `clearDirtyGrids()`, `gridAt()`, `primeFromTile()` declarations; add `#include <set>` and `marine_bathymetry_store/bathymetry_tile.hpp` |
| `src/geo_map_sheet.cpp` | Implement dirty tracking in `addSoundings()`; implement `clearDirtyGrids()`, `gridAt()`, `primeFromTile()` |
| `include/cube_bathymetry/store_import.h` | Add `loadEpochIntoSheet()` declaration + design-decision comment block |
| `src/store_import.cpp` | Implement `loadEpochIntoSheet()` via `primeFromTile` |
| `src/cube_bathymetry_node.cpp` | Migrate `MapSheet`→`GeoMapSheet`; earth-frame TF ingestion; publish projection with cached `map←earth` TF; `last_publish_tf_` + `have_publish_tf_` members; `draft_dir_`, `save_interval_s_`, `epoch_` params; `save_timer_`, `saveDirtyTiles()`; load in `on_configure`; final save in `on_cleanup`; `clearGrid()` updated |
| `test/test_geo_grid.cpp` | Add `SetPredictedDepthAt` test |
| `test/test_geo_map_sheet.cpp` | Add `DirtyTracking`, `PrimeFromTile` tests |
| `test/test_store_import.cpp` | Add `LoadEpochIntoSheet` round-trip test |

No changes to `marine_bathymetry_store` (it is a dependency, not modified here).

---

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | No new file format or manifest; reuses tile_io exactly. `setPredictedDepthAt`/`primeFromTile`/`loadEpochIntoSheet` are the only new functions beyond node wiring. |
| Capture decisions, not just implementations | Design decision recorded inline in `store_import.h`. |
| A change includes its consequences | Published grid contract explicitly preserved (Step 2+3). Consumers enumerated. Tests cover the critical paths. |
| Human control and transparency | `draft_dir_` defaults empty (persistence disabled). Operators opt in per deployment. Save interval is a ROS param. |
| Test what breaks | Round-trip test + dirty-tracking test + publish-equivalence test cover all new code paths. |
| Improve incrementally | 4-commit sequence keeps build green at each step. Sim gate before merging the migration commit. |
| Robustness / no silent failures | TF outage at publish degrades to cached transform with a throttled WARN — never silently starves the collision-avoidance grid. |

---

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | Yes | New params (`draft_dir`, `save_interval`) declared via `declare_parameter` in `on_configure`, before use. |
| ADR-0001 (Adopt ADRs) | Watch | Design decision recorded inline in `store_import.h`; a prose ADR is not warranted here since the decision is a direct owner constraint (not a debated architectural choice), but the inline comment makes the rationale durable. |

---

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Node accumulator: `MapSheet`→`GeoMapSheet` | TF lookup target changes `map_frame_←sensor` → `earth←sensor`; all `map_sheet_` refs replaced by `geo_map_sheet_` (pingCallback, clearGrid, publishGrid, on_configure) | Yes — Step 1 |
| `publishGrid()`: Cartesian origin → geographic cell centers | Map center/length computed from projected cell positions; `map_frame_` contract unchanged | Yes — Step 2 |
| Published grid may use cached TF after outage | Log throttled WARN; never drop grid | Yes — Step 2 |
| `clearGrid()` service resets accumulator | Resets `geo_map_sheet_` (same geometry); clears `dirty_grids_` | Yes — Step 7 |
| `grid_cell_count_` param no longer governs sheet | Retain as no-op or deprecate; GGGS grid is always 960×960 | Yes — Step 1 (noted) |
| Periodic `geoGridToTile()` calls `values()`, flushing pre-filter | Flush on each periodic save is same behavior as end-of-session export; queue refills after flush; documented in `saveDirtyTiles()` comment | Yes — documented in Step 7 |
| `geo_map_sheet_->dirtyGridIndices()` snapshot used during save | Copy is taken to avoid iterator invalidation while saving; `clearDirtyGrids()` called only after successful tile writes | Yes — Step 6 |
| Consumers (costmap, camp, rviz, sim) | Published frame/type/layers unchanged; regression test pins equivalence | Yes — Step 3 + Step 8 |

---

## Open Questions

- [ ] `grid_cell_count_` param: silently retire (no-op, log deprecation) or keep as a
  named no-op parameter for backward compat? Recommendation: keep declared with a WARN
  log that it is ignored post-migration (avoids breaking existing launch files that set it).
- [ ] Source index for live tiles: use `0` (no registry) or wire a `SourceRegistry`?
  Recommendation: `0` for this issue; registry wiring is a follow-on once
  `marine_control` device-control is in place.

---

## Estimated Scope

Single PR. Four commits (see commit sequence above). All changes are in
`cube_bathymetry` only; no changes to `marine_bathymetry_store`. Estimated ~400–500
lines of production code + ~200 lines of tests. The highest-risk step (node migration,
commit 2) has an explicit sim-based verification gate before merging.
