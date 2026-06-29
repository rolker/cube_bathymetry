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
- [x] (must-fix) Reload-failure path silently drops this batch's soundings on the failed tile and the comment claims it "retries on the next revisit" (only *future* batches retry; these beams are lost permanently offline) — contradicts the headline "lossless" guarantee; fix the comment and log the dropped count (buffer+re-add optional) — `cube_bathymetry/src/store_import.cpp:1503-1535` (cross-confirmed by both adversarial passes)
- [x] (suggestion) `Node::setSettledIntensityWelford` writes to `chooseHypothesis()` without the `number_of_samples > 0` gate its siblings (`chosenIntensityWelford`, `extractNodeRecord`) use — harmless today, add for symmetry/robustness — `cube_bathymetry/src/node.cpp:354-362`
- [x] (suggestion) Scratch-dir name `cube_import_spill_<steady_clock ns>_<per-process atomic counter>` is not collision-safe across concurrent `import_bag` processes (counter resets per process; ns is the only cross-process distinguisher) → potential silent spill cross-corruption; add `getpid()`/`mkdtemp` as the test helper already does — `cube_bathymetry/src/store_import.cpp:1291-1300`

## Implementation
**Status**: complete
**When**: 2026-06-29 09:15 +0000
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-92 at `6b9a7d7`
**Addressed**: Local Review (Pre-Push) — When 2026-06-29 08:53 +0000, at `65dfd9f` (Verdict: changes-requested, 3 must-fix + 2 suggestions)
**Commits**: `31ced91`, `4b4caff`, `50c8b56`, `5334319`, `6b9a7d7`

### Actions
- [x] (must-fix) `extractNodeRecord` docstring no longer claims "emitted UNCORRECTED" / raw-set retained — rewritten for record-time correction + streaming Welford; the matching `intensity`-field docstring (node.h:56-59) corrected too — `cube_bathymetry/include/cube_bathymetry/node.h` (`31ced91`)
- [x] (must-fix) ADR reconciled — added a "Phase B.2 addendum (cube#92/#93)" to ADR-0007 documenting the record-time-correction + Welford reversal and the lost in-place re-derivability; marked the now-false Context/Consequences/Tier-2 claims superseded inline, and updated ADR-0001's backscatter re-derivability trade-off note — `cube_bathymetry/docs/decisions/0007-…md`, `0001-…md` (`4b4caff`)
- [x] (must-fix) Reload-failure path no longer mislabels the loss — `addBatch` now counts soundings centred in un-reloadable tiles and emits a `WARNING` before dropping; the "retries on the next revisit" comments/cerr reworded to state this batch's soundings are not re-processed offline; added `GeoMapSheet::gridIndexForSounding` to support the count — `cube_bathymetry/src/store_import.cpp`, `geo_map_sheet.{h,cpp}` (`50c8b56`)
- [x] (suggestion) `setSettledIntensityWelford` now applies the `number_of_samples > 0` gate its siblings use — `cube_bathymetry/src/node.cpp:354-362` (`5334319`)
- [x] (suggestion) Spill scratch dir now created with `mkdtemp` (atomic, retries on collision) — collision-safe across concurrent `import_bag` processes; dropped the steady-clock-ns + per-process counter naming and the now-unused `<atomic>` include — `cube_bathymetry/src/store_import.cpp:1291-1300` (`6b9a7d7`)

**Quick checks**: `ament_cpplint` + `ament_uncrustify` clean on all five touched C++/header files. A full `colcon build` was **not** possible in this worktree — the lower layers (`underlay_ws`/`core_ws`/… providing `marine_autonomy`/`gggs.h`) have no install trees here, so the package cannot be configured. Changes are mechanical (doc/comment + a gate + an additive helper + a `mkdtemp` swap) and were reviewed by hand against current source.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 92 --skill review-code
