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
   average). Add `#include <unordered_map>` and `#include <cstdint>` to
   `geo_grid.h`.
   **[impl amendment]** `#include <map>` is RETAINED, not removed: the public
   `nodeIntensityWelford()` return type is still `std::map<gggs::CellIndex,
   IntensityWelford>` (its CellIndex ordering is part of the eviction-spill
   contract), so `<map>` is still needed. Also audited & updated the non-obvious
   iteration/lookup sites the original step under-counted: `predictedDepthAt`,
   `values`, `nodeRecords`, `setSettledIntensityWelfordAt`, and the
   `nodeIntensityWelford` range-loop (reverse-maps the packed key back to a
   `gggs::CellIndex(index_, key>>16, key & 0xFFFF)` — the must-fix from review).

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
   if no other use remains (done — it was the last gz4d use in the TU); retain
   the equirectangular-accuracy comment.
   **[impl amendment — bit-exact invariant, review suggestion 1]** The box must
   be a SUPERSET of the in-loop distance gate, computed with the SAME
   equirectangular metric. A cell passes the gate only when
   `hypot(dlat_m, dlon_m) < radius`, which forces `|dlat_m| < radius` AND
   `|dlon_m| < radius`; dividing by the gate's own metres-per-degree
   (`kEarthRadiusM*kDeg2Rad` in lat, `× cos_lat` in lon) yields exactly the box
   half-widths, so the box never clips a cell the gate would keep → output
   unchanged. (Documented in-code above the `CellAreaIterator` ctor.) The old
   `radiusFromCenter` used a DIFFERENT (ellipsoidal) metric than the gate, so it
   could clip the gate at high latitude; at the test/Massabesic latitude the four
   settled cells are cluster centres far inside both boxes, so the regression is
   bit-exact (verified).

### Phase B — import_bag: secondary structural fixes

4. **Cache topic→type map per bag** — in `Bag::open()`, call
   `get_all_topics_and_types()` once and store a
   `std::unordered_map<std::string, std::string> topic_types_` in `Bag`.
   Rewrite `Message` ctor to accept a `const std::unordered_map<std::string,
   std::string> &` instead of a reader reference.
   **[impl note — review suggestion 2]** The `Message` ctor has TWO construction
   sites, both inside `Bag` (`open` after first read, and `pop_next`); both now
   pass the cached `topic_types_`. Added `#include <unordered_map>`.

5. **Add `StorageFilter` to `BagReaders`** — accept a
   `std::vector<std::string> topics` parameter in `BagReaders` ctor; set it
   on each `reader->open()` via `rosbag2_storage::StorageFilter` to restrict
   reads to `{/tf, /tf_static, detections_topic, odom_topic}`. Skips sidescan
   imagery that is the bulk of bag bytes.
   **[impl amendment — review suggestions 2 & 3]** Plumbed the filter through
   `Bag::open(file_name, filter_topics)` (not only the `BagReaders` ctor +
   `main()`), since that is where each reader is opened. The filter is NOT a
   static `{/tf, /tf_static, …}` list — tf topic names are namespaced
   (`/bizzy/tf`, `/bizzy/tf_static`) and can't be predicted, and a `StorageFilter`
   matches by EXACT name. Instead it is built from THIS bag's real topic list
   (already cached for item 4): keep any topic that `ends_with("/tf")` /
   `ends_with("/tf_static")` (mirroring the main-loop routing, so transient-local
   `/tf_static` survives) or exactly matches `detections_topic`/`odom_topic`
   (passed from `main()`). Empty result → filter left unset (rosbag2 "read all"),
   the safe fallback for a bag with none of the wanted topics.

6. **Hoist quaternion→matrix conversion** (`import_bag_main.cpp`, per-ping loop) —
   before the inner sounding loop, build the transform frame ONCE per ping and
   apply it as a matvec, dropping the intermediate `geometry_msgs::PointStamped`.
   **[impl amendment — MUST use KDL, not `tf2::Transform`]** The plan originally
   proposed `tf2::fromMsg` + `tf2::Transform * tf2::Vector3`. That is NOT
   bit-exact: `tf2::doTransform` for a point is implemented over **KDL**
   (`gmTransformToKDL(transform) * KDL::Vector(...)`), and a `tf2::Transform`
   matvec builds the rotation from the quaternion differently (btMatrix3x3 vs
   `KDL::Rotation::Quaternion`), perturbing the ECEF low bits and shifting
   boundary soundings between cells — an unexplained tile diff on the host A/B.
   Implemented on the KDL path instead, which IS the exact operation `doTransform`
   performs, just hoisted:
   ```cpp
   const KDL::Frame ping_frame = tf2::gmTransformToKDL(transform);   // once per ping
   // ... per sounding:
   const KDL::Vector sounding_ecef = ping_frame * KDL::Vector(
     s.sonar_relative_position.x, s.sonar_relative_position.y,
     s.sonar_relative_position.z);
   // read via sounding_ecef.x()/.y()/.z()
   ```
   Added `#include "kdl/frames.hpp"`; removed the now-unused
   `#include "geometry_msgs/msg/point_stamped.hpp"`.

7. **Prune `speed_by_ns`** — after draining pings in `drain_pending`, erase
   map entries with key < `drained_ping_ns - kSpeedStalenessNs` (define as
   `constexpr int64_t kSpeedStalenessNs = 30'000'000'000LL` — 30 s). Keeps
   the map small across multi-bag runs; currently grows to 5.7 GB RSS.
   **[impl note]** Output-neutral: pings drain chronologically and `speedAt`
   only ever looks within `kSpeedMaxAgeNs` (5 s) of a ping, so a 30 s window
   never drops a sample that could be the nearest for a current/future ping.
   Added `size_t odom_samples_total` (incremented on ingest) so the end-of-run
   "N odometry samples" diagnostic still reports the true total, not the pruned
   residual `speed_by_ns.size()`.

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

- [x] Use `std::unordered_map<uint32_t, ...>` (O(1) average) or `std::map<uint32_t, ...>`? **Resolved: `unordered_map`** — no caller relies on sorted iteration order; O(1) removes the descent and the comparator entirely.
- [x] Phase B items (4–7) in the same PR as Phase A or a follow-up? **Resolved: same PR** (operator checkpoint 1) — all four in scope.

## Estimated Scope

Single PR.
