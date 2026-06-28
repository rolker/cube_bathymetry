---
issue: 85
---

# Issue #85 — default import_bag bathy output to Processed layer + --bathy-layer flag

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-28 15:51 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-85 at `e937d40`
**Mode**: pre-push
**Depth**: Light (reason: 34 lines, 1 file, no override-trigger files, no Deep triggers)
**Must-fix**: 1 | **Suggestions**: 1
**Round**: 1 | **Ship**: continue — lone must-fix is a one-line stale-comment correction; no design/correctness concerns, re-review should clear

### Findings
- [ ] (must-fix) Backscatter-block comment still says "The bathy layer above is Draft" but bathy now defaults to Processed — stale/self-contradicting after this diff — `cube_bathymetry/src/import_bag_main.cpp:761`
- [ ] (suggestion) "Saved … layer" ternary labels any non-Processed layer as "draft"; fragile if Chart ever becomes selectable — `cube_bathymetry/src/import_bag_main.cpp:752`
