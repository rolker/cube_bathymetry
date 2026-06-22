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

## Plan Review
**Status**: complete
**When**: 2026-06-21 18:30 +00:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-21/plan.md` at `dafaac2`
**PR**: PR-less (--issue mode)
**Verdict**: changes-requested

### Evaluation
| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Concern | Bundles a node re-architecture (MapSheet→GeoMapSheet) that changes the live collision-avoidance grid output into a persistence issue. Should be split. |
| Issue alignment | Needs work | review-issue explicitly recommended Option 1 (output-only) and split the `setPredictedDepth` prime path to a follow-on; the plan re-bundles the risky path AND adds the migration. |
| File targeting | Needs work | Load/prime path needs new `GeoGrid` API (Node access by CellIndex) not listed in Files-to-Change. |
| Consequences | Needs work | Migration changes published `grid_map` frame/representation; downstream consumers (costmap/camp/rviz keyed on `map_frame`) not in the consequences table. |
| Principle alignment | Concern | "Only what's needed" + "Improve incrementally" — migration is far more than persistence needs. |
| ADR compliance | Good | Inline design note proportionate; ADR-0008 param handling sound. |
| ROS conventions | Needs work | publishGrid fallback "skip publish for that cycle" is unacceptable for a collision-avoidance feed. |

### Findings
- [ ] (must-fix) Central claim VERIFIED but migration mis-scoped: live `cube_bathymetry_node.cpp` does use Cartesian `cube::MapSheet`, ingests `MapSounding(x,y,z)` in `map_frame_`, and publishes `grid_map::GridMap` in `map_frame_` (`cube_bathymetry_node.cpp:64,97,132,142,288`). Migrating to `GeoMapSheet` re-architects the runtime grid that feeds collision avoidance — out of proportion for a persistence issue. Split the migration into its own issue. — `plan.md:44`
- [ ] (must-fix) `loadEpochIntoSheet`/prime path needs new `GeoGrid` API not in the plan: `GeoGrid::nodes_` (`std::map<CellIndex, shared_ptr<Node>>`) is **private** with no accessor; the only public mutator is `insert(GeoSounding)`. `Node::setPredictedDepth(float,float)` exists (`node.h:269`) but is unreachable from a `GeoGrid`/`GeoMapSheet`. Step 3 cannot "find or create the Node at the matching CellIndex" without a new `GeoGrid::setPredictedDepthAt(CellIndex,...)` (lazy-create) method. Add it to Files-to-Change or the step is unimplementable as written. — `plan.md:78`
- [ ] (must-fix) Published-grid breaking change not captured in consequences: after migration `publishGrid()` must do per-cell `map←earth` TF and the cells become geographic. The open question even proposes "fall back to skipping publish for that cycle" — that silently starves the collision-avoidance grid. Consumers keyed on `map_frame` (costmap, camp, rviz; `map_frame` is a launch contract, `cube_bathymetry_in_namespace_launch.py:36`) are not in the consequences table. — `plan.md:59,243`
- [ ] (suggestion) Recommended lighter path = alternative (b): run a parallel `GeoMapSheet` fed the same earth-frame soundings purely for persistence, leaving the existing `MapSheet` and its published grid untouched. Zero risk to the live grid; cost is extra memory + double accumulation. The earth→ECEF→lat/lon→`GeoSounding` ingestion already has precedent in `bag_to_geotiff.cpp:553-572`. This delivers issue #21 (output-only persistence) without re-architecting the runtime node. — `plan.md:44`
- [ ] (suggestion) Alternative (a) (save-time convert the Cartesian `MapSheet` grids to GGGS tiles) is NOT lighter — `BathymetryTile`/`tile_io` are GGGS-`GridIndex`-keyed (`tile_io.hpp:68,78`) and `cube::Grid` has only a Cartesian `origin()`; converting would be a lossy resample/re-bin. Reject (a); prefer (b). — `plan.md:9`
- [ ] (suggestion) Periodic `geoGridToTile` calls `GeoGrid::values()`, which flushes the median pre-filter (per `store_import.h` and the plan's own consequences row). The plan acknowledges this but should add a test asserting periodic-save output equals a single end-of-session export, since it now happens every `save_interval`. — `plan.md:237`

### Summary
The crux claim is correct: the live node really is Cartesian `MapSheet`-based. But the proposed `MapSheet→GeoMapSheet` migration is mis-scoped for issue #21 — it re-architects the runtime grid that feeds collision avoidance, contradicting review-issue's explicit Option-1 (output-only) recommendation. A genuinely lighter path exists (parallel `GeoMapSheet` for persistence only) that leaves the published grid untouched. The plan is not ready for implementation as written.

### Recommended Actions
- [ ] Re-scope #21 to output-only persistence via a **parallel** `GeoMapSheet` (alternative b); do not migrate the runtime node in this issue.
- [ ] Split the `MapSheet→GeoMapSheet` runtime migration (changes the published `grid_map` representation/frame) into its OWN issue with its own consequences analysis and consumer regression plan.
- [ ] If the `setPredictedDepth` prime path stays in #21, add the missing `GeoGrid` Node-access API to Files-to-Change; otherwise defer warm-start to a follow-on per review-issue.

## Plan Authored
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Code Agent (claude-sonnet-4-6)

**Plan**: `.agent/work-plans/issue-21/plan.md` at `904b2e0`
**Branch**: feature/issue-21 at `904b2e0`
**Phases**: single (4-commit sequence within the PR)

**Re-author note**: Prior plan (dafaac2) returned `changes-requested` from plan-review.
Owner decision (handoff brief) confirms the migration IS in scope — it is not mis-scoped
but rather the necessary prerequisite for native-GGGS persistence. The re-authored plan
addresses all three must-fixes:
1. Migration scope confirmed by owner; safety constraint on published contract now
   explicit (Step 2: cached `map←earth` TF, never starvation).
2. `GeoGrid::setPredictedDepthAt` + `GeoMapSheet::primeFromTile` added to Files-to-Change
   (Step 4).
3. Consumer regression table and publish-equivalence test added (Steps 3 + 8).

### Open questions
- [ ] `grid_cell_count_` param: retire silently (no-op + deprecation WARN) or keep as declared no-op for backward compat?
- [ ] Source index for live tiles: use 0 (no registry) or wire a `SourceRegistry`? (Recommendation: 0 for this issue.)

## Plan Review
**Status**: complete
**When**: 2026-06-21 22:25 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-21/plan.md` at `904b2e0`
**PR**: PR-less (--issue mode)
**Verdict**: changes-requested

