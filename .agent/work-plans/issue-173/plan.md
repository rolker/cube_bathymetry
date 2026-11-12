# Plan: Slope-aware incidence correction for M3 backscatter via the shared seabed-geometry module (marine_backscatter)

## Issue

https://github.com/rolker/cube_bathymetry/issues/173

This work is "Part of rolker/unh_marine_autonomy#304". No PR for it may close #304. The PR(s) for this plan close #173 only when the last one lands (earlier ones say "Part of #173").

**Revision 2 (2026-11-12).** The operator replaced the single-run two-pass design of revision 1 (commit `6213722`) with a two-layer design and confirmed three points (Decisions 12 to 14). This revision rewrites the Approach around them and folds in the plan review of 2026-11-10 (`progress.md`, "Plan Review"); the section "How the plan review is resolved" maps each finding.

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

- `--slope-surface <bathy store>` and `--bs-store <dir>` are required; `-o` is **refused** (a slope run writes no depths).
- `--depth-adaptive` and the mixed-level flags are refused (single-level backscatter store, uma#383).
- A correction is required to be either `none` (the ledger-producing run of PR 2) or a table or curve file whose header says `# angle_basis: incidence`. A SonarInfo-delivered curve has no marker, so by the absence rule it is beam-basis and is **refused**, including under the default `--backscatter-correction auto`.
- `--bs-store` must be empty or already a slope-run store (it holds `derived_tiles.json`, below). A beam-angle backscatter store is refused. The reverse also: an ordinary run refuses a `--bs-store` that holds `derived_tiles.json`.

Mechanics:

1. The ordinary per-ping loop runs unchanged up to georeferencing and the gain shift. Then `stampIncidence(soundings, sampler, options, counters)` (new `slope_incidence.h/.cpp`, the only code that touches the module) sets `Sounding::incidence_angle` and counts fallbacks. **Then** the per-beam table accounting (`stampCorrectionIndex`, `BackscatterModeResolver::countBeam`) runs on the stamped incidence, so "at or beyond 90 deg", "beyond the outermost bin", `beams_correctable` and the exit-4 `table-unresolved` report are computed on the angle actually applied. Because the slope run is the bag loop itself, the resolver, the ping stamp and the beam index are all at hand; no spill is involved.
2. The estimator runs as in an ordinary import, but its `ImportAccumulator` is pointed at **fresh scratch directories** for both the depth store and the backscatter store (under `--scratch-dir`, default beside `--bs-store`; refused if the chosen scratch path exists and is not empty). A fresh pair means no warm start from any earlier tile, by construction, and eviction and reload work as they do today. `--reference-store` priors are honoured as in an ordinary run. The scratch depth store is deleted at the end; it is never published.
3. **Contributors.** The run keeps, per tile, the set of bag basenames that touched it: after each ping's `addBatch`, the sheet's dirty-grid set (`GeoMapSheet::dirtyGrids()` or the equivalent hook; the implementer confirms which set survives eviction and adds a small "touched since last asked" accessor if neither does) is attributed to the bag being read. This catches soundings that spread across a tile edge, which a position-only rule would miss.
4. **Publish.** After `finalize`, and before any file of the destination is changed: for every tile in the scratch backscatter store, read the destination's `derived_tiles.json`; if the tile is recorded there with a contributor that is not among this run's bags, the run **fails, naming the tile and the missing bags**, and the destination is untouched (decision 13). Otherwise the scratch tiles are moved into `<bs-store>/survey/` (rename when on one filesystem, copy then rename otherwise) and `derived_tiles.json` is rewritten atomically with the new entries merged over the old. A marker file `derived_tiles.publishing` is written before the first move and removed after the manifest; a later slope run that finds it reports the interrupted publish and requires the same bag set (it then republishes everything and clears it).
5. Depth is not compared between the passes. The depths are pass one's; the slope run's in-memory estimate exists only to attribute beams to hypotheses. It can differ slightly from pass one where pass one warm-started from earlier tiles; the README says so.

### 3. `derived_tiles.json` (per-tile record, decision 13)

At the root of a slope-run backscatter store. One entry per tile file name:

- `contributors`: sorted bag basenames.
- `inputs`: `angle_basis`, `slope_baseline_deg`, `slope_layers`, the table or curve SHA-256 (or `none`), the gain-declaration SHA-256 and reference state (or absent), and `depth_tiles`: the SHA-256 of the **content** of every `--slope-surface` tile the stencil could have read for this tile, that is the tile of the same index and its eight neighbours where they exist (streamed hash, computed once per depth tile per run). File sizes are not used: store tiles are fixed-size.
- `written`: timestamp from the clock, and the code version.

Plus a top-level `format` line and `schema_version: 1`. A strict loader (unknown version, malformed entry, duplicate key = error). This file, not `build_fingerprint.json`, is the staleness record of a slope-run store; a later tool can compare `inputs` with the present state of the depth store, table and declaration to list stale tiles. That tool is not in this plan.

`build_fingerprint.json` is written only by runs with `-o`, so the slope run writes none, ordinary runs' fingerprints are unchanged byte for byte, and schema 3 stays as it is.

### 4. The estimator side

`Sounding::incidence_angle` (float, radians, NaN = none). `Parameters::backscatter_angle_basis` selects which angle `Node::insert(double, const Sounding &, const Parameters &)` hands on: `beam` (today's `beam_angle`, byte-identical output, the default) or `incidence` (a NaN one sends the beam to the existing fallback policy). The median queue, `Hypothesis::recordBeam`, `correctBeamIntensity`'s signature and persistence are unchanged. One guard is added in the legacy single-curve path for the incidence basis only: an angle at or beyond 90 deg is not looked up. The recon spill record (`SpilledSounding`) is **not** changed: the slope run does not use it, so `test_recon`'s size bound and every depth-adaptive run are untouched.

### 5. Angle basis in tables and loaders

- **Marker.** A table or curve file may carry `# angle_basis: incidence` plus `# slope_baseline_deg:` and `# slope_layers:`. Absence means `beam`. The derive tool writes the marker **only** for incidence, so default derive output and every existing table SHA-256 are unchanged. Malformed or repeated records are errors in the C++ loaders, the derive tool and the node, following the #176 rules.
- **Check.** `import_bag` refuses a file whose basis differs from the run's, in both directions (decision 9). `batch_regen_bag` refuses an incidence-marked table or curve given as `--backscatter-curve`, and refuses a `--bs-store` that holds `derived_tiles.json` (decision 5); the message points at the slope run. The live node refuses an incidence-marked file at configure (it has no incidence mode in this plan).

### 6. The live node

Not changed beyond the refusal in section 5. See "Question for the operator".

### 7. The derive tool and the beam ledger (PR 2)

- `import_bag --beam-ledger-out <file>` (slope runs only): a JSON header (basis, baseline angle, layers, the `depth_tiles` digest set, bag basenames, gain-declaration digest, code version) then per ping `stamp_ns` (int64), beam count (uint16) and per beam a uint16 beam index (the index in the ping's detection arrays, explicit because the projector or the gate may drop beams), an int16 incidence in 0.01 deg (`-32768` = none) and a uint8 fallback reason. Written atomically. It records the angle as applied, so derive and import use the same number by construction.
- `derive_angular_response.py --angle-basis incidence --beam-ledger <file>[,...]`: bins by the ledger's incidence, keeping the sign of `rx_angle` as the port/starboard side. Beams absent from the ledger are dropped and counted. It writes the incidence marker and refuses a ledger whose header disagrees with the supplied gain declaration or bag basenames. Without `--angle-basis` it behaves exactly as today.
- Workflow (README): (a) ordinary import, giving the depths and the beam-angle layer; (b) slope run with `--backscatter-correction none --beam-ledger-out L` into a scratch backscatter store; (c) derive on `L`; (d) slope run with the table into the slope-corrected store.
- The report also prints per-bin mean incidence minus `|rx_angle|` and the fraction of beams with a surface.

### 8. Sign check (`sounding.h`)

A test with a synthetic ping: beams with `rx_angle > 0` have `sonar_relative_position.y > 0` and an earth-frame cross-track component of the matching sign relative to heading. The `sounding.h` comment changes from "REQUIRED" to the verified statement (already drafted).

### 9. Tests

- **Known-answer sloped seabed** (gtest, no store): a `SeabedHeightSampler` over a plane at slopes 0, 5, 15 deg and four aspects; `stampIncidence` reproduces the closed form to 1e-6 rad; zero slope equals the earth-frame angle from vertical; fallback counts and reasons pinned for a missing cell, a layer seam and a prior-only region; `step_m` follows range x angle and the floor count is pinned.
- **Level independent of aspect** (gtest through `Node`/`Hypothesis`): synthetic response `f(incidence)`, beams over four aspects of a tilted plane; with `incidence` the corrected mean is the same for every aspect; with `beam` the aspects differ by the known amount (negative control).
- **End to end** (pytest, extending the synthetic bags of `test_import_bag_mode_table`): ordinary import of a tilted-plane bag, then a slope run: per-cell backscatter independent of aspect; no depth tile written anywhere outside scratch and scratch removed; `derived_tiles.json` contents (contributors, depth tile hashes incl. neighbours); a second slope run with the same bags rebuilds and gives identical tiles; a second run with a subset of the bags is refused and leaves the store byte-identical; a run adding a bag over the same tiles succeeds when all recorded contributors are given; an interrupted publish (marker present) is reported; every refusal in section 2 and 5, both directions; changing one depth tile changes the recorded hash of it and of its neighbours' entries.
- **Unchanged behaviour**: `beam` basis output byte-identical on the existing end-to-end fixtures, `build_fingerprint.json` included; `test_recon`, `test_mixed_level_import`, `test_import_eviction`, `test_build_fingerprint`, `test_batch_regen` unchanged and green.
- **PR 2**: ledger round trip (C++ writer, Python reader, golden bytes), derive on incidence recovers `f`, header cases like #176.
- **Appledore acceptance** (numbers in the PR description, not the repo): slope run over the August 2026 bags; upslope versus downslope level difference on overlapping cells by slope class, before and after; the baseline distribution and floor fraction; time and sampler cost as measured.

### 10. Docs

A section "Incidence-angle addendum (cube#173)" in the ADR-0007 addendum file (not a new ADR; the design is still moving), with the two-layer rule, the derived-product rule and the angle baseline, and who decided. README: the slope run, its flags (`--backscatter-angle-basis`, `--slope-surface`, `--slope-baseline-deg`, `--scratch-dir`), `derived_tiles.json`, the refusals, the provisional default, the shallow-water note, and that incidence tables cannot be derived until PR 2. ADR-0003: one sentence that slope-run stores carry `derived_tiles.json` instead of a build fingerprint.

### Commit groups

**PR 1** (Part of #173): **A** `Sounding` fields and the beam vector in `georeferencePing`, sign-check test and comment (the uncommitted draft in the worktree covers part of A and B; it fails uncrustify and is to be reworked, not committed as is); **B** `Parameters::backscatter_angle_basis`, the selection in `Node::insert`, the 90 deg guard; **C** the `angle_basis` header record in the table and curve loaders and the refusals in `import_bag`, `batch_regen_bag` and the node; **D** `slope_incidence` with the `marine_backscatter` dependency and its known-answer tests; **E** the slope run: flags, scratch stores, incidence before table accounting, contributor tracking, pass report; **F** `derived_tiles.json`, the contributor check and the publish step; **G** README, ADR text.

**PR 2** (closes #173): ledger writer, derive tool changes, workflow docs.

## How the plan review is resolved

| Review finding (2026-11-10) | Resolution |
|---|---|
| Warm start differs between the passes; populated `-o` | Gone: the slope run has no `-o`, runs on fresh scratch stores, and publishes afterwards |
| Depth equality not established under eviction | Gone: depths are not compared; pass one's depths are the only depths |
| `batch_regen_bag` section on a wrong premise | Rewritten (section 5): refuse an incidence-marked file and a slope-run store |
| `SpilledSounding` growth breaks `test_recon` | Gone: the spill is not used or changed |
| Ledger cannot be written from the spill | Gone: written from the bag loop with explicit `stamp_ns` and beam index |
| SonarInfo curve on incidence only warned | Refused (section 2) |
| Table accounting on the wrong angle | Incidence is stamped before the accounting (section 2.1) |
| `--slope-surface` identity by sizes is empty | Per-tile content hashes of the depth tiles read (section 3) |
| Derive writes the marker everywhere | Only for incidence |
| Byte-identity versus new fingerprint keys | No new fingerprint keys; the slope run writes no fingerprint |
| Fingerprint only when a correction applies; equal results, equal identity | Superseded by `derived_tiles.json`, whose `inputs` are content digests |
| `Node::update` does not exist | Corrected to `Node::insert` |
| Half-width versus baseline; `max(3 cells, 4 m)` not expressible | Replaced by the angle baseline; `step_m` is stated as the half-width; the floor is the module's own |
| `BathyDem` throws on an empty store | Named error, warnings printed |
| Node basis must be configure-only | No node basis parameter in this plan |
| Beam vector float versus double | Double |
| Stale text | Rewritten |
| Which pass writes `-o`; incremental imports under incidence; 4 m half or full | Settled by decisions 12, 13 and 14 |

## Files to Change

| File | Change |
|------|--------|
| `include/cube_bathymetry/sounding.h` | `beam_dir_enu`, `incidence_angle`; sign-check comment |
| `src/import_bag_main.cpp` (and a new `src/slope_run.cpp` / header if `main` would pass cpplint's length limit) | beam vector; slope-run flags, refusals, scratch stores, ordering, contributors, report; PR 2: `--beam-ledger-out` |
| `include/cube_bathymetry/slope_incidence.h`, `src/slope_incidence.cpp` | new: `stampIncidence`, options, counters |
| `include/cube_bathymetry/derived_tiles.h`, `src/derived_tiles.cpp` | new: manifest model, strict loader, atomic writer, contributor check, depth-tile hashing, publish |
| `include/cube_bathymetry/parameters.h`, `src/parameters.cpp`, `src/node.cpp` | angle basis and its selection |
| `src/angular_response_curve.cpp`, `src/backscatter_table.cpp` and headers | `angle_basis` record |
| `src/batch_regen_main.cpp`, `src/cube_bathymetry_node.hpp` | refusals |
| `include/cube_bathymetry/geo_map_sheet.h`, `src/geo_map_sheet.cpp` | only if a touched-tiles accessor is needed |
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
