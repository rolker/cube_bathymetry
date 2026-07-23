# Plan: GeoGrid::insert CPU-bound optimization

## Issue

https://github.com/rolker/cube_bathymetry/issues/107

## Context

Full-campaign `import_bag` regens pin one CPU core for ~2 h on 32 bags / ~2.6B
soundings. GDB sampling shows 8/8 samples inside `GeoGrid::insert`, with 6/8 in
`std::map<gggs::CellIndex,...>` tree descent and its fat `operator<` (calls
`valid()` → `GridIndex::valid()` → `columnCount`/`latitudeScaleFactor` ~20×
per descent, billions of times). One sample is in `tan` inside
`gz4d::BoundsDegrees::radiusFromCenter`. The `live` node and `batch_regen`
share this code path and benefit equally.

## Approach

### Phase A — geo_grid: eliminate the dominant map overhead

1. **Fix triple tree-descent in `insert`** (`geo_grid.cpp:102-105`) — store a
   reference from the first `operator[]` and reuse it:
   ```cpp
   auto & node = nodes_[*i];
   if (!node) { node = std::make_shared<Node>(); }
   inserted = node->insert(distance, sounding, parameters_) || inserted;
   ```
   Apply the same one-reference pattern to `setPredictedDepthAt` (line 119-121)
   and `setSettledDepthAt` (line 130-132) for consistency.

2. **Switch `nodes_` to `unordered_map<uint32_t, ...>`** — replace
   `std::map<gggs::CellIndex, std::shared_ptr<Node>>` with
   `std::unordered_map<uint32_t, std::shared_ptr<Node>>`. Add a private helper:
   ```cpp
   static uint32_t nodeKey(const gggs::CellIndex & c) noexcept {
     return (static_cast<uint32_t>(c.row()) << 16) | c.column();
   }
   ```
   Update every `nodes_[*i]`, `nodes_[cell]`, and `nodes_.find(...)` call site
   to use `nodeKey()`. All existing callers use `CellAreaIterator` or pass a
   `gggs::CellIndex`; no caller relies on sorted iteration order. Eliminates
   the O(log n) red-black descent and the fat `operator<` entirely (→ O(1)
   average). Add `#include <unordered_map>` to `geo_grid.h`; remove `#include <map>`.

3. **Replace `radiusFromCenter` with equirectangular bounds**
   (`geo_grid.cpp:74-81`) — hoist `cos_lat` computation before the bounds,
   then derive lat/lon deltas directly without `tan`/ellipsoid math:
   ```cpp
   const double cos_lat = std::cos(geo_sounding.latitude * kDeg2Rad);
   const double delta_lat_deg = radius / kEarthRadiusM * kRad2Deg;
   const double delta_lon_deg = radius / (kEarthRadiusM * cos_lat) * kRad2Deg;
   gggs::CellAreaIterator i(index_,
     gggs::geoPoint(geo_sounding.latitude - delta_lat_deg,
                    geo_sounding.longitude - delta_lon_deg),
     gggs::geoPoint(geo_sounding.latitude + delta_lat_deg,
                    geo_sounding.longitude + delta_lon_deg));
   ```
   Move `kDeg2Rad`, `kEarthRadiusM`, and add `kRad2Deg` to the earlier
   `static constexpr` block. Remove `#include "marine_autonomy/gz4d_geo.h"`
   if no other use remains; retain the equirectangular-accuracy comment.

### Phase B — import_bag: secondary structural fixes

4. **Cache topic→type map per bag** — in `Bag::open()`, call
   `get_all_topics_and_types()` once and store a
   `std::unordered_map<std::string, std::string> topic_types_` in `Bag`.
   Rewrite `Message` ctor to accept a `const std::unordered_map<std::string,
   std::string> &` instead of a reader reference.

5. **Add `StorageFilter` to `BagReaders`** — accept a
   `std::vector<std::string> topics` parameter in `BagReaders` ctor; set it
   on each `reader->open()` via `rosbag2_storage::StorageFilter` to restrict
   reads to `{/tf, /tf_static, detections_topic, odom_topic}`. Skips sidescan
   imagery that is the bulk of bag bytes.

6. **Hoist quaternion→matrix conversion** (`import_bag_main.cpp:794`) — before
   the inner sounding loop, pre-build `tf2::Transform`:
   ```cpp
   tf2::Transform tf_mat;
   tf2::fromMsg(transform.transform, tf_mat);
   ```
   Replace the per-sounding `tf2::doTransform(sounding_re_sensor, sounding_ecef,
   transform)` call with:
   ```cpp
   tf2::Vector3 p_ecef = tf_mat *
     tf2::Vector3(s.sonar_relative_position.x,
                  s.sonar_relative_position.y,
                  s.sonar_relative_position.z);
   ```
   Drop the intermediate `geometry_msgs::PointStamped` wrapper.

7. **Prune `speed_by_ns`** — after draining pings in `drain_pending`, erase
   map entries with key < `drained_ping_ns - kSpeedStalenessNs` (define as
   `constexpr int64_t kSpeedStalenessNs = 30'000'000'000LL` — 30 s). Keeps
   the map small across multi-bag runs; currently grows to 5.7 GB RSS.

### Phase C — verification

8. **Bit-exact regression test in `test_geo_grid.cpp`** — insert a fixed batch
   of GeoSoundings into a `GeoGrid`, capture `values()` output before the
   change (as expected constants), assert byte-for-byte match after. Mirrors
   the #63 discipline: identical tiles or explained diffs.

9. **Before/after wall-clock + RSS run** on the same 32-bag Massabesic set.
   Record results in progress.md.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/geo_grid.h` | Switch `nodes_` to `unordered_map<uint32_t,...>`; add `nodeKey()`; update includes |
| `cube_bathymetry/src/geo_grid.cpp` | Fix triple descent; replace `radiusFromCenter`; reorder `cos_lat` computation; update `nodeKey()` callsites |
| `cube_bathymetry/src/import_bag_main.cpp` | Cache topic-type map; StorageFilter; hoist TF matrix; prune `speed_by_ns` |
| `cube_bathymetry/test/test_geo_grid.cpp` | Add bit-exact regression test for bulk insert |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Bit-exact regression test in step 8 enforces no output drift |
| Only what's needed | Phase B items are structural fixes; each bounded to known profiled waste |
| Test what breaks | Regression test catches any correctness regression in the hot path |
| Improve incrementally | Phase A (geo_grid) can land and be timed independently of Phase B |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (CUBE math fidelity) | Yes | Equirectangular bounds accuracy already established by the #63 comment; same justification covers bounds |
| ADR-0018 (local-first CI) | Yes | Unit test added; before/after timing done locally before push |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `nodes_` type in `geo_grid.h` | All `nodes_[...]` / `nodes_.find(...)` call sites in `geo_grid.cpp` | Yes — step 2 |
| Remove `gz4d_geo.h` include | Confirm no other use in `geo_grid.cpp` (currently only `radiusFromCenter`) | Yes — step 3 |
| `BagReaders` ctor signature | Call site in `main()` at line 548 | Yes — step 5 |

## Open Questions

- [ ] Use `std::unordered_map<uint32_t, ...>` (O(1) average) or `std::map<uint32_t, ...>` (O(log n), simpler swap, cheaper comparator)? Recommendation: unordered_map — iteration order not needed by any caller; O(1) removes the descent entirely.
- [ ] Phase B items (4–7) in the same PR as Phase A or a follow-up? Recommendation: same PR — they share the issue, the verification run covers both, and the scope is still one logical batch.

## Estimated Scope

Single PR.
