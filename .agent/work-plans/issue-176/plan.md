# Plan: Declared sonar gain-state segments for backscatter import and table derivation

## Issue

https://github.com/rolker/cube_bathymetry/issues/176

## Context

PR #175 (#172) put every ping on the reference pulse mode's level through a per-mode table. It cannot see a gain
change the sonar did not record: on Appledore the M3 receiver gain stepped between 2026-08-26T20:17:30Z and the
end of 2026-08-28 (reported level 23 dB lower, uniform across modes: CW 20/50 us +23.0, 1 ms FM +22.7, within the
quoted spread; measured from 528 overlapping 10 m cells). SonarInfo carries no gain, so the step is invisible to
`BackscatterModeResolver`; a table derived across the step (candidate D) is fitted over two gain states and every
import across the step puts a 23 dB seam in the mosaic. Source-side capture is rolker/marine_tools#97 (out of scope).

This issue adds an operator-written **gain-state declaration**: a reference state plus the deviations from it, as
half-open UTC intervals with an additive dB offset. Cube ships the schema, loader, resolver and a test fixture; the
Appledore file is campaign configuration and lives with the campaign/platform metadata (unh_echoboats_project11 /
the archive), never in this repo.

Shape of the mechanism (decided here, see Design decisions 6-7): the offset is applied **to the raw intensity of
the ping, before everything else**, at the same three stamping sites that already stamp `correction_idx`
(`import_bag` `stampCorrectionIndex`, node `stampCloudMode`, `batch_regen` per-ping loop). It is not folded into
the mode table's `correction_idx` space: that index is per (mode) and carried through `Sounding`, the recon spill
and the packed `ScatterRecord`; a (mode x segment) product would explode it and would leave a ping whose mode
did not resolve (identity fallback) uncorrected for gain, which is exactly the seam this issue removes. Because the
offset is baked into `Sounding::intensity` before the recon spill / bucket write, nothing downstream applies it
twice and no persisted record format changes.

## Design decisions

Operator decisions of 2026-11-09 (settled; honoured as written):

1. **Declaration semantics.** The file names a reference gain state and lists DEVIATIONS from it as half-open UTC
   intervals `[start, end)` with an additive offset in dB (the value ADDED to the reported level to reach the
   reference). Overlap is an error. A ping whose time falls in no segment is AT THE REFERENCE (offset zero), not
   unknown; the report counts pings per segment and pings in undeclared time, so a mistyped stamp shows as a wrong
   count. No new fallback knob.
2. **`batch_regen` consumes the declaration** (parity with `import_bag`); a regen never rebuilds tiles uncorrected
   inside a corrected store.
3. **The build fingerprint records the declaration** (hash + reference state) with a `schema_version` bump, so
   stores and per-mode tables built before the declaration existed read as stale.
4. **The derive tool (`--by-mode`) applies each segment's offset per ping BEFORE the fit**, so one table comes out
   in the reference state (no per-segment fitting).
5. **The declaration is campaign configuration**: cube ships schema, loader and a test fixture only. Provenance
   fields are mandatory (how the offset was obtained).

Decisions this plan adds (flag any you disagree with):

6. **Sign, pinned and tested.** `offset_db` is added: `level_in_reference = reported + offset_db`. The Appledore
   post-step segment is `+23.0` (reported is low); undeclared/pre-step is 0. A known-sign test (reported 40 dB in a
   +23 segment -> 63 dB) guards it; a flipped sign would double the split, the #172 candidate-D failure.
7. **The offset applies to every ping in a segment, whether or not a mode table is loaded or the ping's mode
   resolved.** It is a receiver calibration, independent of the mode table. The shift is its own function
   (`shiftSoundings`), called unconditionally whenever a declaration is loaded: in `import_bag` it is hoisted OUT of
   the `if (mode_resolver)` guard around `stampCorrectionIndex` (~l.2686), and in the node out of the
   `!bs_resolver_ || soundings.empty()` early return of `stampCloudMode` (only the empty-cloud skip remains).
   An `identity`-fallback ping is still shifted; an `exclude`d ping stays excluded; `--backscatter-correction none`
   and a single-curve run are shifted too. NaN intensity stays NaN; `countBeam`/`tableBeamStatus` use angle and
   range, not intensity magnitude, so ordering relative to the shift is harmless.
