# Plan: Evict → Revisit Loses Blunder Gate on Reference-Only Tiles

## Issue

https://github.com/rolker/cube_bathymetry/issues/118

## Context

When a reference-only tile (no survey layer, e.g. unsurveyed nearshore with chart/ENC
prior) is evicted under the LRU budget and then revisited, the blunder gate and slope
correction are silently lost. There are two call sites:

- **Offline (`ImportAccumulator::reloadEvictedTile`, `store_import.cpp:545`)**: restores
  only `SourceLayer::Survey` from the store. `seeded_` is never cleared on eviction, so
  `seedNewTile` cannot re-fire for that tile. A reference-only tile has no survey layer
  to restore, so it comes back with no predicted surface and no blunder gate.

- **Live node (`CubeBathymetryNode::reloadEvictedTile`, `cube_bathymetry_node.cpp:1160`)**:
  likewise restores only `SourceLayer::Survey` from `draft_dir_`. `prior_store_dir_` is
  not consulted on revisit; the `BathymetryStore` object loaded at `on_configure` is
  discarded after the prime. Both call sites carry "deferred as #118" comments from PR #127.

Since PR #122 merged (#59 touchdown interpolation), a missing predicted surface also
silently disables live slope correction on revisited tiles, raising the stakes beyond
the original gate-loss issue.

PR #127 established the layered re-prime semantics (Chart first, Reference overwrites,
exact-level only for the live node; Chart + Reference + cross-level Reference fallback
for the offline path's `seedNewTile` rung 2). The fix here must restore the SAME layered
semantics at both call sites.

## Approach

Fix: extend `reloadEvictedTile` at both call sites to re-apply the reference prior after
restoring the survey settled state. No state-machine changes to `seeded_` / `evicted_`
are needed — the fix is localized to the reload path.

### Design decision — live node re-prime mechanism

At `on_configure` the `BathymetryStore prior` object is local and discarded. Two options
for the revisit re-prime:

**A. Per-tile `loadWindow` on revisit (proposed).** On each revisit, call
`loadWindow(scratch, prior_store_dir_, sw, ne, nullptr)` into a temporary store, then
prime Chart and Reference exact-level tiles from it. Mirrors the existing per-tile
`loadWindow` already used in `reloadEvictedTile` for the survey layer (and in the
offline `seedNewTile` path). Zero additional RAM cost. Disk I/O is bounded by the
revisit rate, which on a long survey is low (a tile is revisited at most a few times
per session). Consistent with `#70` RAM budget discipline.

**B. Retain `BathymetryStore prior_` as a member.** Store the loaded prior object as a
class member, keep it alive after `on_configure`. Avoids disk reads on revisit. RAM cost
is the full prior tile map for the session. Acceptable for typical region-scoped prior
stores (operator already directed `prior_store_dir` to a small region store to avoid
the configure-time RAM spike noted in the existing comment at line 270).

