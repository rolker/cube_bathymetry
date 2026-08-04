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

## Plan Authored
**Status**: complete
**When**: 2026-08-04 14:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-115/plan.md` at `eca426a`
**Branch**: feature/issue-115 at `eca426a`
**Phases**: single

### Open questions
- [ ] Keep `primeFromTileResample` internal (anonymous namespace) vs. declared in `store_import.h` — no current caller outside `seedNewTile`; prefer internal unless a caller exists.
- [ ] Level-walk selects finest available coarser level (highest level number below survey level) — confirm this aligns with issue's "finest→coarsest" walk intent.

## Plan Review
**Status**: complete
**When**: 2026-08-04 02:45 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent review: fresh-context sub-agent, different model than the plan
     author (Sonnet). The `## Plan Authored` By-name matches ($AGENT_NAME is the
     generic "Claude Code Agent" for all agents here), but the annotation exists to
     flag genuine in-context self-review; this is not one, so no annotation. -->

**Plan**: `.agent/work-plans/issue-115/plan.md` at `eca426a`
**PR**: PR-less (`--issue`/file-path invocation; no draft plan PR)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) Consequence completeness — the "Only tiles at the survey GGGS
  level" restriction lives in **two** more sites the plan misses: `batch_regen_main.cpp:85-86`
  (identical `--reference-store` help; batch-regen shares `seedNewTile`) and,
  higher-priority, the call-site comment `import_bag_main.cpp:704` ("a reference tile
  at another level primes a node the soundings never land on (harmless no-op)"),
  which describes the exact bug as intentional and will actively contradict the fix.
  Add both to the Consequences table — `plan.md:93-98`.
- [ ] (suggestion) Test placement — the cross-level test needs an on-disk reference
  store + `ImportAccumulator` harness that already exists in `test_import_eviction.cpp`
  (helpers `makeTempDir`/`makeConfig`/`surveyCell`/`countReferenceFiniteCells`/`loadBathyCells`,
  beside `ReferenceSeedDoesNotAddMeasuredData`). `test_store_import.cpp` (the plan's
  target, `plan.md:69`) tests pure helpers and has none of it — placing it there
  duplicates infra. Prefer `test_import_eviction.cpp`.
- [ ] (suggestion) ADR citation — the ADR-0001 rung-2 addendum cites its enforcement
  test by name (`test_import_eviction.ReferenceSeedDoesNotAddMeasuredData`); step 5
  (`plan.md:57-62`) should likewise name the new cross-level enforcement test.
- [ ] (suggestion) Implementation nit — `CellIndex::position()` returns the cell's
  **SW corner**, not the center that step 1 (`plan.md:31-36`) assumes; add the +0.5-cell
  offset or accept SW-corner nearest-neighbor explicitly. `Level::cellIndex(GeoPoint)`
  already composes gridIndex+CellIndex in one call (simpler than the two-step sketch).
- [ ] (validation) Root cause + approach verified against the code: `loadWindow`
  (`tile_io.cpp:424-426`) stores coarse tiles keyed by their own level, so
  `tiles.find(index)` at the L10 index misses them — the bug is real. GGGS is nested
  (`gggs/core.h:49`), so a survey tile is fully contained in exactly one coarser tile
  per level — the plan's single-coarse-tile selection is correct (no straddle). All
  four `## Issue Review` action items (README, ADR, cross-level test, diagnostic) are
  addressed.

## Implementation
**Status**: complete
**When**: 2026-08-04 03:18 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-115/plan.md` at `f28c117` (updated to fold the 4
plan-review suggestions before implementing — plan-first workflow)
**Branch**: feature/issue-115

### What changed

Implemented the approved plan WITH all 4 plan-review suggestions folded in.

- **`f28c117`** — plan: fold the 4 plan-review suggestions into the plan (step 1
  CellIndex SW-corner nit → fine-cell center + `Level::cellIndex()`; step 3 relocate
  the cross-level test to `test_import_eviction.cpp`; step 5 name the enforcement test
  in the ADR citation; consequences add `batch_regen_main.cpp` help + call-site and
  `import_bag_main.cpp:704` call-site comment; both open questions resolved).
- **`ac99078`** — fix(store_import): `seedNewTile` rung 2 gains a Phase B level-walk
  fallback. When the same-level `tiles.find(index)` misses, it selects the FINEST
  reference tile coarser than the survey level and calls the new anonymous-namespace
  helper `primeFromTileResample`, which resamples the coarse shallow prior onto the
  fine survey cells (nearest-neighbour on each fine cell's CENTER — `CellIndex::
  position()` is the SW corner, so +0.5 cell, resolved via `Level::cellIndex()`),
  predicted-only (never settled, no backscatter). Emits a `std::cerr` diagnostic
  naming the fallback level for auditability. Helper kept internal (no
  `store_import.h` change).
- **`a07f1a2`** — test: `test_import_eviction.CoarseLevelReferenceSeedRejectsDeepBlunder`
  builds a coarser (L7) reference tile filled shallow over the L7 tile containing an
  L10 survey tile, runs a false-deep (−150 m) sounding through an `ImportAccumulator`
  with that reference, and asserts the deep blunder is rejected (no settled cell)
  WITH the cross-level reference but accepted (deep cell settles) WITHOUT any
  reference. Reverting the level-walk makes the exact-level find miss the L7 tile →
  deep cell reappears → the test fails (verified by inspection + the observed
  pre-fix failure while iterating).
- **`bec3308`** — docs: updated all seed-precedence sites to the level-walk behaviour
  — README "Seed precedence" rung 2, ADR-0001 addendum rung 2 (+ enforcement-test
  citation), `import_bag_main.cpp` help + the `:704` call-site "harmless no-op"
  comment, `batch_regen_main.cpp` help + its call-site comment.

### Build & test

- `./sensors_ws/build.sh cube_bathymetry` — clean (warnings only, pre-existing GDAL
  `warn_unused_result` in unrelated files). Dependency layers (`underlay_ws`
  geographic_msgs, `core_ws` marine_autonomy/bathymetry_store/mbes_backscatter_store/
  tiled_raster_store/interfaces) were built first in the `main` layer, as their
  installs were empty.
- `./sensors_ws/test.sh cube_bathymetry` — **472 tests, 0 errors, 0 failures, 62
  skipped**. The new `CoarseLevelReferenceSeedRejectsDeepBlunder` passes; uncrustify
  clean on all edited files.

### Deviations from plan

- **Test construction detail (not a scope change):** the plan sketched building the
  coarse reference via a `GeoMapSheet` at a coarser cell size. In practice a handful
  of synthetic soundings settle only a sparse, patchy set of coarse cells, so the
  survey cell's coarse lookup could hit a NaN and skip gating. The test instead
  constructs the coarse `BathymetryTile` DIRECTLY and fills the whole L7 tile with the
  shallow prior — deterministic coverage, and closer to how a real ENC prior is a
  dense filled surface. Same assertion and enforcement intent.
- No other deviations. Helper kept internal and finest-available coarser level
  chosen, per the resolved open questions.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-04 03:27 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-115 at `db5ca95`
**Mode**: pre-push
**Depth**: Standard (reason: ~200 LOC single-package logic change; navigational-safety-relevant, not cross-layer/security)
**Must-fix**: 1 | **Suggestions**: 0
**Round**: 1 | **Ship**: continue — one must-fix reintroduces the silent gate-off class for boundary-flush tiles

### Findings
- [x] (must-fix) Phase-B fallback picks finest-*level* coarse tile without verifying it *contains* the survey tile; loadWindow overlap is inclusive (tile_io.cpp:139) so a boundary-flush survey tile also gets the edge-adjacent neighbor coarse tile, strict-`>` selection can pick it, and the grid-mismatch guard then skips every cell -> blunder gate silently OFF (reintroduces the #115 silent miss for boundary tiles) -- `cube_bathymetry/src/store_import.cpp:704-716`
- [x] (must-fix, test) Add regression test: survey tile flush against a coarse boundary with the neighbor coarse tile on disk (current test surveys a tile interior with only the containing coarse tile, so it cannot catch the above) -- `cube_bathymetry/test/test_import_eviction.cpp`

## Implementation
**Status**: complete
**When**: 2026-08-04 03:45 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-115 at `ca9c966`
**Addressed**: `## Local Review (Pre-Push)` (2026-08-04 03:27, at `db5ca95`) — both must-fix findings
**Commits**: `0973ec3` (fix), `ca9c966` (test)

Closed the loop on the pre-push review's one must-fix class (silent gate-off for
boundary-flush tiles) and its paired regression-test gap.