8. **Declaration x fitted-curve consistency (per-mode tables AND single-curve CSVs).** The derive tool, with a
   declaration, writes `# gain_declaration_sha256:` and `# gain_reference_state:` into the per-mode table header and
   into the single-curve CSV header (`loadAngularResponseCurveWithHeader` reads them too). `import_bag` and
   `batch_regen` refuse (before any bag is read) a curve/table whose recorded digest/reference differs from the
   declaration supplied, a curve/table that records none while a declaration is supplied, and one that records a
   declaration while none is supplied. Reason: a curve fitted in the reference state applied to un-shifted post-step
   pings is wrong by the step, and one fitted across the step is invalid. Pre-#176 files carry no keys, so with a
   declaration they are refused; with no declaration they load as before. **Escape hatch (operator answer):** a
   season that never crossed a step re-derives with, or imports with, a zero-segment declaration (it still changes
   the fingerprint through the reference label, which is intended: that store is in a named reference state).
   Explicit curve paths only; the SonarInfo-delivered curve (`auto`, import_bag) carries no header, so
   `auto` + declaration emits a loud warning that the curve's gain state is unknown.
9. **Provenance vocabulary** (confirmed by the operator): `settings_file | measured | operator_reported`, plus a mandatory non-empty free-text
   `provenance_note` (the Appledore numbers live in the campaign file and the test fixture only). Both
   required on every segment; a segment without them fails the load.
10. **The reference is a name, not a number.** `reference_state` is a free label (`pre_step`); the offsets are
    relative to it. Two declarations with different reference labels are different corrections (the label is in the
    fingerprint), so a store in `pre_step` is never silently merged with one in another state.
12. **The hash is of a canonical form, not the file bytes:** format version, reference state, and each segment's
    `start_ns`, `end_ns`, `offset_db` (`%.6f`) in sorted order. Notes, ids, `sonar`, comments, whitespace and field
    order are excluded, so editing a note does not stale every store and table; strict equality otherwise (no
    "covers the data" reasoning). Both the C++ and Python loaders compute it; the shared vectors pin the digest. The
    loader still returns the raw bytes, but the fingerprint and headers carry the canonical digest.
13. **Operator answers 2026-11-09 to the review questions:** package README documents the option/parameter now
    (`.agents/README.md` does not exist; creating that guide is its own follow-up issue, not filed here);
    `batch_regen` checks the fingerprint only and writes none; a pre-#176 table with a declaration is refused
    (zero-segment declaration is the escape); the live node loads the declaration at configure only.
11. **Future-proof for marine_tools#97.** A source-declared gain would be one more input on the same timeline; the
    loader/resolver expose `offsetDbAt(stamp_ns)` and nothing else, so a later source-side term composes by sum.
    Not implemented here.

## Declaration file format (`gain-state-declaration v1`)

A `#` header, one CSV table, UTF-8, no quoting (a field may not contain a comma, quote or newline; the loader
rejects them rather than half-parsing).

```
# format: gain-state-declaration v1
# reference_state: pre_step
# sonar: <free text>                            <- optional, informational, not hashed
# sign: offset_db is ADDED to the reported level to reach the reference state
segment,start_utc,end_utc,offset_db,provenance,provenance_note
gain_step,2026-01-02T03:04:05Z,2026-01-03T00:00:00Z,23.0,measured,<how the offset was obtained>
```

Rules (each one a rejecting load error naming file, line and reason):
- `# format:` is the first non-blank line, version exactly `v1`; `# reference_state:` present once, non-empty,
  `[A-Za-z0-9_.-]+`.
- Timestamps `YYYY-MM-DDTHH:MM:SS[.fff...]Z`, UTC only (a numeric offset or missing `Z` is refused), parsed to
  integer nanoseconds. `end_utc` is explicit and exclusive (so "end of 2026-08-28" is written
  `2026-08-29T00:00:00Z`); `start < end`.
- Sorted or not, segments must not overlap (`a.end > b.start` is an error; touching `a.end == b.start` is fine).
  Gaps are legal and mean the reference state.