**Re-review note**: Owner overrode the prior review's "avoid the migration" recommendation.
This re-review accepts the migration as in-scope and verifies it is planned SAFELY. The
prior review's mis-scoping finding is NOT re-litigated. Two NEW must-fixes surfaced that
are independent of the scope question — both are concrete API gaps in the migration's
publish path, the same class of gap the prior review caught for the prime path.

### Evaluation
| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | Migration accepted per owner; the parts (ingestion + publish + load/prime + persistence) are genuinely inseparable for native-GGGS persistence. Single PR with a 4-commit sequence is reasonable. |
| Issue alignment | Good | Addresses #21 (persist draft tiles) plus the owner-mandated migration + warm-start prime. |
| File targeting | Needs work | Publish-projection step relies on a cell-center API that does not exist (see must-fix 1). Otherwise Files-to-Change is complete and accurate. |
| Consequences | Good | Published-grid contract, consumers, cached-TF, pre-filter-flush, dirty-snapshot all enumerated. The flush-per-save row is correct and now de-risked (flush is already status-quo on publish — see finding 4). |
| Principle alignment | Good | Robustness (cached TF), test-what-breaks (publish-equivalence + round-trip), human-control (`draft_dir` opt-in) all addressed. |
| ADR compliance | Good | ADR-0008 param handling sound; inline design note proportionate (ADR-0001 watch). |
| ROS conventions | Needs work | Per-cell ECEF+TF projection loop on the publish path is a latency risk at GGGS scale (960×960/grid); "profile in sim" is necessary but the plan should commit to a batch/raster approach matching the `bag_to_geotiff` precedent rather than per-cell `tf2::doTransform`. |