Proposed: **Option A** (per-tile disk read). Rationale: same pattern as the existing
reload in `reloadEvictedTile`, zero RAM impact, RAM discipline is the stated priority
(#70). Operator adjudicates at plan-review if RAM avoidance is preferred.

### Step-by-step

1. **`ImportAccumulator::reloadEvictedTile` (`store_import.cpp:545`)** — after the
   survey-layer restore, add reference prior re-seeding that mirrors `seedNewTile`'s
   rung 2:
   - If `cfg_.reference_store_dir` is non-empty, `loadWindow` into a scratch store.
   - Chart exact-level prime first (`primeFromTile(..., seed_settled=false)`).
   - Reference Phase A exact-level prime (`primeFromTile(..., seed_settled=false)`).
   - Reference Phase B cross-level fallback (call `primeFromTileResample`, the
     namespace-local helper already in `store_import.cpp`; accessible from the same TU).
   - Emit `std::cerr` INFO line when any prior re-prime fires (auditability, #118 review
     action: "observable re-prime logging").

2. **`CubeBathymetryNode::reloadEvictedTile` (`cube_bathymetry_node.cpp:1160`)** — after
   the draft-survey restore, add prior re-seeding:
   - If `prior_store_dir_` is non-empty, `loadWindow` into a scratch store for this tile.
   - Chart exact-level first, Reference exact-level overwrites (same semantics as
     `primeFromPriorLayers`, exact-level only — cross-level fallback is not needed
     because at `on_configure` the live node already only applies exact-level primes).
   - Use `cube::primeFromTile` (public API in `store_import.h`).
   - `RCLCPP_INFO_STREAM_THROTTLE` when re-prime fires (throttled to avoid log spam on
     repeated revisits).

3. **New regression test (`test_import_eviction.cpp`)** — `ReferenceOnlyTileEvictRevisit`:
   - Build a dense shallow reference tile (no survey data) and a reference store.
   - Survey the tile with gate-accepting soundings, forcing eviction by covering many
     other tiles.
   - Revisit with a clearly-too-deep blunder sounding.
   - Assert: the blunder sounding is NOT settled (the gate is active on revisit, cell map
     is empty or no deep cell). This follows the shape of `ChartLayerSeedRejectsDeepBlunder`
     and `CoarseLevelReferenceSeedRejectsDeepBlunder`.

4. **Remove the two "deferred as #118" comments** (`cube_bathymetry_node.cpp:240`,
   `cube_bathymetry_node.cpp:269`).

5. **Update `store_import.h` private doc comment for `reloadEvictedTile`** to reflect
   that it now also re-seeds the reference prior on reload.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/store_import.cpp` | `ImportAccumulator::reloadEvictedTile`: add Chart+Reference prior re-seeding after survey restore; INFO log on re-prime |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | `CubeBathymetryNode::reloadEvictedTile`: add Chart+Reference prior re-seeding (exact-level) via per-tile `loadWindow`; INFO log; remove "deferred as #118" comments |
| `cube_bathymetry/test/test_import_eviction.cpp` | Add `ReferenceOnlyTileEvictRevisit` test |
| `cube_bathymetry/include/cube_bathymetry/store_import.h` | Update private method doc comment for `reloadEvictedTile` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Safety First (project) | Fix closes the primary false-deep exposure path on nearshore unsurveyed-but-charted tiles; regresion test enforces it mechanically |
| Human control and transparency | Observable re-prime logging at both call sites makes gate activation visible in logs |
| Enforcement over documentation | Regression test enforces the gate invariant after evict/revisit (not just comments) |
| A change includes its consequences | Doc comment updated; no interface changes; test included |
| Only what's needed | Fix is localized to `reloadEvictedTile` at both call sites; no new abstractions |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (tile eviction + lossless reload) | Yes | Fix extends the lossless reload to include predicted-surface restoration, completing the "CUBE state is always complete after reload" guarantee to include the blunder gate |
| ADR-0008 (predicted-surface interpolation geometry) | No | Predicted-surface seeding semantics unchanged; we re-apply the same `primeFromTile`/`primeFromTileResample` calls already established |
| workspace ADR-0013 (progress.md vocabulary) | Yes | This entry |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `reloadEvictedTile` (offline) adds reference prior | `store_import.h` private doc comment | Yes — step 5 |
| `reloadEvictedTile` (live node) adds prior re-prime | Remove "deferred as #118" comments | Yes — step 4 |
| New test for evict/revisit gate | Existing test infra (same `makeTempDir` / `surveyCell` helpers) | Yes — step 3 reuses helpers |

## Documentation & Instruction Impact

- **Stale docs**: None — no public interface changes; the store_import.h comment for the
  private `reloadEvictedTile` method is updated as part of step 5 (a stale-doc fix, not
  new content).
- **Agent-instruction candidates**: The `seeded_` + `evicted_` state machine is a subtle
  invariant. A note in `.agent/knowledge/` about the evict/revisit re-prime pattern (both
  call sites must restore the reference prior, not just the survey layer) would prevent
  recurrence in future PRs that touch the eviction path. Proposed; operator decides.

## Open Questions

- [ ] **Live node re-prime mechanism**: Plan proposes per-tile `loadWindow` (Option A,
  zero RAM cost). If the repeated disk reads are a concern for surveys with high revisit
  rates, retain `prior_store_` as a member (Option B). Operator adjudicates.
- [ ] **Cross-level fallback for live node**: Plan aligns live node to exact-level only
  (same as on_configure `primeFromPriorLayers`). If cross-level resample support is
  wanted on the live node revisit path, it can be added using the same
  `primeFromTileResample` helper exposed from `store_import`. Operator adjudicates.

## Estimated Scope

Single PR.
