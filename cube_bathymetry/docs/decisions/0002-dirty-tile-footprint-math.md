# ADR-0002: Dirty-tile footprint math for incremental regen

## Status

Accepted.

## Context

`batch_regen::addBatch` routes each batch to every L10 tile whose geographic extent
overlaps `boundsForSoundings(batch)` — the batch's per-sounding influence-radius-
expanded bounding box, plus a one-cell floor (see `geo_map_sheet.cpp::boundsForSoundings`,
cube_bathymetry#104). The influence radius per sounding is
`CONF_99PC * sqrt(horizontal_error)`, which for typical IHO Special Order TPU values
(horizontal_error ≈ 1 m) is approximately 1–3 m. L10 GGGS cells at the standard
0.25 m resolution span ~240 m per side; one L10 cell width comfortably contains any
realistic influence radius.

`marine_survey_index` (uma#259) stores, for each indexed ping, the L14 GGGS tiles
overlapping its per-ping geographic bounding box (`tilesForBoundingBox`). The L14
bounding box is a superset of the ping centre but does **not** include the per-sounding
influence radius expansion that `batch_regen` applies at L10.

For incremental regen (cube_bathymetry#111) we must determine which L10 store tiles
are "dirty" (need rebuilding) after new bags are added. The dirty set must be a
**conservative superset** — a missed tile is a correctness failure (stale output in
the live store); a false positive causes unnecessary but bit-identical rebuild (harmless).

## Decision

The dirty L10 set for a new bag is computed as follows:

1. Query the survey index for all L14 tiles the bag's passes touch (`queryPasses`
   against the bag's `PassRow` entries, or equivalently `tilesForBoundingBox` over
   the per-pass geographic bounds already stored in the index).

2. Expand the L14 tile set by **one L14 cell** in each cardinal direction. This
   mirrors the one-cell floor `boundsForSoundings` adds at L10 and ensures that
   a ping whose influence radius reaches the edge of its L14 tile is also attributed
   to the adjacent L14 tile.

3. Roll up the expanded L14 tile set to L10 via the GGGS parent hierarchy:
   iterate the free function `gggs::parent(index)` (one level up per call,
   L14 → L10 = four applications; there is no multi-level `parentAt`).
   Deduplicate.

4. The resulting L10 set is the dirty set. It is a provably conservative superset of
   the L10 tiles `batch_regen::addBatch` would scatter to for the new bag's soundings,
   given that typical influence radii (≤3 m) are much smaller than one L14 cell.

## Rationale

A bounding-box approach (step 1 without expansion) might miss tiles when a sounding's
influence radius extends into a neighbouring L14 cell that rolls up to a different L10
tile. Adding one L14 margin (step 2) covers this safely. The margin is deliberately
expressed in L14 units (not in metres) so the implementation uses only GGGS arithmetic,
not geodetic distance — it stays correct as grid resolution changes.

Influence-radius expansion at L10 (matching `boundsForSoundings` exactly) was
considered but rejected: it would require either replaying sonar parameters at
query time (breaking the index's data independence) or baking a worst-case margin
into the index (over-conservative and fragile). The one-L14-cell margin is simpler,
always safe given realistic TPU, and self-documenting.

A tile rebuilt unnecessarily produces a bit-identical result to full regen by
construction (same scatter-gather path, same reference gating). The only cost is time.

## Consequences

- `survey_index_query.cpp::dirtyL10Tiles()` implements steps 1–3 above. The L14
  expansion is a simple neighbour enumeration using `gggs::GridAreaIterator` over a
  one-cell-padded bounding box of the hit tiles.
- If `marine_survey_index` is absent (no DB file) the dirty set cannot be computed
  and the caller falls back to full regen (explicitly logged).
- A changed sonar TPU model (new `Parameters` that changes `influenceRadius`) does
  not invalidate this decision — the one-cell L14 margin already covers realistic TPU
  bounds. If TPU grows pathologically, a full regen is available via `--fresh`.
- This ADR applies to the bathy dirty set; the backscatter store shares the same
  footprint and uses the same dirty L10 set (ADR-0007 addendum).
