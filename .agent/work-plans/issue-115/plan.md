# Plan: Reference-prior blunder gate silent miss for multi-level reference stores

## Issue

https://github.com/rolker/cube_bathymetry/issues/115

## Context

`ImportAccumulator::seedNewTile` (store_import.cpp, rung 2) loads reference tiles
via `loadWindow` — which correctly picks up tiles at ANY GGGS level — but then
calls `ref.tiles(Reference).find(index)` where `index` is the L10 survey tile
`GridIndex`. Since `std::map::find` compares by key and L5/L7/L8 tiles have
different keys, the lookup always misses when the reference store holds only coarser
tiles. The blunder gate is silently inactive for the whole import.

Confirmed real-world impact: Lewes DE import 2026-08-03, ~14.5% false-deep cells in
tile `10_17252_13419` (nominal −144 m in a 2–46 m bay) entered the authoritative
survey layer despite a valid ENC reference store at L5/L7/L8.

The fix: when the same-level lookup fails, walk the already-loaded reference tiles
at coarser levels (finest-first, i.e. highest available level number below the
survey level), resample the coarse prior onto fine survey cells via center-position
lookup, and emit a diagnostic. Semantics otherwise unchanged: predicted-only,
never settled, no backscatter, `blunder_*` margins apply as today.

## Approach

1. **Add `primeFromTileResample` helper** (`store_import.cpp`, anonymous or
   declared in `store_import.h`) — takes a coarse reference tile and the fine
   survey `GridIndex`, iterates `CellAreaIterator(survey_grid)`, maps each fine
   cell's center position to the coarse reference cell via
   `gggs::Level(ref_lvl).gridIndex(center_lat, center_lon)` → `CellIndex(ref_grid,
   center_pos)` → `ref_tile.get(row, col)`, then calls
   `map_sheet.setPredictedDepthAt(*it, depth, variance)` using `kPrimeVarianceFloor`
   exactly as `primeFromTile` does. Skip NaN reference cells.

2. **Modify `seedNewTile` rung 2** (`store_import.cpp`, after the existing
   `loadWindow` call) — replace the current single `tiles.find(index)` with a
   two-phase lookup:
   - Phase A: same-level exact match (existing `find(index)` path, unchanged).
   - Phase B (new): if Phase A found nothing, scan `tiles` for entries at
     `entry.first.level() < index.level()`, track the finest (highest level number)
     among them, and call `primeFromTileResample` with it. Emit one `std::cerr`
     line naming the fallback level used and the survey tile index.

3. **Add cross-level reference seeding test** in `test_store_import.cpp` —
   `CoarseLevelReferenceSeedRejectsDeepBlunder`: build a shallow tile at a coarser
   GGGS level (e.g. L7 via `BathymetryStore::fromCellSize` at a coarser cell size),
   inject it as the `Reference` layer, run deep soundings through an
   `ImportAccumulator` configured with that reference store, confirm the deep
   blunder is rejected (same assertion pattern as the existing
   `SeededPredictedSurfaceRejectsDeepBlunder` test). Also add a companion
   `CoarseLevelReferenceSeedIsNotSettled` asserting `values()` carries no
   finite cells in the primed-from-coarse grid (gate-only, no contamination).

4. **Update README.md** "Seed precedence" section, rung 2: replace "Only tiles at
   the survey GGGS level gate" with the level-walk description and the conservative
   direction rationale.

5. **Update ADR-0001 addendum** rung 2 paragraph to describe the level-walk fallback
   (add one sentence after "predicted-surface only").

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/store_import.cpp` | Add `primeFromTileResample` helper; modify `seedNewTile` rung 2 |
| `cube_bathymetry/include/cube_bathymetry/store_import.h` | Declare `primeFromTileResample` (or leave internal — decide during implementation) |
| `cube_bathymetry/test/test_store_import.cpp` | Add `CoarseLevelReferenceSeedRejectsDeepBlunder` + `CoarseLevelReferenceSeedIsNotSettled` |
| `README.md` | Update "Seed precedence" rung 2 paragraph |
| `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md` | Update rung 2 description in the two-rung seed precedence addendum |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Diagnostic `std::cerr` line names the fallback level — import log is auditable; operator knows a cross-level prior was active |
| Only what's needed | Change is confined to `seedNewTile` rung 2 + a helper; no structural refactor |
| Test what breaks | Cross-level test added; regression if level-walk is removed will immediately fail |
| A change includes its consequences | README + ADR-0001 updated in this PR |
| Enforce over document | Structural code fix, not doc-only; test enforces the behavior |
| Conservative safety | Coarse ENC tiles are shoal-biased (generalized), so a coarser prior is conservative for a false-deep blunder gate — the right direction |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| cube ADR-0001 (two-rung seed precedence addendum) | Yes | Rung 2 description updated to reflect level-walk fallback; `seed_settled=false` and no-backscatter invariants unchanged |
| workspace ADR-0013 (progress.md vocabulary) | Yes | Progress entries follow schema |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `seedNewTile` rung 2 logic | README "Seed precedence" rung 2 description | Yes — step 4 |
| `seedNewTile` rung 2 logic | ADR-0001 rung 2 paragraph | Yes — step 5 |
| `seedNewTile` rung 2 logic | `import_bag_main.cpp` `--reference-store` help text ("Only tiles at the survey GGGS level gate") | Yes — update the comment in `import_bag_main.cpp` usage() to drop that restriction |
| `primeFromTileResample` API decision | `store_import.h` declaration or keep anonymous | Defer to implementation; no downstream callers yet |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `README.md` "Seed precedence" rung 2 paragraph; `cube_bathymetry/docs/decisions/0001-*` addendum rung 2; `import_bag_main.cpp` `--reference-store` help-text comment.
- **Agent-instruction candidates** (proposals only): None — the pattern (level-walk nearest-neighbor resample via center-position `CellIndex` lookup) is domain-specific; not a broadly reusable knowledge item.

## Open Questions

- [ ] Keep `primeFromTileResample` internal (anonymous namespace in `.cpp`) vs. declared in `store_import.h` — the batch-regen path shares `seedNewTile` and may benefit from a public resample helper in future, but there is no current caller outside `seedNewTile`. Decision left to implementer; prefer internal unless a caller exists.
- [ ] The level-walk chooses the FINEST available coarser level (highest level number below survey level). Is coarser-always-preferred ever the safer conservative choice, or is finest-available always correct? ENC tiles at L5 are more generalized (shoaler) than L8, so L8 is closer to the survey and less conservative. Issue proposal says "finest→coarsest" — align with that.

## Estimated Scope

Single PR.
