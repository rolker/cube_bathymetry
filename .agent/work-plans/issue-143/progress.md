---
issue: 143
---

# Issue #143 — import_bag writes one resolution per run — depth-adaptive store levels (uma#369) need the estimation grid decoupled from the store tiling

## Issue Review
**Status**: complete
**When**: 2026-09-10 14:46 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #143
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-more-detail

### Summary

Verified against the live checkout: `import_bag_main.cpp:1027` constructs one
`cube::GeoMapSheet geo_map_sheet(resolution, iho_order)` per run, and
`import_bag_main.cpp:1129-1130` pins `accumulator_config.cell_size_m` to
`geo_map_sheet.nominalCellSizeMeters()` — confirming the issue's premise that
the estimation grid and the store's native level are the same single value
today, with no call site of the not-yet-called `depthAdaptiveLevel` in this
repo (grep returned nothing, as expected — that function lives in
`unh_marine_autonomy`). The repo already has three project-level ADRs
(`docs/decisions/0001`–`0003`) that assume a **single scalar cell size per
store**, which is a real consequence this issue doesn't currently name (see
below).

### Scope Assessment

**Well-scoped?** Not fully — the issue itself flags its central design
question as open: "Decide the unit of the decision... `depthAdaptiveLevel`
presumes a scalar depth per unit, so its signature may need to change when
this lands." That's an architecture decision (per-tile vs. per-region vs.
two-pass), not an implementation detail, and this repo has precedent for
capturing exactly this class of decision in a local ADR (`0001` tile
eviction, `0002` dirty-tile footprint math, `0003` staleness fingerprint —
all written before their implementations landed). Recommend the unit-of-
decision question be settled via a cube_bathymetry ADR either as a prerequisite
to plan-task or as an early deliverable inside the same issue/PR, rather than
left to be decided ad hoc during implementation.

The issue's own scope section already lists four non-trivial deliverables
(decide the unit, decouple sheet-vs-store-tiling, a storage estimate against
real Shoals data, and verification against the existing mixed-level pyramid
machinery). That's plausibly one PR if the decoupling approach stays additive
(e.g., running multiple `GeoMapSheet`s per import rather than reworking the
accumulator), but if the accumulator itself needs restructuring, splitting the
design decision from the implementation is worth considering at plan-task
time.

