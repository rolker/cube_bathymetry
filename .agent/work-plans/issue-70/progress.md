---
issue: 70
---

# Issue #70 — Tile eviction + incremental publish to bound long-duration growth

## Issue Review
**Status**: complete
**When**: 2026-06-26 14:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #70
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

The issue identifies two concrete unbounded-growth problems with a shared root
cause (PoC-era design carried forward through the #21 GGGS migration) and
proposes bounded, actionable fixes for each. The problems are real: the
monolithic publish caused a confirmed deployment incident (udp_bridge saturation,
2026-06-10). Bundling both problems in one issue is defensible — they share the
same structural cause and the fixes are complementary — though an implementer
may choose to split into separate PRs for reviewability.

The dependency on #69 (single-fused-grid store adaptation) is explicit in the
issue; #69's progress.md shows a "Local Review (Pre-Push): approved" entry dated
2026-06-26, so the dependency appears nearly ready but must be confirmed landed
before implementation of #70 begins.

**Right repo**: Yes — cube_bathymetry is a project repo; the worktree is
correctly scoped to `sensors_ws/src/cube_bathymetry`.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Only what's needed | OK | Fixes concrete operational failures (RAM growth, bandwidth saturation), not speculative |
| Test what breaks | OK | Issue explicitly calls for a bounded-RSS / bounded-tile-count test on a long synthetic track |
| A change includes its consequences | Watch | Retiring monolithic `GridMap` publish changes the Nav2 costmap consumer's input — the issue notes a "bounded live CA grid" but decoupling plan needs detailing in the plan |
| Capture decisions, not just implementations | Action needed | Eviction policy (LRU vs. distance-from-vessel, budget size) and per-tile output message format are non-trivial design choices that belong in an ADR |
| Improve incrementally | Watch | Two distinct changes bundled; consider whether eviction and incremental publish can be PRed separately while keeping the acceptance criterion testable |
| Safety First (project) | Watch | Nav2 CA grid will shift from whole-area to windowed view — plan must specify behavior at the boundary (e.g., unknown cells default to occupied or free) |
| Modularity and Decoupling (project) | OK | Per-tile incremental output naturally decouples survey persistence from live display and boat→CAMP transmission |
| Simulation-First Validation (project) | OK | Acceptance criterion calls for long-duration replay/sim |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0001 — Adopt ADRs | Yes | Eviction policy (budget, LRU vs. vessel-proximity), per-tile output schema, and CA-grid windowing strategy are design decisions that need an ADR or structured rationale |
| 0002 — Worktree isolation | Yes (satisfied) | Already in issue-cube_bathymetry-70 worktree |
| 0008 — ROS 2 conventions | Yes | Per-tile output (new message type or existing GridMap per tile) must follow ROS 2 message/topic conventions; topic namespace and QoS should be documented |

### Consequences

- Retiring monolithic `GridMap` publish → existing subscribers (Nav2, RViz,
  any downstream visualization) must migrate to the new per-tile stream or the
  windowed CA grid; migration notes needed in the PR
- New per-tile output format gates the boat→CAMP live coverage view
  (rolker/unh_marine_autonomy#86, #250) — the tile schema chosen here becomes
  a shared interface contract
- Eviction policy adds runtime parameters (budget, distance threshold) → parameter
  documentation and default values need to be specified
- `test_publish_equivalence.cpp` tests the existing monolithic path — it will need
  adaptation once the monolithic publish is retired

### Actions
- [ ] Confirm #69 is merged before implementation begins
- [ ] Record eviction policy and per-tile output schema in an ADR (or issue-level
  design note committed to the branch) before implementing
- [ ] Plan must specify Nav2 CA-grid windowing behavior (unknown-cell default) to
  preserve safety guarantees
- [ ] Address monolithic-publish consumer migration in the PR (Nav2 costmap,
  visualization) — or explicitly scope out-of-scope subscribers and note them
- [ ] Update `test_publish_equivalence.cpp` (or replace/extend it) to cover the
  new per-tile path as part of the same PR

## Plan Authored
**Status**: complete
**When**: 2026-06-26 15:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-70/plan.md` at `bf81a78`
**Branch**: feature/issue-70 at `bf81a78`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-26 22:56 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-70/plan.md` at `bf81a78`
**PR**: PR-less (`--issue 70`, layer worktree)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) New project ADR numbered `0008` collides with workspace ADR-0008 (ROS 2 conventions), which the plan's own ADR Compliance table cites — and it is the project's *first* ADR (`docs/decisions/` is empty). Renumber to `0001` (or the intended project start) before writing it. — `plan.md:24`, `plan.md:79`, `plan.md:104`
- [ ] (suggestion) Eviction is driven from `saveDirtyTiles()`, which early-returns when `draft_dir_` is empty (persistence disabled). In that mode no save runs, so `evictColdTiles()` never fires and `grids_` stays unbounded — the original bug survives. Make the persistence-disabled behavior explicit (evict anyway with data loss, or document that bounding requires persistence). — `plan.md:38`
- [ ] (suggestion) The plan does not tie `max_resident_tiles` (default 64) to `ca_window_radius_m`. If the budget is smaller than the tile span of the CA window, evicted-but-on-disk tiles render as NaN/lethal *inside* the avoidance window (safe direction, but spurious over-lethality degrades the live CA view). State the coherence constraint: resident budget ≥ tiles spanning the window. — `plan.md:46`, `plan.md:33`
- [ ] (suggestion) Lazy warm-start reload on tile revisit is asserted but the revisit-detection / per-tile on-demand reload mechanism is not designed (`loadIntoSheet` runs once at on_configure for the whole draft). Plan marks it "optional" and eviction correctness does not depend on it, so this is fine to defer — but say so explicitly rather than implying a path that exists. — `plan.md:38`
- [ ] (suggestion) ADR path is written two ways — `docs/decisions/0008-…` (step 1) vs `cube_bathymetry/docs/decisions/0008-…` (Files table). Both resolve to the package `docs/decisions/`; unify for clarity. — `plan.md:24`, `plan.md:79`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-27 07:53 -0400
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-70 at `9977ea3`
**Mode**: pre-push
**Depth**: Deep (reason: safety-relevant — CA grid + survey-data integrity for an autonomous boat; ADR add; ~470 LoC source)
**Must-fix**: 2 | **Suggestions**: 8
**Round**: 1 | **Ship**: continue — a genuine data-loss correctness concern (save-failure eviction) warrants another read after the fix
**Build/test**: green (350 tests, 0 failures, 47 skipped)

### Findings
- [ ] (must-fix) `evictColdTiles` calls void `saveDirtyTiles()` then unconditionally `dropTile`s the cold set; on a save failure (disk-full/permission) `saveDirtyTiles` logs+returns WITHOUT clearing the dirty set, so a dirty-but-unsaved cold tile is dropped from RAM and recorded as evicted — its soundings are lost and revisit-reload finds nothing on disk, silently re-accumulating. Breaks THE lossless invariant. Make `saveDirtyTiles` report success and skip eviction (or evict only confirmed-persisted indices) on failure. — `src/cube_bathymetry_node.cpp:413-419`, `:508-562`
- [ ] (must-fix) Startup prime (`loadIntoSheet`/`primeFromTile`) loads the ENTIRE draft store into RAM at `on_configure`, ignoring `max_resident_tiles`; the disk store grows unbounded by design, so a restart over a long survey re-creates the unbounded-RAM condition #70 targets (ADR Problem 1 explicitly names "primed from a large draft store"), and #70's new per-cell settled `Hypothesis` makes the spike heavier. Evict clean primed tiles down to budget right after prime (lossless — primed tiles are clean/on-disk). — `src/cube_bathymetry_node.cpp:99-122`, `src/store_import.cpp:137`
- [ ] (suggestion) Eviction runs only inside `publishBounded`, which returns early on a `map<-earth` TF miss before reaching `evictColdTiles`; during an early/sustained georef-TF outage soundings keep ingesting (different transform) with no eviction → RAM unbounded. Decouple eviction from the publish path. — `src/cube_bathymetry_node.cpp:318-327`
- [ ] (suggestion) CA-grid TF-miss fallback projects the FULL resident set (up to `max_resident_tiles` full 960x960 tiles); spread over a wide area this is a large mostly-NaN raster, partially reintroducing the monolithic-publish cost on the already-degraded TF-outage path. Bound the fallback (last-known vessel position / last window). — `src/cube_bathymetry_node.cpp:343-350`
- [ ] (suggestion) `seedSettledDepth` variance floor (1e-4 m^2) breaks the exact uncertainty round-trip for cells whose stored CI < ~0.0196 m (reloads inflated). Documented and rare, but not strictly lossless. — `src/node.cpp:47-56`
- [ ] (suggestion) Stored-uncertainty convention mismatch in `primeFromTile`: `setPredictedDepthAt` uses `variance=u*u` (treats `u` as 1-sigma) while the new `setSettledDepthAt`->`seedSettledDepth` treats `u` as the 1.96-sigma CI it actually is (`geoGridToTile` writes `1.96*sigma`); the (currently dormant) predicted-prior variance is ~3.84x too large. Pre-existing but now adjacent — reconcile. — `src/store_import.cpp:126` vs `:137`
- [ ] (suggestion) Coherence WARN models the window as radius R (`ceil(2R/span)+1` per axis) but `gridsInCaWindow` selects within `R + tile_span`, so the WARN under-warns by ~the margin band — a margin-band window tile can be evicted with `max_resident_tiles` set just above the WARN threshold. — `src/cube_bathymetry_node.cpp:155-168` vs `:481-490`
- [ ] (suggestion) Disk I/O on the single-threaded executor's ping/publish path (`reloadEvictedTile` `loadWindow`; eviction `saveDirtyTiles`) blocks sounding ingest and CA-grid publication; a slow/contended disk stalls collision avoidance. Consider a separate callback group / off-thread persistence. — `src/cube_bathymetry_node.cpp:413,485,729-742`
- [ ] (suggestion) `publishDirtyTiles` clears the whole publish-dirty set even when a tile projected no finite cells this cycle (`continue`); if no further sounding lands on that tile its emerging value is never streamed on `~/tiles` until re-marked (best-effort coverage only; durable store unaffected). — `src/cube_bathymetry_node.cpp:383-390`
- [ ] (suggestion) Consider enforcing `max_resident_tiles >= window tile span` (floor) rather than only WARNing, to remove the over-lethal-hole CA degradation entirely. — `src/cube_bathymetry_node.cpp:162-169`

## Implementation
**Status**: complete
**When**: 2026-06-27 (host-inline address-findings)
**By**: Claude Code Agent (Claude Opus)

Addressed the pre-push `## Local Review (Pre-Push)` (Round 1, changes-requested).
Host-inline fix pass (the run-issue host ran implementation inline for #70).

### Both must-fixes fixed
- **Save-failure eviction data loss** — eviction no longer drops cold tiles
  unconditionally. New `trimResidentToBudget()` drops only cold tiles that are
  NOT still in the dirty set after the flush; a tile whose save failed stays in
  the dirty set and is kept resident (RAM transiently over budget, with a WARN,
  rather than losing unsaved soundings). `evictColdTiles` = flush then
  `trimResidentToBudget`. Pinned by new test `EvictionKeepsStillDirtyColdTiles`.
- **Unbounded startup prime** — bounding params now declared BEFORE the draft
  prime; after `loadIntoSheet`, `trimResidentToBudget()` drops the clean,
  on-disk primed tiles down to `max_resident_tiles` (lossless; they reload on
  revisit). The transient whole-store load peak before the trim is documented as
  a known limitation (a windowed prime needs a startup position unavailable at
  on_configure) — follow-up.

### Suggestions addressed
- Eviction decoupled from `publishBounded`: `evictColdTiles()` now runs in
  `pingCallback` independent of the publish path, so a publish-time TF miss
  cannot stall RAM bounding.
- CA-grid TF-miss fallback bounded: `publishCaGrid` caches the last vessel
  lat/lon and reuses it through brief TF gaps; the full-resident-set fallback
  only fires before any fix has ever been seen.
- Coherence WARN now uses the `R + tile_span` extent `gridsInCaWindow` actually
  selects (was modelling R only), so it no longer under-warns the margin band.

### Suggestions deferred (with rationale)
- Variance floor breaking the exact round-trip for stored CI < ~0.02 m:
  documented edge; floor keeps the DLM defined. Low impact, kept.
- `primeFromTile` predicted-prior variance `u*u` vs settled `(u/1.96)^2`
  (~3.84x): the predicted-depth prior is DORMANT (no #59 producer wired) and
  sits in the slope-correction path, which is validation-sensitive (prior wrong
  formulas were caught only in sim). Not changing dormant safety-relevant math
  without its own validation; noted for the #59 producer work.
- Single-threaded disk I/O on the ping/publish path: `saveDirtyTiles` already
  ran there pre-#70; reload only fires on revisit. Off-thread persistence is a
  larger architectural change — follow-up.
- `publishDirtyTiles` clears the whole publish-dirty set: a tile with no finite
  cells has nothing to stream and is re-marked on its next sounding; durable
  store unaffected (best-effort only). Kept.
- Enforce floor vs WARN for `max_resident_tiles` >= window span: WARN keeps the
  operator-tunable contract; default pair satisfies it. Kept as WARN.

**Build/test**: green — 351 tests, 0 failures, 47 skipped.
