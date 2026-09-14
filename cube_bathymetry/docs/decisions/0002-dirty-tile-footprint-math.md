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

2. Expand the L14 tile set by **one L14 tile (grid)** in each cardinal direction —
   i.e. take the bounding box of the hit tiles, pad it by one L14 tile span, and
   re-enumerate. This ensures that a ping whose influence radius reaches the edge
   of its L14 tile is also attributed to the adjacent L14 tile.

   > **Margin sizing (corrected during PR1 implementation, cube#111).** An earlier
   > draft said "one L14 *cell*". At the standard GGGS geometry an L14 grid spans
   > ~54 m (8°/2¹⁴ at the equator) and holds 960×960 cells, so one L14 *cell* is
   > only ~5–6 cm — far smaller than the ≤3 m per-sounding influence radius this
   > margin exists to cover, which would make the dirty set miss boundary tiles (a
   > correctness failure). One L14 *tile* (~54 m) comfortably covers any realistic
   > influence radius, so the margin is one tile. It is still expressed in GGGS
   > units (not metres) so it uses only grid arithmetic and stays correct as the
   > grid resolution changes.

3. Roll up the expanded L14 tile set to L10 via the GGGS parent hierarchy:
   iterate the free function `gggs::parent(index)` (one level up per call,
   L14 → L10 = four applications; there is no multi-level `parentAt`).
   Deduplicate.

4. The resulting L10 set is the dirty set. It is a provably conservative superset of
   the L10 tiles `batch_regen::addBatch` would scatter to for the new bag's soundings,
   given that typical influence radii (≤3 m) are much smaller than one L14 tile.

## Rationale

A bounding-box approach (step 1 without expansion) might miss tiles when a sounding's
influence radius extends into a neighbouring L14 tile that rolls up to a different L10
tile. Adding a one-L14-tile margin (step 2) covers this safely (the margin, ~54 m, is
much larger than the ≤3 m influence radius). The margin is deliberately expressed in
L14 units (not in metres) so the implementation uses only GGGS arithmetic, not
geodetic distance — it stays correct as grid resolution changes.

Influence-radius expansion at L10 (matching `boundsForSoundings` exactly) was
considered but rejected: it would require either replaying sonar parameters at
query time (breaking the index's data independence) or baking a worst-case margin
into the index (over-conservative and fragile). The one-L14-tile margin is simpler,
always safe given realistic TPU, and self-documenting.

A tile rebuilt unnecessarily produces a bit-identical result to full regen by
construction (same scatter-gather path, same reference gating). The only cost is time.

## Consequences

- `survey_index_query.cpp::dirtyL10Tiles()` implements steps 1–4 above. The L14
  expansion pads the hit tiles' geographic bounding box by one L14 tile span and
  re-enumerates it with `marine_survey_index::tilesForBoundingBox` (footprint tiles
  are grouped by level first, so a mixed-level index footprint is handled). The
  rollup applies `gggs::parent()` iteratively (L14 → L10 = four applications;
  there is no multi-level `parentAt`).
- The expansion (step 2) pads the **single bounding box** of all hit tiles at a
  level, not each connected component. For a campaign that ensonifies two or more
  geographically disjoint areas in one incremental batch, the padded box spans the
  gap between them, so tiles in the empty corridor enter the dirty set. This stays
  a conservative superset — every genuinely-dirty tile is still included, and the
  spurious ones rebuild bit-identically (only wasted time). It is accepted for
  simplicity; if multi-area batches ever make the waste material, expand each
  connected component's box separately instead of the global box.
- The contributing-pass set attached to each dirty tile is re-queried over that
  tile's **full** index-level extent (its own bounds re-enumerated), not just the
  new-bag footprint, so an old-bag pass in a sub-tile the new bags did not touch
  is still reported. This is required for a tile-scoped rebuild to replay every
  pass that touches the tile (byte-identity with full regen).
- If `marine_survey_index` is absent (no DB file), unopenable, or not a valid
  index, the dirty set cannot be computed and the caller falls back to full regen
  (explicitly logged).
- An **index miss** — an empty dirty set for a non-empty new-bag list — is treated
  the same way. Bag paths are matched exactly against `bags.path`, so a bag that
  was never indexed (or is spelled differently than at index time) yields zero
  rows and is indistinguishable from an indexed bag that recorded no passes. The
  caller therefore falls back to full regen rather than reporting "nothing dirty":
  conservative in both cases, and correct in the one that matters.
- A changed sonar TPU model (new `Parameters` that changes `influenceRadius`) does
  not invalidate this decision — the one-L14-tile margin already covers realistic TPU
  bounds. If TPU grows pathologically, a full regen is available via `--fresh`.
- This ADR applies to the bathy dirty set; the backscatter store shares the same
  footprint and uses the same dirty L10 set (ADR-0007 addendum).

## Amendment 2026-09-14 — depth-adaptive stores ([cube_bathymetry#143](https://github.com/rolker/cube_bathymetry/issues/143))

The store may now hold **native tiles at several GGGS levels on the same
ground**, written by `import_bag --depth-adaptive` from a *level plan*. This
amendment records the decisions behind that plan (the operator ruled out a new
ADR for them; they live here because the dirty-tile math below depends on them)
and re-argues the margin for a multi-level store.

### Decisions carried here

1. **Unit of decision: the emitted GGGS tile, chosen top-down as a quadtree
   *prefix*, not a cut.** Descending from every touched grid at the policy's
   coarsest level, a grid is emitted when its *target* level is at least its own
   level and refined into its touched children while its target is finer; a
   child whose own target is coarser than its level is left to its parent. So a
   coarse parent keeps a complete estimate under the finer tiles that refine
   part of it — it is the LOD level above them and the native tile for the
   unrefined remainder. uma ADR-0011's overview pyramid skips a parent slot that
   holds a native tile, so the coarse LOD shows the parent's own CUBE estimate.
   Rejected: per-tile at a fixed level (circular), per-region at a fixed
   decision level (exact nesting forces a 3.5 km region, so one shoal drags 3.5
   km of ground fine), a quadtree *cut* (discards the coarse estimate the LOD
   needs, forces a leaf filter that drops seam tiles, and makes any later
   re-tiling a destructive replace), and a depth-only decision (cannot tell
   "shallow and well sounded" from "shallow and sparse").
2. **Two resolutions decide the target.** The level the survey *requires* is
   the uma#369 depth ladder (`depthAdaptiveLevel`, cell = `capture_distance_scale
   × |depth|`, clamped to `[coarsest, finest]`) at the tile's *decision depth*:
   the 2nd percentile of the shallowest soundings per level-14 grid (a flier
   guard), rolled up as the minimum over touched children. The level the data
   *achieves* is Calder's level of aggregation (B. R. Calder, *Resolution
   Determination through Level of Aggregation Analysis*, US Hydro 2019) over a
   count grid: the smallest box around each occupied cell holding `n_req`
   soundings gives the finest spacing the data supports there; the tile's
   achieved level is the 95th percentile of that over its occupied cells,
   rounded toward the coarser level. The target is the **coarser** of the two.
   Where required is finer than achieved, the plan reports a *coverage
   deficit*: the survey has not collected the data its depth calls for.
3. **Touched sets are per level.** A level-L tile is touched when some
   occupied count cell, expanded by `max(the soundings' maximum spread radius,
   the level-L cell)`, intersects it — the same floor `influenceRadius` applies
   and `GeoMapSheet`'s selection window adds. At a single level the emitted set
   is exactly the set of grids the fixed-level import selects, which is what
   makes the single-level equivalence test byte-identical.
4. **Capture distance is spacing-aware.** The node accepts a sounding within
   `max(capture_distance_scale × |depth|, capture_spacing_scale × node spacing)`,
   `capture_spacing_scale` = 0.71 (half the cell diagonal, so every sounding
   reaches at least one node). Calder's hard-coded 0.5 m floor
   (`Capture_Distance_Minimum`, the CUBE User Manual's shallow-water starvation
   guard, which the manual itself says to lower to half the grid spacing) is
   removed: with a fixed floor a coarse parent above the ladder level had cell
   corners no node captured, and every grid finer than ~0.45 m averaged over the
   same 0.5 m disc. The physical floor (horizontal positioning error) moves into
   the uma#369 policy ([uma#386](https://github.com/rolker/unh_marine_autonomy/issues/386)).
5. **Invariant: `finest_level <= 14`, the survey index's footprint level.**
   Validated at startup by both tools. Everything below rests on it.
6. **A later import at other levels is additive**, like any other import; there
   is no re-tiling guard and no `--replace-tiling`. What that leaves behind: a
   re-import over already-covered ground under a different policy leaves the
   earlier import's finer native tiles in place, and a fine-LOD reader prefers
   those over the newer, coarser, complete estimate — the same additive-merge
   contract the store has for same-level re-imports (`importTiles` merges,
   `save()` never deletes).

### Dirty-tile math for a level plan

`survey_index_query.cpp::dirtyTiles(db, bags, plan)` (the fixed-level function
is renamed `dirtyTilesAtLevel`; it already took a store level, so "L10" was a
misnomer) takes the same L14 footprint plus one-tile margin (steps 1–2 above)
and rolls each expanded tile up to the **emitted tile at every level the plan
holds over it**. Parents are estimated in full under their children, so each
one is dirty; the result is a conservative superset exactly as before.

The one-L14-tile margin survives unchanged: with `finest_level <= 14` no
emitted tile is finer than the index footprint, so the ~54 m margin still
dominates the ≤3 m influence radius at every level. The roll-up **throws**
(`ancestorAtLevel`'s existing check) if an index footprint tile is coarser than
an emitted tile over it — the index footprint level is read from the DB and may
be mixed — and the dry-run's catch falls back to full regen rather than trusting
a mis-levelled dirty set.

`batch_regen --level-plan` scatters every batch to every emitted tile at every
level (each level's routing sheet admits only the plan's tiles at that level,
the same admission the import applies) and gathers each tile at its own level;
its output is byte-identical to the import's `MultiLevelAccumulator`.
