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
  the backscatter correction below is shared with cube#80 automatically.

## Approach

1. **Beam-angle backscatter correction (the cube#80 handshake)** — in the shared
   estimator extract (`node.cpp:~315-318`), apply the **first-cut
   beam-angle-vs-nadir** correction to each retained sample instead of passing
   `raw_intensity` through. Because both the live node *and* `store_import` run
   this estimator, live and existing-coverage backscatter come out **identical**
   — no separate helper, no duplicated math. GeoCoder-grade incidence
   (slope, cube#15/#59) is a later refinement (ADR-0007).
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
   cold-tile eviction can't make older tiles un-servable).
6. **Tests + sim-verify** — unit-test quantization (round-trip within the quantum)
   and the band extract (backscatter non-empty when the cloud carries intensity);
   **sim-verify end-to-end** against a Massabesic detections bag
   (`bizzyboat_sonar/2026-06-19…`, populated intensities) → 3-band tile. This run
   **absorbs #70's owed sim-verify** (it exercises the same live CA grid).

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/node.cpp` | First-cut beam-angle backscatter correction in the intensity extract (~L315-318) — shared with cube#80 |
| `cube_bathymetry/src/detections_to_pointcloud.cpp` | Add `beam_angle` field (6→7 fields, step 24→28) |
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | pingCallback reads beam_angle; `SonarVisualizationTile`+`TileCatalog` publishers; `TileRequest` service-from-disk |
| `cube_bathymetry/src/quantize_tile.{h,cpp}` (new) | Quantize a per-tile grid subset → `SonarVisualizationTile` (int16/uint8 bands) |
| `cube_bathymetry/CMakeLists.txt`, `package.xml` | Deps on `marine_interfaces` + `marine_tiled_raster_store` |
| `cube_bathymetry/test/…` | Quantization + band-extract unit tests |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Robustness / tests | Unit tests + an end-to-end sim-verify on real Massabesic data |
| Don't duplicate; single source of truth | Backscatter correction lives once in the shared estimator → live==offline (cube#80) |
| Quality Standard — complete the transition | Absorbs #70's owed sim-verify; `TileRequest` served from disk so eviction can't drop history |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 | Yes | Quantize-at-source, no compression, named bands, `TileCatalog`/`TileRequest` via the merged reconciler; metering rides udp_bridge#19 |
| 0007 | Yes | `{intensity, beam_angle}` sufficient-statistic pair completed; first-cut beam-angle correction now, GeoCoder slope (cube#15/#59) later |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Backscatter correction in `node.cpp` | cube#80 (offline) inherits it — **coordinate; this PR owns it** | Yes — the handshake |
| New publisher topic + msg deps | camp#121 consumer; udp_bridge#19 metering | No — downstream/dependency |
| `detections_to_pointcloud` 6→7 fields | any other cloud consumer reading by offset (none found; read-by-name is safe) | Verify in impl |

## Open Questions

- [ ] **Backscatter quantization range** — uint8 `backscatter` band scale/offset:
  fixed M3 reflectivity-dB range, or **per-tile auto-range** (ADR-0008 says
  per-tile scale/offset for backscatter)? Recommend per-tile auto-range; confirm.
- [ ] **udp_bridge#19 (metering) not yet landed** — ship v1 without priority
  classes (live push on its own topic) and add metering when #19 lands? Recommend
  yes (don't block on #19).

## Estimated Scope

Single PR (medium-large). Natural internal order: (1) beam-angle field +
estimator correction [the shared bits], (2) the quantized publisher +
catalog/request, (3) sim-verify. Could split (1) into a tiny precursor PR to
unblock cube#80 sooner — decide at push time.
