---
issue: 173
---

# Issue #173 — Slope-aware incidence correction for M3 backscatter via the shared seabed-geometry module (marine_backscatter)

## Issue Review
**Status**: complete
**When**: 2026-11-10 12:30 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Issue**: #173
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped (one issue, two PRs at the derive-tool boundary is the safer cut; see Actions)

### Actions
- [ ] Action needed: the issue body predates two decisions. Body says the CUBE adapter samples "the pass's own in-memory grid" and computes incidence in `GeoGrid::insert`; the operator comment (2026-10-30) makes the OFFLINE import two-pass against the finished own-survey surface. The plan must treat the body as superseded for `import_bag` and keep the in-memory/"surface so far" idea only as the live-node open question.
- [ ] Action needed: the issue never says how `derive_angular_response.py` gets the same angle as the import. It reads raw bags and bins by `rx_angle`; an incidence-fitted table applied by the import needs the same number per beam. The plan must name one source of truth for the angle and the table/fingerprint basis marker.
- [ ] Action needed: the dependency "uma geometry module" is not available. The work exists only as local, unpushed, un-code-reviewed branch `feature/issue-304` (10 commits, 318 tests green, plan review done, `/review-code` pre-push still owed). cube#173 cannot start implementation until it is reviewed, pushed and merged.
- [ ] Action needed: `batch_regen_bag` (own `ScatterRecord` spill, no TF, no SonarInfo) is not mentioned. Decide refuse-with-message (like the table) versus support.
- [ ] Watch: "reuse the depth-adaptive recon structure" is right for the spill and replay (`ReconCollector::forEachSpilled`) but `--depth-adaptive` refuses `--bs-store` today (single-level backscatter store), so the two-pass slope path is fixed-level only. Say so.
- [ ] Watch: dependency "cube#172 part B must land first" is satisfied (#172 and #176 are on jazzy); the issue text can be refreshed.
- [ ] Watch: issue and design note use the name `DepthSampler`; the implemented API is `marine_backscatter::seabed::SeabedHeightSampler` (up-positive heights, `SourceId`, per-axis spans, `FallbackReason`). The plan uses the real names.
- [ ] Watch: no ADR; record the design as an addendum to the ADR-0007 addendum set (the design is still evolving).

## Plan Authored
**Status**: complete
**When**: 2026-11-10 12:29 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Plan**: `.agent/work-plans/issue-173/plan.md` at `823fb4e`
**Branch**: feature/issue-173 at `823fb4e` (local only, not pushed, no PR)
**Phases**: single issue; two PRs recommended (commit groups A-D, then E-F)

### Open questions
- [ ] uma#304 must be reviewed, pushed and merged before groups E-F (local branch feature/issue-304, 10 commits, no /review-code yet)
- [ ] Derive tool angle source: C++ beam ledger versus Python re-implementation
- [ ] Fallback definition: earth-frame angle from vertical versus sonar-frame |rx_angle|
- [ ] Live node v1: flat-normal earth-frame (L1) versus staying on beam angle; running-surface slope deferred
- [ ] batch_regen_bag with incidence: refuse versus support
- [ ] Include --slope-surface (skip pass one) now; stencil floor default; one PR or two; default basis stays beam until Appledore comparison

## Plan Authored
**Status**: complete
**When**: 2026-11-10 14:20 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Plan**: `.agent/work-plans/issue-173/plan.md` updated with the operator decisions (a "Decisions" section) at commit 6213722

### Decisions (2026-11-10, operator)
1. uma#304 geometry module first: done (merged, unh_marine_autonomy PR #413, jazzy dd737cd7).
2. The derive tool gets its angle from the import's per-beam output (the ledger), not a Python re-implementation.
3. Fallback angle where there is no surface: earth-frame angle from vertical, not the sonar-frame |rx_angle|.
4. Live node, first version: earth-frame angle with a flat normal, no_surface counted; a running own surface is deferred.
5. batch_regen_bag refuses incidence-angle runs.
6. Include --slope-surface <store> (reuse a depth store, skip pass one).
7. Slope stencil: 3 cells with a minimum baseline in metres (default 4 m).
8. Two PRs, split after commit group D.
9. Existing beam-angle tables stay valid for beam runs and are refused for incidence runs, and the reverse.
10. Default angle basis stays beam until an Appledore upslope/downslope comparison shows the correction helps.
11. The 4 m default is provisional; measure the pass-one depth uncertainty on real data first. Kept as a parameter, marked provisional in README and plan.

### Plan sync with the merged module
- Geometry-module section rewritten to the merged API (heights ellipsoidal up-positive, flat fallback with reasons, caller counts, tile-seam behaviour, thread-safety, cache sizing).
- Angle selection moved to Node::update's door, so the median queue layout is unchanged; beam_index moved to PR 2 (ledger).
- Implementation instruction for PR 1 lists the module-dependent content (sampler adapter, two-pass import, --slope-surface) as PR 1 content; the plan records PR 1 = groups A to E, PR 2 = group F. This differs from the literal "split after D" of decision 8 and is flagged for the operator.

## Plan Review
**Status**: complete
**When**: 2026-11-10 14:25 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Plan**: `.agent/work-plans/issue-173/plan.md` at `6213722`
**PR**: PR-less
**Verdict**: changes-requested

Checked against the merged `marine_backscatter` headers (dd737cd7) and the cube source. The geometry-module claims hold (up-positive ellipsoidal heights, `step_m` as a +/- half-width, flat fallback with reasons counted by the caller, `SourceId` = layer + level, throws on corrupt tiles, `incidenceAngle` raw above pi/2, 1e-6 unit tolerance) and so does the sign argument for the CUBE `processed` layer. The operator's eleven decisions are reflected; the findings below are about the two-pass mechanics, the refusal paths and a few stale or contradictory passages.

### Findings
- [ ] (must-fix) Pass one warm-starts from a populated `-o`, pass two (empty scratch store) does not: `ImportAccumulator::seedNewTile` rung 1 primes from an existing `processed` tile in `store_dir` and only restores backscatter from `--bs-store` when that bathy tile exists. So an `incidence` run into a non-empty `-o`/`--bs-store` either trips the depth-equality compare or, for backscatter, overwrites the existing survey tiles instead of blending. Specify it (refuse a populated `-o`/`--bs-store` for `incidence`, or seed the scratch store); also require the scratch dir to be fresh/empty (a leftover one would warm-start pass two) — `plan.md:70`
- [ ] (must-fix) "Depth estimation is deterministic, so pass two's depth must equal pass one's" is not established under eviction or different batching. Pass one adds per ping; the existing replay (`import_bag_main.cpp` ~2200, `test_mixed_level_import.cpp:239`) adds in 256-sounding chunks; eviction runs `values()`, which flushes the median pre-filter, and `test_import_eviction.cpp` only asserts bounded vs unbounded to `EXPECT_NEAR` 1e-3, byte identity is only shown with `max_resident_tiles = 0`. Either make both passes use identical batch boundaries and the same budget (spill ping boundaries, or run pass one through the same chunker), or state a tolerance; say which the compare uses, and give the "`--slope-surface` run bit-equal to the two-pass run" test (`plan.md:117`) its conditions (no eviction, or identical schedule) — `plan.md:70`, `plan.md:117`
- [ ] (must-fix) `batch_regen_bag` section is built on a wrong premise: it writes no fingerprint (ADR-0003 #176 amendment, `batch_regen_main.cpp` `gainFingerprintProblem` is read-only), and it has no angle-basis flag to refuse. The real gaps are (i) refusing an incidence-marked table/curve given as `--backscatter-curve` (it would otherwise be applied on `|rx_angle|` silently) and (ii) a read-only check of the `-o` fingerprint like the gain one, refusing to regenerate tiles into a store whose fingerprint records `incidence`. `import_bag` has no existing-fingerprint check at all (no `isStale` use in `import_bag_main.cpp`), so a `beam` run into an incidence-built store blends bases through the seed rung; decide and state the refusal — `plan.md:88`, `plan.md:81-82`
- [ ] (must-fix) The `SpilledSounding` growth breaks a pinned test and a claim: `test_recon.cpp:466-467` asserts `kBytesPerSpilledSounding` in [64, 80] (it is 72 today); a beam vector + incidence + index pushes it past 80 (96+ if the vector is double, as the working tree already has it), so "`test_recon`... run unchanged" is false, and every depth-adaptive run's spill and free-space estimate (`import_bag_main.cpp:1745`) grows for fields it never uses. Use a separate record type for `SoundingSpill`, or update the bound and say so — `plan.md:65`, `plan.md:119`
- [ ] (must-fix) The ledger cannot be written from the spill as planned. The spill is one flat stream with no ping boundary or `stamp_ns`, and `ReconCollector::add` drops refused soundings before spilling, so "beam index implied by position" (`plan.md:104`) is wrong whenever the projector or the door gate dropped a beam. The record needs `ping_ns` and `beam_index` per sounding; add both with the PR 1 layout change (one change to the record, not two) rather than in PR 2 — `plan.md:104`, `plan.md:58`, `plan.md:144`
- [ ] (must-fix) A SonarInfo-delivered curve on an `incidence` run only warns (`plan.md:82`), but it has no marker so by the plan's own rule (absence = beam) it is a beam-basis curve, and decision 9 says beam tables are refused on incidence. With `--backscatter-correction auto` (the default) this is the path a user hits first. Refuse it (the gain-header precedent warns only because there the declaration may have applied) — `plan.md:82`
- [ ] (must-fix) Per-beam table accounting is silent about pass two. `stampCorrectionIndex` counts each beam's fallback reason with `beam_angle` in the bag loop (`import_bag_main.cpp:1459`, `BackscatterModeResolver::countBeam`); under `incidence` the angle is only known in pass two, which has no bag or resolver. The "beam at or beyond 90 deg" and "beyond the outermost bin" counts, `beams_correctable` and the exit-4 `table-unresolved` report would be computed on the wrong angle. State that pass two re-counts on the stamped incidence — `plan.md:69-70`
- [ ] (must-fix) `--slope-surface` identity of "sorted tile file names and sizes" (`plan.md:71`) is nearly empty: store tiles are fixed-size 2-band Float64 (`bathy_dem.hpp`: 960x960x2 doubles), so sizes do not tell a rebuilt surface from the old one. A store rebuilt in place with the same tile set would read fresh. Hash the tile contents (streamed SHA-256), or at least mtime plus size, and say what the registry digest adds — `plan.md:71`, `plan.md:83`
- [ ] (suggestion) The derive tool should write `# angle_basis:` only for `incidence` (`plan.md:105` says it writes `beam` by default). The gain header is written only under a declaration, and writing the marker everywhere changes every default derive output and every table sha256 that the fingerprint compares, against "without `--angle-basis` it behaves exactly as today" — `plan.md:105`
- [ ] (suggestion) The "beam output byte-identical, hash of the stores" check (`plan.md:119`) must exclude `build_fingerprint.json` or the fingerprint must omit the new keys for `beam` runs; as written (`angle_basis` in `backscatter_correction`) the file changes for default runs — `plan.md:83`, `plan.md:119`
- [ ] (suggestion) Record basis, slope scale and surface identity in the fingerprint only when a correction is applied (curve/table). The ledger-producing run (`--backscatter-correction none`, step 7a) leaves raw intensities regardless of basis, so fingerprinting it as `incidence` makes a store with identical values read stale against a `beam` run — `plan.md:83`, `plan.md:106`
- [ ] (suggestion) Record the content identity of the finished surface in both cases (`"self"` for the internal two-pass vs a digest for `--slope-surface`): an equal result then carries an equal fingerprint, and the test that claims bit-equality between the two modes is not undermined by their fingerprints differing. Note the fingerprint's `bags`/`reference_store` keys are still unimplemented (ADR-0003 status), so "its inputs are already the fingerprint's other keys" is weaker than it reads — `plan.md:71`
- [ ] (suggestion) `Node::update(const Sounding &)` does not exist; the sounding-level entry is `Node::insert(double, const Sounding &, const Parameters &)` (`node.h:152`, `node.cpp:219`). Fix the name in the text and the Corrections list — `plan.md:47`, `plan.md:77`
- [ ] (suggestion) State that `--slope-min-scale-m` is a half-width (the module probes at +/- `step_m`; the noise formula already assumes it) so "baseline" in decision 7 is not read as the full 8 m. Also `max(3 cells, 4 m)` cannot be expressed in `NormalOptions` (`step_m` is either 0 = auto or fixed), so it needs a centre lookup for the span first (6 lookups per beam, not 5) or the store's finest level; the "about a microsecond" lookup cost and the 40-minute figure are unmeasured (`BathyDem` does filename-set lookups per probe) — measure before promising it — `plan.md:60`, `plan.md:73`
- [ ] (suggestion) `BathyDem`'s constructor throws when the store has no tiles (an empty pass-one result, or a mistyped `--slope-surface`); catch it with a named message and print its `warnings()` — `plan.md:59`
- [ ] (suggestion) The node's `backscatter_angle_basis` should be configure-only like `backscatter_gain_declaration` (reject a runtime set), or a live flip would mix bases inside the node's sums — `plan.md:94`
- [ ] (suggestion) The beam vector is "three floats" in the plan and double in the working tree; say double (the module's 1e-6 unit tolerance and near-nadir precision are comfortable only in double) and include the size in the spill accounting — `plan.md:58`
- [ ] (suggestion) Stale or contradictory text to sync: Section 6 still calls the live node "open question, not decided here" and "The question for Roland"; the Sequencing table still has S1 (review/push/merge uma#304) as future and says A to D are developed "in parallel"; the Principles table and Estimated Scope still say "A to D... the PR can split there" while Decisions says PR 1 is A to E; the L2 bullet still says uma#304 "falls back flat at tile seams" (superseded); the Open Questions section repeats the answered questions. The A-to-E versus "split after D" difference from decision 8 is already flagged in the plan; keep it as an explicit operator confirmation — `plan.md:95`, `plan.md:126-135`, `plan.md:164`, `plan.md:194-212`

### Design questions the eleven decisions do not settle
- Which pass writes `-o`? The plan has pass one write `-o` and pass two a scratch store, which forces the equality compare and the populated-`-o` problem above. The alternative (pass one into a disposable surface store, pass two into `-o`) gives an `incidence` run the same warm-start, backscatter seeding, reference-prior and fingerprint semantics as a `beam` run and makes the compare a sanity check rather than a precondition, at the cost that the surface contains only this run's soundings. Needs an operator choice.
- Incremental imports into an existing store under `incidence`: refuse (simplest, consistent with the "regenerate is the migration" rule) or support.
- Is the 4 m default (decision 7/11) a half-width or the full differencing baseline? The plan treats it as half-width (8 m across).