- `offset_db` finite; `|offset_db| <= 60` (a typo guard); `0.0` allowed (an explicit "reference here" segment).
- `segment` ids unique, `[A-Za-z0-9_.-]+`. `provenance` one of the three words; `provenance_note` non-empty.
- Zero segments is legal (declares only the reference state) and loads with a warning.
- The loader returns the bytes it parsed and the canonical digest (decision 12), so the fingerprint records exactly
  what was applied.

## Approach

Four atomic commits, in this order, on `feature/issue-176` (the plan itself is commit 0).

**Commit 1: declaration schema + loader + fixture + tests**

- New `include/cube_bathymetry/gain_declaration.h`, `src/gain_declaration.cpp` (added to the core library in
  `CMakeLists.txt`, message-free like `backscatter_table.h`):
  - `struct GainSegment {std::string id; int64_t start_ns, end_ns; double offset_db; std::string provenance, note;}`
  - `struct GainDeclaration {std::string path, reference_state, sonar; std::vector<GainSegment> segments;
    std::vector<std::string> warnings; double offsetDbAt(int64_t stamp_ns) const; const GainSegment* segmentAt(...)
    const;}` (binary search over the sorted vector; `[start,end)`).
  - `GainDeclaration loadGainDeclaration(const std::string & path, std::string & error, std::string * bytes)`:
    same contract as `loadBackscatterTable` (whole-or-nothing, empty + error on failure, 64 KiB cap, regular file
    only, bytes returned).
  - `bool isGainDeclaration(const std::string & path)` (first-line probe, for a clear error when the wrong kind of
    file is passed to the option).
  - `bool parseUtcNs(const std::string &, int64_t &)` (own days-from-civil; no `timegm`/locale dependence, handles
    fractional seconds to ns).
- `test/data/gain_declaration_v1.csv` (the Appledore-shaped fixture, clearly labelled a test fixture) and
  `test/data/gain_declaration_vectors.json` (timestamp parse vectors + expected `offsetDbAt` results + a list of
  bad files with expected error substrings). Shared by the C++ and Python loaders so they cannot drift (the
  `mode_keys.json` pattern).
- `test/test_gain_declaration.cpp` (gtest, new target in `CMakeLists.txt`):
  `LoadsFixture`, `SignIsAdded` (reported + offset), `StepBoundaryHalfOpen` (start-1ns -> 0, start -> +23, end-1ns ->
  +23, end -> 0), `UndeclaredTimeIsReference`, `OverlapRejected`, `TouchingSegmentsAccepted`, `StartNotBeforeEndRejected`,
  `NonUtcStampRejected`, `MissingProvenanceRejected`, `UnknownProvenanceRejected`, `EmptyNoteRejected`,
  `DuplicateSegmentIdRejected`, `OffsetNotFiniteOrHugeRejected`, `FractionalSecondsToNs`, `QuotedFieldRejected`,
  `ZeroSegmentsLoadsWithWarning`, `BytesAreTheParsedBuffer`, `NonRegularFileRefused`, `SharedVectors`.

**Commit 2: resolver + `import_bag` + `batch_regen` + live node apply it (+ report, tests)**

