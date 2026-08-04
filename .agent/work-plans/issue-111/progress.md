---
issue: 111
---

# Issue #111 — Tile-scoped incremental regen via the survey index

## Issue Review
**Status**: complete
**When**: 2026-07-30 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #111
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: needs-splitting

### Summary

Issue proposes replacing the current full-campaign batch regen (every bag, every
tile, ~1.4 h) with a tile-scoped incremental path: index new bags via
`marine_survey_index`, query which L10 store tiles they touched (dirty set),
rebuild only those tiles from contributing bags + pass intervals, and atomically
swap them into the store. The design guarantees byte-identity with a full regen by
construction. The full-regen path stays as the fallback and verification target.

### Scope Assessment

**Well-scoped?** Partially — the motivation and high-level architecture are clear
and well-motivated. However, the issue explicitly lists five unresolved design
points that must be settled before or during implementation. These are
implementation questions (footprint math, staleness fingerprint, backscatter
integration) that should be resolved in the plan, but their number and
interdependence make this realistically a multi-PR feature.

**Right repo?** Yes — `cube_bathymetry` owns `batch_regen` and `import_bag`.
The `marine_survey_index` dependency is in `unh_marine_autonomy` (pre-existing)
and will be consumed as a library. Consumer script changes land in
`unh_echoboats_project11`.

