---
issue: 96
---

# Issue #96 — Adopt simplified cube stores: seed precedence, batch-regen, sufficient-statistic backscatter

## Issue Review
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #96
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

The three components (seed precedence, batch-regen, backscatter bands) are tightly
interdependent — all three consume the new uma#248 store format and share the
tile-reload path. Grouping them is appropriate. The batch-regen scatter/gather path
is the most novel and complex piece; it is explicitly scoped by an "bit-exact"
acceptance criterion that makes validation concrete.

**Hard dependency: unh_marine_autonomy#248 must land first.** The two named store
layers (`survey` + `reference`, finalized names per the issue header note) and the
3-band backscatter sufficient-statistic format are defined there. Implementation
cannot begin until uma#248 is merged.

**Right repo?** Yes — cube_bathymetry project repo. All three parts are CUBE-side
processing changes.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | `--help` + README coverage is an acceptance criterion; seed precedence semantics clearly specified with per-rung behavior |
| Enforcement over documentation | Watch | Acceptance criteria imply testable behaviors (bit-exact regen, Welford round-trip, n=1 sentinel) but test coverage for seed-precedence rung semantics (reference layer must NOT be accumulated as measured data) is not explicitly stated as a test requirement |
| Capture decisions, not just implementations | Action needed | Issue references "ADR-0002 (store) — addendum expected" but no ADR-0002 exists in `docs/decisions/`; the existing ADR-0007 addendum file notes that the canonical `0007-mbes-backscatter-store.md` has not yet been authored. Implementation must resolve which ADR documents are authored/addended (see Actions) |
| A change includes its consequences | OK | Acceptance criteria cover ops script (`build_massabesic_store.sh`), help/README, backscatter round-trip test |
| Only what's needed | OK | Explicitly greenfield; scoped to what uma#248 enables; no backwards-compatibility required |
| Improve incrementally | Watch | Scope is substantial (seed precedence + batch-regen + backscatter), but all three parts share the same store-interface change and cannot be split without partial uma#248 adoption — grouping is justified |
| Test what breaks | Watch | "bit-exact vs never-evicted pass" and "Welford round-trip incl. n=1 sentinel" are good regression targets; explicitly add a test asserting reference-layer seed does not increment accumulated sample counts |
| Workspace vs. project separation | OK | All changes in cube_bathymetry project repo |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| Workspace ADR-0001 (adopt ADRs) | Yes | Design decisions being made; addendums expected as part of this work |
| Workspace ADR-0002 (worktree isolation) | Yes | Feature worktree already exists |
| cube_bathymetry ADR-0001 (lossless reload + eviction) | Yes | Seed precedence generalizes the startup prime and revisit-reload paths in ADR-0001 §1/§2; addendum required to document the two-rung precedence semantics and the `seed_settled` flag distinction |
| cube_bathymetry ADR-0007 addendum (MBES backscatter) | Yes | The sufficient-statistic backscatter bands (mean, confidence-scaled standard_error, sample_sd) extend Phase B.2 (Welford spill/restore); the Welford reconstruction formula (`n=(sample_sd/standard_error)^2`, M2, n=1 sentinel) is a new design decision that needs ADR coverage |

**Gap**: "ADR-0002 (store)" cited in the issue is unresolvable as written — no `0002-*.md` exists in `docs/decisions/`. This is likely the not-yet-authored canonical `docs/decisions/0007-mbes-backscatter-store.md` mentioned in the existing ADR-0007 addendum file, or a new addendum to ADR-0001. The plan phase must decide and document which files are created/updated.

### Consequences (from workspace map)

- New store-layer names (`survey` + `reference`) replace prior placeholder names (`cube` + `pre-existing`) throughout — ensure all code paths, docs, and scripts use the finalized names.
- The ops script `build_massabesic_store.sh` (unh_echoboats_project11 #352/PR#353) is an explicit acceptance criterion dependency; changes to the importer CLI (`--seed` flags or equivalent) must keep that script working.
- Downstream: unh_marine_autonomy#247 (sidescan) is parked behind this; no action needed here, but plan/review should note the interface this issue establishes is a dependency for that work.

### Actions
- [ ] Resolve the "ADR-0002 (store)" reference: decide whether to author `docs/decisions/0002-store-layer-design.md` (new) or add an addendum to ADR-0001 and the canonical ADR-0007, and state that decision in the plan.
- [ ] Add an explicit test for reference-layer seed precedence semantics: assert that a tile seeded from the `reference` layer does not contribute to accumulated sample counts (i.e., `seed_settled=false` path is not counted as measured data).
- [ ] Confirm that all identifiers, CLI flags, and store-path logic use the finalized layer names `survey` + `reference` (not the old `cube` + `pre-existing`).
- [ ] Verify uma#248 has landed before beginning implementation; gate the plan on this dependency.

## Plan Authored
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-96/plan.md` at `795b888`
**Branch**: feature/issue-96 at `795b888`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.
