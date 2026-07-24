---
issue: 107
---

# Issue #107 — GeoGrid::insert CPU bottleneck: triple map-descent, fat comparator, ellipsoidal per-sounding bounds

## Issue Review
**Status**: complete
**When**: 2026-07-23 14:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #107
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #107 targets a profiling-confirmed CPU bottleneck in `GeoGrid::insert`, the shared hot path
for `import_bag`, the live node, and `batch_regen`. All 8/8 gdb samples landed inside this loop.
Three primary hot-path fixes (findings 1–3) are clearly described, evidence-backed, and
behavior-identical. Four secondary changes (finding 4) are described as "bundle or split as
convenient." The issue is well-scoped with a clear verification plan (bit-exact output + wall-clock
comparison, same discipline as #63/#96).

**Key code verified against the current worktree (`feature/issue-107`):**
- `geo_grid.cpp:102–105`: triple `operator[]` pattern confirmed (three map descents per hit cell).
- `geo_grid.cpp:74`: `gz4d::BoundsDegrees::radiusFromCenter` call site confirmed (ellipsoidal per sounding).
- The `gggs::operator<` comparator lives in `unh_marine_autonomy/cell_index.h` — cross-repo impact
  if option 2b is chosen.

**Cross-repo concern:** Option 2b (gggs-wide `operator<` change) touches `unh_marine_autonomy`'s
shared header and its documented "invalid sorts first" contract. Option 2a (cube-local packed-uint32
key) captures the same hot-path win with a much smaller blast radius and no gggs contract change.
The issue acknowledges "2a alone captures most of the win." This is the key design decision for
the plan.

### Actions
- [ ] Explicitly choose and record option 2a vs 2b for finding 2 before implementation begins.
  Option 2a (cube-local packed uint32 key) is preferred unless the plan identifies a concrete
  reason 2b is needed; if 2b is taken, open a separate issue in `unh_marine_autonomy`.
- [ ] Ensure the bit-exact verification uses a small, committed/reproducible bag subset — not only
  the live 32-bag set — so the regression check is repeatable in CI or future reruns.
- [ ] Verify finding 4 sub-items individually during planning: some (StorageFilter, doTransform hoist)
  are directly on the hot path; others (speed_by_ns pruning) are memory-management changes.
  Scope the plan explicitly.

### Operator decisions (checkpoint 1, 2026-07-23)

