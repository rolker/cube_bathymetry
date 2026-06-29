---
issue: 92
---

# Issue #92 — Bound resident-tile RAM in the offline importer (persist-then-drop eviction)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-29 08:53 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-92 at `65dfd9f`
**Mode**: pre-push
**Depth**: Deep (reason: 1502/-319 over 17 files; cross-cutting resource-management + numerical + binary-serialization)
**Must-fix**: 3 | **Suggestions**: 2
**Round**: 1 | **Ship**: continue — three must-fixes, all mechanical doc/comment/ADR reconciliation (no algorithmic defect); fast convergence expected after addressing.

### Findings
- [x] (must-fix) `extractNodeRecord` docstring still says intensity is "emitted UNCORRECTED" and "per-beam raw set is retained so it is re-derivable when #15" — both false after the record-time correction + Welford change — `cube_bathymetry/include/cube_bathymetry/node.h:195-204`
- [x] (must-fix) ADR-0007 (and ADR-0001's re-derivability notes) document raw `{raw_intensity, beam_angle}` sufficient-statistic retention + correct-at-extract for cube#15 re-derivability; this PR reverses that decision (record-time correction, O(1) Welford, raw samples discarded) without updating/superseding the ADR — `cube_bathymetry/docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md`
- [ ] (must-fix) Reload-failure path silently drops this batch's soundings on the failed tile and the comment claims it "retries on the next revisit" (only *future* batches retry; these beams are lost permanently offline) — contradicts the headline "lossless" guarantee; fix the comment and log the dropped count (buffer+re-add optional) — `cube_bathymetry/src/store_import.cpp:1503-1535` (cross-confirmed by both adversarial passes)
- [ ] (suggestion) `Node::setSettledIntensityWelford` writes to `chooseHypothesis()` without the `number_of_samples > 0` gate its siblings (`chosenIntensityWelford`, `extractNodeRecord`) use — harmless today, add for symmetry/robustness — `cube_bathymetry/src/node.cpp:354-362`
- [ ] (suggestion) Scratch-dir name `cube_import_spill_<steady_clock ns>_<per-process atomic counter>` is not collision-safe across concurrent `import_bag` processes (counter resets per process; ns is the only cross-process distinguisher) → potential silent spill cross-corruption; add `getpid()`/`mkdtemp` as the test helper already does — `cube_bathymetry/src/store_import.cpp:1291-1300`
