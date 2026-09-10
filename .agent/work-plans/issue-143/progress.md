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
