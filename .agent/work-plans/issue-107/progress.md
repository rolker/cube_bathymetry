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