### Actions
- [x] (must-fix) Phase-B fallback now VERIFIES containment before selecting a coarse
  tile, instead of picking the finest by level alone. `loadWindow`'s overlap test is
  inclusive (`tile_io.cpp` `tileOverlapsBox`), so a survey tile flush against a
  coarse-tile boundary also loads the edge-adjacent coarse neighbor; the old level-only
  tie could pick that neighbor, whose grid shares no cell with the survey tile, and
  `primeFromTileResample`'s grid-mismatch guard then skipped every fine cell — gate
  silently OFF. Fix: compute the survey tile's center and accept a candidate only when
  `Level(cand.level()).gridIndex(center) == cand` (GGGS nesting → the unique containing
  coarse tile), rejecting any neighbor the inclusive window returned — `cube_bathymetry/src/store_import.cpp:704-729`
- [x] (must-fix, test) Added `ImportEviction.BoundaryFlushCrossLevelReferenceRejectsDeepBlunder`:
  the survey L10 tile is flush against the WEST edge of its containing L7 tile, and BOTH
  the container and its west-neighbor L7 tiles are on disk (inclusive `loadWindow` returns
  both). Asserts the deep (−150 m) blunder is still gated. Verified to FAIL against the
  pre-fix level-only selection (map-ordered neighbor wins the tie, `primeFromTileResample`
  skips every cell, deep cell settles) and PASS with the fix — `cube_bathymetry/test/test_import_eviction.cpp`

