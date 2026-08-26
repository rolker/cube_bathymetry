# Plan: Blunder gate cannot use an ENC chart layer (Chart priming is exact-level only)

## Issue

https://github.com/rolker/cube_bathymetry/issues/137

## Context

`ImportAccumulator::seedNewTile` / `reloadEvictedTile` (offline `import_bag`
path, `cube_bathymetry/src/store_import.cpp`) both call the anonymous-namespace
helper `primePriorLayersForTile` (line ~796) to seed a survey tile's predicted
surface from a `--reference-store`. That helper primes `SourceLayer::Chart`
only via an exact `GridIndex` match (`chart_tiles.find(index)`). An
`enc_updater` chart product is built on the chart scale ladder and essentially
never has a tile at the survey's GGGS level, so the Chart exact-level prime
misses every time on a real ENC prior — silently leaving the blunder gate off
for exactly the product the Chart layer exists to hold. `SourceLayer::Reference`
already has a same-function Phase B: a containment-verified, finest-containing
coarser-tile walk plus `primeFromTileResample`, added for the analogous #115
bug. Chart needs the identical fallback.

There is a second, independent bug: `primePriorLayersForTile`'s `bool primed`
return is consumed by `reloadEvictedTile` (which logs on success) but is
**ignored** by `seedNewTile`'s call — a run where `--reference-store` is
supplied and *no* tile in the whole run ever primes produces no diagnostic at
all. The operator sees the "Reference-prior seeding from ..." startup banner
and reasonably concludes the gate is active.

