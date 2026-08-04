---
issue: 115
---

# Issue #115 — Reference-prior blunder gate silent miss for multi-level reference stores

## Issue Review
**Status**: complete
**When**: 2026-08-04 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #115
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

`import_bag` (and `batch_regen_bag`, which shares the same `seedNewTile` path in
`ImportAccumulator`) only loads the reference prior at the exact survey GGGS level
(`BathymetryStore::fromCellSize(cfg_.cell_size_m)`). Multi-level reference stores
(e.g., ENC exports via s57_tools at L5/L7/L8) are invisible to an L10 survey
import — `tiles.find(index)` always misses — so the blunder gate is silently
inactive. The Lewes DE import (2026-08-03) is a confirmed real-world case: 14.5%
false-deep cells in tile `10_17252_13419` (nominal −144 m in a 2–46 m bay)
entered the authoritative survey layer unchallenged.

The proposed fix is well-motivated and minimal: when the exact-level lookup fails,
walk finest→coarsest over available reference levels, resample the coarsest-found
tile's predicted surface over the survey tile. Shoal-biased ENC generalization is
the conservative direction for a false-deep gate, which is the right safety choice.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | Watch | Level-walk fallback should emit a diagnostic when it fires (different level used) so operators know a cross-level prior was active during an import. Issue does not mention this but it is low-cost to add. |
| Enforcement over documentation | OK | Fix is structural (code path change), not doc-only. |
| Capture decisions, not just implementations | Action needed | The two-rung seed precedence is documented in ADR-0001 addendum and README "Seed precedence". Both currently say "Only tiles at the survey GGGS level gate." — this restriction is the bug. Both must be updated in the same PR. |
| A change includes its consequences | Action needed | README "Seed precedence" section and ADR-0001 addendum text need updating. A cross-level seeding regression test is needed (see Test what breaks below). |
| Only what's needed | OK | Fix is confined to `seedNewTile` in `store_import.cpp`; no structural change elsewhere. |
| Improve incrementally | OK | Small, targeted fix to one code path. |
| Test what breaks | Action needed | `test_store_import.cpp` tests the blunder gate with a same-level reference tile (line ~455). A new test case is needed: reference tile at a coarser GGGS level, survey tile at finer level — confirms gate activates via the level-walk fallback. Without it, the bug can silently regress. |
| Workspace vs. project separation | OK | Entirely within `cube_bathymetry`; no workspace-infra changes needed. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| cube_bathymetry ADR-0001 (addendum — two-rung seed precedence) | Yes | Addendum text and README both state "Only tiles at the survey GGGS level gate." — this is the behavior being changed. Update the addendum or add a further addendum to describe the level-walk fallback. |
| workspace ADR-0013 (progress.md entry vocabulary) | Yes | This entry follows the `## Issue Review` schema. |
| workspace ADR-0002 (worktree isolation) | OK | Worktree already exists for this issue. |

### Consequences

Per the consequences map, the following should be updated in the same PR:

- `README.md` — "Seed precedence" section (rung 2 description): replace "Only tiles at the survey GGGS level gate" with description of level-walk fallback and its conservative direction.
- `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md` — the addendum's Rung 2 description: same update.
- `store_import.cpp` inline comment at the Rung 2 block (lines ~618–638): update to reflect new behavior.
- `test_store_import.cpp`: add cross-level reference seeding test.
- Consider: a `std::cerr` / log line when the fallback fires, naming which level was used, so the import log is auditable.

### Actions
- [ ] Update README.md "Seed precedence" rung 2 description (remove "Only tiles at the survey GGGS level gate" restriction).
- [ ] Update ADR-0001 addendum Rung 2 text to describe the level-walk fallback behavior.
- [ ] Add cross-level reference seeding test in `test_store_import.cpp` (reference at coarser level → blunder gate activates for survey tile at finer level).
- [ ] Emit a diagnostic (stderr or ROS log) when the level-walk fallback fires, naming the reference level actually used — supports import-log auditability.