### Build & test
- `./sensors_ws/build.sh cube_bathymetry` — clean (warnings only: pre-existing `tmpnam`
  note in an unrelated test). Dependency layers `underlay_ws` and `core_ws` were built
  first in the `main` layer (their installs were empty this session).
- `./sensors_ws/test.sh cube_bathymetry` — **473 tests, 0 errors, 0 failures, 62 skipped**
  (up from 472: the new boundary-flush test). Revert-verify: neutralizing the containment
  check made the new test fail with the deep cell settling, confirming it catches the bug;
  restored and re-confirmed green.

### Deferred / not-actionable
- None — both findings were actioned.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a
fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 115 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-04 03:54 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-115 at `bb26b58`
**Mode**: pre-push
**Depth**: Standard (reason: ~320 LOC single-package navigational-safety logic change; not cross-layer/security)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 2 | **Ship**: recommended — Round 1's sole must-fix (boundary-flush gate-off) is fixed with a dedicated regression test; this round finds no must-fix

### Findings
- [ ] (suggestion) Cross-level fallback diagnostic hardcodes `import_bag:` prefix (also drives batch_regen) and fires once per seeded tile — cosmetic; matches sibling diagnostics — `cube_bathymetry/src/store_import.cpp:739`
- [ ] (suggestion) ADR-0001 could add a one-line note that the widened gate newly exposes previously-ungated tiles to the legitimate-deep false-reject mode (already covered operationally by the mains' help-text NOTE) — `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md:238`

### Verification
- Static analysis: ament_cpplint + ament_uncrustify clean on all 4 changed C++ files (store_import.cpp, import_bag_main.cpp, batch_regen_main.cpp, test_import_eviction.cpp).
- Claude Adversarial: 2 passes (Lens A logic + Lens B systemic), both independent verdict "sound, no must-fix"; source-level check of GGGS geometry (SW-corner half-cell offset, Level::cellIndex composition, .get/band consistency, single-value gridIndex tie-break) confirmed correct. Both new tests verified as genuine regression guards (fail on revert).
- Local Adversarial: skipped (Ollama not installed on this host). Copilot: off (default).
- Code unchanged since the green test run at `ca9c966` (473 tests, 0 failures); only progress.md changed between there and HEAD.

### Out of scope (awareness, not a finding against this PR)
- `seeded_` is not cleared on eviction and `reloadEvictedTile` restores only the Survey layer, so a reference-only tile evicted and revisited loses its blunder gate on reload. Pre-existing; affects Phase A equally; the Phase-B widening makes it apply to more tiles.

### Next step
Lifecycle: **Local Review (approved)** → push / open PR → **triage-reviews**. Hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 115 --skill triage-reviews

## Integrated Review
**Status**: complete
**When**: 2026-08-04 12:59 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #117 at `5f085d9`
**Sources**: 4 (Copilot review @ `5f085d9` — 0 comments; Local Review (Pre-Push) R1 @ `db5ca95`; Local Review (Pre-Push) R2 @ `bb26b58`; CI rollup @ `5f085d9`)
**Cross-source confirmations**: 0
**CI**: all-pass (ROS 2 Jazzy industrial_ci: success; copilot-pull-request-reviewer: success)

### Findings
- [ ] (suggestion, Local Review R2) ADR-0001 rung-2 paragraph does not note that the widened gate newly exposes previously-ungated tiles to the legitimate-deep false-reject mode; the mains' `--reference-store` help text carries the operational NOTE but the ADR does not — one-line addition — `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md:238`

### False positives
- (Local Review R2) "Cross-level fallback diagnostic hardcodes `import_bag:` prefix (also drives batch_regen)" — not a defect introduced by this PR and not fixable in isolation: all 9 `std::cerr` diagnostics in `store_import.cpp` use the same `import_bag:` prefix, including the sibling catch-block message three lines below. Changing only the new one would break the file-wide convention and make batch_regen output *less* consistent, not more. The per-seeded-tile firing rate is the intended auditability behavior requested by the same review round (one line per tile that actually took the fallback, bounded by resident-tile count, not per sounding). A repo-wide prefix cleanup is a separate concern.

### Notes
- Copilot reviewed 8/8 changed files and generated no comments; its summary describes the change accurately (cross-level containment-verified fallback + two regression tests + docs), so it neither confirms nor contradicts any local finding.
- R1's two must-fix findings (boundary-flush containment gate-off; missing boundary-flush regression test) are both resolved in the code at head: `store_import.cpp` verifies containment via `gggs::Level(cand.level()).gridIndex(survey_center) != cand` rather than inferring it from level alone, and `test_import_eviction.cpp` carries the boundary-flush regression test.
- No must-fix or cross-confirmed findings remain. The single open suggestion is documentation-only and does not gate merge.

### Next step
Lifecycle: **Integrated Review** → address-findings (1 doc-only suggestion) or merge. No must-fix findings remain; PR #117 is merge-ready on the technical gate. Recommendation: land the one-line ADR note first (cheap, and the ADR is the durable record of the widened gate's trade-off), then merge via `.agent/scripts/merge_pr.sh --issue 115`.
