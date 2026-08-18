# Plan: Live chart-prior predicted-surface seeding for blunder gating

## Issue

https://github.com/rolker/cube_bathymetry/issues/91

Also closes: https://github.com/rolker/cube_bathymetry/issues/119

## Context

The offline `import_bag --reference-store` path (landed in #89, #96) seeds
CUBE's predicted surface from a prior store's `Reference` layer at each tile's
first touch, turning on the blunder gate so false-deep detections are rejected.
The live `cube_bathymetry_node` has no equivalent: it has no `prior_store_dir`
parameter and no predicted-surface prime at configure time, so live cubing runs
ungated.

Two related gaps are bundled here:

- **#91** — the live node needs a `prior_store_dir` parameter that primes the
  predicted surface at `on_configure` from both the `Reference` and `Chart`
  layers of the store (Reference takes precedence where they overlap).
- **#119** — the offline import's per-tile seeding (`seedNewTile`) currently
  reads only `SourceLayer::Reference` from the reference store window; the
  `SourceLayer::Chart` layer is never consulted, so a Chart-derived contour in a
  store that has only a `chart/` layer provides no blunder gate.

A shared helper `primeFromPriorLayers(store, sheet)` with two call sites fixes
both. Its side-effect is also noted: with `#59` merged, a non-NaN predicted
surface activates live slope-aware incidence correction, so this prime also
unblocks live slope correction without self-bootstrapping circularity.

**Archive reference** (`archive/issue-91-2026-06`): a June-2026 prototype
exists. Design intents preserved: predicted-only prime (gates, never fills),
warn-and-continue on bad/empty prior. Key differences: the archive read only
the `Chart` layer; this plan reads both `Reference` and `Chart` (orchestrator
note 1), applies budget bounding after the prime (orchestrator note 3), and
bundles #119 via the shared helper (orchestrator note 2).

## Approach

### 1. Add `PriorLayerPrimeResult` struct and `primeFromPriorLayers()` to `store_import.h`

Declare a result struct that carries per-layer tile counts:

```cpp
struct PriorLayerPrimeResult {
  std::size_t reference_tiles = 0;   ///< exact-level tiles primed from Reference
  std::size_t chart_tiles = 0;       ///< exact-level tiles primed from Chart
  std::size_t level_mismatched = 0;  ///< tiles skipped: not at the survey level
  std::size_t total() const { return reference_tiles + chart_tiles; }
};
```

Declare the free function in the `cube::` namespace alongside `loadIntoSheet`:

```cpp
/// Prime both Reference and Chart prior layers (predicted-only, seed_settled=false),
/// EXACT-LEVEL ONLY (plan-review must-fix): a multi-level prior store (#115 ENC
/// case) holds coarse tiles keyed at their own level, and primeFromTile has no
/// cross-level guard — priming a coarse tile would seed wrong-geometry cells.
/// Mismatched tiles are counted and skipped (cross-level needs the resample path).
/// Chart tiles are primed first (lower priority); Reference tiles are primed on top
/// (higher priority, overwrites where layers overlap). Neither seeds settled
/// hypotheses — the prior gates but never fills (no contamination of survey or
/// co-estimated backscatter). Does not mark the sheet dirty.
PriorLayerPrimeResult primeFromPriorLayers(
  const marine_bathymetry_store::BathymetryStore & store,
  GeoMapSheet & map_sheet);
```

The survey level comes from the sheet: add a public `GeoMapSheet::gridLevel()`
accessor for the existing private `grid_level_`.

### 2. Implement `primeFromPriorLayers()` in `store_import.cpp`

```cpp
PriorLayerPrimeResult primeFromPriorLayers(
  const mbs::BathymetryStore & store, GeoMapSheet & map_sheet)
{
  PriorLayerPrimeResult r;
  // Chart first — lower priority; Reference overwrites where they overlap.
  loadIntoSheet(store, mbs::SourceLayer::Chart, map_sheet, /*seed_settled=*/false);
  r.chart_tiles = store.tiles(mbs::SourceLayer::Chart).size();
  loadIntoSheet(store, mbs::SourceLayer::Reference, map_sheet, /*seed_settled=*/false);
  r.reference_tiles = store.tiles(mbs::SourceLayer::Reference).size();
  return r;
}
```

### 3. Update `ImportAccumulator::seedNewTile()` in `store_import.cpp` — fix #119

Current code (Reference only, roughly):
```cpp
// Rung 2 -- reference: exact-match then cross-level fallback
const auto & tiles = ref.tiles(SourceLayer::Reference);
auto it = tiles.find(index);
if (it != tiles.end()) {
  primeFromTile(it->second, sheet_, false);
} else {
  // cross-level fallback via primeFromTileResample ...
}
```

**Revised per plan-review must-fix**: do NOT swap in the whole-window
`primeFromPriorLayers` here — `seedNewTile` seeds ONE tile from a *windowed*
store whose window also holds neighbor tiles and (in the #115 ENC case)
coarse-level tiles; a whole-window prime would seed wrong-geometry cells from
coarse tiles and churn neighbor tiles on every seed. Instead keep the existing
exact-match `find(index)` structure untouched and add only a **Chart
exact-match lookup** before the Reference one (Chart lower priority, so a
same-level Reference tile primed after overwrites where both cover a cell):

```cpp
// Chart exact-level prime FIRST (#119): since the reference→chart split,
// official chart products live in the Chart layer, which this gate never
// consulted -- charted-waters imports ran ungated. Chart cross-level
// resampling is deferred (the #115 fallback below stays Reference-only).
const auto & chart_tiles = ref.tiles(mbs::SourceLayer::Chart);
auto chart_it = chart_tiles.find(index);
if (chart_it != chart_tiles.end()) {
  primeFromTile(chart_it->second, sheet_, /*seed_settled=*/false);
}
// ... existing Reference exact-match + #115 cross-level fallback, unchanged ...
```

This preserves the #115 cross-level Reference behavior (and its
boundary-containment logic) byte-for-byte while adding Chart exact-level
support. `primeFromPriorLayers` (level-scoped) serves the live node, which
loads a whole store rather than a per-tile window.

### 4. Add `prior_store_dir` param and prime call to `cube_bathymetry_node.cpp` — #91

After the `max_resident_tiles_` declaration (required: budget bounding runs on
the primed tiles) and before the draft warm-start block (chart prior sets
predicted surface; draft warm-start sets settled hypotheses — both coexist;
an already-surveyed cell gets its finer draft-derived predicted depth from the
warm-start that follows):

```cpp
// Prior-store predicted-surface prime (#91/#119): seed CUBE's predicted surface
// from both the Reference and Chart layers of prior_store_dir so the blunder
// gate rejects false-deep detections live (Node::insert is a pass-through when
// predicted_depth_ is NaN). Predicted-only (seed_settled=false): the prior gates
// but never settles — no contamination of the survey layer or its co-estimated
// backscatter. Runs BEFORE the draft prime so a draft-warm-started cell keeps
// its finer settled depth; the predicted surface is set by the prior for
// unsurveyed-but-charted cells.
//
// Side-effect (#59): a non-NaN predicted surface activates live slope-aware
// incidence correction — this prime is also what unblocks live slope correction
// without the self-bootstrapping circularity of deriving the surface from the
// survey being corrected.
//
// Configure-time limitation: the prime runs ONCE on configure; evict/revisit
// re-priming over a long survey (so the blunder gate stays active for revisited
// tiles) is tracked as #118.
//
// A lifecycle node WARNS and continues ungated on a bad/empty/level-mismatched
// prior — a misconfigured prior must NEVER take down live perception.
prior_store_dir_ = declare_parameter("prior_store_dir", std::string(""));
if (!prior_store_dir_.empty()) {
  try {
    marine_bathymetry_store::BathymetryStore prior =
      marine_bathymetry_store::BathymetryStore::fromCellSize(
      static_cast<float>(cell_size_));
    marine_bathymetry_store::load(prior, prior_store_dir_);
    const cube::PriorLayerPrimeResult r =
      cube::primeFromPriorLayers(prior, *geo_map_sheet_);
    if (r.total() == 0) {
      RCLCPP_WARN(get_logger(),
        "prior_store_dir='%s' has no Reference or Chart tiles; predicted "
        "surface NOT seeded, blunder gate INACTIVE. Check the path and that "
        "the store has a reference/ or chart/ layer.", prior_store_dir_.c_str());
    } else {
      RCLCPP_INFO(get_logger(),
        "Primed predicted surface from prior store '%s': %zu Reference + "
        "%zu Chart tile(s). Blunder gate ACTIVE (#91). Also activates live "
        "slope correction (#59).",
        prior_store_dir_.c_str(), r.reference_tiles, r.chart_tiles);
    }
    // Budget-bound the prime (#70, ADR-0001): loadIntoSheet loads the whole
    // store; without this trim a large prior re-creates the unbounded-RAM
    // condition #70 prevents. Primed tiles carry no new soundings and are
    // never dirty (primeFromPriorLayers does not mark the sheet dirty), so
    // evicting them is lossless — they contribute only the predicted surface
    // which is not stored per-tile and cannot be reloaded on revisit (#118).
    trimResidentToBudget();
  } catch (const std::exception & e) {
    RCLCPP_WARN(get_logger(),
      "Could not load prior store from '%s': %s (continuing ungated).",
      prior_store_dir_.c_str(), e.what());
  }
}
```

Add `prior_store_dir_` to the private members section alongside `draft_dir_`.

### 5. Add tests in `test_store_import.cpp`

Mirror `SeededPredictedSurfaceRejectsDeepBlunder` for the new helper:

**`PriorLayerReferenceRejectsDeepBlunder`** — store with Reference tiles:
- Build a shallow-surface store with `SourceLayer::Reference` tiles
- Call `primeFromPriorLayers(ref_store, sheet)`
- Add false-deep soundings; verify they are rejected at primed cells

**`PriorLayerChartRejectsDeepBlunder`** — store with Chart tiles:
- Build a shallow-surface store with `SourceLayer::Chart` tiles (import via
  `importTiles` on a `chart_staging_writable` store)
- Call `primeFromPriorLayers(chart_store, sheet)`
- Add false-deep soundings; verify they are rejected at primed cells

**`PriorLayerReferencePrecedesChart`** — Reference overwrites Chart:
- Build stores: Chart at depth -20 m, Reference at depth -10 m, at the same cells
- Call `primeFromPriorLayers(combined_store, sheet)`
- Add a sounding at -15 m: must be REJECTED (Reference gate at -10 m is active, not the Chart -20 m gate)

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/geo_map_sheet.h` | Add public `gridLevel()` accessor (level scoping for the prime helper) |
| `cube_bathymetry/include/cube_bathymetry/store_import.h` | Add `PriorLayerPrimeResult` struct and `primeFromPriorLayers()` declaration |
| `cube_bathymetry/src/store_import.cpp` | Implement level-scoped `primeFromPriorLayers()`; add Chart exact-match rung to `seedNewTile()` (fixes #119) |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | Add `prior_store_dir` param, prime call, budget trim, log/warn; add `prior_store_dir_` member |
| `cube_bathymetry/test/test_store_import.cpp` | Four helper tests: Reference-only gate, Chart-only gate, Reference-over-Chart precedence, level-mismatch skip |
| `cube_bathymetry/test/test_import_eviction.cpp` | End-to-end #119 regression: Chart-only prior store gates a false-deep import via `seedNewTile` |

**Trim interplay note** (plan-review suggestion): the live node runs two
sequential `trimResidentToBudget()` calls (after the prior prime, and after the
draft warm-start prime). Prior-primed tiles are clean and dataless, so either
trim may evict them; being predicted-only they are not reloadable on revisit and
lose their gate — the known evict/revisit re-priming gap, explicitly deferred as
cube#118 and documented at the call site.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | `prior_store_dir` is opt-in (default ""), all warn/info logged loudly; warn-and-continue means a misconfigured prior is visible but not disruptive |
| A change includes its consequences | Tests cover the live-path gate; #59 slope-correction side-effect documented in code comment and PR body; #118 scoped out with an explicit reference |
| Test what breaks | Tests mirror the existing `SeededPredictedSurfaceRejectsDeepBlunder` — they target the regression that matters (false-deep sounding accepted without prior, rejected with it) |
| Improve incrementally | Single PR closing #91 and #119 via one shared helper; #118 (evict/revisit re-priming) explicitly deferred |
| Only what's needed | No new layers, no new store formats; reuses `loadIntoSheet` and `primeFromTile` machinery already in-tree |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (long-duration bounding) | Yes — `primeFromPriorLayers` loads the whole prior store like the draft prime | `trimResidentToBudget()` called after the prime (same as the draft warm-start pattern); limitation noted in comment |
| ADR-0008 (project: predicted-surface touchdown interpolation, #59) | Yes — this prime supplies the predicted surface that activates live slope correction | Predicted-only seed via `primeFromTile`; gates-not-fills upheld; side-effect documented in code comment and PR body |
| ADR-0002 (store conventions, workspace) | Yes — reads `Reference` and `Chart` layers from a BathymetryStore | Uses the established `SourceLayer` enum and `tiles()` / `primeFromTile` API; no writes to prior store |
| ADR-0013 (progress.md vocabulary) | Yes — plan entry written to progress.md | `## Plan Authored` entry written after committing this file |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `primeFromPriorLayers()` signature/behavior | Any future caller (live node, seedNewTile, tests) | Yes — both call sites and tests are in this PR |
| `seedNewTile()` reference-gate logic | `test_import_eviction.cpp` (existing tests must still pass) | CI verification only — no test file edits needed beyond the new tests |
| `cube_bathymetry_node.cpp` (new param) | Launch-file documentation / README if any param table exists | See Documentation Impact below |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): None — there is no per-parameter
  reference doc for `cube_bathymetry_node` parameters; the authoritative
  documentation is the node's inline `RCLCPP_INFO`/`RCLCPP_WARN` calls and the
  PR description.
- **Agent-instruction candidates** (proposals only — operator decides):
  The `trimResidentToBudget()` call after a whole-store prime is now used by
  two code sites (draft warm-start and prior prime). If a third arises, the
  comment pattern "bound the prime to the resident budget (#70)" is a candidate
  for a `.agent/knowledge/` pattern note.

## Open Questions

- [ ] No open questions — all four issue-review action items were answered by the
  orchestrator notes before this plan was written. The plan is review-plan-ready.

## Estimated Scope

Single PR closing both #91 and #119. Four files changed; three new test cases.
No new ADR needed (design reuses established patterns).

## Implementation Notes

*(none yet — to be added inline as the implementation diverges from this plan)*
