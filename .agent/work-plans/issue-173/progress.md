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

## Plan Authored
**Status**: complete
**When**: 2026-11-12 08:29 -05:00
**By**: Claude Code Agent (Claude Fable 5.2)

**Plan**: `.agent/work-plans/issue-173/plan.md` revision 2 at `12682f7`

### Decisions (2026-11-12, operator)
12. Two backscatter layers: pass one is today's import unchanged; pass two is a separate run that re-reads the bags, takes slope from the finished depth layer, writes a separate slope-corrected layer and no depths.
13. The slope-corrected layer is a derived product regenerated per tile; contributors and an input fingerprint are recorded per tile from the first PR; a tile whose recorded contributors are not all among the given bags is not rebuilt.
14. The slope baseline is an angle (range x angle, floor of one cell either side), parameter in degrees, default provisional, distribution reported.

### Plan changes
- Approach rewritten around a slope run (no `-o`, fresh scratch stores, publish step, `derived_tiles.json`).
- The 2026-11-10 plan review findings are mapped one by one in the plan ("How the plan review is resolved").
- Decision 4 (live node flat-normal mode) is raised again as a question for the operator; not built in PR 1.

## Plan Review
**Status**: complete
**When**: 2026-11-12 08:36 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Plan**: `.agent/work-plans/issue-173/plan.md` at `12682f7`
**PR**: PR-less
**Verdict**: changes-requested

Revision 2, checked against the source and the merged `marine_backscatter` headers (dd737cd7). Decisions 12 to 14 taken as settled. Holds: heights up-positive ellipsoidal, `step_m` a +/- half-width with a one-cell floor and `step_clamped`/`step_used_m`, the constructor throws on an empty store and prints nothing itself (`warnings()`), 5 lookups per normal, `build_fingerprint.json` written only through `writeFingerprint(store_dir)` (the node and `batch_regen` write none), the per-ping order georeference, `applyGainState`, `stampCorrectionIndex` (`import_bag_main.cpp:2829-2837`), `batch_regen_bag` needing the refusals as sketched. Findings are about the new publish/contributor machinery and run mechanics.

