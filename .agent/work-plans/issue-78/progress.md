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

## Plan Review
**Status**: complete
**When**: 2026-06-27 22:33 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-78/plan.md` at `04f4a23`
**PR**: PR-less (--issue mode, feature/issue-78)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) Drop approach #1 (the node.cpp backscatter "correction"). It contradicts a deliberate, documented deferral and isn't asked for by #78 — `plan.md:28`
- [ ] (suggestion) Confirm `marine_interfaces` (the 3 msgs) + `marine_tiled_raster_store` (TileCatalogBuilder) are available to build against — neither is in this checkout — `plan.md:64`
- [ ] (suggestion) The "intensity-aware extract sibling" already exists as `extractNodeRecord` (node.cpp:270+); reuse it — the real work is plumbing its value into the tile band — `plan.md:52`
- [ ] (suggestion) Specify QoS for the new `SonarVisualizationTile`/`TileCatalog` publishers and `TileRequest` service (catalog likely transient_local for late joiners) — `plan.md:44`
- [ ] (suggestion) Per-tile auto-range quantization needs per-band scale/offset carried in the message; confirm `SonarVisualizationTile` provides them — `plan.md:94`

### Detail — must-fix #1

`node.cpp:298-322` deliberately keeps the backscatter extract as the **identity
(flat-geometry) correction** (`corrected = sample.raw_intensity`) with an explicit
`TODO(#54-B / cube_bathymetry#15)`: the GeoCoder incidence/Lambert correction is
deferred until the predicted-surface/slope producer (cube#15/#59) is wired, and the
per-beam `{raw_intensity, grazing_angle}` samples are retained precisely so the value
is "fully re-derivable when #15 provides slope -- no information is lost by deferring."
`sounding.h:114-118` adds a hard gate: *"SIGN-CONVENTION VERIFICATION REQUIRED
(cube#15)... A sign error would bias the incidence correction and corrupt the settled
backscatter."*

Issue #78 asks to **surface** the already-co-estimated backscatter into the live tile
(transport-not-storage), not to change the co-estimation math. The plan's "first-cut
beam-angle-vs-nadir correction" applies an unverified `rx_angle`-based correction now,
in the shared estimator that `store_import` also runs — so by the plan's own framing it
would write a corrupted-if-sign-wrong value into the durable/offline path (cube#80). The
"live==offline / no separate helper" rationale only argues *where* a correction would go;
it doesn't justify doing it inside a transport PR ahead of its #15/#59 prerequisite. If a
first-cut correction is genuinely wanted, it belongs in its own issue gated on the
sign-convention check, with `node.cpp` untouched by #78.

Note: review-issue was not run for #78 (optional; not penalized).
