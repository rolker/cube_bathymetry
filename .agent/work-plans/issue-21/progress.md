---
issue: 21
---

# Issue #21 — Persist GeoMapSheet draft tiles between sessions

## Issue Review
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (claude-sonnet-4-6)

**Issue**: #21
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-more-detail

### Summary of findings

Issue #21 proposes adding a NEW per-tile GeoTIFF/binary persistence format +
manifest to `GeoMapSheet` so that live CUBE data survives restarts. This was
authored before `marine_bathymetry_store` existed. Since then:

- `marine_bathymetry_store` (merged, core_ws) has per-tile GeoTIFF persistence
  (`tile_io.hpp`), a dirty-flag mechanism on `BathymetryTile`, and a
  `save()`/`load()` free-function pair that writes incremental (dirty-only) tiles.
- `import_bag` (cube_bathymetry#57, merged) already provides the
  `GeoMapSheet → BathymetryStore` conversion via `mapSheetToEpochTiles` +
  `importEpoch`. The `store_import.h` path is one-way: it calls `grid.values()`
  which **mutates node state** (flushes the median pre-filter) and produces
  output-only `DepthAndUncertainty` — there is no reverse path that would
  reconstruct live CUBE `Node` hypothesis state from a saved tile.
- The owner comment (2026-06-15) explicitly endorses the store-format approach:
  "Persist the GeoMapSheet as `marine_bathymetry_store` `draft/` tiles by reusing
  that package's `tile_io`" with atomic writes, so the `bathymetry_layer` costmap
  plugin (#164) and the sim live loop (#77) read exactly what cube writes.

The original approach (invent a parallel format + manifest in cube_bathymetry)
is now **redundant with the store** and would create two competing tile formats
on disk. The issue needs to be reframed before plan-task starts.

### Scope Assessment

**Well-scoped?** No — the issue proposes a standalone format that the owner has
since superseded with the store-based approach. The scope, API, and semantics
need to be rewritten before implementation. A single PR is achievable once the
approach is settled.

**Right repo?** Yes — cube_bathymetry owns `GeoMapSheet`/`GeoGrid` and the live
CUBE node; the persistence trigger belongs here. The `marine_bathymetry_store`
dependency is already present in `package.xml` (added by #57).

**Dependencies**: rolker/unh_marine_autonomy#164 (bathymetry_layer costmap plugin
that reads tiles written by this issue), rolker/unh_marine_simulation#77 (sim
live loop). Both are consumers, not blockers — this issue can proceed
independently.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Only what's needed | Action needed | Issue proposes a NEW format when `tile_io` already exists and the owner comment mandates using it. Building a parallel format violates this principle. |
| Capture decisions, not just implementations | Action needed | The reframing (store-format draft tiles, atomic writes) appears only in a comment; the issue body still describes the old format. This design decision should be recorded in an ADR or in the issue body before work begins. |
| A change includes its consequences | Watch | On-disk tile layout adds a backward-compat obligation. Must document the tile layout is `draft/<epoch>/` so the costmap layer (#164) and sim (#77) know where to find live tiles. |
| Human control and transparency | Watch | Live-session tiles in `draft/<epoch>/` vs. end-of-day compacted tiles: the epoch naming convention for live sessions (e.g., today's date ISO label, or a running label) must be explicit so a restart does not create a new orphan epoch. |
| Improve incrementally | OK | Once reframed, a single PR for the live-save loop in cube_bathymetry_node is achievable. |
| Test what breaks | Watch | Restart-recovery test needed: verify that a live node reseeded from stored tiles produces the same output map as an uninterrupted session. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 (Adopt ADRs) | Yes | The design choice "persist GeoMapSheet as store-format draft tiles, LiveFused provenance, atomic writes" is a decision that future agents need to understand. Should be recorded — either as a new cube_bathymetry ADR or as an amendment to unh_marine_autonomy ADR-0002. |
| ADR-0008 (ROS 2 conventions) | Watch | Any new ROS 2 parameter for persistence dir / save interval must follow the parameter declaration conventions. |

### Key Open Question: store-overlap reframing

The critical design question the operator must answer before plan-task:

**What semantics does live restart-recovery actually need?**

Two distinct cases:

1. **Output-only recovery** (collision avoidance, coverage, planning read the
   depth/uncertainty values): the store's `load()` path suffices — on restart,
   load the last saved draft epoch into a fresh `BathymetryStore`, then query it
   from the costmap layer. The `GeoMapSheet` starts empty; the store serves
   coverage/collision consumers directly. No re-seeding of CUBE nodes.

2. **CUBE state recovery** (re-seed CUBE nodes with saved depth so CUBE continues
   accumulating from where it left off, not from scratch): this requires the
   reverse path — tile → GeoGrid/Node — which does NOT currently exist.
   `store_import` is one-way (`values()` flushes the pre-filter; there is no
   `Node` deserializer). Implementing this path is substantially more work and
   carries correctness risk (the CUBE hypothesis list, queue state, and
   pre-filter contents are not preserved in a depth+uncertainty tile).

   A practical compromise: on restart, call `Node::setPredictedDepth()` from the
   loaded tile depth to warm-start the CUBE node's prediction surface, then
   continue accumulating. This does NOT reconstruct hypothesis state, but it
   does give the slope-correction path a prior and allows CUBE to converge
   faster on replayed areas. This mechanism exists (`setPredictedDepth` is
   already public) but is not wired in production yet (per the node.h comment).

**Recommendation**: implement Option 1 first (store-format output tiles, atomic
writes, costmap reads them directly) and treat Option 2 / warm-start as a
follow-on issue. This matches the owner comment's framing, is the lower-risk
path, and is immediately useful for collision avoidance and coverage.

### Consequences

- The epoch label for live sessions must be defined. ISO-8601 local date is the
  natural choice (matching the existing epoch convention), but two back-to-back
  sessions on the same day will share an epoch label — the `set()` path handles
  this correctly (LiveFused writes accumulate), but it must be documented.
- `bathymetry_layer` (#164) and sim loop (#77) should be tested against tiles
  written by the live node, not just by `import_bag`.
- No `.agents/README.md` exists in cube_bathymetry; this issue is a good
  opportunity to create one (separate issue recommended, not a side effect here).

### Actions
- [ ] Rewrite the issue body to reflect the store-format approach: drop the
  "new format + manifest" proposal; replace with "write store-format draft
  tiles using `tile_io`, atomic temp-then-rename, LiveFused provenance".
- [ ] Decide and document the epoch labeling convention for live sessions (ISO
  date? rolling label?).
- [ ] Decide which recovery semantics to implement in this issue: output-only
  (store loads for consumers) vs. CUBE warm-start via `setPredictedDepth`.
  Recommendation: output-only first; warm-start as follow-on.
- [ ] Record the design decision (store-format draft tiles, atomic writes) in
  an ADR (cube_bathymetry ADR-0001 or unh_marine_autonomy ADR-0002 amendment)
  before or alongside implementation.
- [ ] Ensure plan includes atomic-write (temp-then-rename) for each tile so the
  costmap reader never observes a half-written tile (owner comment requirement).

## Plan Authored
**Status**: complete
**When**: 2026-06-21 17:00 +00:00
**By**: Claude Code Agent (claude-sonnet-4-6)

**Plan**: `.agent/work-plans/issue-21/plan.md` at `dafaac2`
**Branch**: feature/issue-21 at `dafaac2`
**Phases**: single

### Open questions
- [ ] `publishGrid()` after migration: publish earth-frame PointCloud2 or continue grid_map with map←earth TF at publish time?
- [ ] Source index for live tiles: use 0 (no registry) or wire a `SourceRegistry`?