### Findings
- [ ] (must-fix) Contributor attribution has no bag identity to use: `BagReaders::Message` carries none (`import_bag_main.cpp:377-392`), `next()` interleaves all bags by `send_timestamp` (`:453-470`), and pings sit in `pending` until the TF frontier passes (`:2870-2890`), so "the bag being read" at `addBatch` time is not known. Tag each pending ping with its bag index at read time and say so; TF/odometry from a bag that supplied no ping also shapes the result (record or require every given bag) — `plan.md:73`
- [ ] (must-fix) `GeoMapSheet::dirtyGrids()` is not usable and the plan leaves the answer to the implementer: `dropTile` erases the index from both dirty sets (`geo_map_sheet.cpp:322`, called by eviction and by the failed-reload drop in `addBatch`), `ImportAccumulator` never clears it, and reload/seed do not mark dirty. Decide now: use `gridIndicesForSoundings` (`geo_map_sheet.h:65`) before `addBatch` (immune to eviction, but it is the bounding box of the whole ping plus influence radius, so it over-attributes; say that over-attribution only causes extra refusals, never missed ones) — `plan.md:73`
- [ ] (must-fix) Inputs are recorded per tile but never compared across tiles: two slope runs with different table, gain declaration, baseline or layers over disjoint tiles leave one store with mixed corrections, the exact hazard #176 refuses in `gainFingerprintProblem` (`batch_regen_main.cpp:754-795`). Add a rule: a run whose `inputs` (minus `depth_tiles`) differ from any recorded tile it does not rebuild is refused, or state that mixing is allowed and why — `plan.md:74`, `plan.md:82`
- [ ] (must-fix) `depth_tiles` scope is undefined when levels differ: BathyDem resolves the finest level with data per position across `processed` then `draft` (`bathy_dem.hpp` `lookup` doc), and the slope store's level (`-r`) need not equal any surface level, so "the tile of the same index and its eight neighbours" has no meaning. Define it geometrically (every surface tile, any listed layer and level, that intersects the 3x3 footprint plus the stencil reach) or require equal levels and refuse otherwise. Also cap `--slope-baseline-deg` so the half-baseline plus the sounding spill cannot reach beyond the hashed ring — `plan.md:82`, `plan.md:56`
- [ ] (must-fix) Publish gating is silent about runs that should not publish: `-l` (`import_bag_main.cpp:2556`, `:2890`) stops early, so recorded contributors are incomplete and the refusal rule is then wrong for the next run; a fatal I/O error (`fatal_error`), a table-unresolved exit 4, or a failed `finalize` should publish nothing. Exit 4 today is defined by the fingerprint the slope run does not write (`:1362-1405`); say what the slope run does instead. Refuse `-l` under the slope run or record it in the manifest — `plan.md:74`, `plan.md:71`
- [ ] (must-fix) The publish step moves only the scratch tiles: (i) a tile the run touched but that produced no finite cells has no scratch tile, so a stale published tile keeps its old contributors and inputs while the run reports success; iterate the touched set, not the scratch directory; (ii) `finalize` writes `registry.json` into the scratch backscatter dir (`store_import.cpp:1196-1202`), which the plan discards, so the published slope store has no StoreMetadata sidecar; publish or reconcile it — `plan.md:74`, `plan.md:72`
- [ ] (must-fix) Scratch mechanics do not match the code: `-o` is required today (`import_bag_main.cpp:2590`), `--scratch-dir` is refused without `--depth-adaptive` (`:1524-1531`) and feeds only the recon spill (`:1730`), and the accumulator's own intensity spill goes to `temp_directory_path()` (`store_import.cpp:451-453`), not to any scratch option. State the changes (relax the required `-o`, new scratch-dir validation for the slope run, spill location) and where the fresh depth and backscatter scratch stores live; a crashed run's leftover scratch blocks the next one, so the refusal needs a message saying how to clear it, and the scratch stores must be removed on every exit path — `plan.md:72`, `plan.md:62-67`
- [ ] (must-fix) Publish crash recovery is under-specified: the `derived_tiles.publishing` marker must hold the tile list and the bag set (the plan only says "requires the same bag set"), say what a run with a different bag set does (stuck with no escape), say that the copy fallback copies to a temp name in the destination directory and then renames (a copy straight to the final name is not atomic), fsync before the manifest rename, and take a lock on the store (`flock`) so two slope runs cannot interleave between the check and the moves — `plan.md:74`
- [ ] (must-fix) Contributor identity by bag basename is unsafe: two bags in different directories with the same name collide silently (the refusal then passes wrongly), and a trailing slash gives an empty `filename()`. Refuse duplicate names within a run and normalise the path; consider adding the bag's `metadata.yaml` start time and message count to the identity — `plan.md:73`
- [ ] (must-fix) `georeferencePing` is in the anonymous namespace of `import_bag_main.cpp` (`:884`), so the A-group sign test (earth-frame cross-track sign needs it) and the D-group tests cannot reach it, and `main` is already at the cpplint size limit (`:1905` comment) so the slope-run extraction is not conditional. Add the library move to Files to Change and to group A — `plan.md:160`, `plan.md:128`
- [ ] (must-fix) Commit groups E and F are not green-and-meaningful alone: E has no publish, so its only output is a scratch store that is deleted, and the §9 end-to-end tests need F. Order the manifest/publish library with its unit tests (F) before the slope run (E), or merge them — `plan.md:128`
- [ ] (must-fix) PR 2's per-beam index has no source: neither `Sounding`/`GeoSounding` nor `ProjectionResult` carries one and the projector drops beams by range (`detections_projector.cpp:200-206`). Add the field and the projector change to the Files table and size it — `plan.md:104`, `plan.md:159-168`
- [ ] (must-fix) The test list would not catch a no-op ordering fix or the contributor machinery: add tests for (a) a beam whose `|rx_angle|` is in range but whose incidence is beyond 90 deg or beyond the outermost bin being counted on the incidence (`beams_correctable`, `table-unresolved`), (b) contributors with a tiny `--max-resident-tiles` (eviction) and a sounding spilling across a tile edge, (c) mixed-inputs refusal, (d) a crash between moves and manifest (marker), (e) `-l` — `plan.md:117`
- [ ] (suggestion) Run the contributor refusal at first touch (the recorded set and the bag set are known before any ping is read), not after `finalize` of a multi-hour run — `plan.md:74`
- [ ] (suggestion) Version skew: the C++ curve loader and the table loader silently ignore an unknown `# angle_basis:` key (table header map keeps the first of any key and only errors on repeated gain keys, `backscatter_table.cpp:678-681`), so an older binary on a boat applies an incidence table on beam angle. For tables, give incidence a new format line (the loader refuses unknown ones, `:641-650`); say plainly that the CSV curve cannot be protected. State that a repeated `angle_basis`/`slope_*` key is an error, which needs new code — `plan.md:95`
- [ ] (suggestion) Measure the sampler cost in group D (a micro-benchmark over a store) rather than at Appledore acceptance; `resolveSource` probes layers x levels per lookup — `plan.md:120`
- [ ] (suggestion) Say that the beam vector is the sensor-ENU chord (not the touchdown-ENU frame; difference about range/R, 1e-5 rad at 100 m, negligible) and that the projector traces no refraction, so the chord is the arrival direction — `plan.md:54`
- [ ] (suggestion) A default scratch path "beside --bs-store" should be unique per run (mkdtemp-style), or two runs and a leftover collide — `plan.md:72`
- [ ] (suggestion) In "How the plan review is resolved", the ledger row reads "Gone" but is only half so (the beam index, see above), and the "SpilledSounding" and "depth equality" rows are honest only because the slope run stays fixed-level; say the latter in the table. The warm-start and `-o` rows are truly resolved — `plan.md:134-153`

