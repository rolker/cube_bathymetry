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

1. **Add `primeFromTileResample` helper** (`store_import.cpp`, anonymous
   namespace — no external caller, matching the open-question default) — takes a
   coarse reference tile and the fine survey `GridIndex`, iterates
   `CellAreaIterator(survey_grid)` over the fine cells, and for each fine cell
   computes its **center** position and maps it to the coarse reference cell.

   Note (plan-review nit): `gggs::CellIndex::position()` returns the cell's **SW
   corner**, not its center, so the fine cell's center is
   `(*it).position()` offset by **+0.5 cell** in latitude and longitude (half the
   survey grid's per-cell angular span). The coarse `CellIndex` is then obtained in
   one call via `gggs::Level(ref_lvl).cellIndex(center)` (which composes
   `gridIndex` + `CellIndex` internally — preferred over the two-step
   `gridIndex(...)` + `CellIndex(grid, pos)` sketch). Guard that the resolved
   coarse grid equals `ref_tile.index()` (GGGS nesting guarantees this for an
   interior center, but a boundary center could round to a neighbour); skip the
   fine cell if it does not match. Read `ref_tile.get(row, col)`; skip NaN
   reference cells; otherwise call `map_sheet.setPredictedDepthAt(*it, depth,
   variance)` deriving `variance` from the stored 1-sigma uncertainty floored at
   `kPrimeVarianceFloor`, exactly as `primeFromTile` does (predicted-only, never
   settled).

2. **Modify `seedNewTile` rung 2** (`store_import.cpp`, after the existing
   `loadWindow` call) — replace the current single `tiles.find(index)` with a
   two-phase lookup:
   - Phase A: same-level exact match (existing `find(index)` path, unchanged).
   - Phase B (new): if Phase A found nothing, scan `tiles` for entries at
     `entry.first.level() < index.level()`, track the finest (highest level number)
     among them, and call `primeFromTileResample` with it. Emit one `std::cerr`
     line naming the fallback level used and the survey tile index.

3. **Add cross-level reference seeding test** in `test_import_eviction.cpp`
   (plan-review placement fix) — `CoarseLevelReferenceSeedRejectsDeepBlunder`.
   That file already carries the on-disk-store + `ImportAccumulator` harness this
   test needs (`makeTempDir`/`makeConfig`/`surveyCell`/`countReferenceFiniteCells`/
   `loadBathyCells`, beside `ReferenceSeedDoesNotAddMeasuredData`);
   `test_store_import.cpp` only exercises the pure helpers and has none of it, so
   placing the test there would duplicate infrastructure. Build a **shallow**
   reference tile at a **coarser** GGGS level (L7, via a `GeoMapSheet`/store at a
   coarser cell size so `mapSheetToTiles` emits L7 tiles), save it to a
   `reference/` store, then run **deep** (false-deep) soundings at that location
   through an `ImportAccumulator` configured with that reference store at the L10
   survey cell size. Assert: WITH the cross-level reference the survey store settles
   **no** deep cell (the level-walk fallback seeds the shallow prior and the blunder
   gate rejects the deep sounding); WITHOUT any reference (baseline) the same deep
   sounding is accepted and a finite deep cell is written. This directly fails if
   the level-walk (Phase B) is reverted, since the exact-level `find` misses the L7
   tile and no prior gates the blunder.

4. **Update README.md** "Seed precedence" section, rung 2: replace "Only tiles at
   the survey GGGS level gate" with the level-walk description and the conservative
   direction rationale.

5. **Update ADR-0001 addendum** rung 2 paragraph to describe the level-walk fallback
   (add one sentence after "predicted-surface only"), and — mirroring how the
   addendum already cites its rung-2 enforcement test
   (`test_import_eviction.ReferenceSeedDoesNotAddMeasuredData`) — name the new
   cross-level enforcement test
   (`test_import_eviction.CoarseLevelReferenceSeedRejectsDeepBlunder`) as the
   enforcer of the level-walk behaviour.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/store_import.cpp` | Add `primeFromTileResample` helper (anonymous namespace); modify `seedNewTile` rung 2 |
| `cube_bathymetry/test/test_import_eviction.cpp` | Add `CoarseLevelReferenceSeedRejectsDeepBlunder` (uses the existing on-disk-store harness) |
| `README.md` | Update "Seed precedence" rung 2 paragraph |
| `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md` | Update rung 2 description + name the enforcement test in the two-rung seed precedence addendum |
| `cube_bathymetry/src/import_bag_main.cpp` | `--reference-store` help text (:144-147) **and** the `:704-706` call-site comment ("harmless no-op") — both state the old restriction |
| `cube_bathymetry/src/batch_regen_main.cpp` | `--reference-store` help text (:83-86) **and** the `:562-563` call-site comment — same restriction |

`store_import.h` is intentionally **not** changed: `primeFromTileResample` stays
internal (anonymous namespace) — no caller outside `seedNewTile`.

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
| `seedNewTile` rung 2 logic | `import_bag_main.cpp` `--reference-store` help text (:144-147) | Yes — drop the "Only tiles at the survey GGGS level gate" restriction |
| `seedNewTile` rung 2 logic | `import_bag_main.cpp:704-706` call-site comment ("a reference tile at another level primes a node the soundings never land on (harmless no-op)") | Yes — **highest priority**: it describes the exact bug as intentional and will actively contradict the fix |
| `seedNewTile` rung 2 logic | `batch_regen_main.cpp` `--reference-store` help text (:83-86) — batch-regen shares `seedNewTile` | Yes — same restriction wording |
| `seedNewTile` rung 2 logic | `batch_regen_main.cpp:562-563` call-site comment ("Only tiles at the survey GGGS level coincide with a survey node and gate") | Yes — same restriction wording |
| `primeFromTileResample` API decision | Keep anonymous (no `store_import.h` change) | Yes — no downstream callers |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `README.md` "Seed precedence" rung 2 paragraph; `cube_bathymetry/docs/decisions/0001-*` addendum rung 2 (+ enforcement-test citation); `import_bag_main.cpp` `--reference-store` help text **and** the `:704-706` call-site comment; `batch_regen_main.cpp` `--reference-store` help text **and** the `:562-563` call-site comment.
- **Agent-instruction candidates** (proposals only): None — the pattern (level-walk nearest-neighbor resample via center-position `CellIndex` lookup) is domain-specific; not a broadly reusable knowledge item.

## Open Questions

- [x] Keep `primeFromTileResample` internal (anonymous namespace in `.cpp`) vs. declared in `store_import.h`. **Resolved: internal** — no caller outside `seedNewTile`; `store_import.h` is left unchanged.
- [x] The level-walk chooses the FINEST available coarser level (highest level number below survey level). **Resolved: finest-available** — aligns with the issue proposal's "finest→coarsest" intent. ENC tiles at L5 are more generalized (shoaler) than L8, so the finest coarser level is closest to the survey; it is still shoal-biased relative to the true seabed, so it remains the conservative direction for a false-deep gate.

## Estimated Scope

Single PR.
