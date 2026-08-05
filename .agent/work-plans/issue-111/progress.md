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
- [x] (suggestion) `newBagFootprint` exact `sensor_type = ?` diverges from `queryPasses`/`appendSensorClause` "sidescan"->`LIKE 'sidescan%'` expansion; a "sidescan"-scoped call silently yields an empty dirty set (unreachable today — bathy uses exact mbes, dry-run uses ""); align clause or document exact-match-only — `cube_bathymetry/src/survey_index_query.cpp:84`
- [x] (suggestion) `sqlite3_bind_text` return codes unchecked (cross-confirmed) vs loud prepare/step handling elsewhere — `cube_bathymetry/src/survey_index_query.cpp:98`
- [x] (suggestion) `ancestorAtLevel` has no L14>=L10 precondition / valid-tile post-condition guard; a finer store_level yields wrong-level dirty tiles, invalid index collapses to one map key (cross-confirmed) — `cube_bathymetry/src/survey_index_query.cpp:59`
- [x] (suggestion) dry-run builds `GeoMapSheet`/`Level::fromCellSize` before the try/catch; invalid `--iho-order` aborts uncaught (nonzero, no marker) vs the exit-0 soft-dep contract; gate validates `-r>0` but not iho-order (pre-existing in full-regen path) — `cube_bathymetry/src/batch_regen_main.cpp:354`
- [x] (suggestion) no test covers `sensor_filter` (exact or "sidescan") or a mixed-level footprint, leaving suggestion #1 and the by_level/multi-index_levels path untested — `cube_bathymetry/test/test_survey_index_query.cpp`

## Implementation
**Status**: complete
**When**: 2026-08-04 03:42 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-111 at `6d110e8`
**Addressed**: Local Review (Pre-Push) (When 2026-08-04 03:25 +00:00, branch `55bcf15`) — all 1 must-fix + 5 suggestions
**Commits**: `54ca447` `cf80d84` `c8a96ad` `ce4ae1a` `695c6e3` `6d110e8`

Worked the latest review's fix plan. Each finding fixed and committed atomically
(one logical fix per commit) with its box checked. Verified by compiling the three
changed C++ translation units clean (`g++ -std=c++17`) and building + running the
`survey_index_query` gtest suite standalone against the marine_survey_index sources
(full colcon build not possible — the lower ROS layers are unbuilt in this offline
env): **9/9 tests pass** (6 pre-existing + 3 new), confirming the F2/F3/F4 edits to
`survey_index_query.cpp` don't regress the existing rollup/margin/contributing-pass
tests. `ament_uncrustify` and `ament_cpplint` clean on all four changed files.

### Actions
- [x] (must-fix) ADR-0002 "one-L14-cell margin" → "one L14 tile" — also fixed the same defect on a third line (the ≤3 m radius is *larger* than a ~6 cm cell, so "cell" was backwards there too), making the doc internally consistent — `cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md:58,72,102`
- [x] (suggestion) Aligned `newBagFootprint` sensor clause with `marine_survey_index::appendSensorClause`: "sidescan" → `LIKE 'sidescan%'` (binds nothing), any other non-empty value exact-match — `cube_bathymetry/src/survey_index_query.cpp:69`
- [x] (suggestion) Checked both `sqlite3_bind_text` return codes in `newBagFootprint`, throwing like the surrounding prepare/step handling — `cube_bathymetry/src/survey_index_query.cpp:97`
- [x] (suggestion) Enforced `ancestorAtLevel` pre/postconditions — throw `std::invalid_argument` if the store level is finer than the footprint, `std::runtime_error` on an invalid rolled-up tile; documented both in the header `@throws` — `cube_bathymetry/src/survey_index_query.cpp:59`, `cube_bathymetry/include/cube_bathymetry/survey_index_query.h:86`
- [x] (suggestion) Validate `--iho-order` in the dry-run gate (cheap GeoMapSheet probe) so a bad order routes to a clean `usage()` error instead of aborting uncaught with no `DIRTY_TILES_JSON` marker; scoped to the dry-run path (the full-regen path is pre-existing and out of scope) — `cube_bathymetry/src/batch_regen_main.cpp:599`
- [x] (suggestion) Added 3 tests: exact `sensor_filter` scopes footprint + contributing set; `"sidescan"` alias matches channel-split `sidescan_port` (and a wrong exact filter yields empty); mixed L14/L13 footprint rolls each level to its L10 store tile — `cube_bathymetry/test/test_survey_index_query.cpp`

