# Plan: Live SonarVisualizationTile producer + backscatter wiring

## Issue

https://github.com/rolker/cube_bathymetry/issues/78 (Part of unh_marine_autonomy#171; producer half of ADR-0008 / #230)

## Context

#230 (the `SonarVisualizationTile` transport messages + the anti-entropy
reconciler in `marine_tiled_raster_store`) is merged. cube#78 is the **boat-side
producer**: publish quantized display tiles for the operator/CAMP live view, and
**wire M3 backscatter into the live path**.

What already exists (verified):
- The CUBE estimator (`node.cpp`) **already co-estimates backscatter** (#54) — it
  retains `{raw_intensity, grazing_angle}` samples and extracts a per-cell
  `intensity`/`intensity_var`, currently emitted **uncorrected** (`node.cpp:318`,
  `corrected = raw_intensity`).
- `detections_to_pointcloud` emits `intensity` at field[3] but **drops
  `beam_angle`** (the `Sounding` already computes it = `rx_angles[i]`).
- #70's per-tile incremental publish (`geoGridsToGridMap`, publish-dirty tiles,
  draft store on disk) is the basis for the new publisher.
- **The offline import (`store_import.cpp`) runs the same `Node` estimator** — so
  the *surfaced* backscatter value is already consistent live/offline, **and** any
  correction placed in `node.cpp` would hit the offline path too (the reason #78
  must not add one — see Approach #1).

## Approach

1. **Surface the existing co-estimated backscatter — no estimator change**
   (revised after plan review). Reuse `Node::extractNodeRecord` (`node.cpp:270`),
   which **already** emits the per-cell `intensity`/`intensity_var` (#54). #78 just
   **plumbs that value into the tile's `backscatter` band** — *transport-not-storage*;
   it does **not** modify the co-estimation math. The angle-correction stays
   **deferred**: `node.cpp:298-322` deliberately keeps the identity (flat-geometry)
   value behind an explicit `TODO(#54-B/#15)`, and `sounding.h` mandates a
   **sign-convention verification (cube#15)** before any `rx_angle` correction —
   applying it here, in the estimator `store_import` also runs, would corrupt the
   durable/offline path. So **`node.cpp` is untouched by #78**; the nadir-stripe
   correction is a **separate sign-gated issue** that benefits live + offline once
   it lands. `beam_angle` is still carried (step 2) so the surfaced value stays
   angle-tagged and re-derivable.
2. **`beam_angle` on the soundings cloud** — `detections_to_pointcloud`: add a 7th
   `FLOAT32 beam_angle` field (`point_step` 24→28, `fields.resize(7)`,
   `data_ptr[6]=sounding.beam_angle`, `+= 7`). Consumers read by name, so order
   is safe.
3. **`pingCallback` reads `{intensity, beam_angle}`** — in the live node, read both
   cloud fields into the estimator `update(...)` (which already takes
   intensity+beam_angle; today the cloud read leaves beam_angle unset).
4. **`SonarVisualizationTile` publisher** — new publisher, one quantized tile per
   **publish-dirty** tile, reusing #70's per-tile subset projection. **Quantize at
   source** (int16 cm depth, uint8 uncertainty, uint8 backscatter) — must not hop
   through a full-precision `grid_map`. Bands: `depth`, `uncertainty`,
   `backscatter`. New topic, distinct from #70's `~/tiles` GridMap.
5. **`TileCatalog` + `TileRequest`** — periodically publish a **complete**
   `TileCatalog` (resident **and on-disk** tiles) built via
   `marine_tiled_raster_store::TileCatalogBuilder` (merged #230); serve
   `TileRequest` by reading tiles **from the draft store on disk** (so #70's
   cold-tile eviction can't make older tiles un-servable). **QoS:** tile push
   best-effort/volatile (live, lossy-OK); **`TileCatalog` `transient_local`** so a
   late-joining operator gets the current snapshot; `TileRequest` reliable.
6. **Tests + sim-verify** — unit-test quantization (round-trip within the quantum)
   and the band extract (backscatter non-empty when the cloud carries intensity);
   **sim-verify end-to-end** against a Massabesic detections bag
   (`bizzyboat_sonar/2026-06-19…`, populated intensities) → 3-band tile. This run
   **absorbs #70's owed sim-verify** (it exercises the same live CA grid).

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/detections_to_pointcloud.cpp` | Add `beam_angle` field (6→7 fields, step 24→28) |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | pingCallback reads `intensity`+`beam_angle`; surface `extractNodeRecord` backscatter into the band; `SonarVisualizationTile`+`TileCatalog` publishers; `TileRequest` service-from-disk |
| `cube_bathymetry/src/quantize_tile.{h,cpp}` (new) | Quantize a per-tile grid subset → `SonarVisualizationTile` (int16/uint8 bands, per-band scale/offset) |
| `cube_bathymetry/CMakeLists.txt`, `package.xml` | Deps on `marine_interfaces` + `marine_tiled_raster_store` (from merged #230 — must be built/installed in `core_ws`; worktree build sources lower layers) |
| `cube_bathymetry/test/…` | Quantization + band-extract unit tests |
| `cube_bathymetry/src/node.cpp` | **UNTOUCHED** — estimator's deferred correction stays (must-fix #1) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness / tests | Unit tests + an end-to-end sim-verify on real Massabesic data |
| Respect deliberate deferrals | #78 surfaces the existing co-estimated value; the sign-gated angle-correction (cube#15) stays deferred, `node.cpp` untouched |
| Quality Standard — complete the transition | Absorbs #70's owed sim-verify; `TileRequest` served from disk so eviction can't drop history |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 | Yes | Quantize-at-source, no compression, named bands, `TileCatalog`/`TileRequest` via the merged reconciler; metering rides udp_bridge#19 |
| 0007 | Yes | `{intensity, beam_angle}` sufficient-statistic pair completed (beam_angle now carried); surfaces the co-estimated value — the angle/GeoCoder correction stays deferred to its sign-gated issue (cube#15/#59), not done here |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Surface uncorrected backscatter (no correction) | **cube#80's "first-cut beam-angle" framing must also defer** to a separate sign-gated correction issue (same node.cpp hazard) | No — ripple flagged in Open Questions |
| New publisher topic + msg deps | camp#121 consumer; udp_bridge#19 metering | No — downstream/dependency |
| `detections_to_pointcloud` 6→7 fields | any other cloud consumer reading by offset (none found; read-by-name is safe) | Verify in impl |

## Open Questions

- [ ] **Deferred angle-correction → separate issue (ripple to cube#80).** Plan
  review (must-fix #1) confirmed #78 must **not** correct backscatter in `node.cpp`
  (sign-convention gate, `sounding.h`; corrupts the offline path). #78 surfaces the
  **uncorrected** co-estimated value (still useful — relative brightness for target
  detection). The nadir-stripe correction belongs in its **own sign-gated issue**
  (benefits live + offline once it lands). **cube#80's "first-cut beam-angle"
  framing should defer to it too.** → File the correction issue + tweak cube#80?
- [ ] **Backscatter quantization range** — uint8 `backscatter` band: per-tile
  auto-range with `scale`/`offset` carried in `VisualizationBand` (it provides
  them). Recommend per-tile auto-range; confirm.
- [ ] **udp_bridge#19 (metering) not yet landed** — ship v1 without priority
  classes (live push on its own topic) and add metering when #19 lands? Recommend
  yes (don't block on #19).

## Estimated Scope

Single PR (medium-large). Natural internal order: (1) `beam_angle` cloud field +
pingCallback read + surface `extractNodeRecord` backscatter into the band,
(2) the quantized publisher + catalog/request, (3) sim-verify. `node.cpp` is
untouched, so there is no longer a shared-estimator precursor to split out for
cube#80.