- `GainStateResolver` in `gain_declaration.{h,cpp}` (kept out of `BackscatterModeResolver`, whose history/window logic
  is about SonarInfo, not a static file): `double apply(int64_t ping_stamp_ns)` returns the offset and bumps
  `counts().pings`, `by_segment[id]`, `undeclared`; `std::string report()`: reference state, source path + provenance
  per segment, one line per segment `id [start, end) offset_db: N pings`, and `undeclared (at reference): N pings`;
  a segment with zero pings is printed as `0 pings` and also warned once ("declared but no ping fell in it: check
  the stamps"). A convenience `void shiftSoundings(std::vector<GeoSounding>&, double offset_db)` adds the offset to
  every finite `sounding.intensity`.
- `import_bag_main.cpp`: new option `--backscatter-gain-declaration <file>`; load before any bag opens (next to the
  table load at ~l.974-1040), print reference state + segments, `isGainDeclaration` probe, decision-8 curve/table
  consistency check (per-mode tables and single-curve CSVs). The shift is a separate call at the ping-stamping site (~l.2686), made whenever a declaration is loaded and NOT
  inside `if (mode_resolver)`; `stampCorrectionIndex` stays table-only and runs after it. Print
  `GainStateResolver::report()` next to the mode report; fail (exit 4 class, same as `resolvedNothing`) never: an
  all-undeclared run is legal, it is reported. Verify during implementation that the depth-adaptive pass and the
  fixed pass share this one stamping function (l.2689 is one call site; confirm there is no second).
- `batch_regen_main.cpp`: same option; apply in the per-ping loop (~l.1118-1152) before scatter to buckets (buckets
  hold the already-shifted intensity; the gather needs nothing). batch_regen refuses per-mode tables
  (`isAngularResponseTable`, ~l.937: no SonarInfo source), so its supported paths are `none` and a single-curve CSV;
  **table + declaration parity with `import_bag` is out of reach until batch_regen reads SonarInfo** (stated in the
  README). Decision 8's check applies to the single-curve CSV. **The fingerprint check and its matrix land in commit 4**, with the `gain_declaration` field they read (implementation
  deviation: commit 2 has no field to compare against yet). Fingerprint check on the `-o` store ONLY (not
  `--reference-store`, which only seeds the predicted depth surface), and only when `-o` already holds a store that
  batch_regen merges into (confirm in the implementation; if it always rebuilds fresh, the check fires only when a
  fingerprint is present). Outcomes (batch_regen writes no fingerprint):

  | given declaration | `-o` fingerprint | outcome |
  |---|---|---|
  | present | present, same reference + digest | proceed |
  | present | present, different | refuse, both values named |
  | present | present, `gain_declaration: null` | refuse (store built uncorrected) |
  | present | schema-2 (pre-#176) | refuse (its gain state is unknowable) |
  | present | no fingerprint file | proceed with a warning (nothing to compare) |
  | none | present with a declaration | refuse (would rebuild tiles uncorrected inside a corrected store) |
  | none | `null`, schema-2, or no file | proceed (schema-2 is NOT refused when no declaration is given) |

  Prints the same resolver report.
- `cube_bathymetry_node.hpp`: parameter `backscatter_gain_declaration` (string, default `""`), loaded in
  `on_configure` (a load error returns FAILURE like the bad-fallback path); `stampCloudMode` applies it by cloud
  stamp; the resolver and loaded declaration survive the reset service's `buildSession` (same rule as the mode
  resolver). The shift is hoisted out of the `!bs_resolver_` early return. The node writes no fingerprint, so a node
  resuming/priming from a store built under a different declaration is NOT detected (README + ADR addendum say so). Report is logged at deactivate/cleanup with the mode report; a ping in a +offset segment logs nothing
  per ping. The file is read at configure; a gain change that happens mid-run is entered by editing the file and
  reconfiguring (configure only, no hot reload; stated in the README).
- Implementation notes (commit 2): the shared curve/table check is `gainRecordProblem` in `gain_declaration.{h,cpp}`
  (decision 8; the digest comes from `gainDeclarationSha256` in `build_fingerprint.h`, so the core library needs no
  OpenSSL); `AngularResponseCurve` and `BackscatterTable` gained `gain_declaration_sha256` / `gain_reference_state`
  read from the `# gain_*` header lines (the table fields were planned for commit 3; they are needed by the
  import-time check, so they land here). The node's pingCallback calls `prepareCloudBackscatter` (gain shift, then
  `stampCloudMode`), which is also the test seam. batch_regen's `--bs-store` parity tests live in the new
  `test/test_batch_regen_gain.py` (pytest, both tools handed over by CMake); batch_regen opens its bag readers before
  the declaration is checked (it always did; the refusal is before any message is read).