**Dependencies**:
- `marine_survey_index` (uma#259, part of uma#258 explorer umbrella) — must be
  merged and stable for the L14→L10 tile-key rollup query to work.
- `unh_echoboats_project11/scripts/build_bathy_store.sh` (#382) — consumer script
  that needs to grow an incremental default with `--fresh` fallback.
- The bit-exact A/B acceptance test depends on full regen remaining unchanged.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | `--fresh` fallback preserved; absent index ⇒ full regen; degradation is explicit |
| Enforcement over documentation | Watch | The "built-from fingerprint" (bag ledger + ref/curve hashes + tool version) is described but the enforcement mechanism (how staleness is detected and acted on) is not yet specified — risk of becoming documentation-only |
| Capture decisions, not just implementations | Action needed | Five design decisions are flagged ("design points to settle") but none is yet decided or captured in an ADR: footprint math (bbox vs. influence radius), staleness fingerprint format, backscatter co-driven dirty set, index-absent fallback contract |
| A change includes its consequences | Watch | Consumer script (`build_bathy_store.sh` #382) and backscatter store are called out; the plan must include both. The `survey_index.db` soft-dependency contract and the bit-exact A/B test must also be in scope |
| Only what's needed | OK | Leverages `marine_survey_index` (already merged); no new tooling introduced; per-tile rebuild reuses existing scatter-gather machinery |
| Improve incrementally | Watch | Multi-PR scope: dirty-tile query, contributing-bag set + seek, rebuild + swap, staleness fingerprint, and backscatter integration each warrant their own deliverable; attempting all in one PR risks a large, hard-to-review change |
| Test what breaks | Action needed | The bit-exact A/B acceptance criterion is well-defined in the issue; it must be a required test in the plan, not an advisory note |
| Workspace vs. project separation | OK | All changes are project-repo (cube_bathymetry + unh_echoboats_project11); no workspace infra touched |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Workspace ADR-0001 (Adopt ADRs) | Yes | At least two decisions should be captured as project ADRs: (1) dirty-tile footprint math (bbox vs. influence-radius), (2) staleness fingerprint schema. The issue correctly flags them as unresolved — they need a home |
| Workspace ADR-0002 (Worktree isolation) | OK | Worktree exists |
| cube_bathymetry ADR-0001 (tile eviction / lossless tile I/O) | Yes | Atomic swap of rebuilt tiles must use the same `saveTile`+reload path; avoid the "save then sparse-revisit overwrites good data" failure mode described in ADR-0001 |
| cube_bathymetry ADR-0007 (backscatter store) | Yes | The issue asserts the backscatter dirty set is driven by the same footprint; implementation must maintain the Welford bit-exact round-trip guarantee and the `auto`/`empirical`/`none` correction mode contract |
| Workspace ADR-0013 (progress.md vocabulary) | OK | This entry |

### Consequences

- `unh_echoboats_project11/scripts/build_bathy_store.sh` (#382) must be updated
  to add incremental default with `--fresh` unchanged — in scope per the issue.
- `survey_index.db` becomes a soft dependency — the fallback contract (absent
  index ⇒ full regen) should be stated explicitly in the plan and documented.
- The staleness fingerprint format (bag ledger + reference/curve hashes + tool
  version) is a new persistent artifact; its format and location need a decision
  record.
- If cube_bathymetry ADR-0007 is authoritatively written (the full `0007-mbes-
  backscatter-store.md` doc is still deferred per the addendum), the incremental
  regen work is a good forcing function to do so.

### Recommendations

- Break the feature into at least two PRs: (1) dirty-tile query + contributing-bag
  set (index integration only, no rebuild path yet) and (2) tile-scoped rebuild +
  atomic swap + staleness fingerprint + consumer script update. This keeps each PR
  reviewable and keeps full regen as the only live path until the incremental path
  is fully verified.
- Capture the footprint math decision (bbox vs. influence radius, cf. #104 margin
  lesson) as a project ADR in plan-task — it is load-bearing for correctness and
  should not live only in code comments.
- The bit-exact A/B test should be a required CI step, not just a manual
  acceptance gate.
- `batch_regen` currently rejects `auto` backscatter correction mode (per
  ADR-0007 addendum) because it has no SonarInfo source. The incremental path
  inherits the same constraint — verify the correction mode propagates correctly
  through the per-tile rebuild.

### Actions
- [ ] Capture footprint math decision (bbox vs. influence radius) as a project ADR before or during implementation — correctness is non-recoverable if wrong
- [ ] Capture staleness fingerprint format and location as a project ADR (format, schema version, where it lives relative to the store)
- [ ] Add bit-exact A/B test as a required step in the plan (not advisory)
- [ ] Plan the feature in at least two PRs: index integration first, then rebuild + swap
- [ ] Confirm `batch_regen` correction-mode propagation through the per-tile rebuild path (no `auto` mode, explicit curve/none must round-trip correctly)
- [ ] Include `unh_echoboats_project11/scripts/build_bathy_store.sh` (#382) update in scope

**Operator decision (2026-07-30 checkpoint)**: accept ALL review actions — plan a
2-PR sequence under this issue (PR1 index-integration query only; PR2 rebuild +
atomic swap + staleness fingerprint + build_bathy_store.sh consumer update),
author both project ADRs (footprint math; staleness fingerprint schema/location)
during plan-task, and make the bit-exact A/B a required test.

## Plan Authored
**Status**: complete
**When**: 2026-07-30 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-111/plan.md` at `6857d1c`
**Branch**: feature/issue-111 at `6857d1c`
**Phases**: 2 (PR1: dirty-tile query; PR2: rebuild + atomic swap + fingerprint + consumer)

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-07-31 21:26 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-111/plan.md` at `6857d1c`
**PR**: PR-less (`--issue 111`, layer worktree `issue-cube_bathymetry-111`)
**Verdict**: approve-with-suggestions

> Independence: fresh-context sub-agent, model Claude Opus, distinct from the
> plan author (Claude Sonnet). Not annotated as author self-review — the
> workspace's shared "Claude Code Agent" identity makes the name-based
> self-review heuristic always match in the standard run-issue lifecycle
> (review-plan is always a fresh sub-agent), so a name match here does not
> indicate the author re-reading their own work.

### Evaluation
| Dimension | Verdict | Notes |
|---|---|---|
| Scope | Good | 2-PR split already done per operator decision; PR1 ~6 files (cube_bathymetry only), PR2 rebuild+fingerprint+consumer. Each independently reviewable; full regen stays live until PR2 verified. |
| Issue alignment | Good | All six review-issue actions addressed: ADR-0002 (footprint) + ADR-0003 (fingerprint) authored, bit-exact A/B required (step 10), ≥2 PRs, correction-mode/no-`auto` (step 11), `build_bathy_store.sh` #382 (step 12). |
| File targeting | Good | Verified against real code: `BagReaders` helper exists (`batch_regen_main.cpp:169`); `finalize` gathers one bucket per tile (`batch_regen.h:67`), so "build only dirty tiles" holds by construction; `auto` mode rejected (`batch_regen_main.cpp:106`). Survey-index API is real: `tilesForBoundingBox`, `PassRow`, `queryPasses` all present in `marine_survey_index`. |
| Consequences | Good | CLI→`build_bathy_store.sh`, fingerprint→ADR-0003 schema bump, index-absent→full-regen fallback, ADR-0007 deferral all captured. Two gaps folded into findings (doc-impact + `.gitignore`). |
| Documentation & instruction impact | Concern | Plan has **no** `## Documentation & Instruction Impact` section — a required, non-silent section. `README.md:36-38` documents `batch_regen` and goes stale once `--incremental`/`--index-db`/`--fresh` land. |
| Principle alignment | Good | Enforcement (bit-exact A/B is required CI), decisions captured (2 ADRs committed), consequences (consumer script in scope), only-what's-needed (reuses scatter-gather + merged index). |
| ADR compliance | Good | ADR-0001 (atomic `std::filesystem::rename`, no partial-write), ADR-0007 addendum (`auto` rejection inherited + asserted). ADR-0002/0003 authored here and internally consistent. |
| ROS conventions | N/A | C++ CLI tooling, not nodes/topics. Only ROS-adjacent point is rosbag2 time-window reads (see finding 5). |

### Findings
- [x] (must-fix) Add the required `## Documentation & Instruction Impact` section — it is absent; make it non-silent. `README.md:36-38` (batch_regen docs) is staled by the new `--incremental`/`--index-db`/`--fresh` flags and the `build_fingerprint.json` artifact; either list it to update in-PR or state "None — <reason>". — `plan.md:94`
- [x] (suggestion) ADR-0002 step 3 names `gggs::GridIndex::parentAt(L10_level)`, which does not exist — the only rollup primitive is the free function `gggs::parent(child)` (one level up). L14→L10 needs iterating `parent()` 4× (or add a helper); correct the ADR so the implementer doesn't hunt for a nonexistent method. — `0002-dirty-tile-footprint-math.md:42`
- [x] (suggestion) ADR-0003's `.gitignore` consequence (build_fingerprint.json must never be committed with checked-in stores) is not in the Files to Change table — add the `.gitignore` entry to PR2 scope. — `plan.md:96`
- [x] (suggestion) PR2 spans two repos: `build_bathy_store.sh` lives in `unh_echoboats_project11`, not this worktree. Note the two-repo worktree setup and whether PR2 is one PR per repo. — `plan.md:109`
- [x] (suggestion) Step 8 names rosbag2 `SeekOptions` for per-bag time-window reads; confirm the actual `rosbag2_cpp::Reader` API (`seek(t)` + read-until-`t_end`, or `set_filter`) during implementation — "SeekOptions" as a time-window struct may not exist. Plan already hedges "(or filter)". — `plan.md:60`

### Summary
The plan is well-structured, faithfully addresses every accepted review-issue action, and its code-level assumptions were verified against the real `batch_regen`, `gggs`, and `marine_survey_index` sources — they hold. Both design ADRs are authored and sound; the bit-exact A/B test is correctly made a required CI gate. One must-fix: the required `## Documentation & Instruction Impact` section is missing (README `batch_regen` docs go stale). Remaining items are minor accuracy/scope suggestions. Approve with suggestions — address the doc-impact section (and ideally the ADR-0002 `parentAt` naming) before implementation.

### Recommended Actions
- [x] Add a non-silent `## Documentation & Instruction Impact` section listing `README.md` (batch_regen usage) as staled by the new flags, or state "None — <reason>".
- [x] Fix ADR-0002 step 3 to use `gggs::parent()` iterated to the L10 level (no `parentAt`).
- [x] Add the `build_fingerprint.json` `.gitignore` entry to PR2's Files to Change.
- [x] Note PR2's cross-repo (`unh_echoboats_project11`) worktree/PR strategy.

## Implementation
**Status**: complete
**When**: 2026-07-31 22:10 +00:00
**By**: Claude Opus

**Scope**: PR1 only (plan Approach steps 1–5) — survey-index dirty-tile query +
`--index-db` dry-run. No PR2 work (no rebuild path, fingerprint, atomic swap, or
`build_bathy_store.sh`).
**Branch**: feature/issue-111
**PR**: PR-less (layer worktree `issue-cube_bathymetry-111`; host performs pushes)

### Commits
- `e034ad3` feat(batch_regen): survey-index dirty-tile query library (#111) —
  `survey_index_query.h/.cpp` (`cube::dirtyL10Tiles`, `cube::DirtyTile`), the
  scoped `cube_bathymetry_survey_index_query` CMake target + `package.xml` deps,
  and `test_survey_index_query.cpp` (in-memory SQLite fixture).
- `5fa12ac` feat(batch_regen): --index-db dry-run dirty-tile query mode (#111) —
  `batch_regen_main.cpp` flag + `dirtyTileDryRun()` (human + `DIRTY_TILES_JSON:`
  output; index-absent → full-regen fallback; builds nothing, ignores `-o`/`-d`).
- `485ad54` docs(#111): README `--index-db` subsection; ADR-0002 margin corrected
  to one L14 tile; plan.md PR1 as-built sync.

### What was implemented (steps 1–5)
1. `marine_survey_index` dependency added (build+link) — declared `<depend>` (not
   `<exec_depend>`: the query is compiled/linked; the *soft* part is the
   `survey_index.db` file at runtime → full-regen fallback). `sqlite3` added too.
2. ADR-0002 already authored in plan-task; corrected here (margin, `parentAt`, impl).
3. `dirtyL10Tiles(db, new_bag_paths, store_level, sensor_filter="")` → distinct
   L10 `DirtyTile`s (tile + contributing `PassRow`s). Footprint read from the
   `passes`/`bags` join, expanded one L14 tile via `tilesForBoundingBox`, rolled
   up with iterated `gggs::parent()` (four applications), passes via `queryPasses`.
4. `--index-db` dry-run mode in `batch_regen_main` (store level derived exactly as
   the real build does). No `--index-db` ⇒ full-regen path unchanged.
5. `test_survey_index_query.cpp`: 5 cases — L14→L10 rollup, one-tile margin marks
   adjacent L10 tiles, contributing passes include old bags, no-new-bags empty,
   bag-not-in-index empty.

### Deviations from plan (synced inline into plan.md + ADR-0002)
- **Margin = one L14 tile, not one L14 cell.** An L14 cell is ~6 cm (960×960 cells
  / ~54 m grid) — smaller than the ≤3 m influence radius the margin must cover, so
  one cell would let the dirty set MISS boundary tiles (the exact correctness
  failure ADR-0002 forbids). One L14 tile (~54 m) is a safe conservative superset.
  ADR-0002 Decision/Rationale/Consequences updated; a note records the correction.
- **`<depend>` not `<exec_depend>`** for `marine_survey_index` (build+link needed).
- **Own library target** `cube_bathymetry_survey_index_query` (not compiled only
  into the tool) so the unit test can link the query while the survey-index/SQLite
  dependency stays out of the core `cube_bathymetry` library.

### Verification
- `./sensors_ws/build.sh cube_bathymetry` — passed (had to build the dependency
  layers first: underlay `geodesy` from source — apt's lacks `geodesics.h` needed
  by `marine_sidescan_mosaic` — then the core `unh_marine_autonomy` packages).
- `./sensors_ws/test.sh cube_bathymetry` — **489 tests, 0 errors, 0 failures, 65
  skipped**. New `DirtyTileQuery` gtest: 5/5 pass. All linters (uncrustify, cpplint,
  copyright, etc.) pass on the new/changed files.
- CLI smoke test: `--index-db` on a missing DB logs the full-regen fallback; on a
  populated DB prints the L14→L10 rollup, the one-tile margin (2 adjacent L14 tiles
  → 2×2 L10 block), contributing passes, and valid `DIRTY_TILES_JSON:`.

### Next step
PR2 (separate PR under #111): tile-scoped rebuild + `build_fingerprint.json` +
atomic swap (ADR-0003), then PR2b `build_bathy_store.sh` in `unh_echoboats_project11`.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-04 02:56 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-111 at `f5b9878`
**Mode**: pre-push
**Depth**: Deep (reason: two new ADRs + new SQL/query subsystem + ~1.4k lines)
**Must-fix**: 1 | **Suggestions**: 5
**Round**: 1 | **Ship**: continue — one genuine correctness must-fix (per-tile pass set incomplete) warrants another read after the fix

Specialists: static analysis (copyright/cpplint/uncrustify clean); Claude Adversarial x2 (Lens A + Lens B, both cross-confirmed must-fix #1); Copilot off (default); Local skipped (Ollama unavailable).

### Findings
- [x] (must-fix) `DirtyTile.passes` omits interior old-bag passes for a partially-covered dirty L10 tile (`queryPasses` runs over `expanded`, not the tile's full L14 extent) — breaks PR2 byte-identity + under-reports dry-run contributing bags; test `ContributingPassesIncludeOldBags` can't catch it (same L14 tile) — `cube_bathymetry/src/survey_index_query.cpp:200`
- [x] (suggestion) `DIRTY_TILES_JSON` tile lat/lon truncated to 6 sig-fig; use `setprecision(17)` + classic locale (bounds recomputable from level/row/col, so low sev) — `cube_bathymetry/src/batch_regen_main.cpp:414`
- [x] (suggestion) `expandFootprint` merges disjoint footprints into one bbox, inflating the dirty set on multi-area campaigns (conservative; note trade-off in ADR-0002) — `cube_bathymetry/src/survey_index_query.cpp:141`
- [x] (suggestion) Document machine contract: presence of `DIRTY_TILES_JSON:` line is authoritative, absence ⇒ full regen (exit 0 on absent/failed/non-index file) — `cube_bathymetry/src/batch_regen_main.cpp:361`
- [x] (suggestion) Document `dirtyL10Tiles` antimeridian `std::invalid_argument` throw so PR2's rebuild path replicates the CLI's catch — `cube_bathymetry/src/survey_index_query.cpp:166`
- [x] (suggestion) `jsonEscape` passes signed char to `\u%04x`; use `static_cast<unsigned char>(c)` for obvious correctness — `cube_bathymetry/src/batch_regen_main.cpp:319`

## Implementation
**Status**: complete
**When**: 2026-08-04 03:15 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-111 at `fcb172f`
**Addressed**: Local Review (Pre-Push) — 2026-08-04 02:56 +00:00, branch feature/issue-111 at `f5b9878` (1 must-fix + 5 suggestions)
**Commits**: `50ed7bb`, `000377c`, `f1e9991`, `ed7fcea`, `26a7bf2`, `fcb172f`

All six findings actioned (none deferred). Verified: `./sensors_ws/build.sh
cube_bathymetry` builds clean; `./sensors_ws/test.sh cube_bathymetry` →
**490 tests, 0 errors, 0 failures, 65 skipped** (up one from 489 — the new
`ContributingPassesIncludeOldBagInSeparateL14Subtile` regression test). All six
`DirtyTileQuery` cases pass. Pre-commit linters (uncrustify/cpplint/copyright)
passed on every commit. Had to rebuild the dependency layers first (underlay
`geodesy` from source for `geodesics.h`, then core `unh_marine_autonomy` up to
`marine_survey_index`/`marine_bathymetry_store` et al.) — this environment
started with empty layer installs.

### Actions
- [x] (must-fix) Query dirty-tile passes over each dirty L10 tile's FULL index-level extent, not just the new-bag footprint+margin (`expanded`) — old-bag passes in a dirty tile's other L14 sub-tiles were omitted, under-reporting contributing bags and breaking PR2 byte-identity. Added a regression test with an old-bag pass in a separate L14 sub-tile. — `cube_bathymetry/src/survey_index_query.cpp:201` (`50ed7bb`)
- [x] (suggestion) `DIRTY_TILES_JSON` coords now emitted with `imbue(classic)` + `setprecision(17)` (round-trippable) — `cube_bathymetry/src/batch_regen_main.cpp:405` (`ed7fcea`)
- [x] (suggestion) Documented the disjoint-footprint single-bbox trade-off in ADR-0002 Consequences + an `expandFootprint` code comment — `cube_bathymetry/src/survey_index_query.cpp:129` (`000377c`)
- [x] (suggestion) Documented the `DIRTY_TILES_JSON:` machine contract (marker authoritative; absence ⇒ full regen; exit 0 on absent/failed/non-index) in `dirtyTileDryRun` + README — `cube_bathymetry/src/batch_regen_main.cpp:337` (`fcb172f`)
- [x] (suggestion) Documented `dirtyL10Tiles` antimeridian `std::invalid_argument` throw in the header `@throws` — `cube_bathymetry/include/cube_bathymetry/survey_index_query.h:81` (`f1e9991`)
- [x] (suggestion) `jsonEscape` now casts to `unsigned char` for the `\u%04x` escape — `cube_bathymetry/src/batch_regen_main.cpp:321` (`26a7bf2`)

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off
to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 111 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-04 03:25 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-111 at `55bcf15`
**Mode**: pre-push
**Depth**: Deep (reason: two new ADRs + new SQLite/query subsystem + ~1.5k lines)
**Must-fix**: 1 | **Suggestions**: 5
**Round**: 2 | **Ship**: recommended — sole must-fix is a mechanical ADR wording fix (cell→tile), count not rising vs round 1; address and ship rather than another full round

Specialists: static analysis (ament_cpplint clean, uncrustify PASS on all new/changed C++); Claude Adversarial x2 (Lens A logic + Lens B systemic, fresh-context; cross-confirmed suggestions #2 and #3); Copilot off (default); Local skipped (Ollama unavailable, offline env). Round-1 must-fix (contributing-pass completeness) verified fixed with regression test; tileFromRowCol / gggs geometry verified against real sources.

### Findings
- [x] (must-fix) ADR-0002 lines 72 & 103 still say "one-L14-cell margin" — contradicts the corrected "one L14 tile" Decision + boxed note (internal governance-doc contradiction); mechanical cell->tile fix — `cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md:72`
- [ ] (suggestion) `newBagFootprint` exact `sensor_type = ?` diverges from `queryPasses`/`appendSensorClause` "sidescan"->`LIKE 'sidescan%'` expansion; a "sidescan"-scoped call silently yields an empty dirty set (unreachable today — bathy uses exact mbes, dry-run uses ""); align clause or document exact-match-only — `cube_bathymetry/src/survey_index_query.cpp:84`
- [ ] (suggestion) `sqlite3_bind_text` return codes unchecked (cross-confirmed) vs loud prepare/step handling elsewhere — `cube_bathymetry/src/survey_index_query.cpp:98`
- [ ] (suggestion) `ancestorAtLevel` has no L14>=L10 precondition / valid-tile post-condition guard; a finer store_level yields wrong-level dirty tiles, invalid index collapses to one map key (cross-confirmed) — `cube_bathymetry/src/survey_index_query.cpp:59`
- [ ] (suggestion) dry-run builds `GeoMapSheet`/`Level::fromCellSize` before the try/catch; invalid `--iho-order` aborts uncaught (nonzero, no marker) vs the exit-0 soft-dep contract; gate validates `-r>0` but not iho-order (pre-existing in full-regen path) — `cube_bathymetry/src/batch_regen_main.cpp:354`
- [ ] (suggestion) no test covers `sensor_filter` (exact or "sidescan") or a mixed-level footprint, leaving suggestion #1 and the by_level/multi-index_levels path untested — `cube_bathymetry/test/test_survey_index_query.cpp`
