---
issue: 176
---

# Issue #176 — Declared sonar gain-state segments for backscatter import and table derivation (unrecorded receiver-gain change mid-survey)


## Issue Review
**Status**: complete
**When**: 2026-11-09 13:40 -05:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #176 -- Declared sonar gain-state segments for backscatter import and table derivation
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

Operator-settled facts taken as given: step +23.0 dB (pre minus post) from 2026-08-26T20:17:30Z to the end of 2026-08-28 (528 cells); reference = pre-step season state; declaration is campaign configuration (not cube code); import resolver and derive tool both consume it; source-side prevention is marine_tools#97 (out of scope). These close the two open items in the issue body (reference level, where the declaration lives).

### Findings
- Right repo: yes. The consumers (resolver in `backscatter_mode.cpp`, `import_bag`, `derive_angular_response.py --by-mode`, fingerprint) are all cube_bathymetry. The Appledore declaration file itself must NOT be committed here; cube ships schema + loader + a test fixture only.
- Size: one PR is feasible but touches four surfaces (declaration schema/loader, resolver + live node + import_bag, derive tool, fingerprint). Plan-task should order it as atomic commits in that sequence.
- Builds on #172 / PR #175 (merged per host); depends on nothing else open. marine_tools#97 would later supply the same state from the source; the design should leave room for a source-declared state to be one more input on the same timeline (ADR-0007 #172 addendum already says a SonarInfo declaration is "checked against the table, not used by it").

### Actions
- [ ] (spec gap) Pin the sign convention in the schema and docs: the issue says "offset relative to a named reference state" and the host says "pre minus post = +23". State explicitly that the value is the dB ADDED to the reported level to bring it to the reference state (post-step segment = +23.0, pre-step = 0), and test it with a known-sign case. A flipped sign here amplifies the split, exactly the #172 candidate-D failure.
- [ ] (spec gap) Define segment semantics: half-open [start, end) intervals, UTC stamps, overlap = hard error, gap handling, and what "end of 2026-08-28" means (an explicit end stamp, not open-ended). Decide whether the identity/exclude fallback for pings outside every segment is the existing mode-table fallback knob or a separate one; reusing it silently couples two unrelated policies. For the reference-state-is-default case, an undeclared ping is arguably in the reference state, so the default and its summary line should say which.
- [ ] (spec gap) Define "never fitted across a gain step" in the derive tool: preferred is applying each segment's offset per ping before the fit (all data used, one table in the reference state); the alternative (split and fit per segment) yields tables in different states. Pick one and say so; the former matches the declaration being additive.
- [ ] (decision to record) Add an ADR-0007 addendum (gain-state segments; additive term before the per-mode table) and bump/extend the ADR-0003 fingerprint (new field: declaration hash + reference-state name, next to the table hash; decide schema_version bump so existing stores are flagged stale, which is also required because tables derived across the step are invalid).
- [ ] (consequence) Existing per-mode tables derived across the step (candidate D inputs) must be re-derived; the PR should state this and the fingerprint should force it, not rely on the operator remembering.
- [ ] (consequence) Decide whether `batch_regen` consumes the declaration; ADR-0007 notes bit-exact regeneration vs `import_bag` requires matching the effective correction explicitly, so a store rebuilt by batch_regen without it would silently differ.
- [ ] (consequence) New parameter/CLI option for the declaration path: update the `.agents/README.md` verified-parameter table and the package README in the same PR; add the provenance field values (settings file | measured | operator-reported) to the schema validation.
- [ ] (test) Targeted tests: step boundary ping on either side, overlap/gap rejection, fingerprint changes with the declaration, derive tool produces the same table from pre-only data and from pre+post data with the declaration, resolver summary reports uncovered pings.
- [ ] (data) The 20:17:30 stamp is a measured boundary; record its provenance as measured from overlapping lines (528 cells, 20us/50us CW +23.0, 1ms FM +22.7 -- the 0.3 dB FM difference is within the quoted spread, but note it rather than claiming a mode-dependent gain).

## Plan Authored
**Status**: complete
**When**: 2026-11-09 14:30 -05:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-176/plan.md` at `b5d8ddb`
**Branch**: feature/issue-176 at `b5d8ddb` (local, not pushed)
**Phases**: single (four atomic commits)

### Open questions
- [ ] `.agents/README.md` does not exist in this repo: create it here or document the parameter in the package README only?
- [ ] batch_regen: check-only against the -o / --reference-store fingerprint, or also write one?
- [ ] Refuse a pre-#176 table whenever a declaration is supplied (recommended), or warn?
- [ ] Live node: reload the declaration on the reset service, or configure only?
- [ ] Confirm provenance vocabulary: settings_file | measured | operator_reported (+ mandatory note).


## Plan Review
**Status**: complete
**When**: 2026-11-09 13:58 -05:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-176/plan.md` at `b5d8ddb`
**PR**: PR-less
**Verdict**: changes-requested

Dispatched fresh-context reviewer (handoff header present). Core mechanism verified sound: the table correction is `raw - curve(|theta|) - r_g(log10 R/R0) - offset` (`backscatter_table.h:47`), so an additive dB shift of raw `Sounding::intensity` commutes with every table term, and shifting before the table is correct provided the table was fitted from shifted data (commit 3 does that). The shift is baked in before the recon spill / `ScatterRecord` (single call site `import_bag_main.cpp:2689`, `recon->add` after it), so no double application there. Operator decisions 1-5 honoured; no drift found.