**Right repo?** Yes — `import_bag` and `GeoMapSheet`/`ImportAccumulator` are
owned by `cube_bathymetry`; the policy half correctly landed in
`unh_marine_autonomy` (#369, already merged) since it's domain policy, not
this repo's plumbing.

**Dependencies**: None blocking. `unh_marine_autonomy#369` (policy) and
`unh_marine_autonomy#331` (mixed-level native-wins pyramid) are both already
merged per the issue body. `unh_marine_autonomy#371` (live-costmap read-side
fan-out) and `#376` (unbounded store residency) are sibling follow-ups on the
*read* side and don't gate this *write*-side, offline-only issue — confirmed
the issue explicitly scopes to "Offline `import_bag` / `processed` only."
`unh_marine_autonomy#366` (import ledger) correctly gates only the *retroactive
reprocess* of existing level-10 stores, which this issue explicitly excludes.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Capture decisions, not just implementations | Action needed | The unit-of-decision question (per-tile / per-region / two-pass) is an architecture decision this repo's own convention (ADR-0001/0002/0003) would normally capture as a local ADR before implementation lands, not decide inline in code. |
| A change includes its consequences | Action needed | See ADR Applicability and Consequences below — `docs/decisions/0003-staleness-fingerprint.md`'s `build_fingerprint.json` schema has a scalar `cell_size_m` field, and `batch_regen_main.cpp:827` takes a single `regen_config.cell_size_m`. Neither is mentioned in the issue's scope, but a mixed-level store breaks the "one cell size describes this store" assumption both currently encode. |
| Improve incrementally | Watch | Four listed deliverables in one issue; fine if the decoupling stays additive (multiple sheets), worth re-checking at plan-task if it turns into an accumulator rework. |
| Only what's needed | OK | Issue explicitly declines to re-open policy questions already pinned at the #369 checkpoint, and explicitly excludes retroactive reprocessing — appropriately scoped down. |
| Test what breaks | Watch | The "verify mixed-level output against existing machinery" bullet is the right instinct (the issue itself says this "needs demonstrating rather than assuming") — recommend this verification be a hard requirement of the same PR, not a follow-up, given it touches live survey stores (Shoals/Massabesic per the memory record). |
| Human control and transparency | OK | Policy inputs (driver, no floor, clamp bounds) are traceable to a cited operator checkpoint in #369; the level-boundary table in the issue is a clear operator-facing artifact. |
| Workspace vs. project separation | OK | Correctly split: domain policy in `unh_marine_autonomy`, writer plumbing here in `cube_bathymetry`. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| workspace ADR-0001 (Adopt ADRs) | Yes | A genuine design decision (decoupling store tiling from the single `GeoMapSheet` resolution) is being made; recommend a cube_bathymetry-local ADR per this repo's established pattern. |
| cube_bathymetry ADR-0002 (dirty-tile footprint math) | Yes (consequence) | Its L10-cell geometry reasoning (`batch_regen::addBatch`, `boundsForSoundings`) assumes tiles at one fixed level; a mixed-level store from this issue is a new input `batch_regen`'s dirty-tile math wasn't designed against. Not mentioned in the issue. |
| cube_bathymetry ADR-0003 (staleness fingerprint) | Yes (consequence) | `build_fingerprint.json`'s `cell_size_m` field ("the `--resolution` value used; a different resolution invalidates the store entirely") is a single scalar per store. A mixed-level store needs this schema extended (per-tile or per-region cell size) or the incompatibility needs to be an explicit, documented limitation. Not mentioned in the issue. |
| workspace ADR-0002 (worktree isolation) | Yes (routine) | Already satisfied — work is happening in the assigned worktree/branch. |

### Consequences

- `cube_bathymetry/docs/decisions/0003-staleness-fingerprint.md`'s
  `build_fingerprint.json` schema (`cell_size_m` as a single float) needs
  updating or an explicit compatibility note once a store can hold mixed
  native levels from one import.
- `batch_regen_main.cpp` takes a single `regen_config.cell_size_m` and (per
  ADR-0002) reasons about dirty-tile overlap at one fixed L10 resolution.
  Whether `batch_regen`'s incremental-regen path needs to become
  level-aware, or is explicitly declared out of scope with a follow-up filed,
  should be decided rather than left implicit — a store built by a
  depth-adaptive `import_bag` run and then touched by `batch_regen` under the
  current single-resolution assumption is a plausible silent-mismatch path.
- These two items are not blocking for filing/starting the issue, but should
  be either folded into this issue's scope or explicitly called out as
  follow-up work in the issue body/plan, per "a change includes its
  consequences."

### Actions
- [ ] Settle the unit-of-decision design question (per-tile / per-region /
      two-pass) via a cube_bathymetry ADR, following this repo's ADR-0001/
      0002/0003 precedent, before or as an early step of implementation.
- [ ] Address or explicitly scope out the ADR-0003 `build_fingerprint.json`
      scalar `cell_size_m` consequence for mixed-level stores.
- [ ] Address or explicitly scope out the `batch_regen`/ADR-0002 single-
      resolution dirty-tile-math consequence for mixed-level stores.
- [ ] Keep the "verify mixed-level output against existing pyramid/store
      machinery" bullet as a hard requirement of this PR, not a deferred
      follow-up.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Sonnet`

## Plan Authored
**Status**: complete
**When**: 2026-09-10 14:59 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-143/plan.md` at `6e45b3c`
**Branch**: feature/issue-143 at `6e45b3c`
**Phases**: single (a stacked-PR split point is named, but not preferred)

### Summary

The unit-of-decision question the Issue Review left open is **settled in the plan**
per the operator's instruction (no separate ADR, no design issue): the unit is a
**GGGS quadtree cut** — the emitted tile itself, resolved top-down from
`coarsest_level` (8), descending wherever a grid's shallowest depth implies a finer
level, terminating at `finest_level` (14). `depthAdaptiveLevel(double)` keeps its
**current scalar signature** — the unit is answered by the caller's descent, and the
scalar it is fed is the shallowest depth in the candidate tile, which is already the
signature's documented contract. Per-region-at-a-fixed-decision-level was rejected
because exact nesting forces the decision grid to level 8 (3.48 km), where one 2 m
shoal would drag 3.48 km of ground to level 14 (256x the level-10 storage).

Both consequences the review found are planned **in this PR**, not deferred:

- **ADR-0003 fingerprint**: `schema_version` 1 -> 2, scalar `cell_size_m` replaced by
  a `tiling` object carrying mode, policy and `level_plan_sha256`. Cheaper than the
  review assumed — `build_fingerprint.h/cpp` **does not exist yet** (verified by grep;
  `batch_regen_main.cpp` has no `--incremental` path), so this is an amendment before
  first implementation, with no on-disk migration.
- **ADR-0002 / batch_regen**: `BatchRegen::SheetFactory` becomes level-parameterised,
  scatter/gather route through the plan, and `dirtyL10Tiles` gains a plan-aware
  `dirtyTiles` overload that rolls up to plan leaves. The one-L14-tile margin survives
  and is re-argued: `finest_level` (14) equals the survey index's footprint level, so
  no leaf is ever finer than the index.

Two consequences the review did **not** name were found and folded in: `SheetFactory`'s
single-level signature, and a halo/persist-filter rule (route each batch to every level
whose leaves its influence-expanded bounds touch; never persist a non-leaf grid) without
which level seams would lose the neighbouring region's soundings.

Mixed-level verification is a **hard requirement** of the PR, as the review asked: a
single-level equivalence test (policy pinned to one level must produce a byte-identical
store to today's path) is the load-bearing guard, plus a pyramid-composition test over
`build_depth_overviews`.

The storage estimate is grounded in the real Shoals `processed` layer
(`~/data/world/depths/processed`: 69 native level-10 tiles, 167 MB, 2.42 MB/tile
observed vs 14.7 MB dense) and expressed in uma#376's terms rather than a second
accounting. The recon phase's report also surfaces the area that lands **coarser** than
today's level 10 (the pinned ladder returns level 9 between ~36 m and ~72 m) so that
resolution loss is visible to the operator before an import runs.

### Open questions
- [ ] Recon phase: flat sounding spill (~4.4 GB scratch for a 10 h M3 run) to avoid a
      second projection pass, vs re-reading the bags. Plan defaults to the spill with a
      `--recon-reproject` escape hatch.
- [ ] Whether `--depth-adaptive` reaches
      `unh_echoboats_project11/scripts/build_bathy_store.sh` here or in a follow-up
      (plan assumes follow-up — other repo, and that script is already known-stale).

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`

## Plan Review
**Status**: complete
**When**: 2026-09-14 08:33 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-143/plan.md` at `6e45b3c`
**PR**: PR-less (`--issue` mode; branch `feature/issue-143`, plan not pushed)
**Verdict**: changes-requested

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Needs work | 14 files, 2 new sources, 3 test files, 2 ADR amendments, plus an accumulator rework — the exact condition the Issue Review named as its split trigger ("worth re-checking at plan-task if it becomes an accumulator rework"). The plan names a stacked split but declines it. |
| Issue alignment | Good | All four issue deliverables are addressed, including the unit-of-decision (settled in-plan per the operator's instruction) and the Shoals-grounded storage estimate. |
| File targeting | Needs work | No `marine_mbes_backscatter_store` file appears, yet the plan asserts one backscatter store per level (finding 1). `test_tile_eviction_rss.cpp` is untouched despite the RAM-bound change (finding 5). `build_fingerprint.h/cpp` is absent yet a mitigation depends on it (finding 3). |
| Consequences | Needs work | The table is thorough and finds two the Issue Review missed, but three real consequences are unlisted: the backscatter store's on-disk single-level contract, the multiplied resident-tile RAM budget, and `finest_level` escaping the survey index's footprint level. |
| Documentation & instruction impact | Good | Section present, non-silent, names the stale README/ADR/comment sites, and frames the two instruction items (`.agents/README.md` absence, inert `tool_version`) as follow-up candidates rather than in-PR edits. |
| Principle alignment | Needs work | "A change includes its consequences" — see above. "Human control and transparency" is well served by the recon report, including the resolution *lost* below level 10. "Capture decisions" — the decision currently lives only in a work-plan file (finding 7). |
| ADR compliance | Needs work | 0002/0003 amendments are correctly scoped; the 0002 margin re-argument is sound *only* while `finest_level <= 14`, which the plan itself makes tunable (finding 2). The ADR-0007 row is asserted, not verified (finding 1). |
| ROS conventions | N/A | Offline CLI tools and pure library code; no topics, QoS, parameters, or lifecycle. |

### Findings
- [ ] (must-fix) `MbesBackscatterStore` holds **all tiles at a single GGGS level fixed at construction** (`mbes_store.hpp:51,67-70`), and `load()` scans `<dir>/survey/` and **throws on level mismatch** (`tile_io.hpp:93-96`). Writing one accumulator per level into the single `bs_store_dir` therefore produces a backscatter store that cannot be read back. The plan's ADR-0007 row asserts cell-alignment but names no per-level directory scheme, no `marine_mbes_backscatter_store` change, and no uma-side consequence. Either give the backscatter store a per-level layout (a cross-repo change to scope), or explicitly exclude backscatter from `--depth-adaptive` and refuse the flag combination. — `plan.md:211`
- [ ] (must-fix) `--depth-adaptive-finest` is exposed as a tunable, but GGGS has levels 0-20 (`level_spec.h:108-113`), so a `finest_level > 14` is constructible. ADR-0002's margin re-argument — and the whole dirty-set conservatism — rests on "no leaf is ever finer than the survey index's level-14 footprint". A leaf finer than the index silently breaks the conservative-superset guarantee (a missed tile is a correctness failure, per ADR-0002). Validate `finest_level <= 14` at startup alongside the other policy validation, and state the coupling in the ADR-0002 amendment. — `plan.md:131-135,146-149`
- [ ] (must-fix) The orphan-tile mitigation (`--depth-adaptive` refuses to write into a store whose fingerprint records a different `level_plan_sha256`, unless `--replace-tiling`) depends on `build_fingerprint`, which the plan itself establishes **does not exist** and which this PR does not implement (step 8 amends the ADR only; no `build_fingerprint.h/cpp` in Files to Change). As written the mitigation is inoperative and the additive-merge hazard is unguarded. Either implement the minimal fingerprint read/write here, or drop the claimed mitigation and surface the hazard in the recon report and README instead. — `plan.md:225`
- [ ] (must-fix) The recon phase keys the level decision on the **raw minimum sounding depth** per level-14 grid. The shallowest raw sounding is the single most blunder-prone statistic in the dataset (a flier, fish, or bubble), and CUBE's own blunder rejection has not run at recon time. One outlier drags its ~54 m grid to level 14 — 256x the level-10 storage for that ground. Add an outlier guard (a low percentile rather than the strict min, and/or a minimum sounding count per decision grid) and test it; a pure `min` over unvetted soundings is not a defensible decision input for a store-sizing policy. — `plan.md:56-62`
- [ ] (must-fix) `max_resident_tiles` bounds RAM for **one** accumulator. `MultiLevelAccumulator` holds one per level present in the plan (up to 7), so the effective resident-tile ceiling becomes N x the operator-configured bound, with per-tile cost unchanged (tiles are 960x960 cells at every level). Bounded RAM is a headline property of ADR-0001 and the README comparison table, and `test/test_tile_eviction_rss.cpp` guards it. Specify how the budget is divided across levels and extend that test; neither appears in the plan. — `plan.md:116-124`
- [ ] (suggestion) Scope: the Issue Review set an explicit split trigger ("fine if the decoupling stays additive; worth re-checking at plan-task if it becomes an accumulator rework"), and the plan *is* an accumulator rework plus a `batch_regen` rework. The plan's own named split (steps 1-6 + 8 + import-side tests as PR1; step 7 as stacked PR2) is viable by its own argument, and `--depth-adaptive` being off by default means PR1 alone leaves no degraded state for fixed-level stores. Recommend the split become the default with operator sign-off, rather than the fallback. — `plan.md:256-264`
- [ ] (suggestion) The quadtree-cut decision, its three rejected alternatives, and the halo/persist-leaf rule currently live only in `.agent/work-plans/issue-143/plan.md`. The operator ruled out a new ADR, which makes it the **amendments'** job to carry the decision — but steps 7 and 8 describe only the mechanical changes. Say explicitly which shipped document (the ADR-0002/0003 amendment text, or the README depth-adaptive section) carries the decision and the rejected alternatives. — `plan.md:41-78,197`
- [ ] (suggestion) `--replace-tiling` appears only inside a Consequences-table cell: it is absent from the Approach steps, the Files to Change table, and the Testing table, despite gating whether an import may overwrite a differently-tiled store. Promote it to a numbered step with a test. — `plan.md:225`
- [ ] (suggestion) `level_plan_sha256` becomes a staleness key in the ADR-0003 v2 schema, so the canonical-JSON serialisation must be deterministic across runs and platforms (map iteration order, float formatting, key ordering). `test_level_plan` covers the cut, disjointness, NaN skip and policy throws, but not the JSON round-trip or sha stability. Add it. — `plan.md:98-99,186`
- [ ] (suggestion) "byte-identical" is the load-bearing equivalence assertion but is undefined in scope. Say what is compared: tile raster bytes only, or the whole store directory including `registry.json` and GeoTIFF header metadata (GDAL can emit writer-version tags). `StoreMetadata::date` is operator-supplied acquisition date, not run time, so store-wide comparison is plausible — but it should be stated, not assumed. — `plan.md:187`
- [ ] (suggestion) `dirtyL10Tiles` already takes a `store_level` parameter (`survey_index_query.h:106`, called at `batch_regen_main.cpp:394`), so its name is already a misnomer for any fixed-level store. Adding a parallel plan-aware `dirtyTiles` leaves two similarly named entry points with a stale name on the older one. Prefer renaming the existing one (e.g. `dirtyTilesAtLevel`) in the same PR. — `plan.md:141-147`
- [ ] (suggestion) Terminology drift the plan's README edits should fix while in the area: `README.md:98-99` says uma#248 collapsed the layers into `survey` + `reference`, but uma's `marine_bathymetry_store/README.md:137-140` records the current on-disk names as `processed/`, `draft/`, `reference/`, `chart/`, with a legacy `survey/` **auto-migrating to `processed/`**. `store_import.cpp` writes `SourceLayer::Processed`. The plan's own prose uses `processed`/`draft` correctly; the repo README does not. — `plan.md:180`
- [ ] (suggestion) Two small internal inconsistencies in the algorithm text: step 3 writes `implied <= L` where `depthAdaptiveLevel` returns a `gggs::Level` (needs `.level()`), and step 7's "for a footprint tile *coarser* than a leaf, descend to every leaf beneath it" branch is unreachable while footprints are level-14 and `finest_level == 14` — say whether it is defensive for a future finer index or drop it. — `plan.md:57-59,143-145`

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`

## Plan Review
**Status**: complete
**When**: 2026-09-14 08:46 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-143/plan.md` at `52b25dd` (revision 2)
**PR**: PR-less (`--issue` mode; branch `feature/issue-143`, plan not pushed)
**Verdict**: changes-requested

Round 2. All five r1 must-fix findings and all eight r1 suggestions are
resolved or deliberately declined by the operator (the PR split); the split
is not re-raised. The findings below are new consequences the revision
introduced — three of them (2, 3, 4) are data-loss or data-refusal paths in
the newly added `persist_leaves` / `--replace-tiling` / fingerprint-guard
machinery.

### r1 disposition (verified, not re-litigated)

| r1 finding | Disposition |
|---|---|
| 1 backscatter single-level | Resolved as prerequisite — uma#383 verified filed and OPEN, with the same `mbes_store.hpp:51` / `tile_io.hpp:93-96` evidence. |
| 2 `finest_level > 14` | Resolved — startup validation + ADR-0002 invariant (see new finding 6 on how it is enforced downstream). |
| 3 fingerprint inoperative | Resolved — minimal `build_fingerprint.h/cpp` in scope, `--replace-tiling` promoted to step 6b with tests (see new findings 3, 4). |
| 4 raw-min decision depth | Resolved — percentile + min-count guards, tested (see new finding 5 on the rollup spec). |
| 5 N× RAM budget | Resolved in intent — one shared store-wide budget (see new finding 1 on implementability). |
| 6 split (suggestion) | Declined by the operator. Not re-raised. |
| 7-13 suggestions | All taken: decision's shipped home named (ADR-0002 amendment), canonical JSON + sha determinism test, `--replace-tiling` promoted, "byte-identical" defined, `dirtyL10Tiles` → `dirtyTilesAtLevel`, README layer-name drift, `.level()` fixed and the unreachable descend branch dropped. |

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good (operator-settled) | Unchanged size; the single-PR shape is the operator's decision and is out of review scope this round. uma#383 is correctly externalised. |
| Issue alignment | Good | All four issue deliverables still covered; the recon report still carries the storage estimate and the below-L10 resolution loss. |
| File targeting | Needs work | `test_survey_index_query.cpp` (named in the Consequences table for the rename) is missing from Files to Change, as is the new public `ImportAccumulator` eviction API that step 4 depends on (finding 1). |
| Consequences | Needs work | Three new ones from r2's own additions: the leaf filter's effect on seam-tile coverage (2), the store-wide clear on an accumulate-across-surveys layer (3), and the store-wide fingerprint comparison refusing disjoint-area imports (4). |
| Documentation & instruction impact | Good | Unchanged and still non-silent. |
| Principle alignment | Needs work | "A change includes its consequences" — findings 2-4. "Never work around a bug we own" is respected. "Human control and transparency" improved further (guard + report). |
| ADR compliance | Needs work | ADR-0001's lossless persist-then-drop and `evicted_` bookkeeping must survive MultiLevelAccumulator ownership of eviction (finding 1); ADR-0002's margin argument is now enforced by a runtime check whose form matters (finding 6). |
| ROS conventions | N/A | Offline CLI tools and pure library code. |

### Findings
- [ ] (must-fix) Global-coldest eviction across per-level accumulators is not implementable from the existing last-touch data. `GeoMapSheet::touch_counter_` is a **per-sheet** monotonic counter (`geo_map_sheet.h:228-231`, incremented at `geo_map_sheet.cpp:165,200`), so `lastTouchOf` values from two sheets are incomparable — a level with fewer touches always has the smaller numbers and its tiles always look coldest. Specify a shared touch clock (one counter injected into every sheet, or a MultiLevelAccumulator-side touch sequence recorded per (level, grid)). Also: step 4's `persistAndDrop(grid)` and the resident-set/last-touch accessors are **new public API** on `ImportAccumulator` that the Files-to-Change row for `store_import.h` does not mention, and the primitive must preserve the two invariants `evictColdTiles` holds today — a persist that throws leaves the tile **resident** (`store_import.cpp:1069-1077`), and a dropped tile is recorded in `evicted_` so `addBatch`'s reload-before-add reloads it (`store_import.cpp:1103-1106`). — `plan.md:176-190`
- [ ] (must-fix) The `persist_leaves` filter drops tiles today's path writes, which also makes the load-bearing single-level equivalence test unpassable as written. A grid with no soundings is "skipped, not emitted" (algorithm step 4), so the leaf set is keyed on sounding **positions** — but `GeoMapSheet::gridIndicesForSoundings` deliberately includes near-seam neighbour tiles and a sounding "can also spread into neighbour tiles" (`store_import.cpp:1089-1090,1126-1127`), and today's fixed-level import persists those tiles with their real influence-derived cells. Under step 5 they are non-leaves: never persisted, freely dropped. With `coarsest == finest == 10` the adaptive store would therefore be **missing seam tiles** the fixed path writes — not byte-identical, and a genuine coverage regression. Define the leaf set over the influence-expanded sounding bounds (the same window `gridIndicesForSoundings` uses), or state and document the coverage change explicitly. — `plan.md:73-76,191-197`
- [ ] (must-fix) `--replace-tiling` "clears the `processed/` layer directory" is a destructive operation on a layer that **accumulates across surveys**. The bathy store's `processed/` layer is a single geodata collection that successive imports add to (`importTiles` merges; `save()` never deletes) — clearing it to re-tile one survey area deletes every other survey's tiles in that store, and the same applies to the backscatter `survey/` dir. Scope the clear to tiles overlapping the new plan's footprint, print the exact list of tiles it will delete (dry-run / report before the import starts), and require an explicit confirmation rather than a bare flag. — `plan.md:207-217`
- [ ] (must-fix) The fingerprint guard's granularity refuses legitimate additive imports. `level_plan_sha256` is a **store-wide** hash of the whole cut, so importing a *new, disjoint* survey area into an existing depth-adaptive store always yields a different sha and is refused — with the only offered remedy being the destructive `--replace-tiling` of finding 3. The orphan hazard exists only on ground covered by **both** plans. Compare per-overlap (refuse only when the new plan assigns a different level to ground the store already holds), or record the tiling per leaf/region in the fingerprint so a disjoint-area import proceeds additively. — `plan.md:207-217,250-258`
- [ ] (suggestion) The decision-depth rollup is internally inconsistent. Step 1 says a grid below `--depth-adaptive-min-count` "inherits its parent's decision" and "contributes its soundings to the parent's reservoir", but reservoirs are described as existing only at level 14 and step 2's rollup is a **min over children's decision depths**, not a pooled reservoir. Say which: coarse grids keep pooled reservoirs (percentile recomputed at each level), or the min-rollup simply skips undecided children. Also name the safety direction of the min-count rule — a genuinely shoal level-14 grid with fewer than 25 soundings is stored coarse, which is the shoal-biased policy's *unsafe* direction. — `plan.md:56-72`
- [ ] (suggestion) Say that `dirtyTiles(plan)` **throws** rather than `assert`s the "no footprint tile coarser than a leaf" invariant. The footprint level is read per row from the survey-index DB and is explicitly allowed to be mixed (`survey_index_query.cpp:192-193` "an index that stores per-sensor native levels"), so a static `finest <= 14` check at import startup does not establish it for `batch_regen` against an arbitrary index; and `assert` is compiled out under `NDEBUG`. `ancestorAtLevel` already throws `std::runtime_error` on exactly this case (`survey_index_query.cpp:73-88`) and the dry-run's catch falls back to full regen — reuse that path and say so. — `plan.md:232-245`
- [ ] (suggestion) The recon spill needs a home and a space check. The existing scratch dir is created under `std::filesystem::temp_directory_path()` (`store_import.cpp:449-451`), which on many hosts is a tmpfs — a ~4.4 GB sounding spill would land in RAM, defeating the bounded-RAM property, or hit ENOSPC hours into an import. Add an explicit `--scratch-dir` (defaulting beside the output store, not `/tmp`), and check free space against the projected spill size before phase 1 starts. — `plan.md:161-167`
- [ ] (suggestion) Recon's own RAM is the one unbounded structure in a tool whose headline property is bounded RAM: the reservoir is bounded *per grid*, but the number of touched level-14 grids scales with surveyed area (~54 m grid ⇒ ~340 grids/km²). State the per-grid reservoir cap and the resulting bytes per km², so the operator can size a multi-day survey. — `plan.md:56-62,161-167`
- [ ] (suggestion) Per-level `finalize` writes the store-level provenance sidecars (`registry.json`, backscatter metadata) once per accumulator (`store_import.cpp:1166-1180`). Say which accumulator owns that write — or have `MultiLevelAccumulator` do it once after finalising all levels — so the surviving sidecar is chosen, not whichever level finished last. — `plan.md:176-190`
- [ ] (suggestion) Files to Change omits `test_survey_index_query.cpp`, which the Consequences table itself names as a caller of the renamed `dirtyL10Tiles`. — `plan.md:262-283,342`

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`

## Plan Review
**Status**: complete
**When**: 2026-09-14 11:06 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-143/plan.md` at `ee5af01` (revision 3)
**PR**: PR-less (`--issue` mode; branch `feature/issue-143`, plan not pushed)
**Verdict**: changes-requested

Round 3, after the operator-led design re-base. The six re-based points are
treated as settled and are not re-litigated. The findings below are (a) two
r2 items the revision does not actually resolve, and (b) new internal
inconsistencies in the revision's own material — the count grid, the
required-vs-achieved mapping, the quadtree prefix, and the equivalence test's
pinned parameter.

### r2 disposition (verified against the code, not re-litigated)

| r2 finding | Disposition |
|---|---|
| 1 shared touch clock | **Resolved.** `touch_counter_` confirmed per-sheet (`geo_map_sheet.h:228-231`); the injected optional `shared_ptr<atomic<uint64_t>>`, the named `residentTiles()`/`persistAndDrop()` API on the `store_import.h` row, and the two preserved invariants (throwing persist leaves the tile resident; dropped tiles recorded in `evicted_`) all appear. |
| 5 RAM budget | **Resolved.** One store-wide `max_resident_tiles` with `MultiLevelAccumulator`-owned global eviction; per-accumulator eviction disabled by `max_resident_tiles = 0`, which matches `store_import.cpp:1050`'s early return. `test_tile_eviction_rss` carries a mixed-level case. Recon RAM now quantified separately. |
| 2 `persist_leaves` seam loss | **Not dissolved — inverted.** See finding 3. |
| 3 `--replace-tiling` | Genuinely dissolved (the flag is gone). |
| 4 store-wide plan hash | Dissolved (the hash is gone); one residual documentation gap, finding 13. |
| 5 (sugg.) decision-depth rollup | **Not addressed.** See finding 4. |
| 7, 9, 10 (sugg.) | Taken: `--scratch-dir` + free-space check, single sidecar owner, `dirtyTiles` throws. |
| 8 (sugg.) recon RAM | Partly taken (count grid quantified; reservoir still unstated — finding 11). |

uma#383 and uma#386 verified OPEN with the titles the plan cites.

### Findings
- [ ] (must-fix) The default `--count-level 13` violates the plan's own startup validation `count-level >= finest` (default `finest_level` 14). It is also a real cap, not just a validation clash: achieved spacing is `(2λ+1)·R` with `λ >= 0`, so with R = a level-13 cell (0.113 m) the data can never *achieve* level 14 and the fine end of the ladder is unreachable through the achieved path. Reconcile the default, the validation direction, and the ladder's fine clamp. — `plan.md:140,289`
- [ ] (must-fix) `achieved = fromCellSize(p95 of achieved spacing over g).level()` rounds the wrong way. `gggs::Level::fromCellSize` returns "the coarsest Level whose cells are AT OR FINER than `cell_size`" (`marine_autonomy/gggs/level.h:59-74`, `ceil` of the log2), so an achieved spacing of 0.33 m maps to level 12 (0.227 m cells) — finer than the data supports, the unsafe direction, and the opposite of the plan's own rule ("the level whose cell is **no finer than** the 95th-percentile level of aggregation"). Take the coarser neighbour explicitly. The same level-ordering slip appears in the `LevelPlan` API: `coverageDeficit()` is described as "tiles where required < achieved level", but the decision section defines the deficit as required being *finer* than achieved, i.e. `required > achieved` in level numbers. — `plan.md:153,238`
- [ ] (must-fix) Routing is per **level**, emission is per **tile**, and the plan states there is no filter at persist time ("every tile an accumulator builds is persisted"). A batch is one ping (`import_bag_main.cpp:1293-1296`), so any swath whose influence-expanded bounds clip an emitted level-14 tile is handed in full to the level-14 accumulator, which then builds and persists level-14 tiles over the neighbouring plain that the level plan never emitted. r2's must-fix 2 is therefore not dissolved — it is inverted from under-production (dropped seam tiles) into over-production (fine tiles outside the plan), which breaks the plan report's storage estimate, the coverage-deficit accounting, and `test_level_plan`'s guarantee about what the store contains. Specify a per-tile admission rule at persist time, and state explicitly how it still keeps the near-seam neighbour tiles that `gridIndicesForSoundings` deliberately includes (`store_import.cpp:1089-1090,1103`) so single-level equivalence survives. — `plan.md:33,260-265`
- [ ] (must-fix) `decision_depth[g]` is defined only per **level-14** grid (recon step 1), but the descent evaluates `depthAdaptiveLevel(decision_depth[g])` at every level from the coarsest down (step 3). No rollup rule is given, so the algorithm is not implementable as written for any `g` coarser than level 14. This is r2 suggestion 5, unaddressed in r3; r3 makes it sharper by dropping the min-count guard that the old rollup text leaned on. Say whether coarse grids pool their descendants' soundings and recompute the percentile, or take the min over children's decision depths — and name the safety direction of the choice. — `plan.md:143,151-153`
- [ ] (must-fix) The descent root and the prefix invariant are hard-coded at level 8 ("Descend from each touched **level-8** grid"; "every emitted tile's ancestors up to **level 8** are also emitted", repeated in the `test_level_plan` row) while `coarsest_level` is a tunable whose default is 8. The single-level equivalence test sets `coarsest_level == finest_level == 10`: under a literal level-8 root it would emit levels 8, 9 and 10 and could not be byte-identical to today's fixed-level store. Make the root `coarsest_level` throughout, and say separately that the per-level-8 spill partition is a scratch-file grouping independent of `coarsest_level`. — `plan.md:151,159,365`
- [ ] (must-fix) The equivalence test's pinned parameter is numerically wrong. Level 10's nominal cell is **0.906 m** (uma `depth_adaptive_level.hpp:53` ladder; `gggs::Level::cellSize()`), so `capture_spacing_scale = 0.5 / 0.91` yields a gate of 0.4978 m, not today's 0.5 m — every tile would differ and the load-bearing test fails by construction. Pin `0.5 / gggs::Level(10).cellSize()`. Related: `capture_spacing_scale` is a `Parameters` field with no CLI flag and no ROS parameter in the plan (today's `capture_distance_scale` has neither — `parameters.h:205` is its only site), so also say how the test sets it. — `plan.md:366`
- [ ] (suggestion) The spill size estimate is low by ~1.7x and the lossless-replay requirement is unstated. `GeoSounding` is `gz4d::PositionDegrees` plus `Sounding`, and `Sounding` carries seven floats **and** a `geometry_msgs::msg::Point sonar_relative_position` (`sounding.h`) — roughly 80 B, not 48 B, so a 10 h M3 day is ~7.4 GB, not 4.4 GB. The free-space check is sized from this number. A spilled subset would also have to keep `intensity`, `beam_angle`, `slant_range` and `sonar_relative_position` or the backscatter half of the byte-identical assertion cannot hold (the angular-response correction reads them). — `plan.md:250`
- [ ] (suggestion) The compute consequence of parents-alive is not quantified anywhere. Storage is bounded and stated (the 4/3 geometric series), but over a shoal the full prefix is levels 8 through 14 — seven native levels — so that ground is CUBE-estimated up to seven times and every ping there routes into up to seven accumulators. "one extra estimate per level stacked over a point (two or three on a slope)" understates the shallow case, and the Consequences table has no import-runtime row. — `plan.md:169,399-411`
- [ ] (suggestion) Fingerprint schema ambiguity for `mode: fixed`. The `tiling` object marks `cell_size_m` as "fixed only" but does not say whether `policy` — which now carries `capture_spacing_scale` — is written in fixed mode. The capture-gate change alters fixed-level output too, so a fixed-mode fingerprint that omits the capture policy would fail to mark existing fixed stores stale, which is exactly the job ADR-0003's `cell_size_m` row does today (`docs/decisions/0003-staleness-fingerprint.md:44,62-63`). — `plan.md:308-312`
- [ ] (suggestion) The hole criterion contradicts the chosen `k`. The Context says a node at a cell coarser than `2 × 0.05·depth` has uncaptured corners — that is the mid-edge (`0.5·cell`) criterion, the one the plan elsewhere rejects as "node-centric". Under the adopted half-diagonal rule (`k = 0.71`) the threshold is `cell > capture / 0.71`, i.e. about 1.41x, not 2x. — `plan.md:81`
- [ ] (suggestion) r2 suggestion 8 is only half carried: the count grid now has a per-km² figure, but the per-level-14-grid depth reservoir is still described only as "bounded" with no cap and no bytes/km², and it is the other structure that scales with surveyed area. — `plan.md:246`
- [ ] (suggestion) The spill's claimed bound is close to vacuous. A level-8 grid spans 3478.7 m, so most single-day surveys fall inside one or two of them and "the replay is bounded by one coarse tile's soundings" bounds nothing useful. Either partition the spill finer or drop the claim and rest the residency argument on eviction alone. — `plan.md:251-253`
- [ ] (suggestion) Removing the plan hash (r2 must-fix 4, correctly dissolved) leaves one case undocumented: a re-import over the **same** ground under a different count-level or policy leaves the earlier import's finer native tiles in place, and a fine-LOD reader prefers those stale partial tiles over the newer, coarser, complete estimate. Not asking to reinstate the guard — asking the README depth-adaptive section and the ADR-0002 amendment to state what a re-import over already-covered ground leaves behind. — `plan.md:33-36,402`
- [ ] (suggestion) `--count-level` has a validated lower bound but no upper one (GGGS runs to level 20), and the Calder citation does not support the default as paired: "R of about a quarter of the finest spacing you expect" at level 13 (0.113 m) implies a finest expected spacing of ~0.45 m, i.e. level 11, not the `finest_level` 14 the plan also defaults to. Say which reading the default follows. — `plan.md:140,433-435`

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`

## Plan Review
**Status**: complete
**When**: 2026-09-14 11:16 -04:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-143/plan.md` at `9e7a8dd` (revision 4)
**PR**: PR-less (`--issue` mode; branch `feature/issue-143`, plan not pushed)
**Verdict**: changes-requested

Round 4. The six operator-confirmed design points are settled and not
re-litigated. All six r3 must-fixes are addressed; the two findings below are
the residue of r3 must-fix 3's fix — the *newly added* admission-predicate and
touched-set material, checked against `geo_map_sheet.cpp` and
`store_import.cpp`, does not yet do what the plan claims for it. Everything
else verified resolved.

### r3 disposition (verified against the code, not re-litigated)

| r3 finding | Disposition |
|---|---|
| 1 `--count-level` default vs validation | **Resolved.** Default = `finest_level`; `finest <= C <= 20`; the achieved-path reachability argument (`(2λ+1)·R`) and the deliberate departure from Calder's "quarter of the finest expected spacing" are both stated. |
| 2 `levelNoFinerThan` rounding + deficit direction | **Resolved.** Matches `gggs/level.h:59-74` (`fromCellSize` = coarsest at-or-**finer**, `ceil` of the log2); the helper takes `fromCellSize(s).level() - 1` unless exact, and the test row pins 0.33 m → 11. `coverageDeficit()` is now `required > achieved` in both places. |
| 3 per-tile admission / touched set | **Addressed in intent, incomplete in mechanism** — findings 1 and 2. |
| 4 decision-depth rollup | **Resolved.** Min over children, percentile computed once at level 14, safety direction (shoal-biased) named; coarse grids never pool. |
| 5 `coarsest_level` root | **Resolved** in the algorithm, the prefix invariant and the test row; the spill partition is explicitly independent (now level 10). One stale mention — finding 6. |
| 6 equivalence pin | **Resolved** as far as the arithmetic goes (`0.5 / gggs::Level(10).cellSize()`, and `--capture-spacing-scale` now exists on both tools so the test can set it). One unstated precondition — finding 4. |
| 7-14 (suggestions) | All carried: 80 B/7.4 GB spill with the full `GeoSounding` field list; the compute-multiplier section and Consequences row; `policy` written in both fingerprint modes; the 1.41x hole criterion; the reservoir's 256 B/grid and ~90 KB/km²; the level-10 spill partition replacing the vacuous level-8 bound; the re-import-over-covered-ground paragraph; the count-level upper bound and the R-reading. |

uma#383 and uma#386 re-verified OPEN with the cited titles.

### Evaluation

| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good (operator-settled) | Unchanged; the single-PR shape is the operator's decision and out of review scope. |
| Issue alignment | Good | All four deliverables still covered; the unit of decision is settled in-plan with its rejected alternatives. |
| File targeting | Needs work | The `geo_map_sheet` row scopes the admission predicate to `gridIndicesForSoundings` alone, which is not the creation site (finding 1). |
| Consequences | Good | Compute multiplier, re-import-over-covered-ground, and the fixed-mode `policy` key are all now carried. |
| Documentation & instruction impact | Good | Unchanged, non-silent, candidates framed as proposals. |
| Principle alignment | Needs work | "Test what breaks" — the load-bearing equivalence test is falsifiable only if the emitted set really reproduces today's persisted set at a single level (findings 1, 2). Elsewhere good. |
| ADR compliance | Good | 0002/0003 amendments carry the decision; `finest <= 14` invariant re-argued; ADR-0001 persist-then-drop invariants named. |
| ROS conventions | N/A | Offline CLI tools and pure library code. |

### Findings
- [ ] (must-fix) The admission predicate is scoped to the wrong function, so it cannot deliver "never created, never estimated, never persisted". `gridIndicesForSoundings` only *enumerates* (`geo_map_sheet.cpp:115-130`); creation happens in `addSoundings` → `getOrCreateGridsIn(boundsForSoundings(...))` (`geo_map_sheet.cpp:102,147-168`), and `ImportAccumulator::finalize` persists **every** grid in `sheet_.grids()` (`store_import.cpp:1157-1165`). Filtering only the enumerator leaves the level-14 accumulator creating and persisting exactly the off-plan tiles r3 finding 3 described, while *also* skipping their reload/seed pass (`store_import.cpp:1103-1120`) — a worse state than no predicate. The predicate must gate the shared `boundsForSoundings` consumers together; the source comment at `geo_map_sheet.cpp:41-43` exists precisely because those two paths must never drift. Say so in the Approach step and in the `geo_map_sheet.h/cpp` Files-to-Change row. — `plan.md:314-320,411`
- [ ] (must-fix) The touched set is computed with one influence radius per count cell, but the influence radius is **level-dependent**: `Parameters::influenceRadius` floors at `distance_scale` (`parameters.cpp:100-107`), which `setGridResolution` sets to the sheet's node spacing (`parameters.cpp:112-115`) — 3.62 m at level 8 versus 0.057 m at level 14 — and `boundsForSoundings` adds a further one-**cell** floor at the sheet's own level (`geo_map_sheet.cpp:62-72`). A single recon-time radius therefore under-expands the touched set at every level coarser than the one it was computed at, so coarse tiles that a fixed-level import at that level would populate can be excluded from the emitted set — the same seam loss as r2 must-fix 2, re-entering through admission, and it breaks single-level equivalence for any `coarsest == finest == L` where the floor binds. Expand per level: `max(recorded radius, gggs::Level(L).cellSize())` plus one cell at L, or record the radius per level. — `plan.md:161-167,314-320`
- [ ] (suggestion) The equivalence claim is stated against the wrong set. Today's import *creates* every grid in the batch's expanded **bounding rectangle** (`GridAreaIterator` over `boundsForSoundings`, `geo_map_sheet.cpp:122-125`), which is strictly larger than a union of per-sounding influence discs — but an all-no-data tile is never written (`store_import.cpp:505-509`), so what the emitted set must reproduce is the **persisted** set, not the created set. Restate the invariant as "the emitted set at a level is a superset of the grids today's import persists, and admitted-but-empty tiles write nothing", which is both true and sufficient for the byte-identical test. — `plan.md:165-167`
- [ ] (suggestion) The equivalence pin carries an unstated precondition. `GeoMapSheet` builds `parameters_` from the **requested** cell size, not the GGGS-snapped nominal (`geo_map_sheet.cpp:76`), so `distance_scale` equals `--resolution`, while the pin `0.5 / gggs::Level(10).cellSize()` assumes it equals 0.906 m. A fixed-level baseline run with `--resolution 1.0` would give a 0.552 m gate and the test fails by construction again. Either pin `capture_spacing_scale = 0.5 / <the sheet's distance_scale>`, or state that both sides construct the sheet with `gggs::Level(10).cellSize()`. — `plan.md:465`
- [ ] (suggestion) Two undefined edges in the new LoA material: (a) `levelNoFinerThan(s)` is `fromCellSize(s).level() - 1`, which underflows at level 0 and has no stated clamp to `coarsest_level`; (b) the summed-area table is built **per count-grid tile**, so a box near a tile edge is truncated and a sparse cell may never reach `n_req` for any λ inside the tile — say what λ saturation yields (presumably `coarsest_level`, reported as coverage deficit, which is the safe direction) and test it. — `plan.md:170-180`
- [ ] (suggestion) The `import_bag_main.cpp` Files-to-Change row still says "per-L8 spill" while Approach step 4 (correctly, per r3 finding 5) partitions the spill per level-10 grid. — `plan.md:418`
- [ ] (suggestion) The decision-depth min-rollup should say it is a min over **touched** children only (an untouched child has no decision depth) and what an entirely undecided grid yields — otherwise the descent's `depthAdaptiveLevel(decision_depth[g])` is undefined on the sparse edge tiles the touched set deliberately includes. — `plan.md:168-172`

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-09-14 14:03 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-143 at `2f93049`
**Mode**: pre-push
**Depth**: Deep (reason: ~6.9k lines, 39 implementation files, cross-module, two ADR amendments, on-disk format change)
**Must-fix**: 6 | **Suggestions**: 10
**Round**: 1 | **Ship**: continue — two headline guarantees (bit-exact rebuild, bounded RAM) are asserted in shipped docs but unsupported by the code, and the suite bypasses the path that breaks

Specialists: Static Analysis (cpplint/uncrustify/lint_cmake clean; cppcheck rows are its C-vs-C++ header misdetection), Governance, Plan Drift, Claude Adversarial Lens A + Lens B. Copilot and Local off (default). Clean rebuild + full suite re-run independently: 752 tests, 0 errors, 0 failures, 87 skipped.

All plan-review must-fixes from rounds 1-4 were traced to code and confirmed honored. The operator-settled design points were not re-litigated.

### Findings
- [ ] (must-fix) Phase-two replays the spill in spatial (level-10 grid) order, not chronological; CUBE's sliding-median pre-filter is order-dependent, so tiles coarser than the spill level — the default levels 8 and 9 — differ from a chronological import. Puts README:107 "bit-exact vs import_bag --depth-adaptive" and ADR-0002:198 "byte-identical" in doubt; both equivalence tests bypass the spill. SpilledSounding carries no sequence number, so this is a design decision — `src/import_bag_main.cpp:1157`, `src/recon.cpp:174`
- [ ] (must-fix) The recon count grid is unbounded in RAM — no budget, no eviction, no reported peak — contrary to the plan's explicit commitment (plan.md:228-232) and README:106's bounded-RAM claim; ~630 MB/km² at the default --count-level 14. Undisclosed in Implementation sync — `src/recon.cpp:97`, `include/cube_bathymetry/count_grid.h:241`
- [ ] (must-fix) The recon spill opens one ofstream per level-10 grid with no cap; BatchRegen solves the same problem in this PR at kMaxOpenStreams=128 with LRU + flush check. fd exhaustion aborts a large survey mid-recon — `src/recon.cpp:140`
- [ ] (must-fix) The build fingerprint omits the inputs that decide the plan: --depth-adaptive-scale (LevelPlanPolicy::depth.capture_distance_scale), achieved_percentile, decision_depth_percentile, iho_order; the capture_distance_scale it does record has no CLI setter and can never change (false assurance) — `src/build_fingerprint.cpp:49`, `src/import_bag_main.cpp:1048`
- [ ] (must-fix) Plan-aware dirtyTiles silently drops a footprint tile with no emitted ancestor at any plan level (ground the plan never covered), contradicting ADR-0002's "conservative superset"; the empty-set guard only fires on a total miss — `src/survey_index_query.cpp:395`
- [ ] (must-fix) BuildFingerprint::write promises ADR-0003's fsync (and the comment says "flush it to disk") but calls only std::fflush — `src/build_fingerprint.cpp:186`
- [ ] (suggestion) recon.cleanup() deletes the spill before accumulator.finalize() persists resident tiles; reorder to narrow the crash window — `src/import_bag_main.cpp:1183`
- [ ] (suggestion) Spill files open ios::app with no empty-dir check; orphaned .recon_spill_<pid> dirs after a kill are never swept or warned about — `src/recon.cpp:142`
- [ ] (suggestion) A failed fingerprint write only warns; the run still prints "done!" and exits 0 — `src/import_bag_main.cpp:1061`
- [ ] (suggestion) The live node inherits the new capture gate with no ROS parameter, while both offline tools got --capture-spacing-scale (the floor removal itself is operator-settled) — `src/node.cpp:151`
- [ ] (suggestion) ShallowReservoir kCapacity=64 is sized for decision_depth_percentile ~0.02; validate() accepts any value in [0,1] and a larger one silently returns a far-too-shallow depth — `include/cube_bathymetry/recon.h:88`
- [ ] (suggestion) Plan bookkeeping: multi_level_accumulator.*, recon.*, map_sheet.h and three touched test files are in neither Files-to-Change nor Implementation sync — `.agent/work-plans/issue-143/plan.md:425`
- [ ] (suggestion) Orphaned `/// @brief Tiles currently resident in RAM.` now heads persistAndDrop, giving it two @brief lines — `include/cube_bathymetry/store_import.h:441`
- [ ] (suggestion) Six closing braces indented to ~column 56 in the new test files — `test/test_count_grid.cpp:178`
- [ ] (suggestion) test_mixed_level_import got TIMEOUT 300; test_import_eviction (~44 s against the 60 s default) did not — `CMakeLists.txt:466`
- [ ] (suggestion) batch_regen reads and writes no fingerprint at all though ADR-0003 names it the writer; nothing calls isStale() — `src/batch_regen_main.cpp`

### Operator decisions for the fix pass (2026-09-14, host-recorded)

- **Must-fix 1 (spill replay order)**: replace the per-level-10-grid spill files
  with **one chronological spill file** replayed front to back — the order the
  fixed path saw the pings — so byte-identity holds by construction. Extend the
  single-level equivalence test to run through the recon spill (recon → plan →
  replay), not only through direct `addBatch` calls. This also resolves must-fix
  3 (one open file). Update `recon.h/cpp`, `import_bag_main.cpp`, the README and
  the plan's Implementation-sync text (the level-10 partition is gone).
- **Must-fix 2 (count-grid RAM)**: **directory-backed count tiles with an LRU**
  in `CountGrid`: a resident budget (default a few hundred tiles), a cold tile
  written to the scratch dir as a UInt16 GeoTIFF and reloaded on demand — during
  recon and during plan computation, where the level of aggregation needs only a
  3×3 neighbourhood at a time. The plan report prints the resident peak.
- Must-fixes 4–6 as found: fingerprint records `capture_distance_scale`,
  `achieved_percentile`, `decision_depth_percentile` and `iho_order`; the
  plan-aware `dirtyTiles` must remain a conservative superset (off-plan ground is
  never dropped — roll it up to the plan's coarsest level, or report it, never
  silently omit); `BuildFingerprint::write` fsyncs.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus`