Correction to the review-issue comment on this issue: that comment cites
`test/test_store_import.cpp`'s `PrimeFromPriorLayers*` tests as the existing
pattern to extend. Those tests exercise a **different** function —
`primeFromPriorLayers`/`PriorLayerPrimeResult`, the whole-store bulk prime used
by the *live* node (`cube_bathymetry_node.cpp`), which deliberately stays
exact-level-only for both layers ("cross-level priming is the per-tile
importer's resample path, not this bulk one" — comment at store_import.cpp
line ~349). The actual precedent for this issue's fix lives in
`cube_bathymetry/test/test_import_eviction.cpp`: `ChartLayerSeedRejectsDeepBlunder`
(exact-level Chart, #119), `CoarseLevelReferenceSeedRejectsDeepBlunder`
(Phase B cross-level Reference, #115), and
`BoundaryFlushCrossLevelReferenceRejectsDeepBlunder` (containment/boundary case
for Reference). Those three are the templates this plan follows and extends to
Chart; `test_store_import.cpp` is untouched by this issue.

## Approach

1. **Refactor Phase B into a layer-parameterized helper.** In
   `store_import.cpp`, extract the existing Reference cross-level walk (the
   `for (const auto & entry : tiles)` containment/finest-coarser-tile loop,
   currently inline under the Reference `else` branch) into a small local
   lambda/function `findCrossLevelPrior(const auto & tiles, const GridIndex &
   index)` returning the winning `BathymetryTile*` (or `nullptr`). Keep the
   containment-vs-inclusive-overlap logic byte-for-byte — it is already
   correct and tested (BoundaryFlush test).
2. **Apply Chart exact-level prime, then Chart Phase B fallback, before
   Reference.** Restructure `primePriorLayersForTile` so each layer runs its
   own Phase A (exact `find`) then Phase B (cross-level fallback via the new
   helper) independently, in the existing priority order: Chart first (primes
   `primed=true` on either phase hitting), Reference second (overwrites where
   both cover a cell, same as today). This preserves the documented ordering
   invariant that a Reference exact-or-fallback prime overwrites a Chart one.
3. **Auditability parity.** Emit the same `std::cerr` cross-level fallback
   line the Reference path already has, naming the layer (`"chart"` vs
   `"reference"`) so an import log line reads e.g. `import_bag: chart blunder
   gate for survey tile ... seeded via cross-level fallback (chart level 8 ->
   survey level 10)`. Factor the existing literal message into a small
   helper taking the layer name as a parameter, rather than duplicating the
   `std::cerr <<` chain.
4. **Silent-no-op diagnostic (second defect).** Add an out-parameter to
   `primePriorLayersForTile`, e.g. `bool * primed_out` is already the return
   value — instead add a small struct or two extra out-params carrying what
   layers/levels were *found* in the loaded window (even if none matched a
   usable prior), e.g. `std::set<std::pair<SourceLayer, uint8_t>> *
   found_layers_out = nullptr`, populated from `ref.tiles(layer)` before the
   match logic runs. Give `ImportAccumulator` two new private counters,
   `prior_prime_attempts_` and `prior_prime_hits_`, plus an aggregate
   `std::set<std::pair<SourceLayer, uint8_t>> prior_layers_seen_`, updated at
   every `seedNewTile`/`reloadEvictedTile` call site that invokes
   `primePriorLayersForTile` (guarded by `!cfg_.reference_store_dir.empty()`,
   same guard as today). In `ImportAccumulator::finalize()`, if
   `prior_prime_attempts_ > 0 && prior_prime_hits_ == 0`, emit one `std::cerr`
   warning naming the reference store dir and, if `prior_layers_seen_` is
   non-empty, the layer/level pairs found (so a level mismatch is visible);
   if empty, say no tiles were found under any layer for the surveyed area.
5. **Tests** (`test_import_eviction.cpp`, mirroring the three Reference
   precedents cited above):
   - `CoarseLevelChartSeedRejectsDeepBlunder` — an L8-only Chart layer under
     an L10 survey (mirrors `CoarseLevelReferenceSeedRejectsDeepBlunder`,
     `SourceLayer::Chart` instead of `Reference`).
   - `BoundaryFlushCrossLevelChartRejectsDeepBlunder` — the containment vs.
     edge-adjacent-neighbor case for Chart (mirrors
     `BoundaryFlushCrossLevelReferenceRejectsDeepBlunder`).
   - Regression: `ChartLayerSeedRejectsDeepBlunder` (existing, exact-level)
     stays green unmodified — confirms Phase A still runs first per layer.
   - `NoUsablePriorEmitsWarning` — a `--reference-store` whose only content is
     an out-of-area or level-mismatched tile that primes nothing for the whole
     run; capture stderr with `testing::internal::CaptureStderr()` /
     `GetCapturedStderr()` around `acc.finalize()` and assert the warning
     substring appears exactly once. Also assert a companion "happy path" run
     (chart tile that DOES prime) produces **no** such warning, so the check
     doesn't false-positive on ordinary imports.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/store_import.cpp` | Extract cross-level containment walk into a layer-parameterized helper; apply Phase A+B to Chart before Reference; add found-layers/hit tracking out-params; `ImportAccumulator` counters + `finalize()` warning |
| `cube_bathymetry/include/cube_bathymetry/store_import.h` | Add private `prior_prime_attempts_`, `prior_prime_hits_`, `prior_layers_seen_` members to `ImportAccumulator`; update the `primePriorLayersForTile`/related Doxygen comments if the public-facing contract text changes |
| `cube_bathymetry/test/test_import_eviction.cpp` | Add `CoarseLevelChartSeedRejectsDeepBlunder`, `BoundaryFlushCrossLevelChartRejectsDeepBlunder`, `NoUsablePriorEmitsWarning` (+ a no-warning-on-success companion assertion) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | The core of this issue: today the gate silently no-ops for the exact product (ENC charts) it exists to protect against. Both the Chart cross-level fix and the new warning directly serve this. |
| A change includes its consequences | Test plan covers the new cross-level Chart case, the boundary/containment case, an exact-level regression check, and the new warning (plus a no-false-positive check on the happy path). |
| Only what's needed | Scoped to `primePriorLayersForTile` and its two call sites; no changes to the live-node bulk prime (`primeFromPriorLayers`), which is intentionally exact-level-only for a different (whole-store) reason and out of scope here. |
| Improve incrementally | Same shape/size as the #115 (Reference cross-level) and #119 (Chart layer split) fixes it extends; reuses their tested containment logic via extraction rather than reimplementing it. |
| Test what breaks | Targets the real regression class from the issue (false-deep blunder acceptance under an ENC-scale-ladder chart prior) with field-motivated synthetic geometry (L8 chart under L10 survey, matching the observed Isles of Shoals mismatch). |
| Workspace vs. project separation | Entirely project-repo (`cube_bathymetry`) domain logic; no workspace-level files touched. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| Project ADR-0008 (predicted-surface touchdown interpolation geometry) | No | Governs bilinear touchdown interpolation in a different function (`Node::insert`/`cube_grid_interpolate`); `primeFromTileResample` (nearest-neighbour resample for priming) is unchanged by this plan — Chart reuses the existing, already-ADR-compliant Reference resample path. |
| Workspace ADR-0008 (ROS 2 conventions) | No | No new params, topics, or interfaces — internal `import_bag` store-import logic only. |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `primePriorLayersForTile`'s Chart handling | The Doxygen comment above the function (currently states "Chart cross-level resampling is deferred") | Yes — comment must be corrected to describe the new Chart Phase B, in the same commit as the code change |
| `ImportAccumulator`'s prior-prime tracking | `ImportAccumulator`'s class-level Doxygen and the `reloadEvictedTile`/`seedNewTile` inline comments describing the two-rung precedence | Yes — the "silently inactive" framing in the existing Phase B comment block becomes stale once the warning exists; update it |
| New counters on `ImportAccumulator` | Any existing test that constructs `ImportAccumulator` directly and would need updating for a new constructor/field | No — counters are private members with no public API surface change, so no follow-up needed beyond the new tests |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): The inline comment block above
  `primePriorLayersForTile` (store_import.cpp ~line 780) explicitly says
  "Chart cross-level resampling is deferred (the #115 fallback below stays
  Reference-only)" — this becomes false once Chart gets Phase B and must be
  rewritten in the same commit. The `reloadEvictedTile` Doxygen's "silently
  OFF" framing for the pre-#118 state is unaffected and stays.
