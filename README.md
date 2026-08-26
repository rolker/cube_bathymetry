# cube_bathymetry

A ROS implementation of the [Combined Uncertainty and Bathymetric Estimator](https://ccom.unh.edu/theme/data-processing/cube) algorithm, also known as CUBE.

It is based on Brian Calder's original c code found [here](https://bitbucket.org/ccomjhc/cube) as well as Eric Younkin's Python port found [here](https://github.com/noaa-ocs-hydrography/bathycube).

## Nodes

| Node | Subscribes | Publishes | Purpose |
|---|---|---|---|
| `detections_to_pointcloud` | `detections` (`marine_acoustic_msgs/SonarDetections`), `odom` (`nav_msgs/Odometry`) | `soundings` (`sensor_msgs/PointCloud2`) | Turns each beam detection into a sounding in the **sonar frame** (x/y/z from travel-time + beam angles) and attaches **per-sounding TPU** (`vertical_uncertainty`, `horizontal_uncertainty`) computed by `cube::ErrorModel`. Lifecycle node. |
| `cube_bathymetry_node` | `soundings` (`sensor_msgs/PointCloud2`) | `grid` (`grid_map_msgs/GridMap`, layers `elevation` + `uncertainty`) | Transforms soundings into `map_frame` via TF, runs the live CUBE estimator, and emits the gridded surface consumed by CAMP / rviz. Lifecycle node. |
| `bag_to_geotiff` | (offline, reads a bag) | GeoTIFF on disk | Offline gridding tool. Reads pre-projected soundings (`-t /soundings`) or, with `-d /detections`, projects raw `SonarDetections` to soundings in-process via the same `DetectionsProjector` the node uses — no live graph, full CPU speed, deterministic. The `-d` path needs the projector frames to match the bag's (namespaced) frames; override with `--base-link-frame` / `--level-frame` / `--tide-frame` (see *Configuring frames per platform* below), or the grid comes out empty (a one-line warning is printed). |

`soundings` carries six `float32` fields per point, in order: `x`, `y`, `z`,
`intensity`, `vertical_uncertainty`, `horizontal_uncertainty`. `intensity` is the
per-beam acoustic backscatter copied from `SonarDetections.intensities` — usually
uncalibrated, but reflectivity in dB for the Kongsberg M3 (via `kongsberg_em_bridge`);
it is `NaN` when the source omits intensities. The positions are in the detections'
own frame (`header.frame_id`); `cube_bathymetry_node` georeferences them with a TF
lookup `map_frame ← header.frame_id` at the ping stamp. Consumers read fields **by
name** (`cube_bathymetry_node` and `bag_to_geotiff` both use named PointCloud2
iterators), so the field order is not load-bearing.

## Live coverage stream parameters (`cube_bathymetry_node`)

`~/coverage_tiles` is the live seafloor-coverage view the shore operator sees,
and it goes over a rate-limited link — 1,500,000 B/s on BizzyBoat's VPN, shared
with telemetry, costmap, video and TF. A whole tile is 960x960 cells x 3 bands =
**3,686,400 B** of payload, so these three parameters decide whether coverage is
a rounding error on that link or the thing that takes it down. It took it down on
2026-08-25 (cube_bathymetry#112, ADR-0001 addendums 1 and 2).

| Parameter | Type / default | When read | What it does |
|---|---|---|---|
| `publish_dirty_subwindow` | bool, **`true`** | configure (`read_only`; a runtime set is REFUSED, not silently ignored) | Send only each tile's dirty sub-window as a patch, instead of the whole tile. Measured on the 2026-08-25 Appledore bags: the coverage stream costs 56.0 kB/s on transit and 85.7 kB/s on station as whole tiles, against 11.2 and 9.1 kB/s as patches with the default refresh — 80% and 89% less. Peak single message falls from 1,120,845 B to 172,040 B compressed. `false` reproduces the whole-tile stream byte for byte. |
| `subwindow_refresh_interval` | double seconds, **`60.0`** | **runtime** (validated and applied to the live tracker) | How long a patched tile may go without being re-sent WHOLE. This is what makes patching safe: the live push is best-effort and a lost patch is not discoverable by the consumer, so a periodic whole re-send bounds the gap without depending on the consumer noticing anything. `0` disables the heal — only safe against a consumer that tracks patch possession itself. Measured cost: 300 s -> 7.9/3.5 kB/s, 60 s -> 11.2/9.1, 30 s -> 15.2/16.1 (transit/station). |
| `subwindow_refresh_tiles_per_cycle` | int, **`2`**, refused above `8` | **runtime** (same path) | Whole tiles re-sent per 5 s drain tick to clear outstanding patch debt, over and above the tiles that changed. Bounds the heal so it cannot become the burst it exists to prevent: at ~183 kB compressed per whole tile, `8` is already ~293 kB/s of the operator's link. `0` disables the quiet-tile drain, which leaves a tile the vessel has moved off unhealed. |

**The heal latency is a target under load, not a bound.** The drain clears at
most `subwindow_refresh_tiles_per_cycle` tiles per tick — 24 per minute at the
defaults — so a line-end turn that quiets more tiles than that at once heals
them oldest-first over longer than `subwindow_refresh_interval`. A throttled
WARN says so when it happens. Sustained saturation is worse than not
sub-windowing at all: the drain's own ceiling (~73 kB/s) exceeds the 56.0 kB/s
transit stream it replaces.

**Consumer contract.** A consumer must (a) apply the band data at
`window_row`/`window_col` rather than over the whole tile, and (b) dequantize
per message — the backscatter band's scale/offset is auto-ranged over the
window, so it varies between patches (uma ADR-0008 D1). It need NOT track patch
possession: the catalog version is bumped only on a whole send, so a consumer
that keys possession on either rule converges. Both in-tree consumers are
correct today — CAMP's `SonarLiveTile::applyPatch` and `marine_web_view`'s
`coverage_renderer` — and they use opposite possession rules, which is why the
producer, not the consumer, carries the guarantee.

**Not solved here.** This bounds the typical message, not the worst one. A
diagonal survey line's dirty bounding box still degrades toward the full tile;
fixed-size chunking is what cube_bathymetry#112 actually asks for and it
remains open.

## Exported libraries for other repos

Most of this package's CMake targets exist to keep the node's own dependencies
apart. One is deliberately a cross-repo contract:

| Target | Header | Consumers | Notes |
|---|---|---|---|
| `cube_bathymetry::cube_bathymetry_ssp_ray_tracer` | `cube_bathymetry/ssp_ray_tracer.h` | `unh_marine_autonomy#300` (sound-speed inversion, phase 2), `marine_perception_tools#28` (CUBE lab re-projection) | Forward constant-gradient SSP ray tracer (#126). Depends on nothing beyond the standard library (plus libm, propagated on the target) — no ROS, no GDAL, no message types — so a consumer *links* this target alone. Note the isolation is link-level: `find_package(cube_bathymetry)` still resolves this package's full dependency set at configure time. Built position-independent so it drops into shared libraries and Python extension modules. |

The ray tracer's sign, unit, and status conventions are the contract both
consumers build on and are documented in the header itself, not here — read
`ssp_ray_tracer.h` before wiring it up, in particular the positive-down depth
convention (opposite in sign to `Sounding::depth`) and the fact that no sea
surface is modelled.

## Offline store import & rebuild

Two offline tools replay a detections bag through CUBE and write a
`marine_bathymetry_store` (and optional `marine_mbes_backscatter_store`). Both
share `bag_to_geotiff`'s projection pipeline and write the **`survey`** layer
(unh_marine_autonomy#248 collapsed the old `draft`/`processed`/`chart` layers into
`survey` + `reference`; the off-boat re-run is authoritative).

| Tool | RAM | Output | Use when |
|---|---|---|---|
| `import_bag` | bounded by `--max-resident-tiles` (persist-then-drop eviction, #92) | lossless, but a tile evicted mid-disambiguation has a slightly re-derived depth **uncertainty** (depth value faithful) | streaming / very large surveys where RAM is the constraint |
| `batch_regen_bag` | bounded by one tile's soundings | **bit-exact** vs a whole-survey-in-RAM build (depth, uncertainty, and backscatter) | the authoritative off-boat product |

`batch_regen` scatters each projected sounding to a per-tile bucket on disk, then
gathers each tile in a single unbounded pass (no eviction) — so no tile is ever
evicted mid-disambiguation. Its scratch scatter directory is cleaned up at the end.

### Incremental regen: dirty-tile query (`--index-db`, dry-run)

`batch_regen_bag --index-db <survey_index.db> <bag> [<bag> ...]` runs a **dry-run**
that reports which store tiles a tile-scoped incremental rebuild *would* touch for
the given bags — it builds nothing and does not need `-o` or `-d`. This is the
query half of the incremental-regen work (cube_bathymetry#111, PR1; the rebuild +
atomic swap land in PR2).

Using the `marine_survey_index` sidecar (`survey_index.db`, unh_marine_autonomy#259),
it takes the L14 tile footprint the new bags ensonified, expands it by a one-tile
conservative margin, rolls it up to the store's L10 tiles via the GGGS parent
hierarchy, and lists each dirty L10 tile with the bags + pass intervals that
contribute to it (see [`cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md`](cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md)).
Output is a human-readable summary plus a machine-parseable `DIRTY_TILES_JSON:`
line. **Machine contract:** the `DIRTY_TILES_JSON:` line is authoritative and is
emitted only on a successful query that found an indexed footprint; its *absence*
means "fall back to full regen" (index absent, unopenable, not a valid index, or
an *index miss* — an empty dirty set for a non-empty bag list, which cannot be
told apart from a bag that was never indexed, since bag paths are matched
exactly). All of those cases still exit 0 — a missing soft dependency is not a
failure — so a consumer must key off the marker line, not the exit code. Each
case prints a `note:` on stderr saying a real run falls back to full regen (the
index is a **soft** dependency). Without `--index-db`, the full-regen path is
unchanged.

### Seed precedence (`--reference-store`)

On the first touch of each tile, both tools seed it with a two-rung precedence:

1. **survey** — a `survey/` tile already in the output store (a pre-existing store,
   or a tile written earlier this run) is warm-started as measured CUBE data
   (settled depth **and** its backscatter Welford, restored losslessly).
2. **prior (`reference/` + `chart/`)** — else, if `--reference-store <dir>` is
   given, a prior tile primes the **predicted surface only**: it turns CUBE's
   blunder-rejection gate on so false-deep detections are dropped, but is **never
   settled** as measured data and seeds **no** backscatter. The store's **both**
   prior layers are consulted, in `SourceLayer` priority order — `chart/` primes
   first, then `reference/` **overwrites** where the two overlap a cell:
   - **`chart/`** (charted-waters gate, #119) — official chart products live in the
     `Chart` layer, which the gate previously never consulted, so charted-waters
     imports ran ungated. Only an **exact survey-level** chart tile is primed;
     there is **no** cross-level fallback for chart (coarse-chart resampling is
     deferred).
   - **`reference/`** (prior/contour) — a reference tile at the exact survey GGGS
     level gates cell-for-cell; a **coarser** reference tile (a multi-level prior,
     e.g. ENC exports at L5/L7/L8 under an L10 survey) is picked up by a level-walk
     fallback (#115) that resamples the finest available coarser tile's shallow
     prior onto the fine survey cells — coarse ENC generalization is shoal-biased,
     the conservative direction for a false-deep gate. The fallback logs the
     reference level it used, so the import is auditable.

   (Replaces the pre-#96 `--prior` flag, which loaded the whole prior into RAM up
   front and defeated eviction.)
3. else **blank**.

### Backscatter fidelity

The co-estimated per-cell backscatter is stored as a 3-band Welford sufficient
statistic `{mean, standard_error, sample_sd}` (uma#248), so the estimate
reconstructs losslessly on an off-boat re-run: a single-beam cell is the `n = 1`
sentinel (`sample_sd = 0`, finite mean), and a multi-beam cell round-trips
`n`/mean/variance exactly. The per-beam angular-response (and tier-2 TL)
correction defaults to **auto** (#102): a valid curve published in
`marine_interfaces/SonarInfo` beside the sonar stream (or recorded in the bag)
enables it, with TL provenance validated (uma#268) — no curve means values
stay uncorrected, exactly the pre-SonarInfo behavior. Pass
`--backscatter-correction none` to hard-disable, or
`--backscatter-correction empirical --backscatter-curve <csv>` (node:
`backscatter_curve_file`) to override the published curve with an explicit
CSV (e.g. reprocessing with a better curve; `--sonar-info-topic` overrides
the scanned topic, default = the detections topic's sibling `sonar_info`).
See
`docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md`.

## Live prior-gate prime (`prior_store_dir`)

`cube_bathymetry_node` accepts an optional **`prior_store_dir`** parameter (empty
by default = off, the pre-#91 behaviour). When set, on `on_configure` the node
loads that `marine_bathymetry_store` and primes CUBE's **predicted surface** from
its prior layers — the *live* equivalent of the offline `--reference-store` gate
above, sharing the same `primeFromPriorLayers` semantics:

- **Predicted-only** (never settled) — the prior turns the blunder-rejection gate
  on so false-deep detections are dropped live, but seeds no measured depth and no
  backscatter (cube#89, ADR-0008). As a side-effect a primed predicted surface
  also activates **live slope-corrected depth** (#59) — this parameter is what
  turns that on afloat.
- **`chart/` first, `reference/` overwrites** where both layers cover a cell — the
  store's `SourceLayer` priority order.
- **Exact survey level only** — unlike the offline `reference/` path there is **no**
  cross-level (#115) fallback here; a coarser multi-level prior tile is counted as
  level-mismatched and skipped.
- Runs **before** the `draft_dir` warm-start, so an already-surveyed cell keeps its
  finer draft-derived predicted depth (the draft prime overwrites the prior).
- **Budget-bounded** like the draft prime (`max_resident_tiles`): the whole prior
  store is loaded into RAM at configure *before* `trimResidentToBudget` bounds it,
  so a very large prior spikes RAM transiently — prefer a **region-scoped**
  `prior_store_dir`. Evicted prior-primed tiles are predicted-only and **not
  reloadable on revisit**, so they silently lose their gate until evict/revisit
  re-priming lands (deferred, #118).
- **Fail-safe** — a missing / unreadable / empty / all-level-mismatched prior logs
  a warning and the node continues **ungated**; a misconfigured prior never takes
  down live perception.

## Data flow & pose sourcing (design: #31)

The package faithfully ports Calder's CUBE *algorithm* but originally diverged
from his *data flow*. Issue
[#31](https://github.com/rolker/cube_bathymetry/issues/31) refactored it onto a
**single, coherent pose authority** so that the pose used to *place* a sounding
and the pose used to compute its *error budget* can never disagree.

**The defect that motivated it:** the old `detections_to_pointcloud` computed TPU
from its own `mru_transform::NavigationSensors` instance (topic subscriptions,
"latest message within 1 s"), while soundings were *placed* downstream by the TF
tree. Two pose mechanisms with different timing — a sounding could be placed
correctly while its uncertainty was computed from stale or `NaN` attitude. The
placement and the uncertainty disagreed about where the boat was.

**The fix (PR [#33](https://github.com/rolker/cube_bathymetry/pull/33)):**
`detections_to_pointcloud` now draws pose from the **same TF** that places the
soundings, plus two genuinely-non-pose scalars. What `cube::ErrorModel` actually
consumes, and where each input now comes from:

| Error-model input | Source |
|---|---|
| `roll`, `pitch` (dominant per-beam term) | TF: `level_frame ← base_link_frame` at the ping stamp (interpolated to the exact time, not "latest within 1 s") |
| `heave` | TF: `z(base_link_frame) − z(tide_frame)` at the ping stamp; enters the budget only squared, so it defaults to `0` if the transform is absent |
| `vessel_speed` (SOG) | `/odom` twist (`hypot(linear.x, linear.y)`), cached |
| `surf_sspeed`, `mean_speed` | `SonarDetections.ping_info.sound_speed` |
| ~~`latitude`, `longitude`, `heading`~~ | **removed** (#32) — Calder used them for *georeferencing*, a job that moved to TF in the ROS port, so they were orphaned: written by the node, read by no consumer |

**The grid stays ellipsoid-referenced** (`map`). The seabed's ellipsoid height is
tide-independent, so it is the stable representation; chart-datum reduction is a
purely spatial transform applied downstream on demand. This deliberately keeps
the (currently costmap-grade, not hydro-grade — see
[unh_echoboats_project11#138](https://github.com/rolker/unh_echoboats_project11/issues/138))
tide/datum chain *out of* the bathymetry critical path.

Remaining design step (open in #31): converge `cube_bathymetry_node` and
`bag_to_geotiff` onto the same mapsheet path so the offline placeholder TPU
disappears (the other half of
[#30](https://github.com/rolker/cube_bathymetry/issues/30)).

## Divergences from Calder's CUBE

This package is a faithful port of Brian Calder's CUBE algorithm (vendored under
`original_cube/`), but it differs from the reference in a few deliberate ways —
the ellipsoid-referencing tide decision above among them. Where and why the port
diverges on purpose (datum-aware tide terms, parameterized device errors, the
unported IHO f(z) model, `CONF_99PC`, and the nomination-uncertainty choice) is
documented in
[`cube_bathymetry/docs/divergences_from_calder.md`](cube_bathymetry/docs/divergences_from_calder.md),
the deliverable of [#30](https://github.com/rolker/cube_bathymetry/issues/30).

## Configuring frames per platform (REQUIRED)

`detections_to_pointcloud`'s frame parameters default to the **unprefixed**
`mru_transform` names. These defaults are a fallback for a single-vehicle,
un-namespaced graph — **any namespaced deployment (a real boat, or the
simulator) MUST override them**, or the attitude TF lookup misses and no usable
grid is produced. The failure is not silent: `detections_to_pointcloud` logs a
throttled warning when the attitude TF is missing, and `cube_bathymetry_node`
warns when it drops non-finite soundings — but the headline symptom is an empty
grid in CAMP/rviz, so the warnings are easy to miss.

| Param | Default | Set it to (namespaced example) |
|---|---|---|
| `base_link_frame` | `base_link` | `<ns>/base_link` |
| `level_frame` | `base_link_north_up` | `<ns>/base_link_north_up` |
| `tide_frame` | `map_tide` | `<ns>/map_tide` |
| `cube_bathymetry_node`'s `map_frame` | `map` | `<ns>/map` |

…and remap `odom`. The node is normally launched in a sensor **sub**-namespace
(e.g. `<ns>/sensors/mbes`), where a relative `odom` resolves to
`/<ns>/sensors/mbes/odom`; remap `odom → /<ns>/odom` to reach the platform's
odometry. (Only if the node runs directly under `/<ns>` does the namespace
alone resolve it, with no remap needed.)

Live boat wiring is in
[unh_echoboats_project11#226](https://github.com/rolker/unh_echoboats_project11/pull/226)
(`bizzyboat.yaml` frame params + `perception_launch.py` odom remap).

> **Note for the simulator:** the sim launch
> (`unh_marine_simulation`/`marine_simulation/launch/sim_robot_launch.py`)
> predates this refactor. It still sets the now-dead
> `sensors.default.topics.{orientation,velocity}` params (consumed only by the
> retired `NavigationSensors` path) and does **not** set the new frame params,
> so under a namespace such as `ben/` the attitude TF lookup misses. See the
> failure mode below.

## Failure modes: active node, soundings flowing, but no usable grid

If `cube_bathymetry_node` is lifecycle-**active** and `soundings` is publishing
but nothing useful reaches CAMP/rviz, there are **two distinct symptoms** with
different causes — check which one you have first.

### A. `grid` publishes no messages at all

The placement TF (`map_frame ← soundings frame_id`) never resolves at the ping
stamps, so no soundings are added, `gridBounds()` stays `NaN`, and
`publishGrid()` early-returns every time. A `ros2 topic hz /…/grid` shows zero.

This was the BizzyBoat symptom in
[#36](https://github.com/rolker/cube_bathymetry/issues/36): the `map ← sensor`
chain includes the position-driven `map` transform, which updated ~1 Hz against
9 Hz pings, so an exact-stamp lookup routinely extrapolated and the ping was
dropped. The placement lookup now falls back to the latest available transform
(`lookupAtOrLatest`), so this no longer blanks the grid.

### B. `grid` publishes, but the cells are all-`NaN` / empty

Messages arrive (roughly every 5 s once any tiles exist) but carry no usable
depth — the grid renders empty. This is **`NaN` uncertainty poisoning**:

1. The attitude TF (`level_frame ← base_link_frame`) lookup throws — usually
   because the frame-name params above were not set for the namespace — so
   `detections_to_pointcloud` sets `roll`/`pitch` to `NaN`
   (or `vessel_speed` is `NaN` because `odom` is missing/mis-resolved).
2. `cube::ErrorModel` propagates that into `NaN` `vertical_uncertainty` /
   `horizontal_uncertainty`. **The sounding x/y/z stay valid** (pure geometry),
   so the cloud looks healthy in rviz — only the uncertainty fields are bad.
3. A `NaN` uncertainty becomes a `NaN` CUBE variance → a `NaN` cell estimate,
   so the cell drops out of the published grid.

`cube_bathymetry_node` now guards against (B): it drops non-finite soundings
before they reach the estimator (and `Grid::insert` rejects them defensively),
logging a throttled count that names the offending frame — so a future
occurrence is diagnosable rather than a silently empty grid. The real fix is
still to set the frame params / `odom` remap above so the uncertainty is valid.

This is the diagnosis for the empty-grid symptom seen both in the simulator and
on BizzyBoat (gabby, 2026-06-09 —
[#36](https://github.com/rolker/cube_bathymetry/issues/36)). The first checks are
therefore: confirm the frame params are set for the namespace, and inspect the
`soundings` cloud's uncertainty fields for `NaN` (the x/y/z passing visual
inspection does **not** rule this out).