### Findings
- [ ] (must-fix) Publish projection names a non-existent API. Step 2 (`plan.md:91`) says "Get the cell's center lat/lon from `gggs::CellAreaIterator` / `gggs::Level::cellCenter(CellIndex)`." There is **no `gggs::Level::cellCenter`**. The only per-cell geographic accessor is `CellIndex::position()`, which returns the **south-west corner**, not the center (`cell_index.h:109-111`). The plan's publish path is unimplementable as written. Either add a cell-center helper to the GGGS API (out-of-repo: `unh_marine_autonomy`, a separate PR/issue) or derive the center from `position()` + half a `cellAngularSpan()` and document the half-cell offset. Add the chosen approach to Files-to-Change. — `plan.md:91`
- [ ] (must-fix) Publish projection ignores the established `bag_to_geotiff` precedent and is a real latency risk. The offline tool (`bag_to_geotiff.cpp:605-693`) does NOT project per-cell through ECEF — it builds a lat/lon raster keyed by GGGS `row()`/`column()` with a uniform `cellSizeDegrees()` geotransform, and must explicitly handle GGGS **polar column-stretch (1×/3×/9×)** with per-row interpolation. The plan's "convert each cell center lat/lon→ECEF→`PointStamped`→`tf2::doTransform`→`map.getIndex`" runs that per-cell for up to 960×960 cells per grid on the ~5 s publish cycle — orders of magnitude heavier than today's Cartesian copy and heavier than the offline batch raster. "Profile in sim" (Step 2) is insufficient as the sole mitigation for the live CA feed. The plan must (a) commit to a batched projection (compute the `map←earth` affine once and apply it to cell positions in bulk, mirroring the offline geotransform approach) and (b) state the polar-stretch handling, or explicitly scope to non-polar (lake/coastal) and assert it. — `plan.md:93-102`
- [ ] (suggestion) `saveDirtyTiles()` writes tiles via the low-level `saveTile(tile, path)` free function into a hand-built `draft_dir_/draft/<epoch>/` path, but the load path uses the store-level `marine_bathymetry_store::load(store, draft_dir_)`. These must round-trip: `load()` scans `<dir>/<layer>/<epoch>/<level>_<row>_<col>.tif` and defaults provenance to `live-fused` when the per-epoch `provenance` marker is absent (`tile_io.hpp:131-133`), and `saveTile` does not write that marker or `registry.json`. The combination is workable (load tolerates the missing marker), but the plan should (a) confirm `layerDirName(SourceLayer::Draft) == "draft"` so the hand-built path matches what `load()` scans, and (b) add a save→load round-trip test through the **store** `save()`/`load()` functions, not just `saveTile`/`loadTile`, to pin the layout contract. Prefer calling the store-level `save(store, draft_dir_)` over hand-rolling `saveTile` paths if a transient store is acceptable. — `plan.md:248-269`
- [ ] (suggestion) The `if (!tile.dirty()) continue;` "all-NaN skip" in `saveDirtyTiles()` (`plan.md:262`) is correct but the inline comment's reasoning ("all NaN — skip") is imprecise: `dirty()` is the value-raster dirty flag set by `set()`, not an all-NaN test. It works only because `geoGridToTile` marks dirty iff it wrote a finite cell — which is exactly the established `mapSheetToEpochTiles` precedent (`store_import.cpp:80`). Keep the idiom; fix the comment to reference the precedent. — `plan.md:262`
- [ ] (suggestion) CA-starvation claim is sound but narrower than stated. The cached `map←earth` publish-TF fallback (Step 2) only helps when ingestion **succeeded** (a ping was accumulated) but the publish-time `map←earth` lookup failed. Publish is purely ping-driven (`cube_bathymetry_node.cpp:313-319`; no standalone publish timer), and ingestion still drops the ping on an `earth←sensor` TF miss (Step 1, matching current behavior). So a total TF outage still yields no new publishes — but that is no worse than today, and the cached transform genuinely prevents the *new* failure mode (geographic cells un-projectable at publish). State this scope precisely so the "never starves" claim isn't over-read. — `plan.md:83-91`

### Summary
The migration is now planned much more carefully than the prior (rejected) draft: the
published `grid_map`/`map_frame` contract is explicitly preserved, the prime-path API
(`setPredictedDepthAt`/`primeFromTile`/`loadEpochIntoSheet`) is coherent and in
Files-to-Change, `Node::setPredictedDepth` is verified (`node.h:269`), the ECEF→lat/lon
ingestion matches the `bag_to_geotiff` precedent (`bag_to_geotiff.cpp:553-559`), the
4-commit sequence is buildable-at-each-step, and the pre-filter-flush concern is real but
de-risked (per-publish flush is already status-quo via `Grid::values()`/`GeoGrid::values()`).
However, the **publish projection** — the explicit crux of the migration — has two concrete
defects: it calls a non-existent `gggs::Level::cellCenter` API, and it specifies a per-cell
ECEF+TF loop that ignores the batch-raster precedent and poses a genuine latency risk on the
live CA publish path. These are the same class of unimplementable-as-written API gap the prior
review caught for the prime path. Fix the publish-projection API + approach and the migration
is safe to implement.

### Recommended Actions
- [ ] Replace `gggs::Level::cellCenter` with a real cell-center derivation (`CellIndex::position()` SW-corner + half `cellAngularSpan()`, or a new GGGS helper in a separate `unh_marine_autonomy` PR); add to Files-to-Change.
- [ ] Re-specify the publish projection as a batched map←earth affine applied to cell positions (mirror `bag_to_geotiff.cpp:605-693`), state polar-stretch handling or scope to non-polar, and keep the sim profile gate as a confirmation rather than the sole mitigation.
- [ ] Add a store-level `save()`→`load()` round-trip test (not just `saveTile`/`loadTile`) and confirm `layerDirName(Draft) == "draft"` so the live-write/load layout round-trips.
- [ ] Tighten the CA-starvation wording to the ingestion-succeeded / publish-TF-missed window; fix the `tile.dirty()` comment.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-22
**By**: Claude Code Agent (Claude Opus 4.8)

