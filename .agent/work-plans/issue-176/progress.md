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
