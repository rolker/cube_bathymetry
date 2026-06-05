---
issue: 34
---

# Issue #34 — bag_to_geotiff: consume the cloud per-sounding TPU

## Integrated Review
**Status**: complete
**When**: 2026-06-05 03:30 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #35 at `faee084`
**Sources**: 1 (Copilot R1 @ `c4ad5e4`)
**Cross-source confirmations**: 0
**CI**: build-and-test success; copilot-reviewer success

### Findings
- [x] (valid, Copilot R1) has_tpu set from vertical_uncertainty alone but horizontal iterator built unconditionally -> uncaught throw on a one-field cloud; now requires both fields -- `src/bag_to_geotiff.cpp`

### False positives
- (Copilot R1) possible past-end TPU iterator read -- a valid PointCloud2's per-point fields share one point_step/data length, so all field iterators have identical count and advance in lockstep; cannot desync.
