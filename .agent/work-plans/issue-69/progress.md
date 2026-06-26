---
issue: 69
---

# Issue #69 — cube_bathymetry: drop per-day epoch, adopt single-fused-grid store API

## Issue Review
**Status**: complete
**When**: 2026-06-26 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #69
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Coordinated follow-on to `unh_marine_autonomy#221` (drop per-day epoch partitioning in
`marine_bathymetry_store`). The issue asks `cube_bathymetry` to migrate from the old
`draft/<epoch>/` API to the new single-fused-grid API.

**Key finding**: `marine_bathymetry_store#221` is already reflected in the checked-out
headers — `epoch.hpp` is absent from the store include dir, and `BathymetryStore::epochs()`
no longer exists. The `cube_bathymetry` package will **not build** against current headers
without this migration. Implementation can proceed immediately.

### Scope Assessment

- **Well-scoped?** Yes. Targets specific files: `cube_bathymetry_node.cpp`,
  `store_import.h`, `store_import.cpp`, and tests (`test_persistence.cpp`,
  `test_store_import.cpp`). Changes are mechanical API migrations with no new
  feature surface.
- **Right repo?** Yes. `cube_bathymetry` is a project sensor package (`sensors_ws/src/`);
  workspace infra is not touched.
- **Dependencies**: `unh_marine_autonomy#221` is listed as the prerequisite store change.
  Based on current checked-out state it is already merged/reflected. No further upstream
  blockers identified.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| A change includes its consequences | OK | Issue explicitly includes test updates; `test_persistence.cpp` and `test_store_import.cpp` both use epoch API heavily and are named as targets |
| Improve incrementally | OK | Focused API migration, no scope creep beyond #221 follow-on |
| Capture decisions, not just implementations | OK | Issue references parent #86 and dependency #221; rationale is clear |
| Workspace vs. project separation | OK | All changes stay within `cube_bathymetry` package |
| Human control and transparency | OK | Log messages that reference "epoch = UTC date at each save" should be cleaned up to reflect new behavior |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0008 — Follow ROS2 Official Conventions | Yes | Lifecycle node patterns (on_activate/on_deactivate, timer management) must be preserved through the migration |
| ADR-0013 — progress.md entry-type vocabulary | Yes | This skill writes the `## Issue Review` entry |
| ADR-0002 — Worktree isolation | Yes | Already in the correct worktree |

### Consequences

- `store_import.h` line 85: docstring references `BathymetryStore::importEpoch` —
  now `importTiles`. Must be updated.
- `store_import.h`/`.cpp`: `mapSheetToEpochTiles` function retains "Epoch" in its name
  but is a local API boundary — rename to `mapSheetToTiles` for consistency with the
  new store model (cosmetic, but prevents confusion during plan review).
- `cube_bathymetry_node.cpp`: `currentUtcDateString()` and all epoch-dir path
  construction must be removed; save path becomes `<draft_dir>/draft/` (flat layer
  dir via `marine_bathymetry_store::layerDirName`).
- Restart-recovery path in `cube_bathymetry_node.cpp`: replace `store.epochs()` +
  newest-epoch selection + `loadEpochIntoSheet(store, layer, epoch, sheet)` with a
  direct `loadIntoSheet(store, layer, sheet)` (load all tiles in the single draft
  layer). `loadEpochIntoSheet` in `store_import.h`/`.cpp` must be updated accordingly
  (drop the `epoch` parameter).
- Log messages referencing "epoch" or "UTC date" should be updated to match the new
  single-fused-grid semantics.

### Recommendations

- The `SourceRegistry` parameter is now present in the `save()`/`load()` tile_io API
  (mandatory `nullptr` is acceptable). Check whether the node's save path passes a
  registry; if not, confirm `nullptr` is intentional and add a brief comment.
- The issue notes "Out of scope: pre-seeding CUBE from `chart`/`processed` priors
  (separate feature, #48)" — confirm the restart-recovery path after migration still
  only loads from `Draft`, not from higher-priority layers.

### Actions
- [ ] Update `store_import.h`/`.cpp`: rename `mapSheetToEpochTiles` → `mapSheetToTiles`; drop `epoch` param from `loadEpochIntoSheet` → rename to `loadIntoSheet`; remove `#include "marine_bathymetry_store/epoch.hpp"`; fix docstring reference from `importEpoch` to `importTiles`.
- [ ] Update `cube_bathymetry_node.cpp`: remove `currentUtcDateString()`, replace epoch-dir construction with flat layer-dir path, replace `store.epochs()` + epoch selection with direct layer load; remove `#include "marine_bathymetry_store/epoch.hpp"`.
- [ ] Update `test_persistence.cpp` and `test_store_import.cpp` to use new flat-layer save/load path and renamed functions.
- [ ] Verify `SourceRegistry` handling in the node's save path is intentional (pass `nullptr` or wire in a registry).
- [ ] Confirm restart-recovery loads from `Draft` layer only (not `Processed`/`Chart`).

## Plan Authored
**Status**: complete
**When**: 2026-06-26 04:45 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-69/plan.md` at `2447c31`
**Branch**: feature/issue-69 at `2447c31`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-26 02:32 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independence: the `## Plan Authored` agent-name "Claude Code Agent" matches
     $AGENT_NAME, but this is a fresh-context dispatch on a different model
     (Sonnet authored, Opus reviewing). Treated as independent — the name-only
     heuristic false-positives under the workspace's uniform agent identity, so
     the `(in-context — author self-review)` annotation is deliberately omitted. -->

**Plan**: `.agent/work-plans/issue-69/plan.md` at `2447c31`
**PR**: PR-less (--issue / file path mode; gh unauthenticated in this environment)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) Test names still carry "Epoch"; re-purpose `LoadEpochIntoSheetMissingEpochIsNoOp` as an empty-store no-op — `test_store_import.cpp:200,245` (plan.md:50-54)
- [ ] (suggestion) `Provenance::LiveFused` arg is dropped, not just renamed — new `importTiles` is 2-arg, `Provenance` gone from store headers — `test_store_import.cpp:215-217` (plan.md:50-54)
- [ ] (suggestion) Extend stale-comment sweep to test files (`<epoch>` paths) — `test_persistence.cpp:23-24,73-74` (plan.md:56-58)
- [ ] (note) Do not touch unrelated `steady_clock` `epoch` locals during the rename — `cube_bathymetry_node.cpp:266,521`
- [ ] (note) review-issue item 4 (SourceRegistry) moot: `saveTile` takes no registry, `load` defaults `registry=nullptr` — no build-break, plan correctly omits it
- [ ] (note) review-issue item 5 satisfied: planned `loadIntoSheet(store, Draft, sheet)` loads Draft only

### Verification performed
- Confirmed against checked-out store headers: `epoch.hpp` absent; `BathymetryStore::epochs()` removed; `importTiles(SourceLayer, std::map<GridIndex,BathymetryTile>)` and `tiles(SourceLayer)` present; `layerDirName`/`saveTile`/`tileFilename` present in `tile_io.hpp`.
- Verified all 5 plan-named files contain the cited epoch sites; grep across the package found no epoch API usage outside them.
