---
issue: 135
---

# Issue #135 — Live draft tile writes are not atomic — costmap loadWindow can hit a truncated tile when sharing the depths store (D8 unification)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-11-12 09:19 -05:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: approved

**Branch**: feature/issue-135 at `1d6a890`
**Mode**: pre-push
**Depth**: Standard (reason: 554 changed lines, persistence/lifecycle path of the live node)
**Must-fix**: 0 open (3 raised, all addressed) | **Suggestions**: 3 open
**Round**: 1 | **Ship**: recommended — every must-fix was raised independently by both Claude lenses, Gemini and Codex, fixed with tests, and the full suite is green

Reviewers: Claude Adversarial Lens A + Lens B, Cross-Model (gemini + codex). Static analysis = the package's cpplint/uncrustify/flake8 run (clean).

### Findings
- [x] (must-fix) unlistable draft/ dir was reported as a tile named "draft", leaving saves enabled over unloaded tiles — fixed: loader throws, node blocks all saves — `store_import.cpp` loadStoreSkippingUnreadable
- [x] (must-fix) throwing is_regular_file overload aborted salvage on a stat error — fixed with ec overloads — `store_import.cpp`
- [x] (must-fix) refused tiles were marked clean, so eviction/cleanup could drop their soundings — fixed: they stay dirty, saving resumes when the damaged file is removed — `cube_bathymetry_node.hpp` saveDirtyTiles
- [x] (suggestion) operator message said repair-to-resume; reworded — `cube_bathymetry_node.hpp`
- [x] (suggestion) draft-dir exists() error now blocks saves — `cube_bathymetry_node.hpp`
- [ ] (suggestion) offline paths still write non-atomically (marine_bathymetry_store::save in the draft-clearing step, count_grid scratch tiles); same remedy as unh_marine_autonomy#256 — `store_import.cpp`
- [ ] (suggestion) fallback after a failed load() imports only processed/ and draft/; a failure confined to reference/ or chart/ yields no diagnostic — `store_import.cpp`
- [ ] (suggestion) temp name <path>.partial is not unique per writer; one writer per tile is assumed (commented) — `store_import.cpp`