### Findings
- [ ] (must-fix) Decision 7 ("applies with `--backscatter-correction none`, to unresolved-mode pings") contradicts the actual gating: `import_bag_main.cpp:2686` stamps only `if (mode_resolver)` and the node's `stampCloudMode` returns early on `!bs_resolver_ || soundings.empty()` (`cube_bathymetry_node.hpp:1018-1023`). A declaration with no table (or a single curve) would silently do nothing. Plan must say the shift is hoisted out of both guards (own function, called unconditionally when a declaration is loaded), and add tests for declaration-only (no table) on import_bag and node — `plan.md:53`, `:23`
- [ ] (must-fix) `batch_regen` cannot take a per-mode table at all (`batch_regen_main.cpp` ~l.937 refuses `isAngularResponseTable`; it only applies a single-curve CSV). The plan's "bit-exact parity with `import_bag` on the same bag" and "batch_regen consumes the declaration" must be restated for the single-curve / `none` path only (parity test = same bag, same single curve or `none`, declaration on both). Say explicitly that table+declaration parity is out of reach until batch_regen reads SonarInfo — `plan.md:39,170-171,271`
- [ ] (must-fix) Decision 8's table-vs-declaration consistency check covers only per-mode tables. A single-curve CSV (`loadAngularResponseCurveWithHeader`, used by batch_regen and import_bag) fitted across the step is the same failure with no guard. Either carry the two `# gain_*` header keys in single-curve files too and check them in both tools, or state the gap and emit a loud warning when a declaration is combined with a curve that records none — `plan.md:57-63`
- [ ] (must-fix) Define the fingerprint comparison table completely. `fromJson` returns a bare readable-but-stale object for schema != current, so for a schema-2 file `gain_declaration` reads as "absent". The plan's batch_regen rule "absent vs present counts as different" therefore refuses a declaration against every pre-#176 store (intended?) and, with no declaration, must NOT refuse against a schema-2 store. Also the no-fingerprint-file case (batch_regen into a store with none) is unspecified: pass silently or warn? State all four cells (declaration x {null, present, schema-2, no file}) and test them — `plan.md:151-153,203-207`
- [ ] (must-fix) `--reference-store` should not be in the batch_regen gain check. It only seeds the CUBE predicted depth surface (`batch_regen_main.cpp:94`, `:1013`); backscatter gain is irrelevant to it and refusing on it is a false refusal. Check the `-o` store only (and only if batch_regen actually merges into an existing `-o` store; confirm in the incremental path) — `plan.md:151,249,289`
- [ ] (suggestion) Staleness semantics: hashing the raw file bytes means editing a `provenance_note`, a comment, whitespace, or appending a segment for an unrelated date marks every store and every derived table stale and forces a re-derive (decision 8 compares the same hash). Prefer a canonical hash over semantic content only (format version, reference_state, and per-segment start_ns/end_ns/offset_db in sorted order; exclude notes, ids, `sonar`, comments) and say so in ADR-0003. Keep strict equality (no "covers the data" reasoning) — `plan.md:41,57-63,203-207`
- [ ] (suggestion) Zero-segment declaration is the natural escape hatch for decision 8's "refuse a pre-#176 table with a declaration": document it (a season that never crossed a step re-derives with an empty declaration, or imports with no declaration). Also decide whether a zero-segment declaration still changes the fingerprint (it does, via reference label) and say that is intended — `plan.md:61,125`
- [ ] (suggestion) Node resume mixing: a store primed/resumed by the live node (`~/reset`, prior-store prime) holds already-shifted sums; the node writes no fingerprint, so starting a node with a different declaration than the one that built the store it resumes from is undetected. Not double application, but note it in the README/ADR addendum as a known unguarded case — `plan.md:155-160`
- [ ] (suggestion) Format/vocabulary genericity is good (reference label, no Appledore constant in code). Trim the example header: `# sonar:` is informational and the example line "Kongsberg M3 (Appledore 2026)" / M3 numbers belong only in the fixture, not the README format section. Keep `provenance` three words + mandatory note — `plan.md:92-97,112`
- [ ] (suggestion) Sign-pinning coverage is good (C++ unit, import_bag wrong-sign control, derive wrong-sign control). Add one test where the shifted segment edge falls mid-bag at a ping stamp exactly equal to `start_ns` through the real import_bag path (not just the loader), since the half-open boundary is where a stamp-source mismatch (header stamp vs `ping_ns`) would show — `plan.md:164-170`
- [ ] (suggestion) `countBeam` must see the shifted intensity only for NaN gating; confirm the shift preserves NaN and that `tableBeamStatus` does not depend on intensity magnitude (it uses angle/range per `backscatter_mode.cpp:455`), so ordering is harmless — note, not a defect.

### Open questions
- [ ] Q1 `.agents/README.md` absent: agree with package README now, guide creation as its own issue (no verified-parameter table exists to edit; don't expand scope).
- [ ] Q2 batch_regen check-only: agree; it writes no fingerprint today and ADR-0003's consumer is unimplemented (fix must-fix 4/5 first).
- [ ] Q3 Refuse pre-#176 table when a declaration is supplied: agree (matches the Appledore candidate-D case); document the zero-segment declaration escape.
- [ ] Q4 Node: configure-only, no hot reload: agree (a mid-run edit would split one store across two declarations undetected; reconfigure is the explicit act).
- [ ] Q5 Provenance vocabulary `settings_file | measured | operator_reported` + mandatory note: confirm.
