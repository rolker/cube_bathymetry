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
   - `CoarseLevelChartSeedRejectsDeepBlunder` — a coarse-only Chart layer under
     an L10 survey (mirrors `CoarseLevelReferenceSeedRejectsDeepBlunder`,
     `SourceLayer::Chart` instead of `Reference`). The 8 m fixture the test
     actually uses is **L7**, not L8 as an earlier draft of this line said.
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
| Project ADR-0008 (predicted-surface touchdown interpolation geometry) | **Yes** | It names the Chart/Reference prior-seeding path, so it IS triggered — the earlier "No" was a wrong verdict, not a wrong compliance claim. The change **complies**: ADR-0008 governs bilinear touchdown interpolation in a different function (`Node::insert`/`cube_grid_interpolate`), and `primeFromTileResample` (nearest-neighbour resample for priming) keeps its existing geometry — Chart reuses the already-ADR-compliant Reference resample path unchanged. |
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

### Round-1 review pass (pre-push `review-code`, verdict changes-requested)

The core Chart cross-level fix reviewed clean. The *second* defect (the
silent-no-op warning) had four correctness holes of its own, and four
operator-facing documents still described the exact-level-only Chart behaviour
this branch reverses. Both were fixed as coherent groups rather than
finding-by-finding:

- **A "hit" now means a primed CELL, not a matched tile.**
  `primeFromTileSkippingMask` / `primeFromTile` / `primeFromTileResample` return
  the number of cells they primed. A prior tile that covers the survey tile but
  is no-data over it primed nothing yet counted as a hit, permanently
  suppressing the warning — the header comment claiming otherwise was false.
  Consequences taken with it: Phase A no longer short-circuits Phase B (a hollow
  exact-level tile falls through), and `findCrossLevelPrior` became
  `findCrossLevelPriors`, returning every containing coarser tile finest-first so
  an empty finest prior falls through to the next-coarser one.
- **`PriorPrimeTally`** replaced the three loose counters. It is a public struct
  because `batch_regen` needs to merge one per gathered tile. It splits out
  `read_failures` (an unreadable store must never be reported as "no tiles
  overlap the surveyed area" — `found_layers_out` was populated *after*
  `loadWindow` inside the same `try`) and `survey_warm_starts` (a rung-1
  warm-start returns before the prior rung, so "INACTIVE for this entire run"
  could be false; it now reads "for every tile that reached the prior rung" and
  names the warm-started tiles separately). The unit is "attempt(s)", since a
  revisited tile re-primes and counts again. `audit_seen` de-duplicates the
  cross-level audit line to once per (layer, level).
- **The guard is now reachable from `batch_regen`** — the authoritative off-boat
  rebuild, which takes the same `--reference-store` and prints the same banner
  but calls `persistResidentTile`, never `ImportAccumulator::finalize`. It merges
  the per-tile tallies and calls the shared `reportPriorPrimeOutcome` once.
- `reportPriorPrimeOutcome` is emitted **first** in `finalize()` (the persists
  below have no `try` around them) and **once** (a second `finalize()` would
  otherwise repeat it).
- **Documentation sweep**: README seed precedence, both `--reference-store` help
  strings, both startup banners (which now say outright that the banner is not
  evidence the gate engaged), the `finalize` / `seedNewTile` /
  `primeFromTileResample` Doxygen, and ADR-0001's rung 2 — including extending
  its recorded widened-gate false-reject trade-off to Chart, where it is now the
  dominant gating path.
- **Tests**: `MatchedButEmptyChartPriorStillWarns`,
  `EmptyExactLevelChartPriorFallsThroughToCoarsePrior`,
  `BatchRegen.PriorThatPrimesNothingWarnsFromGather`; a `StderrCapture` RAII
  wrapper in both test files (gtest's capture is single-capturer, and a throw
  before the release aborts the whole binary at the next capture);
  "exactly once" and second-`finalize()` assertions on the warning; the
  cross-level audit line asserted in `CoarseLevelChartSeedRejectsDeepBlunder`;
  and the no-prior baseline added to
  `BoundaryFlushCrossLevelChartRejectsDeepBlunder`. All three new tests were
  verified to FAIL against the pre-fix behaviour. Suite: **563 tests, 0
  failures** (was 560).

### Deferred — operator decides, NOT implemented

1. **Bound how coarse a Chart prior may be.** An L2 chart tile is ~232 m/cell
   under an L10 survey, and #137 makes Chart the dominant gating path — so the
   coarse/shallow-biased false-reject mode (recorded in ADR-0001 for Reference)
   now applies at potentially very large resample gaps. Options: cap the level
   gap, scale the blunder margin with the gap, or make cross-level Chart priming
   opt-in. **Explicitly held for the operator at the publish checkpoint**;
   recorded in ADR-0001 as an open question, not decided. Today it is unbounded
   and the audit line names the level used, so a large gap is visible in the log.
2. **Priors FINER than the survey level are still dropped** (`cand.level() >=
   index.level()`). Using one needs an aggregation rule — which of the N finer
   cells gates a survey cell, shoalest or mean — and shoalest-wins is a
   conservatism decision of the same kind as (1). Same checkpoint.
3. **The live node's `primeFromPriorLayers` is still exact-level-only for both
   layers**, so this issue's own premise (an ENC product never has a
   survey-level tile) leaves the gate off *afloat*, where it is safety-relevant.
   Deliberately out of scope here (whole-store bulk prime, different function),
   and worth its own issue — **not filed**: per AGENTS.md the operator is asked
   before an issue is opened, and a dispatched sub-agent cannot ask. Surfaced
   at the publish checkpoint.
4. **Instruction candidate (operator decides, not auto-applied)**: "a layer-keyed
   lookup added for one `SourceLayer` must be checked against every other layer
   sharing the code path" — the third instance of the class (#115, #119, #137).
   Proposed for `.agent/knowledge/`, not written.
