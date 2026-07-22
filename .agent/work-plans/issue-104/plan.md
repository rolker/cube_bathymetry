# Plan: Live coverage tiles: neighbor tiles never update — grid selection under-margined vs sounding influence radius

## Issue

https://github.com/rolker/cube_bathymetry/issues/104

## Context

`boundsForSoundings` (`src/geo_map_sheet.cpp:39`) pads the sounding batch's
bounding box by **one cell**, but `GeoGrid::insert` (`src/geo_grid.cpp:56`)
spreads each sounding over an influence radius up to
`CONF_99PC·√(horizontal_error)` — typically several cells. A neighbor tile
reached only by spillover is never selected, so it is never written or marked
dirty — reproducing both 2026-07-21 field symptoms (only the home tile updates
near a seam; spillover-only tiles never update).

Desk verification closed the issue's open questions:

- `gggs::GridAreaIterator` (unh_marine_autonomy) correctly enumerates the full
  inclusive grid rectangle — beam-straddle is **ruled out**; under-margin is
  the sole cause.
- All three ingest paths share the same selection: live
  `GeoMapSheet::addSoundings`, `store_import.cpp:701` and
  `batch_regen.cpp:196` (both via `gridIndicesForSoundings` →
  `boundsForSoundings`). One fix covers all, and scatter/live stay consistent.
- The 07-21 gabby bags (`~/data/logs/gabby/logs/bizzyboat_sonar/…`) recorded
  the **inputs** (`m3/detections`, tf, sonar_info), not the tile outputs — so
  verification is a before/after replay through `import_bag`, not a log diff.

## Approach

1. **Shared radius helper** — extract the influence-radius formula (currently
   duplicated in `geo_grid.cpp:48-68` and legacy `grid.cpp:64-84`) into
   `Parameters::influenceRadius(const Sounding &) const` (`parameters.h`).
   Use it in `GeoGrid::insert`, `Grid::insert`, and `boundsForSoundings` so
   selection and spreading can never drift again.
2. **Radius-based padding** — in `boundsForSoundings`, expand the bounds by
   each sounding's `gz4d::BoundsDegrees::radiusFromCenter(s, influenceRadius)`
   (handles lat/lon scaling), keeping the existing one-cell pad as a floor.
   Skip padding for a sounding whose radius is non-finite (NaN
   `horizontal_error` must not poison the whole batch's bounds; the legacy
   `Grid::insert` already gates non-finite soundings — `GeoGrid::insert`
   currently relies on upstream filtering).