- Tests: `test_gain_state_resolver` (gtest): `CountsPerSegmentAndUndeclared`, `ShiftAddsOffsetToFiniteIntensityOnly`,
  `UnresolvedModePingStillShifted`, `ReportNamesEmptySegment`. `test_import_bag_mode_table.py` gets new cases
  (extend, same synthetic two-mode bag with a table derived in-reference): `test_declared_step_restores_reference_level`
  (bag whose second half is generated 23 dB low, declaration covering it: corrected cells equal the undeclared
  baseline; same run without the declaration shows the 23 dB seam, proving the test can fail),
  `test_wrong_sign_would_double_the_step` (a declaration with `-23.0` over the same bag produces a ~46 dB seam; assert
  it, documenting why the sign is pinned), `test_declaration_without_table_shifts` (declaration only, no curve; and with `--backscatter-correction none`: the 23 dB seam disappears), `test_stamp_equal_to_start_ns_is_shifted` (a ping stamped exactly at a segment's `start_ns`, through the real import_bag path, is shifted; one 1 ns earlier is not), `test_single_curve_without_gain_header_refused`, `test_auto_with_declaration_warns`, `test_declaration_report_counts` (report lists per-segment and undeclared
  counts; a segment stamped a day late reads `0 pings` and warns), `test_table_without_declaration_hash_refused`,
  `test_hash_mismatch_refused`. `batch_regen` cases (`test_batch_regen.cpp` or a pytest, whichever has the CLI harness): bit-exact parity with `import_bag` on the same bag for the `none` and single-curve paths only, declaration on both; one test per row of the outcome table above (`FingerprintDeclarationMatrix_*`); `--reference-store` with a differing fingerprint does not refuse; a per-mode table is still refused. Node: `test_node.cpp` /
  `test_node_lifecycle.cpp` cases `GainDeclarationShiftsCloudIntensityWithoutTable` (no table, no resolver) and `GainDeclarationShiftsCloudIntensityAndSurvivesReset`.

**Commit 3: derive tool consumes it (+ tests)**

- `derive_angular_response.py`: `--gain-declaration <file>` (single-curve and `--by-mode`). New `load_gain_declaration(path)` implements the same format and
  the same checks as the C++ loader (shared vectors test). In `accumulate_by_mode`'s ping loop (~l.3084) and
  `ping_beams` (~l.1929) the segment offset for `stamp_ns(msg.header.stamp)` is added to each beam's intensity
  BEFORE binning, so the angle bins, range term, switch-offset events and `solve_offsets` all see reference-state
  data; the switch-adjacency offsets across the step boundary are thereby consistent.
- Single-curve output (`write_csv` / `build_header`) gets the same `# gain_*` header keys when a declaration is given; `--gain-declaration` is accepted in the single-curve mode as well as `--by-mode` (offset applied per ping before binning in `accumulate`/`bin_ping`).
- Table header gains `# gain_reference_state:` and `# gain_declaration_sha256:` (and `# gain_declaration_source:`
  informational), written only when a declaration was given; `mode_table_lines` adds a `# gain_segments:` summary
  line per segment with ping counts. The accumulation cache identity (`accumulation_cache_identity`) includes the
  declaration hash so a cache built without it is never reused with it.
