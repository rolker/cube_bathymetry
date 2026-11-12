# Plan: Slope-aware incidence correction for M3 backscatter via the shared seabed-geometry module (marine_backscatter)

## Issue

https://github.com/rolker/cube_bathymetry/issues/173

This work is "Part of rolker/unh_marine_autonomy#304". No PR for it may close #304. The PR(s) for this plan close #173 only when the last one lands (earlier ones say "Part of #173").

**Implementation (2026-11-12).** PR 1 (groups A0 to G) is implemented on `feature/issue-173`; "Implementation record (PR 1)" at the end lists what differs from the text below and why. The sections below are edited inline only where the code contradicted them.

**Revision 3 (2026-11-12).** Revision 2 (commit `12682f7`) rewrote the Approach around the operator's two-layer design (Decisions 12 to 14) and folded in the plan review of 2026-11-10. Revision 3 folds in the review of revision 2 (`progress.md`, "Plan Review" at `bce012f`): bag identity and contributor tracking, one set of inputs per slope store, the publish protocol, the scratch mechanics, the commit order. "How the plan reviews are resolved" maps the findings.

## Context

Today every M3 beam is corrected by an angle read straight off the ping: the sonar-frame `rx_angle` (`Sounding::beam_angle`), used as `|beam_angle|` by the per-pulse-mode table (cube#172) and the single curve, after the gain-state shift (cube#176). On a sloping seabed the angle the seabed sees (incidence against the local normal) differs from it by up to the slope, so upslope and downslope passes over the same ground come out at different levels. **This plan does not claim slope explains the nadir line on the Appledore map**; it makes the slope term measurable.

### The geometry module (uma#304)

Merged: `rolker/unh_marine_autonomy` PR #413 (jazzy `dd737cd7`), package `marine_backscatter`, namespace `marine_backscatter::seabed`. What the design uses (read from the merged headers):

- `SeabedHeightSampler::heightAt(lat, lon)` returns a `HeightSample` (status, height, per-axis native cell spans `span_north_m`/`span_east_m`, `SourceId`). Heights are WGS84 ellipsoidal, up-positive. `BathyDem` reads band 0 of a bathymetry store's tiles as that; the CUBE `processed` layer stores an ellipsoidal up-positive height, so no negation is needed. A test pins the sign with a tilted store.
- `seabedNormal(sampler, lat, lon, NormalOptions)`: central differences at plus and minus `step_m` (a half-width); `step_m = 0` means `auto_step_cells` native cells; the step is floored at one native cell (`Normal::step_clamped`, `Normal::step_used_m`). A flat fallback returns `{0,0,1}` with `from_surface = false` and a `FallbackReason`. The caller counts. Sampler faults (a corrupt tile) throw.
- `incidenceAngle(beam_dir, normal)`: `beam_dir` is a unit ENU vector from the sensor toward the seabed; radians; raw above pi/2 means back-facing; a non-unit or non-finite vector gives `valid = false`.
- `BathyDem` source identity is (layer, level); a stencil across a tile boundary inside one layer and level gives a real normal; a different layer or level inside the stencil is `layer_seam`. Its constructor throws on a store with no tiles.
- A sampler is not thread-safe. One normal costs 5 height lookups; `cache_tiles` is sized above its default of 8.

`marine_backscatter` becomes a direct dependency (`package.xml`, `CMakeLists.txt`).

## Decisions

Decided by the operator on 2026-11-10 (1 to 11) and 2026-11-12 (12 to 14). Where a later decision changes an earlier one, the earlier one says so.

1. The uma#304 geometry module comes first: done.
2. The derive tool gets its angle from the import's per-beam output (the beam ledger), not from a Python re-implementation.
3. The fallback angle, where there is no surface, is the earth-frame angle from vertical (it includes vessel attitude), not the sonar-frame `|rx_angle|`.
4. Live node, first version: earth-frame angle with a flat normal. **Open again under decision 12**, see "Question for the operator" below; not built in PR 1.
5. `batch_regen_bag` refuses incidence-angle work.
6. `--slope-surface <store>`: under decision 12 this is no longer an option that skips a pass, it is the required input of the slope run.
7. Slope stencil of 3 cells with a floor of 4 m: **replaced by decision 14**.
8. Two PRs.
9. Existing beam-angle tables stay valid for beam-angle runs and are refused for incidence runs, and the reverse.
10. The default angle basis stays `beam` until an Appledore upslope/downslope comparison shows the correction helps.
11. The slope scale default is provisional until measured on real data (now applies to the angle of decision 14).
12. **Two backscatter layers.** Pass one is today's import, unchanged: it writes the depths and the beam-angle backscatter (corrected for everything except slope). Pass two is a separate run that re-reads the bags, takes the slope from the finished depth layer, and writes a separate slope-corrected backscatter layer. Pass two writes no depths. The two never mix, the status-quo layer always exists, and pass two can be re-run by itself.
13. **The slope-corrected layer is a derived product, regenerated per tile.** The beam-angle layer keeps accumulating as today. Each slope-corrected tile records its contributors (which bags) and an input fingerprint (the depth tiles it read including stencil neighbours, the table, the gain declaration, the angle basis and slope settings). In the first PR a slope run always rebuilds every tile it touches, records both from day one, and refuses to rebuild a tile whose recorded contributors are not all among the bags it was given. Automatic stale-tile detection and bag lookup come later.
14. **The slope baseline is an angle.** The differencing baseline subtends a fixed angle at the sonar, so it scales with range: full baseline = slant range x angle, with a floor of one native cell either side. The parameter is in degrees, the run report gives the distribution of the baseline actually used and how often the floor applied, and the default angle is provisional until measured on Appledore.

## Approach

### Design in one paragraph

One **angle definition**: `incidence = angle between the beam's earth-frame arrival direction (unit ENU vector, sensor to touchdown) and the seabed normal`, the normal being the surface normal where a surface exists and the vertical where not. A new run mode of `import_bag`, the **slope run**, reads the bags exactly as an ordinary import does, stamps each sounding's incidence from a finished depth store given with `--slope-surface`, runs the usual estimator in memory with the table applied on incidence, and publishes only backscatter tiles into its own backscatter store, with a per-tile record of what went into each. Ordinary imports are untouched. Tables carry an explicit `angle_basis`; the derive tool reads a per-beam ledger the slope run writes (PR 2).

### 1. The angle, and where slope comes from

- **Beam vector.** `georeferencePing` already computes, per sounding, the earth-frame ENU offset of the touchdown from the sensor origin. Normalised, that is the beam direction (straight ray; the SSP tracer is unchanged). Carried on `Sounding` as `beam_dir_enu` (three doubles, NaN = absent; double because near nadir a float vector is good to only about 0.03 deg and the module's unit tolerance is 1e-6). It is always computed; it costs nothing to ordinary runs beyond the field.
- **Which surface.** The `processed` layer of the store named by `--slope-surface` (and `draft` if present, the own-survey rule of uma#304), read-only through `marine_backscatter::BathyDem`. Never `reference`, `chart` or `overviews/`. Where there is no survey cell the normal falls back to the vertical and the beam is **counted per reason, never slope-corrected**. A store with no tiles, or a mistyped path, is a named error before any bag is read; `BathyDem::warnings()` are printed.
- **Baseline (decision 14).** `--slope-baseline-deg <deg>` (default 5.0, **provisional**). Per beam: `step_m = 0.5 x slant_range x radians(angle)`, passed as `NormalOptions::step_m`; the module floors it at one native cell and reports `step_clamped` and `step_used_m`. No centre lookup is needed for the floor, so the cost stays at 5 lookups per beam. The pass report prints the percentiles of the full baseline used (2 x `step_used_m`), the fraction of beams at the floor, a histogram of slope in degrees, and the fallback counts per reason. Note for the README: on a 0.9 m grid the floor (1.8 m across) is about 10 deg at 10 m range, so in shallow water the floor decides and a finer depth level is what improves the slope, not the angle.
- **Back-facing beams** (incidence at or beyond 90 deg) go to the existing "beam at or beyond 90 deg" fallback (`identity` or `exclude`) and are counted under their own reason.
- **Datum.** Only height differences enter the normal.

### 2. The slope run in `import_bag`

Selected by `--backscatter-angle-basis incidence` (default `beam`, decision 10). Requirements, each a usage error naming the flag when not met:

- `--slope-surface <bathy store>` and `--bs-store <dir>` are required; `-o` is **refused** (a slope run writes no depths). Today `-o` is mandatory (`import_bag_main.cpp` argument check); the check becomes "`-o`, or a slope run".
- `--depth-adaptive`, the mixed-level flags and `-l` (a ping limit would publish tiles with incomplete contributors) are refused.
- `--slope-baseline-deg` must be in (0, 30].
- The correction is either `none` or a **table** whose format line is the incidence one (section 5). The legacy single curve, and a SonarInfo-delivered curve, are refused on incidence, including under the default `--backscatter-correction auto`.
- `--bs-store` must be empty or already a slope-run store (it holds `derived_tiles.json`). A beam-angle backscatter store is refused. The reverse also: an ordinary run, `batch_regen_bag` and the live node refuse a backscatter store that holds `derived_tiles.json`.
- Two given bags with the same name are refused (contributor identity, below).

Mechanics:

1. **Order per ping.** Georeference, gain shift, then `stampIncidence(soundings, sampler, options, counters)` (new `slope_incidence.h/.cpp`, the only code that touches the module), **then** the per-beam table accounting (`stampCorrectionIndex`, `BackscatterModeResolver::countBeam`) on the stamped incidence, so "at or beyond 90 deg", "beyond the outermost bin" and `beams_correctable` are computed on the angle applied. The "table given, nothing resolved" outcome (exit 4) is taken from the resolver report directly, since the slope run writes no fingerprint.
2. **Scratch.** The `ImportAccumulator` is pointed at a fresh depth store and a fresh backscatter store inside one scratch directory unique to the run (`<scratch root>/slope-run-<pid>-<time>`; the root is `--scratch-dir`, which becomes legal for a slope run, default the parent directory of `--bs-store`). `ImportAccumulatorConfig` gains a spill root so the per-tile intensity spill, which today goes to `temp_directory_path()`, lands in the same scratch directory. Fresh stores mean no warm start from any earlier tile by construction; eviction and reload work as today. The scratch directory is removed on every exit path (a guard object; also on a thrown error). A leftover from a killed process cannot block a later run, because names are unique; a slope run that sees leftovers beside its own prints their paths and that they can be deleted.
3. **Bag identity and contributors.** `BagReaders::Message` gains the index of the bag it came from, and a ping keeps the bag index of its detections message while it waits in `pending` for the TF frontier, so the bag is known when the ping is finally added. Immediately before each `addBatch`, `gridIndicesForSoundings` (`geo_map_sheet.h`) gives the tiles the batch can reach, and the ping's bag is added to each tile's contributor set. This over-attributes at edges, which costs at most an extra refusal, never a missed contributor. (`GeoMapSheet::dirtyGrids()` is not used: eviction erases from it.) A bag's identity is its normalised directory name (trailing slash stripped) together with the SHA-256 of its `metadata.yaml`; a copy of a bag on another disk is the same bag, two different bags that share a name are told apart. A recorded name that matches with a different hash is refused by name.
4. **Early refusal.** The destination's `derived_tiles.json` is loaded before any bag is read. Its store-level inputs (section 3) must equal this run's, or the run is refused at once. The first time a batch reaches a tile that the manifest records with a contributor not among this run's bags, the run fails there, naming the tile and the missing bags. Nothing has been published at that point.
5. **Publish only a complete run.** Nothing is published after a fatal read or I/O error, a failed `finalize`, an early refusal, or exit 4. The scratch directory is removed and the destination is as it was.
6. **Publish protocol** (new `derived_tiles` library), under an exclusive `flock` on `<bs-store>/.slope-run.lock` held from start to end of the slope run (a second run on the same store is refused at once, not queued):
   1. Copy each scratch backscatter tile to `<bs-store>/survey/.incoming-<run>/` (same filesystem as its final place), fsync each file and the directory.
   2. Write the complete new manifest as `derived_tiles.json.pending` (temp name, fsync, rename). It is the complete new manifest and carries a `last_publish` record (run id, tiles written, tiles removed, whether `registry.json` is installed), which is all the roll-forward needs. **Its existence is the commit point.**
   3. Rename each incoming tile over its final name; delete each tile that this run reached but produced no cells for (its contributors were all given, so its old content is stale) and drop its entry; write `registry.json` as an ordinary run does.
   4. Rename `derived_tiles.json.pending` to `derived_tiles.json`; remove the incoming directory.
   Recovery, on the next slope run and before anything else: a `.pending` manifest means the run committed, so the remaining steps are **rolled forward** from it (no bags needed, any bag set); an incoming directory with no `.pending` manifest means the run did not commit, so it is deleted and the store is as it was. Both are reported.
7. Depth is not compared between the passes. The depths are pass one's; the slope run's in-memory estimate exists only to attribute beams to hypotheses. It stays fixed-level, like pass one. It can differ slightly from pass one where pass one warm-started from earlier tiles; the README says so.

### 3. `derived_tiles.json` (decision 13)

At the root of a slope-run backscatter store. A `format` line, `schema_version: 1`, then:

- **Store-level `inputs`**, the same for every tile in the store: `angle_basis`, `slope_baseline_deg`, `slope_layers`, the correction (`none`, or the table's SHA-256), the gain-declaration SHA-256 and reference state (or absent), the store level. A slope run whose inputs differ is refused (this mirrors the gain-state refusal of #176): one store holds one correction. A new table means a new store directory, or deleting the old one. A `none` run (the ledger-producing run of PR 2) may publish; its store says `none` and can never receive corrected tiles.
- **Per tile** (keyed by tile file name): `contributors` (sorted list of `{name, metadata_sha256}`); `depth_tiles`: path relative to the surface store and SHA-256 of the **content** of every tile file, at any level, under the `--slope-surface` layers used, whose bounds intersect this tile's bounds grown by `stencil_margin_m`; `stencil_margin_m`: one number for the run: the largest half-baseline it used, plus the largest sounding influence radius it saw, plus twice the coarsest cell of the surface (a stencil's bilinear reach), recorded identically on every tile it writes; `written` (from the clock) and the code version. Each surface tile is hashed once per run (streamed).

A strict loader (unknown version, malformed entry, duplicate key = error) and an atomic writer. This file, not `build_fingerprint.json`, is the staleness record of a slope-run store; a later tool can compare it with the present depth store to list stale tiles. That tool is not in this plan.

`build_fingerprint.json` is written only through `writeFingerprint(store_dir)` by runs with `-o`, so the slope run writes none, ordinary runs' fingerprints are unchanged byte for byte, and schema 3 stays as it is.

### 4. The estimator side

`Sounding::incidence_angle` (float, radians, NaN = none). `Parameters::backscatter_angle_basis` selects which angle `Node::insert(double, const Sounding &, const Parameters &)` hands on: `beam` (today's `beam_angle`, byte-identical output, the default) or `incidence` (a NaN one sends the beam to the existing fallback policy). The median queue, `Hypothesis::recordBeam`, `correctBeamIntensity`'s signature and persistence are unchanged. The recon spill record (`SpilledSounding`) is **not** changed.

The beam vector is the chord from the sensor to the touchdown in the sensor's ENU frame; against the touchdown's own ENU frame it is off by about 1e-5 rad at 100 m, and the projector traces no refraction. Both are stated in the header comment.

### 5. Angle basis in tables and loaders

- **Marker.** An incidence table has its **own format line** (`# format: angular-response-table v2`; v1 stays the beam table, and the existing near-miss-format test now uses v3 as its unsupported example), plus the records `# angle_basis: incidence`, `# slope_baseline_deg:` and `# slope_layers:`, each required exactly once. The loader already refuses a format line it does not know, so a binary built before this change refuses an incidence table instead of applying it on beam angle (an unknown `#` key under the old format line would have been skipped silently). Existing tables are untouched and mean `beam`. The legacy single curve (CSV) has no incidence form.
- **Check.** `import_bag` refuses a table whose basis differs from the run's, in both directions (decision 9). `batch_regen_bag` and the live node refuse an incidence table (decision 5); the message points at the slope run.

### 6. The live node

Not changed beyond the refusal in section 5. See "Question for the operator".

### 7. The derive tool and the beam ledger (PR 2)

- **Beam index.** Nothing carries a beam's index in the ping's detection arrays today, and the projector drops beams by range. PR 2 adds a `uint16 beam_index` to the projector's per-beam output and to `Sounding`.
- `import_bag --beam-ledger-out <file>` (slope runs only): a JSON header (the store-level inputs of section 3, bag identities, code version) then per ping `stamp_ns` (int64), beam count (uint16) and per beam the beam index, an int16 incidence in 0.01 deg (`-32768` = none) and a uint8 fallback reason. Written atomically, and only by a run that completes.
- `derive_angular_response.py --angle-basis incidence --beam-ledger <file>[,...]`: bins by the ledger's incidence, keeping the sign of `rx_angle` as the port/starboard side. Beams absent from the ledger are dropped and counted. It writes the incidence format line and records and refuses a ledger whose header disagrees with the supplied gain declaration or bags. Without `--angle-basis` it behaves exactly as today.
- Workflow (README): (a) ordinary import, giving the depths and the beam-angle layer; (b) slope run with `--backscatter-correction none --beam-ledger-out L` into its own store; (c) derive on `L`; (d) slope run with the table into the slope-corrected store.
- The report also prints per-bin mean incidence minus `|rx_angle|` and the fraction of beams with a surface.

### 8. Sign check (`sounding.h`)

A test with a synthetic ping: beams with `rx_angle > 0` have `sonar_relative_position.y > 0` and an earth-frame cross-track component of the matching sign relative to heading. The `sounding.h` comment changes from "REQUIRED" to the verified statement (already drafted).

### 9. Tests

- **Known-answer sloped seabed** (gtest, no store): a `SeabedHeightSampler` over a plane at slopes 0, 5, 15 deg and four aspects; `stampIncidence` reproduces the closed form to 1e-6 rad; zero slope equals the earth-frame angle from vertical; fallback counts and reasons pinned for a missing cell, a layer seam and a prior-only region; `step_m` follows range x angle and the floor count is pinned.
- **Level independent of aspect** (gtest through `Node`/`Hypothesis`): synthetic response `f(incidence)`, beams over four aspects of a tilted plane; with `incidence` the corrected mean is the same for every aspect; with `beam` the aspects differ by the known amount (negative control).
- **End to end** (pytest, extending the synthetic bags of `test_import_bag_mode_table`, with a hand-written incidence table): ordinary import of a tilted-plane bag, then a slope run: per-cell backscatter independent of aspect; no depth tile anywhere after the run and the scratch directory gone, also after a refused and a failed run; manifest contents (store inputs, contributors, depth tile hashes with the margin); a second run with the same bags gives identical tiles; a run with a subset of the bags is refused at first touch and leaves the store byte-identical; a run adding a bag succeeds when all recorded contributors are given; two bags over one tile edge both appear as contributors of both tiles; contributors correct under forced eviction (`max_resident_tiles` 1); a run with a different table, declaration or baseline is refused before reading bags; same-named bags refused; `-l` refused; a tile that loses all its cells is deleted and its entry dropped; changing one depth tile changes the recorded hash in the entries whose margin reaches it.
- **Table accounting on incidence**: a ping whose beam angle is inside the table and whose incidence is beyond the outermost bin (steep slope) is counted as beyond the bin, and the reverse; this fails if the stamping runs after the accounting.
- **Publish and recovery** (gtest on the `derived_tiles` library, with an injected failure point): a failure before the commit point leaves the store byte-identical and the next run cleans the incoming directory; a failure after it is rolled forward by the next run to the same bytes a clean publish gives; a held lock refuses a second run.
- **Sampler cost**: a micro-benchmark of `stampIncidence` against a real-sized `BathyDem` store, in the group that adds it, so the cost is known before the Appledore run.
- **Unchanged behaviour**: `beam` basis output byte-identical on the existing end-to-end fixtures, `build_fingerprint.json` included; `test_recon`, `test_mixed_level_import`, `test_import_eviction`, `test_build_fingerprint`, `test_batch_regen` unchanged and green.
- **PR 2**: ledger round trip (C++ writer, Python reader, golden bytes), derive on incidence recovers `f`, header cases like #176.
- **Appledore acceptance** (numbers in the PR description, not the repo): slope run over the August 2026 bags; upslope versus downslope level difference on overlapping cells by slope class, before and after; the baseline distribution and floor fraction; time and sampler cost as measured.

### 10. Docs

A section "Incidence-angle addendum (cube#173)" in the ADR-0007 addendum file (not a new ADR; the design is still moving), with the two-layer rule, the derived-product rule and the angle baseline, and who decided. README: the slope run, its flags (`--backscatter-angle-basis`, `--slope-surface`, `--slope-baseline-deg`, `--scratch-dir`), `derived_tiles.json`, the refusals, the provisional default, the shallow-water note, and that incidence tables cannot be derived until PR 2. ADR-0003: one sentence that slope-run stores carry `derived_tiles.json` instead of a build fingerprint.

### Commit groups

**PR 1** (Part of #173), each group buildable and green by itself:

- **A0** move `georeferencePing` and its helpers out of the anonymous namespace of `import_bag_main.cpp` into a library unit (`ping_georeference.h/.cpp`), no behaviour change; `main` is at cpplint's length limit and the tests of A and D need to reach it.
- **A** `Sounding::beam_dir_enu` and `incidence_angle`, the beam vector, sign-check test and comment. (The uncommitted draft in the worktree covers part of A and B; it fails uncrustify and is to be reworked, not committed as is.)
- **B** `Parameters::backscatter_angle_basis` and the selection in `Node::insert`.
- **C** the incidence table format and the refusals in `import_bag`, `batch_regen_bag` and the node. (The refusal of a store that holds `derived_tiles.json` by an ordinary run and by `batch_regen_bag` needs the manifest and lives in F; the live node writes no backscatter store, so it has nothing to refuse there.)
- **D** `slope_incidence` with the `marine_backscatter` dependency, known-answer tests, the micro-benchmark.
- **E** the `derived_tiles` library: manifest, strict loader, bag identity, depth-tile hashing, lock, publish and recovery, with its tests.
- **F** the slope run in `import_bag`: flags and refusals, scratch, bag index on messages, contributors, early refusal, report, publish through E; the end-to-end tests.
- **G** README, ADR text.

**PR 2** (closes #173): beam index, ledger writer, derive tool changes, workflow docs.

The operator's decision 8 was "two PRs, split after group D" of revision 1's grouping. That grouping no longer exists; the split here (everything needed to run a slope correction, then the tooling to fit a table on incidence) is this plan's reading of it and is flagged for confirmation.

## How the plan reviews are resolved

**Review of revision 1 (2026-11-10)**

| Finding | Resolution |
|---|---|
| Warm start differs between the passes; populated `-o` | Resolved by design: no `-o`, fresh scratch stores, publish afterwards |
| Depth equality not established under eviction | No compare; pass one's depths are the only depths. Holds because the slope run stays fixed-level |
| `batch_regen_bag` section on a wrong premise | Rewritten (sections 2, 5) |
| `SpilledSounding` growth breaks `test_recon` | The spill is not used or changed |
| Ledger cannot be written from the spill | Written from the bag loop; the beam index is new work in PR 2 (section 7) |
| SonarInfo curve on incidence only warned | Refused |
| Table accounting on the wrong angle | Incidence stamped first, with a test that fails otherwise |
| `--slope-surface` identity by sizes is empty | Content hashes of the surface tiles within the stencil margin |
| Derive writes the marker everywhere; fingerprint byte-identity | Marker only in the incidence format; no fingerprint change |
| `Node::update`; half-width; empty store; float vector; stale text | Corrected |

**Review of revision 2 (2026-11-12)**

| Finding | Resolution |
|---|---|
| 1. No bag identity when `addBatch` runs | Bag index on `BagReaders::Message`, kept on the pending ping (2.3) |
| 2. `dirtyGrids()` not usable | `gridIndicesForSoundings` before `addBatch` (2.3) |
| 3. Inputs never compared across tiles | Store-level inputs, a differing run refused (3, 2.4) |
| 4. `depth_tiles` scope undefined | Geometric: bounds grown by the recorded margin, any level; baseline angle capped at 30 deg (3, 2) |
| 5. Publish gating | Only a complete run publishes; `-l` refused; exit 4 from the resolver report (2.5) |
| 6. Stale tile when a touched tile yields no cells; `registry.json` | Deleted and dropped in publish; registry written (2.6) |
| 7. Scratch mechanics do not match the code | `-o` check, `--scratch-dir`, spill root, unique directory, removal on every path (2.2) |
| 8. Crash recovery | Incoming directory in the destination, pending manifest as commit point, roll forward, fsync, flock (2.6) |
| 9. Basename identity | Name plus `metadata.yaml` hash; same-named bags refused (2.3) |
| 10. `georeferencePing` unreachable | Group A0 |
| 11. E and F not meaningful alone | Library first (E), then the run (F) |
| 12. Beam index has no source | Added in PR 2, listed in Files |
| 13. Tests would not catch a no-op | Tests added (section 9) |
| Suggestions | First-touch refusal, new format line, micro-benchmark in D, chord precision note, unique scratch: all taken |

Operator-level points the review raised, and what this plan does pending a word from the operator: mixed corrections in one slope store are **refused**; a `none` run **may** publish (into a store that says `none`); the PR split is as in "Commit groups"; the live node question stands.

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/sounding.h` | `beam_dir_enu`, `incidence_angle`; sign-check comment |
| `src/import_bag_main.cpp`, new `ping_georeference.h/.cpp`, new `slope_run.h/.cpp` | extraction (A0); beam vector; bag index on `BagReaders::Message` and pending pings; slope-run flags, refusals, scratch, ordering, contributors, report; PR 2: `--beam-ledger-out` |
| `include/cube_bathymetry/store_import.h`, `src/store_import.cpp` | spill root in `ImportAccumulatorConfig` |
| `src/detections_projector.cpp` and header | PR 2: `beam_index` |
| `include/cube_bathymetry/slope_incidence.h`, `src/slope_incidence.cpp` | new: `stampIncidence`, options, counters |
| `include/cube_bathymetry/derived_tiles.h`, `src/derived_tiles.cpp` | new: manifest model, strict loader, bag identity, contributor check, depth-tile hashing, lock, publish and recovery |
| `include/cube_bathymetry/parameters.h`, `src/parameters.cpp`, `src/node.cpp` | angle basis and its selection |
| `src/backscatter_table.cpp` and header | incidence format line and records |
| `src/batch_regen_main.cpp`, `src/cube_bathymetry_node.hpp` | refusals |
| `scripts/derive_angular_response.py` | PR 2 |
| `CMakeLists.txt`, `package.xml` | `marine_backscatter` dependency, new sources and tests |
| `test/test_slope_incidence.cpp`, `test_incidence_basis.cpp`, `test_derived_tiles.cpp`, `test_beam_geometry.cpp`, `test_import_bag_slope.py`, extensions to `test_backscatter_table`, `test_batch_regen` | section 9 |
| `docs/decisions/0007-...addendum...md`, `0003-staleness-fingerprint.md`, `README.md` | section 10 |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Opt-in run mode; every fallback and refusal counted and printed; the report shows the baseline actually used |
| Capture decisions, not just implementations | ADR-0007 addendum records decisions 12 to 14 and who made them |
| A change includes its consequences | Tables, loaders, `batch_regen`, node, README and ADR text are in the plan; a wrong-basis table is a refusal, not a doc note |
| Only what's needed | No spill refactor, no depth compare, no fingerprint change, no live-node mode, no stale-tile tool, no new store layer in the shared backscatter store (a second store directory carries the second layer) |
| Improve incrementally | Ordinary runs are byte-identical; the slope run is additive |
| Test what breaks | Known-answer plane, negative control, contributor refusal leaving the store untouched, interrupted publish |
| Workspace vs project separation | Nothing sonar- or survey-specific in code; Appledore appears only in PR numbers |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| cube ADR-0007 addendum set | Yes | New incidence addendum |
| cube ADR-0003 (fingerprint) | Touches | No key change; a sentence on `derived_tiles.json` |
| cube ADR-0008 (predicted surface) | Touches | The predicted surface is not used for slope; only finished `processed`/`draft` cells are |
| uma ADR-0007 (#248 amendment, single `survey` layer in the backscatter store) | Touches | Respected: the second layer is a second store directory, each with its `survey` layer; no change to the shared store |
| workspace ADR-0008 (ROS conventions) | Yes | License headers, cpplint, parameter docs |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| A second backscatter product exists | README; whoever exports a GeoTIFF picks the store directory | Yes (README); export tool needs no change |
| Angle basis of fitted tables | derive tool, loaders, header | Yes (section 5; derive in PR 2) |
| CLI flags | README tables | Yes |
| `marine_backscatter` dependency | `package.xml`, CMake, layer build order (core below sensors) | Yes |
| Depth store re-imported after a slope run | Slope tiles become stale; `derived_tiles.json` holds what is needed to tell, the tool that tells is later work | Recorded, not automated |

## Documentation & Instruction Impact

- **Stale docs** (land in PR 1): `README.md` (Backscatter fidelity, parameters tables, per-mode tables, `batch_regen` limits), the ADR-0007 addendum file, ADR-0003, the `sounding.h` comment.
- **Agent-instruction candidate** (proposal only): "a table is only valid on the angle basis it was fitted on; check `# angle_basis` before reusing one". The repo has no `.agents/README.md` yet.

## Question for the operator

Decision 4 (live node: earth-frame angle with a flat normal) predates the two-layer design. Under decision 12 the live node produces the status-quo beam-angle layer, and a live incidence mode without slope would be a third kind of value in that same layer. This plan therefore leaves the live node on beam angle and builds no live incidence mode. Confirm, or say that the flat-normal live mode is still wanted.

## Estimated Scope

Large, but smaller than revision 1: no spill refactor and no depth compare. Seven commit groups in PR 1, one PR after it.

## Implementation record (PR 1)

Commit order on `feature/issue-173`: A0 `georeferencePing` moved to `ping_georeference.h/.cpp` (verbatim); A `Sounding::beam_dir_enu` / `incidence_angle`, the sign test; B `Parameters::backscatter_angle_basis`; C the v2 table and the refusals; D `slope_incidence` and its tests; E `derived_tiles`; F the slope run in `import_bag`; G README and ADR text. Every group was built and run against the whole package suite before it was committed. The beam-basis outputs were compared byte for byte against a build of the plan commit (depth store, backscatter store and `build_fingerprint.json`, for `none`, a beam table and `--max-resident-tiles 1`): identical.

Where the code differs from, or settles, what the plan says:

1. **Group F library split.** The slope run's bookkeeping is a library (`slope_run.h/.cpp`, target `cube_bathymetry_slope_run`) with its own gtest (`test_slope_run`), so that `import_bag_main.cpp` stays under cpplint's limit (it is at 500 lines of `main`; the numeric-parse lambdas became free functions to make room).
2. **Table accounting test.** It is an end-to-end test (`test_import_bag_slope.py`), not a unit test: a four-beam bag whose beam angles and incidence angles fall on opposite sides of the table's outermost bin. It fails if the stamping runs after the accounting.
3. **`batch_regen_bag`** already refused every per-mode table; the refusal now happens before any bag is opened (so it can be tested without one) and names the incidence case.
4. **A table's recorded `# slope_baseline_deg` / `# slope_layers`** are compared with the run's and a difference is warned about (the plan only required the basis check).
5. **`derived_tiles.json`** gained the optional `last_publish` key (see section 2.6). Unknown keys are otherwise errors.
6. **Scratch depth tiles.** `ImportAccumulator::finalize` still persists the resident depth tiles into the scratch depth store (no change to the accumulator beyond the spill root); they are discarded with the scratch directory.
7. **Eviction.** With `--max-resident-tiles 1` over two tiles the contributors are identical and the cells agree to a median difference of 0.0 dB and a maximum of 0.14 dB (the median pre-filter is flushed at every reload), so the end-to-end test asserts median < 0.02 dB and maximum < 0.5 dB, not equality.
8. **Exit codes.** A bad request exits through `usage()`; a refusal after the request is accepted (lock, store inputs, surface, a contributor refusal, a failed read) is 1; exit 4 as for an ordinary run. Nothing is published on 1 or 4.
9. **Sampler cost** (`test_slope_incidence`, `SamplerCostOverARealSizedStore`): about 32 us per beam over a 2 x 2 tile processed store through the real `BathyDem`, 100000 beams, 24 % of baselines at the floor, on a workstation with another build running.
