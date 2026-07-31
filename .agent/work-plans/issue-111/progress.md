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
