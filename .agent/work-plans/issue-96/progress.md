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

## Plan Review
**Status**: complete
**When**: 2026-07-01 11:54 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-96/plan.md` at `795b888`
**PR**: PR-less (`--issue 96`)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) File table omits three compile-breaking files that use removed `SourceLayer::Draft`/`Chart`: `src/cube_bathymetry_node.cpp`, `test/test_persistence.cpp`, `test/test_tile_eviction_rss.cpp` — `plan.md:104-116`
- [ ] (must-fix) Live-node `Draft`→`Survey` layer switch is an unstated behavioral consequence; confirm/document the live node's target layer — `plan.md:29-42`
- [ ] (suggestion) `welfordFromCell` collapses `n→1` when `sample_sd==0` for `n≥2` (identical samples); guard or document in ADR-0007 addendum — `plan.md:54-60`
- [ ] (suggestion) Explicitly resolve the issue's "ADR-0002 (store)" citation as addressed via ADR-0001/0007 addenda — `plan.md:129-134`

## Implementation
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Opus

**Branch**: feature/issue-96 (5 new commits: `ae5f68c`..`e022eb5`, on top of plan `6b92cb1`)

### What was implemented (all plan steps)

1. **uma#248 migration (compile fix).** `SourceLayer` Draft/Processed/Chart →
   Survey/Reference; `BathyCell` 2-band; `MbesCell` 3-band; per-cell
   `SourceRegistry` → store-level `StoreMetadata`. Touched `store_import.{h,cpp}`,
   `cube_bathymetry_node.cpp` (live node now writes the **`survey`** layer,
   operator-confirmed), `import_bag_main.cpp`, and all four affected tests.
2. **Backscatter 3-band write** (`geoGridToBackscatterCells`): n=1 sentinel +
   n≥2 formulas, `kBackscatterConfidenceScale = 1.96f` shared constant.
3. **Backscatter Welford reconstruction** (`welfordFromCell`): exact inverse;
   n≥2 identical-sample limitation documented in the ADR-0007 addendum (not
   code-guarded, as planned).
4. **Seed precedence** (`ImportAccumulator::seedNewTile` + `seeded_` set): survey
   warm-start (settled + backscatter Welford restore) > reference gate
   (predicted-only, not measured, no backscatter) > blank. `import_bag` gains
   `--reference-store` (replacing `--prior`); the upfront whole-sheet load is gone.
5. **Batch-regen**: `BatchRegen` (`batch_regen.{h,cpp}`, in the store_import
   library) + `batch_regen_bag` binary (`batch_regen_main.cpp`) + CMake target.
   Scatter to per-tile disk buckets → gather each tile in one unbounded pass →
   write once via new `ImportAccumulator::persistResidentTile`. Scratch dir cleaned
   up in `finalize`.
6. **Tests**: `test_batch_regen` (byte-exact vs single-pass unbounded, multi-tile
   + revisit + scratch cleanup); `test_import_eviction.ReferenceSeedDoesNotAddMeasuredData`;
   `test_store_import` Welford round-trips (`WelfordFromCellInvertsEncode`,
   `BackscatterWelfordRoundTripMultiSample`, n=1 sentinel in `BackscatterCellsMatchGridRecords`).
7. **Docs**: ADR-0001 addendum (two-rung seed precedence + `seed_settled` contract
   + batch-regen exact path + live-node→survey); ADR-0007 addendum (3-band
   write/reconstruct formulas + n=1 sentinel + n≥2 identical-sample limitation);
   README offline-import/rebuild + seed-precedence + backscatter-fidelity section.

### Build / test status — GREEN (built in-container)

The store libs (`marine_bathymetry_store`, `marine_mbes_backscatter_store` and deps)
were built from `layers/main/core_ws` source in-container, then `cube_bathymetry`
built and tested against them:

- `cube_bathymetry`: **build GREEN**, **tests GREEN** — 441 tests, 0 failures, 58
  skipped (skips are unrelated pre-existing); lint (uncrustify + cpplint) clean on
  all new/changed files. `batch_regen`'s 3 bit-exact-match tests pass.

### Deviations (also noted in plan.md § Deviations)

- Registry migration to `StoreMetadata` was required for compilation but unlisted
  in the plan; `finalize()` signature changed to `StoreMetadata *` pointers.
- Removed dead `timestamp_ns`/`source_index` params from the conversion functions
  and `ImportAccumulatorConfig` (the new cells don't carry them).
- Dropped `--source-id`/`--sensor-class` from `import_bag` (greenfield; no per-cell
  source id). `BatchRegen` added as a library class (not just a binary) so it is
  unit-testable.

### Host must finish

- **Out of scope (follow-up ops PR, different repo):** `build_massabesic_store.sh`
  in unh_echoboats_project11 must drop `--bathy-layer`/`--prior`/`--source-id`/
  `--sensor-class` and switch to `--reference-store`; it will otherwise error on the
  removed flags. Provenance now flows through `--platform`/`--sensor`/`--campaign`.
- Push + open PR (local-first; not pushed by this sub-agent).
