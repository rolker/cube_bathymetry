# cube_bathymetry

A ROS implementation of the [Combined Uncertainty and Bathymetric Estimator](https://ccom.unh.edu/theme/data-processing/cube) algorithm, also known as CUBE.

It is based on Brian Calder's original c code found [here](https://bitbucket.org/ccomjhc/cube) as well as Eric Younkin's Python port found [here](https://github.com/noaa-ocs-hydrography/bathycube).

## Nodes

| Node | Subscribes | Publishes | Purpose |
|---|---|---|---|
| `detections_to_pointcloud` | `detections` (`marine_acoustic_msgs/SonarDetections`), `odom` (`nav_msgs/Odometry`) | `soundings` (`sensor_msgs/PointCloud2`) | Turns each beam detection into a sounding in the **sonar frame** (x/y/z from travel-time + beam angles) and attaches **per-sounding TPU** (`vertical_uncertainty`, `horizontal_uncertainty`) computed by `cube::ErrorModel`. Lifecycle node. |
| `cube_bathymetry_node` | `soundings` (`sensor_msgs/PointCloud2`) | `grid` (`grid_map_msgs/GridMap`, layers `elevation` + `uncertainty`) | Transforms soundings into `map_frame` via TF, runs the live CUBE estimator, and emits the gridded surface consumed by CAMP / rviz. Lifecycle node. |
| `bag_to_geotiff` | (offline, reads a bag) | GeoTIFF on disk | Offline gridding tool. |

`soundings` carries five `float32` fields per point: `x`, `y`, `z`,
`vertical_uncertainty`, `horizontal_uncertainty`. The positions are in the
detections' own frame (`header.frame_id`); `cube_bathymetry_node` georeferences
them with a TF lookup `map_frame ← header.frame_id` at the ping stamp.

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