**Scope**: 12-commit diff `origin/jazzy..feature/issue-21` (HEAD `0580123`),
2376 insertions / 61 deletions across 18 files. Reviewed against the issue (#21
persist GeoMapSheet draft tiles), the owner-mandated full migration
(Cartesian `MapSheet` → geographic `GeoMapSheet`), and the Quality Standard.
**PR**: PR-less (pre-push, `--issue` mode).
**Verdict**: ready-to-push (sim-verification gate still required before merge).

### Plan-review must-fix resolutions — verified in source
- [x] **Cell-center API gap** — `geoMapSheetToGridMap` derives the center from
  `CellIndex::position()` (SW corner) + half `latitudinalSpan()/cellRowCount()`
  and half `longitudinalSpan()/cellColumnCount()`
  (`grid_projection.cpp:90-118`). No fictitious `gggs::Level::cellCenter`. The
  per-grid `longitudinalSpan()` already encodes the GGGS 1×/3×/9× polar
  column-stretch, so centers are correct at any in-envelope latitude with no
  interpolation pass. Confirmed against `cell_index.h` (`position()` = SW corner).
- [x] **Per-cell ECEF+TF loop / latency** — the `map←earth` transform is looked
  up ONCE per publish and converted to a single `Eigen::Isometry3d`; every cell
  reuses that one affine (`grid_projection.cpp:53-66`). No per-cell
  `tf2::doTransform`, no per-cell buffer lookup. Two-pass design caches each
  projected cell once (`ProjectedCell`) so the lat/lon→ECEF→affine work runs
  exactly once per finite cell. Scoped/asserted to non-polar survey latitudes
  (|lat| < 72°), matching the store envelope.
- [x] **Round-trip layout** — persistence verified by `test_persistence.cpp`
  (233 lines) exercising save→load through the store layout.
- [x] **Wording/comment fixes** — CA-starvation scope and `tile.dirty()` comment
  corrected per the plan-review recommendations.

### Evaluation
| Dimension | Verdict | Notes |
|---|---|---|
| Published contract | Good | Topic `grid`, frame `map_frame`, layers `elevation`+`uncertainty` unchanged. Geometry derived from projected-cell bounds + half-cell pad (mirrors legacy Cartesian extent). |
| Correctness | Good | `ProjectionPlacesCellCentersExactly` asserts cell-center placement to ~mm; `GeoProjectionMatchesLegacyMapSheet` compares a multi-cell survey patch against the legacy `MapSheet` path with a half-cell-shift negative control (`shift_frac < true_frac − 0.2`). |
| Robustness | Good | Last-good-TF fallback (`last_publish_tf_`/`have_publish_tf_`) degrades gracefully on a publish-time `map←earth` miss; skips publish only on the first cycle before any TF is cached. |
| Persistence/lifecycle | Good | Save timer gated to `on_activate`/`on_deactivate`; epoch recomputed per save; `clearGrid()` flushes the median accumulator before swap; `draft_dir=""` disables persistence (opt-in, human-control). |
| Tests | Good | 332 tests, 0 failures, 46 skipped. New: publish-equivalence (485 lines), persistence (233), geo_grid/geo_map_sheet/store_import units. |
| Lint | Good | cpplint + uncrustify clean. |

### Residual risks (deferred to follow-up issues, NOT blockers)
- [ ] **Stale-TF staleness bound** — the publish fallback reuses the last good
  `map←earth` with no age bound; during a long TF outage it could misplace cells
  on the live CA grid. No worse than today's drop-on-miss behavior for *new*
  data, but worth an explicit staleness ceiling. → follow-up issue.
- [ ] **Full-grid `values()` densification perf** — each publish allocates a
  dense `vector<DepthAndUncertainty>` per grid (up to 960×960) and the residual
  per-cell ECEF trig runs over the dense set; sparse iteration would cut both. →
  follow-up issue.

### Required before merge (not a code change)
- [ ] **Sim-verification gate** — run the migrated node in sim and confirm
  `/grid` still populates with depths equivalent to the legacy path and that
  `draft_dir` writes round-trippable tiles. This touches the live
  collision-avoidance feed; unit tests (publish-equivalence) are necessary but
  not sufficient. Operator/owner-driven.

### Summary
The migration is implemented faithfully to the re-authored plan: both
publish-projection must-fixes are genuinely resolved in `grid_projection.cpp`
(single cached affine + real cell-center derivation, polar-stretch handled by
construction), the published `map_frame` grid contract is preserved and pinned
by a strengthened equivalence test with a negative control, persistence is
opt-in and lifecycle-gated, and the suite is green and lint-clean. Two residual
risks (stale-TF bound, densification perf) are real but non-blocking and are
being filed as follow-ups. Safe to push; merge remains gated on the sim run.
