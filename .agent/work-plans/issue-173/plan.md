# Plan: Slope-aware incidence correction for M3 backscatter via the shared seabed-geometry module (marine_backscatter)

## Issue

https://github.com/rolker/cube_bathymetry/issues/173

This work is "Part of rolker/unh_marine_autonomy#304". No PR for it may close #304. The PR(s) for this plan close #173 only when the last one lands (earlier ones say "Part of #173").

## Context

Today every M3 beam is corrected by an angle read straight off the ping: the sonar-frame `rx_angle` (`Sounding::beam_angle`), used as `|beam_angle|` by the per-pulse-mode table (cube#172) and the single curve, then the gain-state shift (cube#176) before both. On a sloping seabed the angle the seabed sees (incidence against the local normal) differs from it by up to the slope, so upslope and downslope passes over the same ground come out at different levels. The Appledore August 2026 M3 map, after the gain-step fix, still has a bright line under the vessel track, some across-swath ribbing and no slope term. **This plan does not claim slope explains the nadir line**; a separate investigation owns that. It does make the slope term measurable, and the ledger below (step 7) reports incidence beside beam angle so that investigation can use it.

### State of the geometry module (uma#304), as found 2026-11-10

- Branch `feature/issue-304` of `unh_marine_autonomy`, worktree `/home/roland/project11/layers/worktrees/issue-unh_marine_autonomy-304`, head `65345f82`, **10 commits, local only: not pushed, no PR**. Based on `d9122f8f`; `jazzy` has moved 50 commits (`git merge-tree` against local `jazzy` reports no conflicts).
- Lifecycle done: plan authored, plan reviewed (5 must-fixes folded), implemented. **`/review-code` pre-push, the push and the PR are not done.** The 318-test result (`marine_backscatter` 122, `marine_sidescan_mosaic` 196, 0 failures, 37 cppcheck skips) is the branch's own record, not re-run here.
- It is the first slice only ("Part of #304"): `marine_backscatter::seabed::` with `SeabedHeightSampler` (`heightAt(lat, lon)` returning height up-positive WGS84, per-axis cell spans, a `SourceId` and a status), `metresPerDegree`, `seabedNormal(sampler, lat, lon, NormalOptions)` (central differences, default stencil 3 native cells floored at one, flat fallback with a `FallbackReason`: missing sample, incomplete stencil, layer/level/tile seam, no cell size, bad step, bad position), `incidenceAngle(beam_dir, normal)` (raw acos, may exceed 90 deg, invalid result flagged not thrown), `grazingAngle`, `beamFromAzimuthDepression`, `enuFromNed`; and the store-backed `BathyDem` moved down from `marine_sidescan_mosaic` and made a `SeabedHeightSampler` (strict stencil, never reads `overviews/`). Footprint area, ray-march shadow and the slope-aware `grazingQuality` overload are not in it.
- Consequences for this issue: **cube#173 can build on it as is** (it matches what the design needs: generic, no store/ROS types in the header, own-survey layer list as a caller argument, flat fallback with reasons). It must first be reviewed (`/review-code` pre-push), merged up to current `jazzy`, pushed, PR'd and merged. Until then nothing here can compile against it from `layers/main`.
- Known behaviour to design around: a stencil straddling a GGGS tile boundary falls back to flat even in one layer (about 1.5% of area at the default step, operator-accepted in uma#304); a sampler is not thread-safe (one per thread); exceptions propagate (a corrupt tile aborts, never silently flat).

## Approach

### Design in one paragraph

One **angle definition**, shared by the importer, the live node and the derive tool: `incidence = angle between the beam's arrival direction (earth-frame, unit vector sensor to touchdown, ENU) and the seabed normal`, where the normal is the surface normal when one is available and **the vertical when not** (so the fallback is "earth-frame angle from vertical", which also equals today's `|rx_angle|` only for a level vessel). Offline, `import_bag` runs two passes: pass one is today's import with backscatter off (builds the depth surface and spills the soundings); pass two replays the spill, stamps each sounding's incidence from the **finished pass-one `processed` layer** (read-only, through `BathyDem`), then runs the usual estimator with the table applied on incidence. Tables and stores carry an explicit `angle_basis`; the derive tool does not re-implement the geometry, it reads a per-beam **ledger** that `import_bag` writes.

### 1. The angle, and where slope comes from

- **Beam vector.** `georeferencePing` already computes, per sounding, `local = enu.toLocal(sounding_ecef)`: the earth-frame ENU offset of the touchdown from the sensor origin. That vector, normalised, is the beam direction (straight-ray, the SSP tracer stays as it is, per the issue). Carry it on `Sounding` as `beam_dir_enu` (three floats, NaN = absent) and a `uint16 beam_index` (the index in the ping's detection arrays; the projector range-filters, so the index is otherwise lost). It uses attitude and the sonar mounting, which `rx_angle` alone lacks, and it needs no `rx_angle` sign convention for the incidence itself. The `sounding.h:152` sign check is still done as a test (below) because the derive tool's port/starboard fold uses the sign.
- **Which surface.** The **finished own-survey `processed` layer** of the pass-one bathymetry store, read with `marine_backscatter::BathyDem(store, {"processed"})` (and `draft` if present, mirroring uma#304's own-survey rule). Never `reference`, `chart`, `overviews/`, never the store being written by pass two. Where pass one has no survey cell (prior-only, edge of survey) the sample is `kNoData`, the normal falls back to flat and the beam is **counted, never slope-corrected** (issue acceptance).
- **Resolution and noise.** The surface is the CUBE node grid (cell size = `--resolution`). `seabedNormal` is a central difference over a stencil half-width `s`; its noise is about `sigma_h / (sqrt(2) s)` per axis (for a surface uncertainty of 3 cm: 0.4 deg at `s = 3 m`, 1.6 deg at `s = 0.75 m`). So the default is the module's auto step (3 native cells) **with a floor in metres**: `--slope-min-scale-m` (default 4 m, decision 7). The scale is a run parameter, recorded in the fingerprint and the table header, and the pass report prints the stencil actually used, the clamp count, and a histogram of slope in degrees so a too-fine or too-coarse scale shows. Real relief shorter than twice `s` is smoothed away; at the M3's footprint (about 1 deg beams) that is the intended trade. Beams whose incidence comes out at or beyond 90 deg (back-facing) are handed to the existing "beam at or beyond 90 deg" fallback (`identity` or `exclude`) and counted under their own reason, not clamped into range.
- **Datum.** Only height differences enter the normal, so the pass-one store's ellipsoidal heights need no conversion; the sampler returns up-positive heights (the cube store is already ellipsoidal, up-positive in `BathyDem`'s terms; a test pins the sign with a tilted store).

### 2. Two-pass mechanics in `import_bag`

Reuse the depth-adaptive recon's spill and replay, not a new mechanism (operator note on the issue), but note `ReconCollector` is tied to a `LevelPlanPolicy` and the count grid. Plan: factor the single-file chronological spill and `forEachSpilled` (with the free-space check and the count-verified replay) out of `ReconCollector` into a `SoundingSpill` class that both use; `ReconCollector` behaviour stays byte-identical (its tests, `test_recon`, `test_mixed_level_import`, run unchanged).

Flag: `--backscatter-angle-basis beam|incidence` (default `beam`, decision 10). With `incidence` and a fixed-level run:

1. **Pass one.** The normal per-ping loop (project, georeference, gain shift, mode index stamp) feeds `accumulator.addBatch` into `-o` with backscatter **off** (no `--bs-store` write, no correction), and every sounding is also written to a `SoundingSpill` in `--scratch-dir` (default beside `-o`; `SpilledSounding` gains the three beam-vector floats and `beam_index`). Pass one needs no table, so it is independent of the table that pass two applies. At the end the accumulator finalises; the `processed` layer is the surface.
2. **Pass two.** Fresh `ImportAccumulator` and sheet, the same Parameters, same reference/chart priors, **depth output into a scratch bathymetry dir, not `-o`** (a pass-two accumulator pointed at the pass-one store would warm-start from its `processed` tiles and no longer reproduce pass one) and backscatter into `--bs-store`. Replay `forEachSpilled` front to back; for each ping's soundings call `stampIncidence(soundings, sampler, options, counters)` (new `slope_incidence.h/.cpp`, the only place that touches the module) which sets `Sounding::incidence_angle` and counts fallbacks per reason; then `addBatch`. Depth estimation is deterministic, so pass two's depth must equal pass one's: the run compares the two bathymetry stores' tile sets and values at the end and fails loudly on a mismatch (a free integrity check and the guard against a spill that lost a record; the replay count check from the recon already guards truncation). The scratch depth store is deleted after the compare.
3. **Surface supplied instead** (decision 6): `--slope-surface <bathy store dir>` skips pass one and the spill, reads the supplied store and runs the normal single per-ping loop with `stampIncidence` inline. Used to re-run with a new table without repeating pass one, and to reuse a surface from another run. Its identity in the fingerprint is the store's `registry.json` SHA-256 plus tile count and a digest of the sorted tile file names and sizes (a store whose tiles changed under the same registry must read stale); the internal two-pass case records `"self"`, and its inputs are already the fingerprint's other keys.
4. `--depth-adaptive` with `incidence`: refused with a message (it already refuses `--bs-store`; same reason, single-level backscatter store, uma#383).
5. Memory and time: pass two does 5 sampler calls per beam (centre plus four probes) against a tile LRU; a real `BathyDem` lookup is about a microsecond, so an M3 survey of 5e8 beams costs about 40 minutes of lookups on top of the second estimation. The pass report prints sampler time and cache misses; if it bites, memoise per (ping, tile) neighbourhood is the first lever, not the interface change.

### 3. The estimator side

`Sounding::incidence_angle` (float, radians, NaN = none) rides the median queue as a new trailing float of `DepthAndUncertainty` (the layout note there allows appends at the end; every raster read uses `sizeof`), into `Node::update` and `Hypothesis::recordBeam`. A new `Parameters::backscatter_angle_basis` selects which angle `correctBeamIntensity` is given: `beam` (today's `beam_angle`, byte-identical output, the default) or `incidence` (`incidence_angle`; a NaN one is "no vector": the beam goes to the existing fallback policy, counted). `correctBeamIntensity` itself does not change; only the angle argument does, so the table loader, `correctWithTable` and the range/offset terms are untouched. The recon spill and `batch_regen`'s `ScatterRecord` carry the new field(s).

### 4. Angle basis in tables, loaders, fingerprint (consistency, existing tables)

- **Marker.** Curves and tables get a header line `# angle_basis: beam|incidence` plus, for incidence, `# slope_scale_m:` and `# slope_surface: processed` (the sampler layers). Absence means `beam` (every existing table and curve). The record follows the #176 rules: both-or-neither where paired, each once and non-empty, a malformed record is an error in all three tools (C++ loaders, derive, node configure).
- **Check.** `import_bag` and the node refuse (naming the flag) a table whose basis differs from the run's `--backscatter-angle-basis`, in both directions, exactly like the gain-declaration digest check. A `SonarInfo`-delivered curve has no header: with `incidence` it warns once that its basis is unknown, as the gain case does. **Existing tables stay valid** for `beam` runs; they are never silently applied on incidence, and a new run on `incidence` needs a table derived on incidence (this is the "curves must be re-derived" consequence, made a refusal instead of a doc note).
- **Fingerprint.** `backscatter_correction` gains `angle_basis` and, for `incidence`, `slope_scale_m`, `slope_layers` and `slope_surface` (`"self"` or the digest from step 2.3), compared by `isStale`; a fingerprint without the keys reads as `beam`, so a pre-#173 store is stale against any `incidence` run and not stale against a `beam` run. Schema stays 3 (additive, same as the #176 note says for future keys); a unit test pins that an old fingerprint file loads. The ADR-0003 text gets the new keys.
- **`kind: table-unresolved`** logic is unchanged.

### 5. `batch_regen_bag`

Refuses `--backscatter-angle-basis incidence` (decision 5), as it refuses a table: it has no TF, no SonarInfo and an independent per-tile spill, so slope parity is not available and a silent `beam` rebuild inside an `incidence` store is the failure to prevent. Its fingerprint writes `angle_basis: beam`; the refusal message points at `import_bag`. A follow-up (not this issue) would carry `incidence_angle` in `ScatterRecord`, which is trivially bit-exact once pass two has stamped it, but needs the surface at regen time.

### 6. The live node (open question, not decided here)

The node gets the same earth-frame beam vector: its cloud is in the sensor frame and it already looks up `earth <- frame` at the stamp (`cube_bathymetry_node.hpp` around the cloud callback), so the chord is `R_enu_from_ecef * R_tf * p`, sharing one helper with `georeferencePing` (a test pins both against the same ping). Options for the slope source:

- **L1 (recommended for this issue): flat normal, earth-frame angle.** Parameter `backscatter_angle_basis` (`beam` default, `incidence`); with `incidence` and no surface the node uses the fallback definition (angle from vertical in the earth frame), counts every beam as `no_surface`, and logs it once. A table fitted on incidence applies consistently (it is within the definition) but without the slope term, and the node says so at configure. Rejects a table whose basis differs, like `import_bag`.
- **L2: its own running surface.** Slope from the node's own best estimate in neighbouring nodes (survey-derived only, never the Chart/Reference-primed predicted surface; the issue's rule). Cost and risk: the surface is "built so far", so the first pass over a slope sees no or partial surface and a later pass sees the first pass's; results depend on pass order and differ from offline; needs a per-node survey flag or a sampler over best-estimate values, and a cross-tile stencil story (uma#304 falls back flat at tile seams). Worth a design note once the offline result is validated on Appledore, not before.
- **L3: none.** Live stays on `beam`.

The plan builds L1 only. The question for Roland (decision 4) is whether L1 is enough for v1 or the live node stays on `beam` (L3); L2 is deferred either way.

### 7. The derive tool and the beam ledger

`derive_angular_response.py` reads bags and has no surface; re-implementing the normal in Python (needing a Python tile reader and an exact copy of the stencil rules) would be a second implementation to drift, so it does not. Instead:

- `import_bag --beam-ledger-out <file>` (valid with `incidence`, pass two or `--slope-surface` runs) writes a compact binary ledger: a JSON header (basis, scale, surface identity, bag basenames, code version) then per ping `stamp_ns` (int64), beam count (uint16), and per beam an int16 incidence in 0.01 deg (`-32768` = none), the beam index implied by position, and a uint8 fallback reason. About 3 bytes per beam; an 8e8-beam survey is a couple of GB, so it is per run, written atomically and optional. It records the angle **as applied** (fallback pings included), which makes "derive and import use the same angle" true by construction, not by argument.
- `derive_angular_response.py --angle-basis incidence --beam-ledger <file>[,...]`: bins by the ledger's incidence instead of `rx_angle`, keeping the sign of `rx_angle` as the port/starboard side so the fold and asymmetry diagnostics keep working. Pings or beams absent from the ledger (or reason-coded none) are dropped and counted in the report; the gain declaration, `--by-mode`, mode keys, range term, switch offsets and `--box` are unchanged and see the same beams. It writes `# angle_basis: incidence`, the slope scale and the surface identity into the header, and refuses a ledger whose header disagrees with the supplied gain declaration or bag basenames. Without `--angle-basis` it behaves exactly as today and writes `# angle_basis: beam`.
- Workflow (documented in the README): (a) two-pass import with `--backscatter-correction none --beam-ledger-out L`; (b) derive on `L`; (c) re-run with the table, using `--slope-surface <store from a>` to skip pass one. The table's angle bins are in incidence degrees; the existing "at or beyond 90 deg", "beyond the outermost bin" fallback rules apply to the incidence value unchanged.
- Nadir diagnostic: the report also prints the per-bin mean incidence minus `|rx_angle|` and the fraction of beams with a surface, so the nadir-line investigation has the numbers.

### 8. Sign check (`sounding.h:152`)

A test with a synthetic ping: beams with `rx_angle > 0` must have `sonar_relative_position.y > 0` (starboard) and a ledger/chord cross-track component of the matching sign relative to the ping's heading; a real-bag smoke (the existing pier excerpt) counts beams where `sign(rx_angle)` and `sign(sonar_relative_position.y)` disagree and the pass report prints it. The `sounding.h` comment is updated from "REQUIRED" to the verified statement.

### 9. Tests

- **Synthetic sloped seabed, known answer** (gtest, no store): a `SeabedHeightSampler` over a plane `h = a e + b n` at slopes of 0, 5 and 15 deg and aspects 0/90/180/270; a sensor at height H and beams across track; the expected incidence is closed-form (`acos(-d.n)`). `stampIncidence` must reproduce it to 1e-6 rad; zero slope must equal the earth-frame angle from vertical exactly; the fallback counts and reasons are pinned for a missing cell, a seam and a prior-only region.
- **Level independent of aspect** (gtest through `Node`/`Hypothesis`, acceptance 1): a synthetic angular response `f(incidence)` (the table is built from `f`), beams generated over the four aspects of a tilted plane with raw intensity `A + f(incidence)`; with `incidence` basis the corrected mean is `A` for every aspect and equals the flat result when slope is zero; with `beam` basis the aspects differ by the known amount (negative control, so the test fails if the basis switch is a no-op).
- **End to end through `import_bag`** (pytest, extending the synthetic bag of `test_derive_angular_response`/`test_import_bag_mode_table`): a synthetic bag over a tilted plane, two-pass run, store read back: per-cell backscatter independent of aspect; pass-two depth equals pass-one depth (the compare must fire on a deliberately corrupted scratch tile); `--slope-surface` run bit-equal to the two-pass run; fingerprint keys; `--depth-adaptive` and `batch_regen_bag` refusals; table basis mismatch refused both ways; prior-only cells counted flat.
- **Derive**: ledger round trip (C++ writer, Python reader, golden bytes), derive on incidence recovers the synthetic `f`, and a table made on beam angle is refused by an `incidence` import. Header both-or-neither cases like #176.
- **Refactor safety**: `test_recon`, `test_mixed_level_import`, `test_import_eviction`, `test_build_fingerprint` (old-file load), `test_batch_regen` unchanged and green; `beam` basis output byte-identical to the previous release on the pier excerpt (hash of the stores).
- **Appledore acceptance** (not a unit test, recorded in the PR): re-import of the August 2026 M3 bags; compare per-bag stores on overlapping cells for upslope versus downslope passes before and after, reporting the median level difference by local slope class. The numbers go in the PR description, not the repo.

### 10. Docs and the design record

A new section in the ADR-0007 addendum file, **"Incidence-angle addendum (cube#173)"** (not a new ADR: the design is still evolving, the live-node source is open; this follows the #172 and #176 precedent), and the "Deferred" tier-3 entry marks the incidence term done. README: parameters table rows for every new flag (`--backscatter-angle-basis`, `--slope-min-scale-m`, `--slope-surface`, `--beam-ledger-out`, `--scratch-dir` reuse, node `backscatter_angle_basis`), the workflow in step 7, the refusals, the Appledore note on which stores are stale. `AGENTS.md`/`.agents` do not exist for this repo's guide; no new instruction file.

### Sequencing with uma#304 (concrete)

| Step | Where | Gate |
|---|---|---|
| S1 | uma#304: run `/review-code` pre-push on `feature/issue-304`, fix findings, merge current `jazzy` (clean today), push, open the PR "Part of #304", merge | Roland content-reviews the PR (core-layer geometry, not just green CI) |
| S2 | cube#173 groups A to D (below) developed locally **in parallel with S1**; they need no module code | none |
| S3 | cube#173 group E (`stampIncidence` plus the sampler) after S1 merges, built against the merged core layer (`make build` in core then sensors) | S1 merged |
| S4 | open the cube PR(s) | S3 green, `ci-local` full-scope attestation (ADR-0018) |

Commit groups (atomic, each green on its own): **A** `Sounding` fields (`beam_dir_enu`, `beam_index`, `incidence_angle`), `DepthAndUncertainty` append, spill and `ScatterRecord` fields, with the `beam` path byte-identical; **B** `Parameters::backscatter_angle_basis`, `Node`/`Hypothesis` angle selection, header marker in loaders; **C** fingerprint keys, `isStale`, refusals (table basis, `batch_regen`, `--depth-adaptive`); **D** `SoundingSpill` extraction from `ReconCollector`, beam-vector computation shared by `georeferencePing` and the node, sign check; **E** `slope_incidence` plus `package.xml`/`CMakeLists.txt` direct dependency on `marine_backscatter`, the two-pass driver, `--slope-surface`, depth-equality compare, report; **F** ledger writer, derive changes, workflow docs, the ADR addendum. The PR can be split after D (A to D, no behaviour change in any default run) and E to F, decision 8.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/sounding.h`, `common.h` | `beam_dir_enu`, `beam_index`, `incidence_angle`; `DepthAndUncertainty` trailing `incidence`; sign-check comment resolved |
| `cube_bathymetry/src/error_model.cpp`, `detections_projector.cpp` | stamp `beam_index` on each sounding |
| `cube_bathymetry/src/import_bag_main.cpp` | `georeferencePing` keeps the ENU vector; flags; two-pass driver; `--slope-surface`; pass report; `--beam-ledger-out`; refusals |
| `cube_bathymetry/include/cube_bathymetry/slope_incidence.h`, `src/slope_incidence.cpp` | new: `stampIncidence`, counters, options, the only user of `marine_backscatter::seabed` |
| `cube_bathymetry/include/cube_bathymetry/recon.h`, `src/recon.cpp` | extract `SoundingSpill`; `SpilledSounding` gains the vector and index |
| `cube_bathymetry/src/node.cpp`, `src/hypothesis.cpp`, `include/.../node.h`, `hypothesis.h` | carry and select the angle |
| `cube_bathymetry/include/cube_bathymetry/parameters.h`, `src/parameters.cpp` | `backscatter_angle_basis` and slope options |
| `cube_bathymetry/src/angular_response_curve.cpp`, `backscatter_table.cpp` and headers | `angle_basis` header record and loader checks |
| `cube_bathymetry/src/build_fingerprint.cpp`, header | new keys, `isStale`, JSON round trip |
| `cube_bathymetry/src/batch_regen.cpp`, `batch_regen_main.cpp` | refuse `incidence`; `ScatterRecord` unchanged this issue |
| `cube_bathymetry/src/cube_bathymetry_node.hpp` | earth-frame vector, parameter, configure check (L1) |
| `cube_bathymetry/scripts/derive_angular_response.py` | `--angle-basis`, `--beam-ledger`, header, diagnostics |
| `cube_bathymetry/CMakeLists.txt`, `package.xml` | `marine_backscatter` dependency, new tests |
| `cube_bathymetry/test/test_slope_incidence.cpp`, `test_incidence_basis.cpp`, `test_import_bag_slope.py`, extensions to `test_build_fingerprint`, `test_derive_angular_response.py`, `test_backscatter_table`, `test_recon` | as in step 9 |
| `cube_bathymetry/docs/decisions/0007-...addendum...md`, `0003-staleness-fingerprint.md`, `README.md` | design record, fingerprint keys, parameters and workflow |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Opt-in basis flag; every fallback and refusal is counted and printed; the pass report names the stencil, surface identity and fallback fractions |
| Capture decisions, not just implementations | Addendum to the ADR-0007 set; operator decisions (two-pass offline, live open) recorded with who decided |
| A change includes its consequences | Derive tool, tables, loaders, fingerprint, `batch_regen`, node, README and ADR text all in the plan; stale-table handling is a refusal, not a doc note |
| Only what's needed | No footprint area, no shadow march, no L2 live surface, no `ScatterRecord` support, no new ADR |
| Improve incrementally | A to D change nothing for default runs; the PR can split there |
| Test what breaks | Known-answer sloped plane, negative control, depth-equality compare, refusals, golden ledger bytes |
| Workspace vs project separation | Nothing sonar- or survey-specific in code; Appledore appears only in the PR numbers, not the repo |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| cube ADR-0007 addendum set | Yes | New incidence addendum; "Deferred" updated |
| cube ADR-0003 (fingerprint) | Yes | Angle basis and slope surface are fingerprinted; old stores read as `beam` |
| cube ADR-0008 (predicted surface) | Touches | The predicted surface is NOT used for slope (it is primed from Chart/Reference); only the finished `processed` layer is |
| uma ADR-0006 D4 / ADR-0007 D6 | Yes | M3 call site of the shared GeoCoder chain |
| workspace ADR-0008 (ROS conventions) | Yes | License headers, cpplint, parameter docs for new parameters |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Correction angle (stored M3 values change) | Re-import of existing stores; fingerprint staleness | Yes (fingerprint, refusal of mismatched tables); the re-import itself is operator work |
| Angle basis of fitted tables | `derive_angular_response.py`, loaders, header | Yes (steps 4, 7) |
| `Parameters` / CLI flags / node parameters | README parameter tables | Yes |
| `DepthAndUncertainty`, spill records | Eviction spill, recon spill, `batch_regen` | Yes (A, D); `ScatterRecord` deliberately not (refused) |
| ADR-0003 key list | ADR text | Yes |
| uma `marine_backscatter` dependency | `package.xml`, CMake find, layer build order (core below sensors) | Yes |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): `cube_bathymetry/README.md` (Backscatter fidelity, parameters tables, derive section, per-mode tables section, `batch_regen` limits); `docs/decisions/0007-...addendum...md` ("Deferred" tier-3 entry and the new addendum); `docs/decisions/0003-staleness-fingerprint.md` (new keys); the `sounding.h:152` comment.
- **Agent-instruction candidates** (proposals only): a pitfall for the repo guide, once one exists: "a table is only valid on the angle basis it was fitted on; check `# angle_basis` before reusing one". The repo has no `.agents/README.md` yet (gap noted, not part of this work).

## Open Questions

1. uma#304 sequencing: review, push and merge it first, with cube groups A to D developed meanwhile (recommended), or hold cube work entirely?
2. Source of truth for the angle in the derive tool: C++ ledger (recommended) versus a Python re-implementation.
3. Fallback definition: earth-frame angle from vertical (recommended) versus the sonar-frame `|rx_angle|`.
4. Live node v1: L1 earth-frame flat-normal (recommended) versus staying on `beam`; L2 deferred.
5. `batch_regen_bag` with `incidence`: refuse (recommended) versus support now.
6. `--slope-surface <store>`: include now (recommended) versus only the internal two-pass.
7. Stencil scale default: 3 cells with a 4 m floor, or the module's bare 3 cells?
8. One PR or two (split after group D)?
9. Existing tables: keep valid for `beam`, refuse on `incidence` (recommended), or auto-convert (not possible without the data).
10. Default basis: `beam` until the Appledore comparison is in (recommended) versus `incidence` as the new default.
11. Where the `slope-min-scale-m` default of 4 m comes from: a 3 cm surface uncertainty is my assumption from the CUBE output, not measured on Appledore; the pass-one `uncertainty` band should be read to confirm before the default is fixed.

## Estimated Scope

Large: one issue, six commit groups, two PRs recommended (A to D with no default-run change; E to F the feature). Blocked for merge on uma#304 being reviewed and merged; groups A to D are not.