3. **Doc-comment sync** — update the "one-cell-expanded bounds" references:
   `geo_map_sheet.cpp:35-38`, `geo_map_sheet.h` (`gridIndicesForSoundings`
   doc), `batch_regen.h` scatter doc (~line 57), `batch_regen.cpp:183`, the
   revisit-reload comment at `cube_bathymetry_node.cpp:1138` ("addSoundings
   expands the bounds by a cell"), and repo ADR-0001's batch-regen addendum
   ("the sounding's one-cell-expanded window", line 257).
   (`store_import.cpp:687` turned out to be margin-agnostic — "computes the
   SAME expanded window" — so it needs no edit.)
4. **Unit tests** (`test/test_geo_map_sheet.cpp`):
   - Boundary-adjacent sounding whose spillover reaches a neighbor grid it
     doesn't enter → neighbor grid is created, receives data, and appears in
     both `dirtyGrids()` and `publishDirtyGrids()`.
   - `gridIndicesForSoundings` returns the same widened set (scatter parity).
   - Regression: small-radius sounding far from any seam still selects only
     its home tile (no over-selection).
   - Non-finite guard: a batch containing one sounding with NaN/negative
     `horizontal_error` must not poison the batch bounds — finite soundings
     still select and write their tiles.
5. **Existing-suite pass** — `test_batch_regen`, `test_store_import`,
   `test_publish_equivalence` must stay green (the bit-exact scatter/gather
   claim is preserved because scatter and live widen through the same code).
6. **Bag verification** — replay the 2026-07-21 gabby sonar sessions
   through `import_bag` pre- and post-fix into scratch stores (outputs under
   the session scratchpad; bags read-only) at the live node's `-r 0.5`, and
   diff the stores. **Result (2026-07-22, sessions 17:50 and 14:09 UTC)**:
   identical tile sets, zero new cells; only 3 / ~200 seam-cell value
   refinements respectively. The under-margin is real but, at Bizzy's 0.5 m
   cells and Massabesic depths, `Node::insert`'s capture gate (see
   Implementation Notes) bounds spillover to a hair past the old margin — so
   this fix does NOT explain the 2026-07-21 live-coverage symptom; that RCA
   continues downstream (publish/transport). Recorded on the issue.

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/parameters.h` | Add `influenceRadius(const Sounding &)` |
| `src/geo_grid.cpp` | Use helper in `insert`; door-gate degenerate soundings (parity with `Grid::insert`, review round-1) + non-finite radius backstop |
| `src/grid.cpp` | Use helper in `insert` (keep non-finite gate) |
| `src/geo_map_sheet.cpp` | Radius-based padding in `boundsForSoundings`; comment |
| `include/cube_bathymetry/batch_regen.h`, `src/batch_regen.cpp`, `src/store_import.cpp` | Doc comments only |
| `src/cube_bathymetry_node.cpp`, `docs/decisions/0001-tile-eviction-and-incremental-publish.md` | Doc/comment wording only ("one-cell-expanded") |
| `test/test_geo_map_sheet.cpp` | Seam spillover + parity + no-over-selection tests |
| `test/test_parameters.cpp` | Helper matches the historical formula (drift guard) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Test what breaks | Tests target the exact field failure (seam spillover) plus the drift risk (shared helper) |
| A change includes its consequences | batch_regen/store_import doc comments and their tests updated in the same PR |
| Only what's needed | Keeps the rectangle-of-grids selection; no redesign. Legacy planar `MapSheet` bounds (2-cell buffer, no live users) left as-is |
| Improve incrementally | Single focused PR |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| repo ADR-0001 (tile eviction & incremental publish) | Yes | Dirty/publish-dirty semantics unchanged; spillover tiles now correctly enter the publish-dirty set. LRU last-touch now bumps the occasional extra seam tile — negligible (radius ≪ tile span). The evicted-tile revisit reload is keyed off the **dirty set**, not sounding centres (`cube_bathymetry_node.cpp:1149-1156`), so widened selection flows into the reload protection consistently by construction — no node logic change needed. ADR-0001's addendum wording updated per step 3 |
| workspace ADR-0002 (worktree isolation) | Yes | Layer worktree `issue-cube_bathymetry-104` |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `boundsForSoundings` widening | batch_regen scatter docs (bit-exact claim) | Yes — step 3 |
| Radius formula location | Both `insert` implementations | Yes — step 1 |
| Tiles emitted near seams | `~/tiles` publish volume (occasional extra tile) | Noted; no cap exists, no change needed |

## Open Questions

- None blocking. Legacy planar `MapSheet::addSoundings` keeps its 2-cell
  buffer (unused code path; shared helper still removes its radius-formula
  duplicate).

## Estimated Scope

Single PR.

## Implementation Notes

- **Capture-gate interplay (discovered writing the seam test)**:
  `Node::insert` accepts a deposit only within
  `max(capture_distance_scale·|depth|, 0.5)` m of the sounding
  (`node.cpp:154`), independent of the influence radius — so the *effective*
  spillover reach is `min(influenceRadius, capture reach)`. The seam unit test
  uses depth −100 m (capture reach 5 m) so the 3 m seam gap is depositable.
  Field implication: Bizzy runs `cell_size: 0.5`, so the pre-#104 margin was
  0.5 m while capture reach at Massabesic depths (~10–15 m) is 0.5–0.75 m —
  the under-margin bites there only in a narrow seam band. The bag replay
  (step 6) is therefore the arbiter of how much of the 2026-07-21 symptom
  this fix explains; the selection fix is correct regardless (selection must
  cover everything the insert path can deposit).
- `GeoGrid::insert` also gained a full door-gate matching `Grid::insert`
  (non-finite position/depth/errors, `vertical_error<=0`,
  `horizontal_error<0` — review round-1 suggestion), plus a non-finite-radius
  backstop before the cell iterator.