### Operator decisions needed (not mechanical)
- Decision 8 originally split PR 1/PR 2 after group D; the plan now puts A to G in PR 1 and only the ledger/derive in PR 2. Confirm.
- Mixed corrections in one slope store: refuse (recommended, mirrors #176) or allow with the per-tile record only.
- Whether a `--backscatter-correction none` ledger run may publish into a store at all (its tiles are uncorrected), or writes only the ledger.
- The "Question for the operator" on decision 4 (live node) is still open.

## Implementation
**Status**: complete (groups A0 to G; review and ci-local follow)
**When**: 2026-11-12 10:05 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Plan**: `.agent/work-plans/issue-173/plan.md` revision 3, with its "Implementation record (PR 1)" section
**Branch**: feature/issue-173 (local only, not pushed, no PR)

Commit groups, each built and run against the whole package suite before it was committed:

- A0 georeferencePing moved into the library, verbatim.
- A `Sounding::beam_dir_enu` and `incidence_angle`; `test_beam_geometry` pins the sign of `rx_angle` through rotated transforms at four headings in both hemispheres.
- B `Parameters::backscatter_angle_basis` and its selection in `Node::insert`.
- C the v2 incidence table and the basis refusals in `import_bag`, `batch_regen_bag` and the node.
- D `slope_incidence` (closed-form planes, fallbacks counted, the baseline against the probes the sampler saw, the aspect-independence with its negative control) and the sampler benchmark: about 32 us per beam over a 2 x 2 tile processed store, under load.
- E `derived_tiles`: the strict manifest, bag identity, depth-tile hashing, the lock, publish and recovery, with a crash injected at every point.
- F the slope run in `import_bag` (`slope_run` library, scratch, bag index on messages, contributors, early refusals, publish), `test_slope_run` and the end-to-end `test_import_bag_slope.py`.
- G README and ADR text.

The beam basis is byte-identical to a build of the plan commit (depth store, backscatter store and `build_fingerprint.json`) for `none`, a beam table and `--max-resident-tiles 1`.

Whole package suite after F: 52 of 52 ctest entries pass (the baseline was 1694 tests in 46 entries, none failing).

### Deviations from the plan
Listed in the plan's "Implementation record (PR 1)": the slope-run library split, the table-accounting test as an end-to-end one, batch_regen's table refusal moved before the bag opens, the warning on a table's recorded baseline and layers, the `last_publish` key, the stencil margin definition, and the eviction tolerance.

### Open
- [ ] Action needed: `/review-code` (pre-push, `--no-local`) and a full-scope `ci_local.sh` attestation on the final head.
- [ ] Operator: the live-node question (decision 4) is unchanged by this work and stays open.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-12 10:49 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: approved

**Branch**: feature/issue-173
**Mode**: pre-push
**Depth**: Deep (reason: large cross-cutting change)
**Must-fix**: 0 open | **Suggestions**: 9 open
**Round**: 1 | **Ship**: recommended — all must-fix found were fixed in place

Sources: two Claude adversarial passes (lens A and B), Gemini and Codex (local review not run, --no-local).

### Findings
- [x] (must-fix) store inputs omitted fallback, ping floor, IHO order, capture spacing — fixed
- [x] (must-fix) trailing slash on --bs-store put the scratch inside the store — fixed
- [x] (must-fix) exception escaping main leaked the scratch directory — fixed
- [x] (must-fix) damaged detections message not fatal for a slope run — fixed
- [x] (suggestion) registry.json overwritten by every publish; unrecorded tile deleted; put/delete overlap; counters not adding up; NaN guards; baseline text precision; "never committed" text for an empty incoming dir — fixed
- [ ] (suggestion) accumulator reload/restore failures do not block a slope publish (Codex) — `store_import.cpp`
- [ ] (suggestion) depth tiles are hashed at publish time, not at first read (both lenses) — `slope_run.cpp`
- [x] (suggestion) a run with no finite backscatter, or with dropped georeferencing, exits 0 with a warning — `slope_run.cpp` (fixed in a06fed1, merged with PR 179: no cells exits 1 and publishes nothing; dropped georeferencing keeps the ordinary run's tolerance and is counted in the report and recorded per tile as `dropped_georef_pings`, as instructed then)
- [x] (suggestion) a table's recorded baseline/layers only warn on mismatch — `import_bag_main.cpp` (fixed in a06fed1, merged with PR 179: refused, naming both values)
- [ ] (suggestion) --reference-store is not recorded in the store inputs — `import_bag_main.cpp`
- [ ] (suggestion) longitude intersection in SurfaceTileIndex does not wrap at the antimeridian — `derived_tiles.cpp`
- [ ] (suggestion) beam importers do not take the store lock while checking for derived_tiles.json — `import_bag_main.cpp`
- [ ] (suggestion) layers decided by directory existence, not tile presence — `slope_run.cpp`
- [ ] (suggestion) over-attribution makes a refusal conservative; message could say so — `slope_run.cpp`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-12 11:56 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: approved

**Branch**: feature/issue-173
**Mode**: pre-push (delta: the follow-up commit after the jazzy merge)
**Depth**: Light (reason: review of a delta)
**Must-fix**: 0 open | **Suggestions**: 5 open
**Round**: 2 | **Ship**: recommended — the two must-fix found were fixed

### Findings
- [x] (must-fix) batch_regen_bag dry run took the store lock and could fail or create a directory — fixed
- [x] (must-fix) import_bag took the lock before argument validation — now only for a run with -o
- [x] (suggestion) referenceStoreDigest ignored a directory read error — now throws
- [x] (suggestion) a spill file that went missing is not counted by integrityProblem — `store_import.cpp` (fixed: tiles with a written spill are tracked, a missing file counts)
- [ ] (suggestion) the reference-store digest is not re-verified at the end of the run — `import_bag_main.cpp`
- [ ] (suggestion) eager hashing covers every surface tile, so an unrelated tile rewritten mid-run fails the publish (stricter than needed) — `slope_run.cpp`
- [ ] (suggestion) two beam runs into one store are now serialized (documented in the README)
- [ ] (suggestion) manifests written by earlier builds of this branch no longer load (new required keys)