- **Agent-instruction candidates** (proposals only — operator decides): None.
  This is a bounded, self-contained bug fix; no new workspace-wide pattern or
  pitfall to promote to `.agent/knowledge/`.

## Open Questions

- None — the fix, test plan, and comment updates are all fully determined by
  the existing Reference Phase B precedent and the issue's own suggested fix.

## Estimated Scope

Single PR.

## Implementation Notes (kept in sync with the branch)

Implemented as planned, with these specifics worth recording:

- The cross-level containment walk was extracted as **`findCrossLevelPrior`**
  (free function in the anonymous namespace), and Phase A + Phase B were
  factored together into **`primeLayerForTile(tiles, index, sheet,
  layer_name)`**. `primePriorLayersForTile` now iterates a two-entry
  `kLayers` array — `{Chart, "chart"}, {Reference, "reference"}` — so the
  Chart-primes-first / Reference-overwrites priority invariant is explicit in
  the data rather than implied by two hand-written blocks. The containment
  logic moved verbatim; only its layer became a parameter.
- The audit line is emitted from `primeLayerForTile` with the layer name
  interpolated, so a chart fallback now logs `import_bag: chart blunder gate
  for survey tile ... (chart level 7 -> survey level 10)`.
- Silent-no-op tracking is `prior_prime_attempts_` / `prior_prime_hits_` plus
  `prior_layers_seen_` (a `std::set<std::pair<SourceLayer, int>>`), fed by the
  new `found_layers_out` out-param at **both** call sites (`seedNewTile` and
  `reloadEvictedTile`). `finalize()` warns once when attempts > 0 and hits ==
  0, naming the store, the tile count, and the layer@level pairs found versus
  the survey level.
- Both plan-review suggestions were folded in: the
  `store_import.cpp` forward-declaration comment gained the `found_layers_out`
  contract alongside the existing `read_ok` text, and
  `NoUsablePriorEmitsWarning` carries an explicit note that
  `testing::internal::CaptureStderr` is new to this file and why asserting on
  stderr is unavoidable here (the warning *is* the behaviour under test).

**Verified the tests bite**: with Chart's Phase B temporarily disabled to
reproduce pre-#137 behaviour, `CoarseLevelChartSeedRejectsDeepBlunder` and
`BoundaryFlushCrossLevelChartRejectsDeepBlunder` both FAIL, while the
exact-level `ChartLayerSeedRejectsDeepBlunder` regression and
`NoUsablePriorEmitsWarning` still pass. Restored, the full package suite is
**560 tests, 0 failures** (was 557 before the three additions).
