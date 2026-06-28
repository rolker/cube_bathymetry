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

## Plan Revised
**Status**: complete
**When**: 2026-06-27 18:55 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

Container review-plan verdict **changes-requested**; folded in:
- [x] **(must-fix)** Dropped the `node.cpp` backscatter "correction". #78 now **surfaces** the existing co-estimated value via `extractNodeRecord` (node.cpp:270) — `node.cpp` untouched. The angle-correction is deliberately deferred (sign-convention gate in `sounding.h`, cube#15) and would corrupt the offline path (`store_import` runs the same estimator). → becomes a separate sign-gated issue; **cube#80's "first-cut beam-angle" framing must defer too** (Open Question).
- [x] (suggestion) Reuse existing `extractNodeRecord` rather than a new "intensity-aware extract".
- [x] (suggestion) Build deps: `marine_interfaces` + `marine_tiled_raster_store` (merged #230) must be built in `core_ws`; noted.
- [x] (suggestion) QoS specified — catalog `transient_local`, push best-effort, request reliable.
- [x] (suggestion) Per-tile auto-range carried via `VisualizationBand` scale/offset (confirmed present).

### Open questions
- [ ] File the separate sign-gated angle-correction issue + adjust cube#80's framing? (ripple from must-fix #1)
- [ ] Backscatter uint8 range: per-tile auto-range (recommend).
- [ ] udp_bridge#19 metering: ship v1 without, add later (recommend).

## Implementation (in progress)
**Status**: 2 of 3 parts done (committed, tested); node endpoints + sim-verify remain
**When**: 2026-06-27 19:45 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

**Branch**: feature/issue-78 (off jazzy `07260a2`)

Done (committed, 366 tests pass, uncrustify clean):
- `8d430b6` — **backscatter into the live estimator**: beam_angle cloud field; pingCallback reads intensity+beam_angle (optional/robust for old bags). node.cpp untouched.
- `8a141a2` — **tile quantization core**: `GeoGrid::nodeRecords()` (row-major NodeRecord) + `quantizeTile()` (depth int16/uncertainty uint8/backscatter uint8 per-tile auto-range) + test_quantize_tile (2 tests). Own lib target.

Underlay rebuilt with merged #230 (marine_interfaces + marine_tiled_raster_store).

Remaining:
- [ ] Node endpoints (cube_bathymetry_node.cpp): `SonarVisualizationTile` publisher (call `quantizeTile` per dirty tile in the publish path), `TileCatalog` publisher (via `TileCatalogBuilder`, transient_local), `TileRequest` service served from the draft store on disk.
- [ ] End-to-end **sim-verify** against a Massabesic detections bag (absorbs cube#70's owed sim-verify).
- [ ] Then `/review-code` (container) → push + PR (Closes #78).

Notes: full-tile window v1 (dirty sub-window = later refinement); backscatter surfaced UNCORRECTED (correction = cube#81).

## Implementation complete (pending sim-verify)
**When**: 2026-06-27 20:15 -04:00
**By**: Claude Code Agent (Claude Opus 4.8)

All 3 parts done + committed (366 tests pass, uncrustify clean):
- `8d430b6` — backscatter into the live estimator (cloud field + pingCallback).
- `8a141a2` — quantization core (`nodeRecords()` + `quantizeTile()` + test).
- `587703f` — node endpoints: `~/coverage_tiles` (live push), `~/coverage_catalog`
  (periodic complete, transient_local, via `TileCatalogBuilder`), `~/coverage_requests`
  (serve resident tiles). Lifecycle-managed catalog timer.

Producer is functionally complete. Remaining:
- [ ] End-to-end **sim-verify**: launch the node, replay a Massabesic detections bag
  (`detections_to_pointcloud` → cube node), confirm `~/coverage_tiles` carries
  depth+uncertainty+backscatter and `~/coverage_catalog` lists the tiles. Absorbs
  cube#70's owed sim-verify (touches the live CA grid).
- [ ] `/review-code` (container) on the full diff → push + PR (Closes #78).

Scoped-as-follow-up (documented in code/commits): dirty sub-window (full-tile v1);
TileRequest from-disk catch-up for evicted tiles (resident-serving v1); backscatter
angle-correction (cube#81).

## Sim-verify PASSED
**When**: 2026-06-28 (headless, ROS_DOMAIN_ID=88)
**By**: Claude Code Agent (Claude Opus 4.8)

End-to-end on a real Massabesic M3 detections bag
(`bizzyboat_sonar/2026-06-19T16-56-37`, 1h offset, 50s slice) through
`detections_to_pointcloud` → cube node:

```
VERDICT=PASS tiles=7 bands=[backscatter, depth, uncertainty]
depth_range=[47.5,47.71] backscatter_finite_cells=129 catalog_max_entries=1
```

- 3-band `SonarVisualizationTile` published; **backscatter non-empty** (129 cells)
  — the #54 co-estimate flows through the new beam_angle wiring → quantize → tile.
- Depths round-trip (~47.5 m ellipsoidal seabed). `TileCatalog` lists the tile.
- Absorbed cube#70's owed sim-verify (CA grid published + draft tiles saved OK).

Three harness bugs fixed along the way (all harness, not producer): d2p is a
LifecycleNode needing configure/activate; `--start-offset` skips `tf_static`
(mount `bizzy/base_link->bizzy/m3`) so it is replayed separately and tf2-cached;
checker topic namespace matched the remapped node name.

Next: `/review-code` (container) on the full diff → push + PR (Closes #78).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-28 10:28 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-78 at `ef83f3a`
**Mode**: pre-push
**Depth**: Deep (reason: 776 lines / 10 files — both Deep thresholds; new wire-protocol transport)
**Must-fix**: 2 | **Suggestions**: 4
**Round**: 1 | **Ship**: continue — one cross-confirmed D4 contract defect + one reconfigure state-leak; both bounded/cheap.

Static analysis (ament_uncrustify + ament_cpplint) clean on all 5 changed C++ files.
Two Deep-tier Claude Adversarial passes (Lens A logic, Lens B systemic) independently
converged on the catalog-completeness defect (cross-confirmed). SingleThreadedExecutor —
no data-race surface. `depth_var` "uncertainty" band confirmed unit-correct (CI stddev in m).

### Findings
- [ ] (must-fix) `TileCatalog` doesn't track the servable set — under-advertises primed/resident tiles (never enter `catalog_builder_`; consumer prunes valid coverage, acute as empty catalog right after activate) and over-advertises evicted tiles (no `remove()` on eviction → unsatisfiable `TileRequest` loop); breaks ADR-0008 D4 completeness. Seed builder from startup prime + `remove()` on eviction — `src/cube_bathymetry_node.cpp:501,181,560-616`
- [ ] (must-fix) `catalog_builder_` never reset on reconfigure (configure→cleanup→configure advertises phantom prior-session tiles); mirror the line-98 `evicted_indices_.clear()` — `src/cube_bathymetry_node.cpp:94-98,339`
- [ ] (suggestion) `tileRequestCallback` O(requests × resident) linear scan; use indexed `gridAt()` (cross-confirmed A+B) — `src/cube_bathymetry_node.cpp:540-551`
- [ ] (suggestion) Reliable `TileRequest` answered on best-effort `~/coverage_tiles`; catch-up reply can be dropped, convergence bounded only by `catalog_interval_s_` — `src/cube_bathymetry_node.cpp:233,238`
- [ ] (suggestion) `tileRequestCallback` doesn't gate on `PRIMARY_STATE_ACTIVE`; a request while configured-but-inactive publishes on an inactive LifecyclePublisher (log-spam) — `src/cube_bathymetry_node.cpp:536`
- [ ] (suggestion) No unit test for the populated/auto-range backscatter band (finite intensity → bmin/bmax/bspan, single-value degenerate); plan called for a band-extract test, only sim-verify covers it — `test/test_quantize_tile.cpp`

## Review Triage (Pre-Push)
**When**: 2026-06-28
**By**: Claude Code Agent (Claude Opus 4.8)

Container review-code (Deep, both adversarial lenses) verdict **changes-requested**:
2 must-fix + 4 suggestions. Resolved:
- [x] **(must-fix)** Catalog now tracks the SERVABLE set: `publishCatalog` builds from
  the resident `grids()` filtered by tracked version — auto-excludes evicted tiles
  (no over-advertise / unsatisfiable request) and includes primed tiles. Seeded the
  version registry from the startup prime (no under-advertise / empty-catalog-after-activate).
- [x] **(must-fix)** Reset `catalog_builder_` on (re)configure (mirrors `evicted_indices_.clear()`)
  so a fresh sheet can't advertise phantom prior-session tiles.
- [x] (suggestion) `tileRequestCallback` gates on `PRIMARY_STATE_ACTIVE` (no inactive-publisher spam).
- [x] (suggestion) Added `BackscatterBandAutoRangesOverInsertedIntensities` test (auto-range offset/scale + populated cells).
- [ ] (suggestion, ACCEPTED-AS-IS) `tileRequestCallback` O(req×resident) scan: `gridAt()` needs a `gggs::GridIndex`, whose `(level,row,col)` ctor is private — the resident scan (≤ max_resident_tiles) is the available path. Noted.
- [ ] (suggestion, ACCEPTED-AS-IS) Reliable `TileRequest` → best-effort tile reply: a dropped catch-up reply is re-driven by the next periodic catalog (bounded by `catalog_interval_s_`); acceptable for the live preview.

Rebuilt; **369 tests pass**, uncrustify clean. Re-running sim-verify after the catalog change.
