---
issue: 52
---

# Issue #52 — Preserve backscatter: carry SonarDetections intensity into the soundings point cloud

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-20 17:47 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-52 at `3e89ec0`
**Mode**: pre-push
**Depth**: Light (reason: small, single-field passthrough; consumers read by field name)
**Must-fix**: 0 | **Suggestions**: 0

Static analysis (ament_cpplint + ament_uncrustify) clean. One Claude adversarial pass:
clean — verified field/offset/point_step bookkeeping (24, stride 6), the intensity
population path through ErrorModel::compute, and that both consumers
(cube_bathymetry_node, bag_to_geotiff) read strictly by field name so the reorder is
safe. M3 source verified to populate intensities (kongsberg_em_bridge reflectivity_db).
247 tests, 0 failures (+2 new: CarriesPerBeamIntensity, IntensityIsNaNWhenAbsent).

### Findings
- [ ] No issues found. LGTM.