- Per-segment ping counts and beams reported in the run summary and `mode_report_lines`; an undeclared-time count is
  printed beside it (decision 1's check, same as the C++ side). A declared segment with zero pings is a warning.
- Implementation notes (commit 3): `PingRecord` gained a `stamp_ns` slot (the single-curve path had no ping stamp);
  per-ping offsets are added in `ping_beams` / `bin_ping` (`offset_db` argument); a `GainTracker` mirrors the C++
  resolver and counts pings and beams per segment; the headers also carry `# gain_declaration_source:`, `# gain_sign:`,
  one `# gain_segment:` line per segment and `# gain_undeclared:`; `ACCUMULATION_VERSION` goes 1 -> 2 (the cache payload
  holds the tracker counters) and the cache identity carries `[reference_state, digest]`. The two end-to-end tests
  (derive across the step with the declaration, import with it; and the same table refused without it) live in
  `test_import_bag_mode_table.py`, where both tools meet.
- Make sure the C++ table loader tolerates the new `#` header lines (it ignores unknown `# key:` lines today per the
  README, but verify with a test: `test_backscatter_table.cpp::UnknownGainHeaderLinesIgnored`) and read the two
  keys into `BackscatterTable::gain_declaration_sha256 / gain_reference_state` for the decision-8 check.
- Tests (`test_derive_angular_response.py`): `test_gain_declaration_same_table_from_pre_only_and_pre_plus_post` (the
  issue's acceptance: synthetic bag pre-only vs the same bag with a second half 23 dB low and the declaration -> equal
  curve/range/offset columns within tolerance), `test_without_declaration_post_data_corrupts_the_table` (control),
  `test_wrong_sign_declaration_doubles_the_split`, `test_declaration_header_lines_in_table`,
  `test_single_curve_header_carries_gain_keys`, `test_cache_not_reused_across_declaration`,
  `test_shared_vectors_match_cpp` (reads `gain_declaration_vectors.json`), `test_overlap_rejected`.

**Commit 4: fingerprint + ADR addenda + docs**

- `build_fingerprint.h/cpp`: `kSchemaVersion` 2 -> 3. New `struct GainDeclaration {std::string reference_state,
  sha256, source;}` and `std::optional<GainDeclaration> gain_declaration` in `BuildFingerprint`; JSON key
  `"gain_declaration": {"reference_state": ..., "sha256": ..., "source": ...}` or `null`; `isStale` compares
  `reference_state` and `sha256` (not `source`) and treats absent vs present as stale (the generic comparison; batch_regen's matrix above is stricter in one cell and laxer in another by design, since it is not a staleness check). `fromJson` already returns a
  non-current `schema_version` as readable-but-stale without interpreting its fields, so every pre-#176 store reads
  stale with no extra code (decision 3); the existing test that pins "schema 2 file reads stale" is updated and a
  new one added. `import_bag` `writeFingerprint` passes the declaration's reference state and the hash of the bytes
  the loader returned (`cube::sha256Text`).
- Tests (`test_build_fingerprint.cpp`): `SchemaVersion2FileReadsStale`, `GainDeclarationRoundTrips`,
  `DifferentDeclarationHashIsStale`, `DifferentReferenceStateIsStale`, `SameDeclarationUnderAnotherPathNotStale`,
  `NullVsPresentIsStale`, `SourcePathNotCompared`, `CanonicalDigestIgnoresNotesAndComments`, `Schema2FileHasNoDeclaration`.
- ADR-0003 addendum (schema 3, canonical-form digest: `gain_declaration`, what it hashes, why the bump invalidates old stores and tables)
  and ADR-0007 addendum ("Gain-state declaration addendum (#176)": additive receiver-gain term applied to raw
  intensity before the mode table; reference state; sign; undeclared = reference; interplay with the mode-table
  consistency check; why not folded into `correction_idx`; source-side declaration via marine_tools#97 remains a
  checked input per the #172 addendum). Both stay in the existing ADR files (addenda, not new ADRs).
- Docs: package `README.md` (a "Gain-state declaration" section: the format, the sign, the Appledore example as
  prose with the file living in campaign config, the four consumers, and the explicit consequence that
  per-mode tables derived across the step must be re-derived with `--gain-declaration`), the options in the
  `import_bag`/`batch_regen`/derive usage text, and the node parameter. The parameter is documented in the package README only (`.agents/README.md` does not exist; creating it is a separate follow-up issue, not filed here). README also states: the live node writes no fingerprint (resume under another declaration undetected), batch_regen has no table+declaration support, the zero-segment escape hatch. The README format section carries no Appledore text (fixture only).

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/include/cube_bathymetry/gain_declaration.h`, `src/gain_declaration.cpp` | New: types, loader, `offsetDbAt`, `GainStateResolver`, report |
| `cube_bathymetry/CMakeLists.txt` | Add sources and the new gtest targets |
| `cube_bathymetry/src/import_bag_main.cpp` | `--backscatter-gain-declaration`; load, curve/table consistency check, unconditional shift at the ping site, report, fingerprint input |
| `cube_bathymetry/src/batch_regen_main.cpp` | Same option; shift in the per-ping loop; fingerprint mismatch refusal; report |
| `cube_bathymetry/src/cube_bathymetry_node.hpp` | `backscatter_gain_declaration` param; shift in `stampCloudMode`; survive reset; report |
| `cube_bathymetry/include/cube_bathymetry/backscatter_table.h`, `src/backscatter_table.cpp` | Read `gain_*` header lines into the table struct |
| `cube_bathymetry/include/cube_bathymetry/build_fingerprint.h`, `src/build_fingerprint.cpp` | Schema 3, `gain_declaration` key, `isStale` |
| `cube_bathymetry/scripts/derive_angular_response.py` | `--gain-declaration`, loader, per-ping offset before the fit, header lines, cache identity |
| `cube_bathymetry/test/data/gain_declaration_v1.csv`, `gain_declaration_vectors.json` | New shared fixture + vectors |
| `cube_bathymetry/test/test_gain_declaration.cpp` | New (loader + resolver tests) |
| `cube_bathymetry/test/test_import_bag_mode_table.py`, `test_derive_angular_response.py`, `test_build_fingerprint.cpp`, `test_backscatter_table.cpp`, `test_batch_regen.cpp`, `test_node*.cpp` | New cases listed above |
| `cube_bathymetry/docs/decisions/0003-staleness-fingerprint.md`, `0007-*.md` | Addenda |
| `README.md` (package README) | Format, options, node parameter, known gaps |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Fingerprint schema 2 -> 3 | Every existing store reads stale; `isStale` tests that pin schema 2; ADR-0003 text; anything that writes schema 2 | Yes (commit 4) |
| Intensity shifted before the correction | `countBeam` accounting sees shifted values; recon spill / `ScatterRecord` hold already-shifted intensity (no second application; no format change) | Yes (commit 2, parity test) |
| Per-mode tables derived across the step (candidate D inputs) | Must be re-derived with `--gain-declaration`; decision 8 refuses a table/declaration mismatch; fingerprint flags the stores | Yes; stated in README + ADR addenda |
| Derive `--by-mode` | Accumulation cache identity includes the declaration hash; table header lines; C++ loader reads them | Yes (commit 3) |
| `batch_regen` gains the option | Fingerprint read-only check against the `-o` store only (outcome matrix); it still writes no fingerprint | Yes; writing one stays the ADR-0003 follow-up |
| Live node | New parameter; configure-time load; reset keeps it; no hot reload | Yes |
| Appledore declaration file | Lives in unh_echoboats_project11 / archive, not here | No - follow-up for the campaign repo (operator) |
| marine_tools#97 source-side gain | Would add an input on the same timeline; not built here | No - out of scope |
| Re-importing Appledore with the new tooling | Re-derive the table, re-run `import_bag` (store goes stale by fingerprint) | No - operator action after merge |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Test what breaks | Known-sign test, boundary-ping tests, wrong-sign control, "pre-only equals pre+post with the declaration", report-count test for a mistyped stamp, fixtures shared C++/Python |
| Robustness on open water | Loader is whole-or-nothing and rejects ambiguity (overlap, non-UTC, quoting); a mismatch between table and declaration is a refusal, not a warning |
| Capture decisions | Operator decisions recorded above; ADR addenda land in the same PR (commit 4) |
| Enforcement over documentation | The fingerprint and the table-consistency refusal enforce "re-derive across the step"; the README does not rely on memory |
| Prefer general over instance | No Appledore constant in code; reference state is a label; the file is campaign config |
| Defaults that limit visibility are decisions | Undeclared = reference is the stated default and is counted and printed every run |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0003 staleness fingerprint | Yes | Schema 3 + `gain_declaration`; addendum |
| ADR-0007 backscatter store (incl. #172 addendum) | Yes | New addendum; parity with `import_bag` for `batch_regen`; declared inputs stay checked, not silently used |
| ADR-0001, 0002, 0008 | No | Tiling/eviction/geometry untouched |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): README option lists for `import_bag`, `batch_regen`, derive `--by-mode`,
  the node parameter, the table-format header lines; ADR-0003 and ADR-0007 addenda; the `.agents/README.md`
  verified-parameter table entry (location open, see Open Questions).
- **Agent-instruction candidates** (proposals only): "a table's `# gain_declaration_sha256` must equal the
  declaration used at import" as a pitfall line in the project agent guide; the rule that an unrecorded sonar
  state change is handled by a declaration, never by splitting data, as a backscatter-processing note.

## Open Questions

None blocking; the review questions are answered (decision 13). Follow-up (not filed): create `.agents/README.md`
for this repo from the workspace template. To confirm in implementation: whether batch_regen merges into an existing
`-o` store (affects only how often its fingerprint check can fire).

## Estimated Scope

Single PR, four atomic commits (loader, consumers, derive tool, fingerprint + docs). Largest risk is commit 2's
three stamping sites; the `import_bag`/`batch_regen` bit-exact parity test is the guard.