- **Finding 2 = option 2a in this repo** (packed uint32 `(row,column)` key for GeoGrid's
  single-grid node map). Option 2b (gggs `operator<` field-lexicographic, no `valid()`
  pre-checks) is deliberately split out and filed as
  [unh_marine_autonomy#270](https://github.com/rolker/unh_marine_autonomy/issues/270) —
  do NOT change gggs headers in this PR.
- **Finding 4 = ALL FOUR sub-items in scope** for this PR: per-bag topic→type map cache,
  rosbag2 StorageFilter on the main-pass readers, `speed_by_ns` pruning, per-ping
  doTransform hoist.
- Review action "reproducible verification subset" stands: plan must pin a small committed
  bag subset for the bit-exact check, not only the live 32-bag Massabesic set.

## Plan Authored
**Status**: complete
**When**: 2026-07-23 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-107/plan.md` at `a50edd5`
**Branch**: feature/issue-107 at `a50edd5`
**Phases**: single

### Open questions
- [ ] Use `unordered_map<uint32_t>` (O(1), recommended) or `map<uint32_t>` (O(log n), simpler swap) for `nodes_`?
- [ ] Bundle Phase B (`import_bag_main.cpp` secondaries) in same PR as Phase A (`geo_grid.cpp` hotspot fixes), or split? Recommendation: same PR.

## Plan Review
**Status**: complete
**When**: 2026-07-23 14:56 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-107/plan.md` at `1634d7e`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

Plan is technically sound, evidence-backed, and aligned with the issue and all
operator checkpoint-1 decisions (2a chosen, uma#270 split, all four finding-4
sub-items in scope, bit-exact via committed synthetic test). The
`unordered_map<uint32_t>` approach was validated against the real `CellIndex`
type: `uint16_t row_/column_` and a single shared `grid_index_` per `GeoGrid`, so
the `(row<<16)|column` packing round-trips losslessly. One must-fix omission plus
three consequence/plumbing tightenings; none change the approach.

### Findings
- [ ] (must-fix) Step 2 omits the `nodes_` range-iteration site in `nodeIntensityWelford()` — `ret.emplace(entry.first, w)` into a `std::map<gggs::CellIndex,...>` breaks once the key is `uint32_t`; needs a reverse helper `gggs::CellIndex(index_, key>>16, key & 0xFFFF)` at `geo_grid.cpp:207` — `plan.md:41`
- [ ] (suggestion) State the "bounds box ⊇ distance-gate" invariant that keeps step 3 bit-exact (box uses the same equirectangular metric as the in-loop gate) — `plan.md:45`
- [ ] (suggestion) Consequences table misses: `Message` ctor change affects both construction sites (`Bag::open` `import_bag_main.cpp:331` + `pop_next` `:351`); `StorageFilter` must be plumbed through `Bag::open`, not only `BagReaders` ctor + `main()` `:547` — `plan.md:134`
- [ ] (suggestion) Confirm `/tf_static` (transient-local) survives `StorageFilter`; check dropped-georef counts don't change in the 32-bag timing run — `plan.md:70`

### Next step
Lifecycle: **Plan Review** → **implement** → **review-code**. Verdict is
approve-with-suggestions: implementation may proceed, addressing the (must-fix)
`nodeIntensityWelford` reconstruction and amending the plan inline per plan-task's
"During implementation" rules.

## Implementation
**Status**: complete
**When**: 2026-07-23 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-107
**Plan**: `.agent/work-plans/issue-107/plan.md` (amended inline; see `[impl amendment]`/`[impl note]` markers)

### Commits (7 code commits, oldest→newest)
- `b8db746` perf(geo_grid): packed-uint32 unordered_map node container — triple map-descent fold + `nodeKey()` at all sites + `nodeIntensityWelford` CellIndex reconstruction (the review must-fix).
- `1855c08` test(geo_grid): bit-exact bulk-insert regression, golden captured from the pre-refactor build.
- `9cc5ba0` perf(geo_grid): equirectangular insert bounds, drop ellipsoidal `radiusFromCenter` (+ last gz4d_geo.h use).
- `df4dac4` perf(import_bag): per-bag topic→type cache (both `Message` ctor sites).
- `20f1083` perf(import_bag): main-pass `StorageFilter` plumbed through `Bag::open`, built from each bag's real topics via `/tf`+`/tf_static` suffix match (namespaced-safe, transient-local survives).
- `02608c8` perf(import_bag): per-ping KDL-frame hoist out of the per-sounding transform.
- `d80c4d3` perf(import_bag): prune stale `speed_by_ns` samples (30 s window) + `odom_samples_total` diagnostic.
- (this progress + plan-sync entry commits on top.)

### Test results
- `./sensors_ws/build.sh cube_bathymetry` — clean build.
- `./sensors_ws/test.sh cube_bathymetry` — **464 tests, 0 errors, 0 failures, 61 skipped** (colcon aggregate incl. linters; uncrustify/cpplint/cppcheck/copyright all green). Underlying gtest: **183 tests, 0 failures**.
- New `GeoGridTest.BulkInsertBitExactRegression` passes: `values()`, `nodeRecords()` (backscatter), and `nodeIntensityWelford()` reproduce the pre-refactor golden byte-for-byte across BOTH the container and the bounds change.
- Dependency layers were not pre-built in this container; built `core_ws` (`marine_autonomy` + stores) `--packages-up-to` first (geodesy/grid_map/marine_acoustic_msgs are apt). 

### Plan amendments (folded into plan.md)
1. **`<map>` retained** (step 2) — `nodeIntensityWelford()` still returns `std::map<gggs::CellIndex, IntensityWelford>`. Added `<unordered_map>`+`<cstdint>`; audited every `nodes_` site (`values`/`nodeRecords`/`predictedDepthAt`/`setSettledIntensityWelfordAt`/the range-loop), not just `insert`.
2. **doTransform hoist MUST use KDL, not `tf2::Transform`** (item 6) — `tf2::doTransform` for a point is KDL-based (`gmTransformToKDL(t) * KDL::Vector`); a `tf2::Transform` matvec builds the rotation differently and would NOT be bit-exact (unexplained host-A/B tile diff). Implemented `tf2::gmTransformToKDL` + `KDL::Frame*KDL::Vector` — the exact same op, hoisted. Added `kdl/frames.hpp`, dropped unused `point_stamped.hpp`.
3. **Bit-exact bounds invariant documented** (step 3, review suggestion 1) — box ⊇ gate, same equirectangular metric, in-code + plan.
4. **StorageFilter design** (item 5, review suggestions 2/3) — plumbed through `Bag::open`; built from each bag's real topic list by tf/tf_static suffix match so namespaced `/bizzy/tf` and transient-local `/tf_static` survive; empty→read-all fallback.
5. **`odom_samples_total`** added (item 7) so the end-of-run diagnostic reports the true total after pruning.

### Deferred / not done here (by design)
- gggs `operator<` change → split to **uma#270** (do not touch gggs headers).
- Live **32-bag wall-clock + RSS A/B run** is a HOST task (out of container scope); the import_bag Phase B items are validated by build + the #63 "identical tiles or explained diffs" discipline, and item 6 is bit-exact by construction (KDL path preserved).

### Next step
Lifecycle: **implement** → **review-code**.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-24 01:54 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-107 at `dc73380`
**Mode**: pre-push
**Depth**: Deep (reason: 342 code lines ≥ 200; performance-critical hot path with bit-exactness/memory claims)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 1 | **Ship**: recommended — 0 must-fix; static analysis clean, bit-exactness independently confirmed, only non-blocking suggestions remain.

Static analysis (ament_cpplint, ament_uncrustify, cppcheck) clean on all four
changed files. Two disjoint-lens Claude Adversarial passes (Deep) found no real
must-fix: Lens B's `tf2::gmTransformToKDL` "undefined" claim was a false positive
(declared in `tf2_geometry_msgs.hpp`, included at line 130; its own point
`doTransform` is `gmTransformToKDL(t) * KDL::Vector(...)` — identical to the
hoist, confirming bit-exactness). `nodeKey` packing is lossless (row/column are
`uint16_t`, grid 960×960); public `GeoGrid` API unchanged so the 5 downstream
includers are unaffected; the plan-review must-fix (nodeIntensityWelford
reconstruction) is implemented.

### Findings
- [ ] (suggestion) Performance win unverified — Phase C step 9 (32-bag before/after wall-clock + RSS A/B run) deferred to host, not yet recorded; complete before closing #107 — `plan.md:153`
- [ ] (suggestion) Equirectangular bounds bit-exact at survey latitudes but may differ from the old ellipsoidal box at high latitude (arguably a latent-bug fix); fold caveat into the #63-style tile-diff writeup — `cube_bathymetry/src/geo_grid.cpp:73`

### Next step
Lifecycle: **Local Review** → push / open PR → **triage-reviews**. Verdict is
**approved** (0 must-fix); the two suggestions are non-blocking. The diff is
shippable — remaining suggestions (host A/B perf run, high-lat writeup note) can
be tracked without another review round.

## Integrated Review
**Status**: complete
**When**: 2026-07-23 22:12 -0400
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #109 at `ce84e4f`
**Sources**: 3 (Copilot R1 @ `ce84e4f`, Local Review (Pre-Push) @ `dc73380`, CI rollup)
**Cross-source confirmations**: 1
**CI**: all-pass (ROS 2 Jazzy industrial_ci: success)

### Findings
- [ ] (cross-confirmed: Copilot + Local Review) Equirectangular bounds degenerate
  at extreme latitude — Copilot: `delta_lon_deg` divides by `cos_lat`, 0 at the
  poles (inf bounds) and negative for |lat|>90; Local Review: bounds box may
  differ from the old ellipsoidal box at high latitude. Pre-refactor ellipsoidal
  solve was equally singular at ±90 and no GPS fix produces |lat|≥90, but the
  hardening (`abs(cos_lat)` + pole-safe fallback) is bit-exact-neutral for all
  valid inputs — `cube_bathymetry/src/geo_grid.cpp:96`
- [ ] (low, Copilot) Bit-exact regression's welford golden asserts in `std::map`
  iteration order — brittle to a legitimate future `gggs::CellIndex` comparator
  change (uma#270); assert by key lookup instead —
  `cube_bathymetry/test/test_geo_grid.cpp:296`
- [x] (suggestion, Local Review) High-latitude equirect caveat folded into the
  writeup — done: PR #109 body documents it — `cube_bathymetry/src/geo_grid.cpp:73`
- [ ] (suggestion, Local Review) Wall-clock + RSS A/B run (plan Phase C step 9)
  deferred to host post-merge; record on #107 before close — `plan.md:153`

### False positives
- (Copilot, ×4 sites + 3 suppressed dups) Cross-grid aliasing via packed
  `(row,column)` node key (`geo_grid.cpp:129,141,150,232`) — unreachable: every
  production caller routes through `GeoMapSheet::*(cell)`, which selects the grid
  via `getOrCreateGrid(cell.grid())` (`geo_map_sheet.cpp:144,208,215`), so
  `cell.grid() == index_` holds by construction at every `GeoGrid` boundary; no
  direct-`GeoGrid` caller with a foreign cell exists. Optional hardening: a
  debug `assert(cell.grid() == index_)` — non-blocking.
