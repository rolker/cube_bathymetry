---
issue: 78
---

# Issue #78 — Live SonarVisualizationTile producer + backscatter (intensity+beam_angle) wiring

## Plan Authored
**Status**: complete
**When**: 2026-06-27 18:35 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

**Plan**: `.agent/work-plans/issue-78/plan.md` at `04f4a23`
**Branch**: feature/issue-78 at `04f4a23`
**Phases**: single PR (internal order: beam-angle+estimator correction → quantized publisher + catalog/request → sim-verify)

Key design point: the beam-angle backscatter correction lives **once** in the
shared CUBE estimator (`node.cpp`), which both the live node and the offline
import (`store_import.cpp`) run — so cube#80 inherits identical backscatter with
no separate helper. This PR owns landing it.

### Open questions
- [ ] Backscatter uint8 quantization range: fixed M3 reflectivity-dB range vs per-tile auto-range (recommend per-tile, per ADR-0008).
- [ ] udp_bridge#19 metering not yet landed — ship v1 without priority classes, add when #19 lands (recommend yes).
