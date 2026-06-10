---
issue: 31
---

# Issue #31 — Design: single pose source (TF) for the cube data flow; slim Platform; ellipsoid grid

## Integrated Review
**Status**: complete
**When**: 2026-06-10 14:05 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #37 at `86d9f23` (docs: document single-pose-source data flow + per-platform frame config)
**Sources**: 1 (Copilot R2 @ `86d9f23`; R1 @ same SHA errored out)
**Cross-source confirmations**: 0 (no local review timeline for #31)
**CI**: all pass (build-and-test success; copilot-reviewer success)

### Findings
- [ ] (doc-accuracy, Copilot R2) "silently produces useless output" overstates — `detections_to_pointcloud` emits throttled WARNs on missing attitude TF, and (after PR #38) `cube_bathymetry_node` warns on dropped soundings + logs a grid heartbeat. Reword to drop "silently" — `README.md:65`
- [ ] (doc-accuracy, Copilot R2) `odom` remap presented as unconditional — it is needed because the node runs in a sensor **sub**-namespace (`sensors/mbes`, `sensors/m3`), where a relative `odom` resolves to `/<ns>/sensors/<sonar>/odom`, not `/<ns>/odom`. Clarify the *why* rather than implying namespacing the node alone never suffices — `README.md:74`
- [ ] (doc-accuracy, Copilot R2) failure-mode conflates two symptoms — "grid publishes 0 messages" is the *placement-TF-fails / no soundings added* case (#36 on the boat: `gridBounds` stays NaN → `publishGrid` early-returns). The NaN-uncertainty-poisoning case instead publishes a `GridMap` whose cells are all-NaN/empty. Separate the two — `README.md:93`

### False positives
- (none — all three are legitimate accuracy improvements; verified against the cube source and the offline bag-replay findings)
