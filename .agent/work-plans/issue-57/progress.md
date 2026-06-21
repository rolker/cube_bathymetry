---
issue: 57
---

# Issue #57 — Bag-replay store importer: detections bag -> CUBE GeoMapSheet -> epoch import (PR-B of unh_marine_autonomy#147)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-21 10:54 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved
**Round**: 1
**Ship**: recommended

**Branch**: feature/issue-57 at `b247a7c`
**Mode**: pre-push
**Depth**: Deep (reason: new cross-repo offline tool + GeoGrid->store-tile conversion + new lib target)
**Must-fix**: 1 (resolved) | **Suggestions**: 2 (1 fixed, 1 filed as follow-up)

Two disjoint-lens Claude adversarial passes. Lens A: CLEAN — the load-bearing
values()<->CellAreaIterator lockstep conversion VERIFIED correct (same single-arg
iterator, same bounds/order; values() cached once; NaN skipped; float->double), determinism
holds, replay chain mirrors bag_to_geotiff -d, 3 tests genuinely pin it. Lens B: 1 must-fix
(store dependency leaked into the core lib -> runtime nodes) + 2 suggestions. 285 tests pass.

### Findings
- [x] (must-fix) store_import.cpp was in the core cube_bathymetry library, so cube_bathymetry_node/detections_to_pointcloud transitively linked the store — moved to a dedicated cube_bathymetry_store_import target; verified node no longer links the store — `CMakeLists.txt`
- [x] (suggestion) rosbag2_cpp used but undeclared in package.xml (pre-existing via bag_to_geotiff) — added — `package.xml`
- [x] (suggestion) nlohmann_json store-export gap forcing a cube-side workaround — filed unh_marine_autonomy#203 to fix the store export; workaround kept until then