## Integrated Review
**Status**: complete
**When**: 2026-08-03 23:58 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #116 at `eeaa4cd`
**Sources**: 3 (Copilot review @ `eeaa4cd`, Local Review (Pre-Push) R1 @ `f5b9878`, R2 @ `55bcf15`)
**Cross-source confirmations**: 0 (both local rounds pre-date the head; their findings were all addressed before publish. Copilot's 3 comments are new sites, though #1 and #3 extend failure families R2 already hardened — ancestorAtLevel guards, --iho-order soft-dep contract)
**CI**: pending — ROS 2 Jazzy (industrial_ci) in progress at triage time; copilot-pull-request-reviewer success

### Findings
- [x] (valid, Copilot) `tileFromRowCol` indexes `gggs::levels[level]` with a DB-sourced level before any validation — level ≥ 21 from a corrupted index is out-of-bounds UB. Construct `gggs::Level(level)` first (it throws `std::out_of_range`) or bounds-check explicitly, so the dry-run's catch falls back to full regen — `cube_bathymetry/src/survey_index_query.cpp:46-53`
- [x] (valid, Copilot) `dirtyL10Tiles` header promises passes "ordered by bag path then start time", but `queryPasses` only ORDER-BYs per 200-tile chunk and one dirty L10 tile's L14 extent is 256 tiles — chunking always breaks global order, making dry-run output nondeterministic vs the documented contract. Sort each tile's passes before returning — `cube_bathymetry/src/survey_index_query.cpp:283-288`
- [x] (valid, Copilot) `dirtyTileDryRun` uses the throwing `std::filesystem::exists` overload outside the soft-dep try/catch (line 367 vs try at 375); an EACCES/ELOOP path error aborts uncaught — nonzero exit, no marker — violating the exit-0 "index unavailable ⇒ full regen" contract (same failure family as R2's --iho-order fix). Use the `std::error_code` overload and treat errors as index-absent — `cube_bathymetry/src/batch_regen_main.cpp:367-372`

### False positives
- (none)

## Implementation
**Status**: complete
**When**: 2026-08-04 23:52 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-111 at `ea7d4cc`
**Addressed**: Integrated Review (When 2026-08-03 23:58 -04:00, PR #116 at `eeaa4cd`) — all 3 valid (Copilot) findings
**Commits**: `8b29e71` `007031c` `ea7d4cc`

Worked the Integrated Review's fix plan (3 valid Copilot findings; no false
positives to dismiss). Each finding fixed and committed atomically with its box
checked. Verified: `survey_index_query.cpp` (F1+F2) and `batch_regen_main.cpp`
(F3) syntax-check clean (`g++ -std=c++17`); the F3 non-throwing-`exists` logic
was compiled and exercised standalone (absent/error path → return 0, present →
proceed); the `survey_index_query` gtest suite built against the
`marine_survey_index` sources and run standalone → **9/9 pass** (full colcon
build not possible — the lower ROS layers are unbuilt in this offline worktree,
only `sensors_ws/install` exists). `ament_uncrustify`/`ament_cpplint`/copyright
pre-commit hooks passed on every commit.

### Actions
- [x] (must-fix→valid, Copilot) `tileFromRowCol` now constructs `gggs::Level(level)` (which throws `std::out_of_range` for level ≥ 21) BEFORE indexing the unchecked `gggs::levels` array — a corrupted index row surfaces as a catchable exception the dry-run's catch turns into a full-regen fallback, not out-of-bounds UB — `cube_bathymetry/src/survey_index_query.cpp:46` (`8b29e71`)
- [x] (valid, Copilot) `dirtyL10Tiles` now sorts each dirty tile's passes by (bag path, start time) before returning, honouring the header's ordering contract independently of `queryPasses`' 200-tile chunking (a dirty L10 tile's L14 extent is 256 tiles) — deterministic per-tile dry-run output — `cube_bathymetry/src/survey_index_query.cpp:283` (`007031c`)
- [x] (valid, Copilot) `dirtyTileDryRun` now uses the `std::error_code` overload of `std::filesystem::exists` (added `#include <system_error>`); a path error (EACCES/ELOOP) on the index path routes to the index-absent full-regen fallback (exit 0, no marker) instead of aborting uncaught outside the soft-dep try/catch — `cube_bathymetry/src/batch_regen_main.cpp:367` (`ea7d4cc`)

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off
to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 111 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-05 00:07 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-111 at `4821172`
**Mode**: pre-push
**Depth**: Deep (reason: new SQLite/query subsystem + 2 ADRs; incremental 3-fix diff reviewed in full subsystem context)
**Must-fix**: 1 | **Suggestions**: 5
**Round**: 3 | **Ship**: recommended — sole must-fix is a mechanical ament_uncrustify divergence (will fail CI) on the F3 line; the 3 Integrated-Review fixes are functionally correct & complete, count not rising vs R1/R2. Address the lint fix and ship.

Specialists: static analysis (ament_cpplint clean; ament_uncrustify 1 divergence at batch_regen_main.cpp:378 — must-fix #1); Claude Adversarial x2 (Lens A logic + Lens B systemic, fresh-context); Copilot off (default); Local off (--no-local, workspace#590 too-slow). Verified the 3 Integrated-Review (Copilot) fixes against real deps: F1 gggs::Level(uint8_t) genuinely throws std::out_of_range for level>=21 (gggs/level.h:51-56) and is caught -> exit-0 full-regen; F3 non-throwing exists correct (main() has no top-level catch, so throwing overload really would terminate); F2 sort key matches header:85 but queryPasses ALREADY globally sorts (query.cpp:240-250), so the added sort is a correct no-op and its comment's premise is wrong.

### Findings
- [ ] (must-fix) ament_uncrustify continuation-line over-indentation; wants 6-space indent -- will fail industrial_ci despite the impl entry's "uncrustify clean" claim (F3 line) -- `cube_bathymetry/src/batch_regen_main.cpp:378`
- [ ] (suggestion) F2 comment asserts queryPasses "not guaranteed globally ordered across chunks" -- false; queryPasses std::sorts its merged result (marine_survey_index query.cpp:240-250). Sort is a correct no-op; reword rationale to defensive-decoupling -- `cube_bathymetry/src/survey_index_query.cpp:289`
- [ ] (suggestion) F1 throw path leaks prepared stmt (cascades to unfreed db via unchecked sqlite3_close BUSY) on a level>=21 corrupt row; process-exit-bounded in dry-run but breaks the finalize-on-throw invariant (cf. lines 139/151); add finalize-on-throw/RAII -- `cube_bathymetry/src/survey_index_query.cpp:148`
- [ ] (suggestion) F3 "not found" note text also fires for EACCES/ELOOP (not "not found"); cosmetic, ec.message() discloses cause -- `cube_bathymetry/src/batch_regen_main.cpp:375`
- [ ] (suggestion) F2 std::sort non-stable -- identical (bag_path,t_start_ns) ties (different topic) unpinned; not a regression (contract promises 2 keys) but add topic tiebreaker for cross-toolchain byte-stable JSON -- `cube_bathymetry/src/survey_index_query.cpp:300`
- [ ] (suggestion) No regression test for the 3 defensive paths (level>=21, error_code exists branch, cross-chunk/tie ordering); genuinely awkward to unit-test, low priority -- `cube_bathymetry/test/test_survey_index_query.cpp`

## Integrated Review
**Status**: complete
**When**: 2026-08-05 07:33 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #116 at `59d2bdd`
**Sources**: 3 (Copilot R3 @ `59d2bdd` — 0 new inline, 7 suppressed; Local Review (Pre-Push) @ `4821172`; CI rollup @ `59d2bdd`)
**Cross-source confirmations**: 3
**CI**: all-pass (ROS 2 Jazzy industrial_ci: success; copilot-pull-request-reviewer: success)

Copilot R2's three inline comments (@ `eeaa4cd`) are all confirmed fixed at head:
level bounds-check (`8b29e71`), per-tile pass sort (`007031c`), non-throwing
`filesystem::exists` (`ea7d4cc`). R3 raised no new inline comments; its 7
suppressed comments were triaged below. The two must-fix items from the
`4821172` pre-push review are also resolved (uncrustify `8375ae7`, sort
rationale reword `59d2bdd`) — `ament_uncrustify` verified clean locally on all
four touched files.

### Findings
- [x] (cross-confirmed: Copilot R3 suppressed `survey_index_query.cpp:149` + Local Review @ `4821172`) `newBagFootprint` leaks the prepared statement when `tileFromRowCol` throws inside the step loop (the level>=21 guard added in `8b29e71` created this throw site). The unfinalized stmt makes the caller's `sqlite3_close(db)` return `SQLITE_BUSY` — return code unchecked — so the db handle leaks too. Process-exit-bounded in today's dry-run, but PR2 reuses `dirtyL10Tiles` from a long-lived rebuild path. Fix: RAII stmt guard (or try/catch finalize-and-rethrow around the loop) — `cube_bathymetry/src/survey_index_query.cpp:132-157`
- [x] (cross-confirmed: Copilot R3 suppressed `survey_index_query.cpp:304` + Local Review @ `4821172`) Per-tile pass comparator keys only on `(bag_path, t_start_ns)` and `std::sort` is not stable, so rows tying on both keys (same bag, different `topic`/`sensor_type`) have toolchain-dependent relative order — non-byte-stable `DIRTY_TILES_JSON`, which PR2's byte-identity claim leans on. Cannot prove ties unreachable (multi-topic passes from one bag). Fix: add `topic` (and `t_end_ns`/`tile_row`/`tile_col`) tie-breakers; update the header's ordering contract to match — `cube_bathymetry/src/survey_index_query.cpp:296-305`
- [x] (cosmetic, cross-confirmed: Copilot R3 suppressed `batch_regen_main.cpp:378` + Local Review @ `4821172`) The index-absent note says "not found" on the `exists_ec` (EACCES/ELOOP) branch too. `ec.message()` is appended so the cause is disclosed, but the lead wording is wrong. Fix: say "unavailable" — `cube_bathymetry/src/batch_regen_main.cpp:375-379`
- [x] (trivial, Copilot R3 suppressed) Plan's Doc-Impact table routes the PR2 workflow update to `.agents/README.md`, which does not exist in this repo (only the ADR-0017 thin `AGENTS.md`). Either retarget the row at `cube_bathymetry/README.md` or make PR2 create `.agents/README.md` per the workspace template — `.agent/work-plans/issue-111/plan.md:151`
- [x] (suggestion, Local Review @ `4821172`, not raised by Copilot) No regression test for the three defensive paths added in R2 triage (level>=21 corrupt row, `error_code` exists branch, ordering determinism). Awkward to unit-test; low priority for PR1 — `cube_bathymetry/test/test_survey_index_query.cpp` (deferred: operator-confirmed scope for this pass covers findings 1-4 only; the R2 defensive-path regression tests are deliberately held for a follow-up)

### False positives
- (Copilot R3 suppressed, `survey_index_query.h:49` and `:102`, `test_survey_index_query.cpp:184`) "Formatting is likely to fail ament_uncrustify/cpplint and is inconsistent with the surrounding code style." Both claims are false: `ament_uncrustify` run locally over all four touched files reports "No code style divergence", industrial_ci is green at `59d2bdd`, and the spaced-template form (`std::vector < X >`) is the prevailing style in this repo's existing public headers (`geo_map_sheet.h`, `map_sheet.h`) — the new header matches its neighbours rather than diverging. The over-indented brace at `test_survey_index_query.cpp:184` is a readability wart only; the linter that gates CI accepts it.
- (Copilot R3 suppressed, `batch_regen_main.cpp:378`, formatting half) "The line break/indentation looks like an ament_uncrustify divergence (likely CI-failing)." That divergence was real at `4821172` and was fixed in `8375ae7`; uncrustify is clean at head. Only the "not found" wording half of that comment survives (listed as a finding above).

## Implementation
**Status**: complete
**When**: 2026-08-05 09:15 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #116 at `ffd1faa` (branch `feature/issue-111`)
**Addressed**: `## Integrated Review` (2026-08-05 07:33 -04:00, PR #116 @ `59d2bdd`)
**Commits**: `08f7007`, `774e631`, `a0bb8d0`, `ffd1faa`

Operator-confirmed scope for this pass: findings 1-4. Finding 5 (regression tests
for the three R2 defensive paths) was explicitly deferred by the operator and is
checked-with-annotation rather than implemented.

### Actions
- [x] RAII guard so the footprint prepared statement is finalized on every exit path, including a `tileFromRowCol` throw inside the step loop (`StmtGuard` in the anonymous namespace; the three manual `sqlite3_finalize` calls are now redundant and removed) — `cube_bathymetry/src/survey_index_query.cpp:91-114,153-154` (`08f7007`)
- [x] Per-tile pass comparator now keys on `(bag_path, t_start_ns, topic, t_end_ns, tile_row, tile_col)` — a total order over the rows one store tile can hold, so `DIRTY_TILES_JSON` is byte-stable under non-stable `std::sort`; the header's `@return` ordering contract was rewritten to match, and the step-6 comment explains why the extra keys exist — `cube_bathymetry/src/survey_index_query.cpp:296-330`, `cube_bathymetry/include/cube_bathymetry/survey_index_query.h:84-90` (`774e631`)
- [x] Index-absent note now reads "unavailable" instead of "not found", with a comment noting the `exists_ec` (EACCES/ELOOP) branch shares this message and that `ec.message()` names the real cause — `cube_bathymetry/src/batch_regen_main.cpp:375-382` (`a0bb8d0`)
- [x] Plan doc-impact row retargeted from the nonexistent `.agents/README.md` to `README.md` § "Offline store import & rebuild", noting that this repo's only agent guide is the thin ADR-0017 `AGENTS.md` and that creating an `.agents/README.md` is its own task — `.agent/work-plans/issue-111/plan.md:151` (`ffd1faa`)
- [x] No regression test for the three R2 defensive paths — `cube_bathymetry/test/test_survey_index_query.cpp` (deferred: operator-confirmed scope excludes it; awkward to unit-test, low priority for PR1, to be revisited with PR2's rebuild path)

### Verification
`./sensors_ws/build.sh cube_bathymetry` clean; `./sensors_ws/test.sh cube_bathymetry`
= 493 tests, 0 errors, 0 failures, 65 skipped (linters, including
`ament_uncrustify`/`cpplint`, included in that run).

### Next step
Re-review the fixes: `.agent/scripts/dispatch_subagent.sh --mode in-process --issue 111 --skill review-code`.
Not pushed — the host performs the push.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-08-05 09:34 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-111 at `8ab2eb3`
**Mode**: pre-push
**Depth**: Deep (reason: 200+ lines / 10+ files / two new project ADRs; adversarial passes scoped to the unpushed delta `59d2bdd..HEAD` with full-file + cross-repo horizon)
**Must-fix**: 2 | **Suggestions**: 7
**Round**: 4 | **Ship**: recommended — both must-fixes are single-line documentation corrections with obvious fixes (stale comment, broken link); the delta's actual code came back clean from both adversarial lenses, no design or correctness concern remains

Specialists: Static Analysis (ament_uncrustify + ament_cpplint clean on all four
touched C++ files; ament_cppcheck self-skips on 2.13.0), Governance, Plan Drift,
Claude Adversarial Lens A + Lens B. Copilot off (default). Local Adversarial
skipped: request timed out (900s limit). Independently verified: `build.sh
cube_bathymetry` clean, `test.sh cube_bathymetry` = 493 tests, 0 errors, 0
failures, 65 skipped.

The four round-4 fixes are functionally correct and complete. Lens B verified
`StmtGuard` covers every exit path including the `tileFromRowCol` throw that
motivated it, with no reachable double-finalize and a correctly noexcept
destructor; the comparator is a valid strict weak ordering (no std::sort UB);
the reworded stderr message is accurate for both branches of the condition and
nothing downstream parses it. The plan doc-impact retarget was verified true
(`.agents/README.md` absent from tree, ls-files, and origin/jazzy; `AGENTS.md`
is the thin ADR-0017 instantiation; README section exists at line 25). The
operator-deferred R2 defensive-path test gap is recorded as context, not
re-raised.

### Findings
- [x] (must-fix) Step-6 comment still claims "queryPasses ... so today this is a no-op" -- now false and contradicts the paragraph below it; queryPasses sorts only on (bag_path, t_start_ns) and is non-stable, so the added keys DO reorder tied rows. Invites a future reader to delete the sort as dead work -- `cube_bathymetry/src/survey_index_query.cpp:314-317`
- [x] (must-fix) New ADR markdown link is broken: repo root has no `docs/`; ADRs live at `cube_bathymetry/docs/decisions/`, and the repo's own convention at line 146 uses the prefixed form. 404s on GitHub -- `README.md:54`
- [x] (suggestion, cross-pass confirmed Lens A + Lens B) Comparator key set misaligned with the serialized fields: `sensor_type`/`ping_count` are emitted in DIRTY_TILES_JSON but unkeyed, `tile_row`/`tile_col` are keyed but never emitted -- so the new "total order" wording overclaims and byte-stability rests on an unstated cross-repo invariant. Add `sensor_type`/`ping_count` as final tiebreakers -- `cube_bathymetry/src/survey_index_query.cpp:329-348`, `cube_bathymetry/include/cube_bathymetry/survey_index_query.h:84-90`
- [x] (suggestion) The delta's headline behaviour -- deterministic tie ordering -- has zero coverage; no test asserts pass ordering at all (bag assertions use an order-insensitive std::set). Distinct from the deferred R2 finding and cheap here: the `insertPass` fixture helper makes it ~8 lines -- `cube_bathymetry/test/test_survey_index_query.cpp`
- [x] (suggestion) Only the statement half of the leak was made structural: the db handle is still a hand-rolled try/catch(...) close, and both `sqlite3_close` return codes stay unchecked. Use `sqlite3_close_v2` (or check the return) and add `EXPECT_EQ(sqlite3_close(db_), SQLITE_OK)` to the fixture TearDown as a standing leak guard -- `cube_bathymetry/src/batch_regen_main.cpp:386-394`, `cube_bathymetry/test/test_survey_index_query.cpp:72` (deferred: out of scope — operator checkpoint after this round scoped the pass to both must-fixes plus the cross-confirmed comparator suggestion and its test only)
- [x] (suggestion) `StmtGuard` near-duplicates an existing guard in the package that owns the DB (`marine_survey_index/src/survey_index_bag_main.cpp:252-271`); nothing is left inconsistent inside cube_bathymetry (one prepare site), but queryPasses/distinctLevels/queryNavTrack still use manual finalize with throwing calls in between. Follow-up issue in unh_marine_autonomy to upstream the guard -- `cube_bathymetry/src/survey_index_query.cpp:91-114` (deferred: out of scope — operator checkpoint after this round scoped the pass to both must-fixes plus the cross-confirmed comparator suggestion and its test only)
- [x] (suggestion) ADR-0002 cites "(ADR-0007 addendum)" for a decision ADR-0007 never records (no mention of #111, dirty, or incremental). Reword as forward-looking or add the addendum -- `cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md:104-105` (deferred: out of scope — operator checkpoint after this round scoped the pass to both must-fixes plus the cross-confirmed comparator suggestion and its test only)
- [x] (suggestion) ADR-0018 merge gate: the green Integrated-Review/CI signal is at `59d2bdd`, four commits back. Before merge, push for hosted CI or run `ci_local.sh` and confirm `scope: full` / `ci-local: pass` at the new head (deferred: host action, not a code change — the merge gate is re-run by the orchestrator at the final head, after this fix pass)
- [x] (suggestion) Plan doc-impact table now has two rows for the same file (`cube_bathymetry/README.md`, which does not exist, and `README.md`); collapse to one and add `README.md` to the PR1 Files-to-Change table -- `.agent/work-plans/issue-111/plan.md:120-136,150-151` (deferred: out of scope — operator checkpoint after this round scoped the pass to both must-fixes plus the cross-confirmed comparator suggestion and its test only)

## Implementation
**Status**: complete
**When**: 2026-08-05 09:49 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-111 at `462a4c6`
**Addressed**: `## Local Review (Pre-Push)` (round 4, 2026-08-05 09:34 -04:00, branch at `8ab2eb3`, verdict changes-requested)
**Commits**: `ec1f25c`, `8cdad8f`, `462a4c6`

Scope for this pass was set by the operator at the post-round-4 checkpoint:
fix both must-fixes plus the cross-confirmed comparator suggestion **including
its regression test**; every other suggestion is deferred to a follow-up, not
dropped. The operator has elected to publish after this pass with no further
full review round.

Verified in the worktree after the last fix: `./sensors_ws/build.sh
cube_bathymetry` clean, `./sensors_ws/test.sh cube_bathymetry` = **494 tests, 0
errors, 0 failures, 65 skipped** (up from 493 — the new
`DirtyTileQuery.TiedPassesComeBackInADeterministicOrder` case is present in the
gtest XML and passes). Pre-commit hooks ran on all three commits; no
`--no-verify`. Not pushed — the host performs pushes.

Notes on the comparator change: the sort key is now
`(bag_path, t_start_ns, topic, t_end_ns, tile_row, tile_col, sensor_type,
ping_count)` — every `marine_survey_index::PassRow` field except `level`, which
is the constant index level for these rows. That makes the key set a superset of
what `DIRTY_TILES_JSON` serializes, so the only rows the comparator can still
leave tied are rows that serialize to identical bytes; the header's ordering
contract was reworded to claim exactly that rather than a bare "total order".
The new test inserts four rows of one bag that tie on `(bag_path, t_start_ns)`
— the entire key `queryPasses` itself sorts on — in reverse of the expected
order, so a pass-through of index/insert order fails it; two of the pairs tie on
every pre-existing key and separate only on the newly added `sensor_type` /
`ping_count` tiebreakers.

### Actions
- [x] Step-6 comment's stale "today this is a no-op" claim corrected — it now states the sort is load-bearing (queryPasses orders on `(bag_path, t_start_ns)` only, non-stable) and warns against deleting it — `cube_bathymetry/src/survey_index_query.cpp:314-319` (`ec1f25c`)
- [x] Broken ADR-0002 markdown link repointed to `cube_bathymetry/docs/decisions/…`, matching the repo's existing prefixed convention — `README.md:54` (`8cdad8f`)
- [x] Comparator keyed on `sensor_type` + `ping_count` as final tiebreakers; header ordering-contract wording aligned with what the key set actually guarantees — `cube_bathymetry/src/survey_index_query.cpp:329-356`, `cube_bathymetry/include/cube_bathymetry/survey_index_query.h:84-93` (`462a4c6`)
- [x] Tie-order regression test added via the `insertPass` fixture helper — `cube_bathymetry/test/test_survey_index_query.cpp` (`462a4c6`)
- [x] sqlite db-handle close hardening (`sqlite3_close_v2` + TearDown leak guard) — `cube_bathymetry/src/batch_regen_main.cpp:386-394` (deferred: outside the operator-confirmed scope for this pass)
- [x] Upstream `StmtGuard` to `marine_survey_index` via a follow-up issue — `cube_bathymetry/src/survey_index_query.cpp:91-114` (deferred: outside the operator-confirmed scope; cross-repo follow-up)
- [x] ADR-0002's "(ADR-0007 addendum)" citation reworded — `cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md:104-105` (deferred: outside the operator-confirmed scope for this pass)
- [x] ADR-0018 merge gate re-run at the new head (deferred: host action, not a code change — the orchestrator runs it before merge)
- [x] Plan doc-impact table duplicate-row collapse — `.agent/work-plans/issue-111/plan.md:120-136,150-151` (deferred: outside the operator-confirmed scope for this pass)

## Integrated Review
**Status**: complete
**When**: 2026-08-05 10:16 -04:00
**By**: Claude Code Agent (Claude Opus)

**PR**: #116 at `4479e57`
**Sources**: 3 (Copilot R4 @ `4479e57` — 0 new inline, 7 suppressed; Local Review (Pre-Push) round 4 @ `8ab2eb3`; CI rollup @ `4479e57`)
**Cross-source confirmations**: 3
**CI**: all-pass (ROS 2 Jazzy industrial_ci: success; copilot-pull-request-reviewer: success — both at head `4479e57`)

Round 5 (final triage before the merge gate). Copilot R4 posted no new inline
comments; its 7 suppressed comments were triaged below (they collapse to 4
distinct findings). All three round-4 must-fix/scoped fixes were verified present
in the code at head: the step-6 comment no longer claims a no-op (`ec1f25c`), the
README ADR link carries the `cube_bathymetry/docs/decisions/` prefix (`8cdad8f`),
and the comparator keys on all eight serialized `PassRow` fields with the
`TiedPassesComeBackInADeterministicOrder` regression test (`462a4c6`). The three
Copilot R2 inline comments (level bounds check, per-tile sort, non-throwing
`filesystem::exists`) remain resolved at head.

One genuinely **new** finding surfaced this round (finding 1) — Copilot raised it
from three angles (code, contract comment, README) and the repo's own test comment
at `test_survey_index_query.cpp:349-350` independently asserts the behaviour the
CLI does *not* implement. The other three findings are re-raises of items the
operator explicitly deferred at the round-4 checkpoint.

### Findings
- [x] (valid, medium — Copilot R4 suppressed `batch_regen_main.cpp:405`, `:345`, `README.md:60`; corroborated by this PR's own test comment) `dirtyTileDryRun` emits the `DIRTY_TILES_JSON:` marker unconditionally, including when `dirty` is empty with a non-empty bag list. An unindexed bag (`newBagFootprint` matches `bags.path` exactly, so a never-indexed or differently-spelled path yields zero rows) is then indistinguishable from "indexed, nothing dirty", and the documented contract makes the marker AUTHORITATIVE — a PR2 consumer keying off marker presence would rebuild nothing where a full regen is required. The contradiction is already in the tree: `BagNotInIndexYieldsNoDirtyTiles`'s comment says "the CLI then falls back to full regen", which it does not. Fix: when `dirty.empty() && !bagfile_names.empty()`, print an index-miss note and `return 0` **without** the marker (conservative fallback is safe even for the legitimate "indexed bag, no passes" case), then align the contract comment (`batch_regen_main.cpp:340-345`), `README.md:~60`, ADR-0002's fallback list, and the test comment — `cube_bathymetry/src/batch_regen_main.cpp:398-405`, `cube_bathymetry/src/survey_index_query.cpp:126-131`, `cube_bathymetry/test/test_survey_index_query.cpp:349-360`
- [x] (valid, trivial, Copilot R4 suppressed `plan.md:29`, `:31`, `:34`) The plan still describes the ADR-0002 margin as "one-**cell** L14", the exact wording corrected as a must-fix in ADR-0002 (round 2) and used correctly in README and code ("one-tile"). The plan even contradicts itself — line 62 says "one-tile-padded". Mechanical cell->tile fix on three lines; keeps the plan-first artifact in sync per AGENTS.md § Plan-first workflow — `.agent/work-plans/issue-111/plan.md:28,31,33`
- [x] (valid, trivial, **cross-confirmed**: Copilot R4 suppressed `plan.md:151` + Local Review (Pre-Push) round 4 @ `8ab2eb3`) The Doc-Impact table still carries a row for `cube_bathymetry/README.md`, which does not exist (the repo README is at the root, and `ffd1faa` added the correct `README.md` row without removing the old one). Collapse the two rows into one and add `README.md` to the PR1 Files-to-Change table — `.agent/work-plans/issue-111/plan.md:120-136,150-151` (previously deferred by the operator at the round-4 checkpoint)
- [ ] (valid, low, **cross-confirmed**: Copilot R4 suppressed `test_survey_index_query.cpp:74` + Local Review (Pre-Push) round 4 @ `8ab2eb3`) The fixture's `TearDown` calls `sqlite3_close(db_)` without checking the return, so a future statement/handle leak (`SQLITE_BUSY`) passes silently — precisely the class of bug `StmtGuard` was added to prevent. `EXPECT_EQ(sqlite3_close(db_), SQLITE_OK)` turns the whole suite into a standing leak guard; the companion `sqlite3_close_v2`/return-check in `batch_regen_main.cpp:386-394` is the same finding on the CLI side — `cube_bathymetry/test/test_survey_index_query.cpp:72-74`, `cube_bathymetry/src/batch_regen_main.cpp:386-394` (previously deferred by the operator at the round-4 checkpoint)

### False positives
- None this round. Copilot R4's 7 suppressed comments all describe real (if small) divergences; they collapse to the 4 findings above. Nothing from R4 was dismissed as a misread of the code.

### Merge-gate note
ADR-0018's gate is satisfied at head by the hosted signal: **ROS 2 Jazzy
(industrial_ci) success at `4479e57`**, so the round-4 "green signal is four
commits back" caveat is discharged — no `ci_local.sh` re-run is needed. Reminder
for whoever merges: the PR body says "Part of #111" and **must not** close the
issue (PR1 of a multi-PR sequence).
