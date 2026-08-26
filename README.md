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
     imports ran ungated.
   - **`reference/`** (prior/contour).

   **Both** layers use a **level walk** (#115 for `reference/`, extended to
   `chart/` by #137): every **containing** coarser tile is resampled onto the fine
   survey cells, coarsest first, then the exact survey-level tile last — so each
   cell is gated by the **finest** prior that holds data there, and a cell no finer
   prior covers is still gated by a coarser one. Chart needs this most — an ENC
   product is built on the chart scale ladder (usage bands) and essentially never
   has a tile at the survey GGGS level, so an exact-level-only chart prime missed
   every time and the gate stayed silently off for exactly the product the `Chart`
   layer exists to hold. A tile that *matches* but is **no-data** over the survey
   tile primes nothing and is not treated as a gate; nor does a sliver of data in a
   finer prior suppress a coarser one with fuller coverage (#137 review). The walk
   logs each layer and level it used (once per layer/level), so the import is
   auditable, and a run whose prior primed **nothing at all**, gated only **part**
   of the run, or **failed to read** the store ends with an explicit warning naming
   what the store did hold (`import_bag` at `finalize`, `batch_regen` after its
   gather) rather than leaving the operator to infer a working gate from the startup
   banner (#137).

   **Trade-off, worth knowing before you point this at a chart.** Coarse ENC
   generalization is shoal-biased, which is the conservative direction for a
   false-deep gate — but the same bias **falsely rejects legitimate deeper-than-
   charted returns**, and the widened gate exposes tiles that previously ran
   ungated to that mode. How coarse a prior may gate is **not capped** — an L2
   chart tile is ~232 m/cell under an L10 (1 m) survey — but the **blunder margin
   scales with the resample gap**: a cross-level prime inflates the seeded 1-sigma
   by `--prior-relief-slope * half-cell-span` (default 0.05, a gentle 5 % seabed
   slope), so a coarse band no longer gates as hard as a survey-resolution prior.
   Without it the variance limit could never bind at all — `Node::insert` takes the
   `min()` of its three blunder limits, which picks the most *permissive*, and an
   uncertainty-less chart cell seeds sigma = 1 cm — so one L2 cell blending a shoal
   with a channel would permanently reject the channel's real bottom. Raise the
   slope over steep seabed; set it to 0 to restore the previous behaviour. The audit
   line names the level used, so a large gap stays visible in the import log, and the
   margin is additionally tunable via the `blunder_*` parameters.

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
- **Exact survey level only** — unlike the offline path (where **both** prior
  layers walk levels since #137) there is **no** cross-level (#115) resample here; a
  coarser multi-level prior tile is counted as level-mismatched and skipped. On the
  usual ENC prior — built on the chart scale ladder, so essentially never at the
  survey level — that leaves the live gate **off**; tracked as a known gap (#137).
- **A match is not a gate** — a prior tile that exists at the survey level but holds
  no data over it primes nothing, and is counted separately (`empty_tiles`) rather
  than reported as a live gate (#137).
- Runs **before** the `draft_dir` warm-start, so an already-surveyed cell keeps its
  finer draft-derived predicted depth (the draft prime overwrites the prior).
- **Budget-bounded** like the draft prime (`max_resident_tiles`): the whole prior
  store is loaded into RAM at configure *before* `trimResidentToBudget` bounds it,
  so a very large prior spikes RAM transiently — prefer a **region-scoped**
  `prior_store_dir`. Evicted prior-primed tiles are predicted-only and are
  **re-primed from `prior_store_dir` on revisit** (#118), so eviction costs one
  windowed disk read on return, never the gate; a prior read error there keeps the
  tile evicted so the next revisit retries rather than running ungated.
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
