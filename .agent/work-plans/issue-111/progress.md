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
