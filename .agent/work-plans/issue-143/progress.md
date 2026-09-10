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
