---
issue: 44
---

# Issue #44 — Chart prior importer: contour depth-below-surface → ellipsoidal Chart tiles (A2 of unh_marine_autonomy#163)

## Plan Authored
**Status**: complete
**When**: 2026-06-15 07:21 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-44/plan.md` at `03dbd69`
**PR**: https://github.com/rolker/cube_bathymetry/pull/45 (`[PLAN]` prefix)
**Phases**: cube_bathymetry importer PR + one small sibling mru_transform datum-config change

### Open questions
- [ ] Where `datum_polygons.massabesic.yaml` lives + who ships it (recommend mru_transform/config, sibling PR; importer takes --datum-config).
- [ ] Massabesic polygon ring coordinates — need a coarse lake outline for the entry.
