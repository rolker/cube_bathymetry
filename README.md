# cube_bathymetry

A ROS implementation of the [Combined Uncertainty and Bathymetric Estimator](https://ccom.unh.edu/theme/data-processing/cube) algorithm, also known as CUBE.

It is based on Brian Calder's original c code found [here](https://bitbucket.org/ccomjhc/cube) as well as Eric Younkin's Python port found [here](https://github.com/noaa-ocs-hydrography/bathycube).

## Nodes

| Node | Subscribes | Publishes | Purpose |
|---|---|---|---|
| `detections_to_pointcloud` | `detections` (`marine_acoustic_msgs/SonarDetections`), `odom` (`nav_msgs/Odometry`) | `soundings` (`sensor_msgs/PointCloud2`) | Turns each beam detection into a sounding in the **sonar frame** (x/y/z from travel-time + beam angles) and attaches **per-sounding TPU** (`vertical_uncertainty`, `horizontal_uncertainty`) computed by `cube::ErrorModel`. Lifecycle node. |
| `cube_bathymetry_node` | `soundings` (`sensor_msgs/PointCloud2`) | `grid` (`grid_map_msgs/GridMap`, layers `elevation` + `uncertainty`; `elevation` is the seabed's **WGS84 ellipsoid height in metres, positive up** (a seabed lies below the water surface's ellipsoid height, so it is negative only where that surface is near or below the ellipsoid), see *The `grid` `elevation` convention* below; note the OPPOSITE convention to `soundings` below — `uncertainty` is a 95 % (1.96 sigma) confidence half-width in **metres**, not a variance) | Transforms soundings into `map_frame` via TF, runs the live CUBE estimator, and emits the gridded surface consumed by CAMP / rviz. Lifecycle node. Service `~/reset` (`std_srvs/Trigger`) starts a fresh session in RAM, see *Starting a fresh sheet without restarting*. |
| `coverage_assessment_node` (package `coverage_assessment`) | one or more CUBE `grid` topics (`grid_topics`), `survey_area` (`marine_interfaces/SurveyArea`, transient_local), TF | `coverage_assessment` (`marine_interfaces/CoverageAssessment`, one per area, transient_local), `coverage_grid` (`grid_map_msgs/GridMap`, layer `meets_standard`, transient_local) | Scores how much of each target polygon the sources' grids cover and how much of that meets the area's survey standard, with credit per source. Keeps the union over time of every source's `grid`, so the 200 m window does not matter. Lifecycle node; service `~/reset` (`std_srvs/Trigger`) empties the unions; see *Coverage assessment* below and `coverage_assessment/README.md`. |
| `bag_to_geotiff` | (offline, reads a bag) | GeoTIFF on disk | Offline gridding tool. Reads pre-projected soundings (`-t /soundings`) or, with `-d /detections`, projects raw `SonarDetections` to soundings in-process via the same `DetectionsProjector` the node uses — no live graph, full CPU speed, deterministic. The `-d` path needs the projector frames to match the bag's (namespaced) frames; override with `--base-link-frame` / `--level-frame` / `--tide-frame` (see *Configuring frames per platform* below), or the grid comes out empty (a one-line warning is printed). |

`soundings` carries seven `float32` fields per point, in order: `x`, `y`, `z`,
`intensity`, `vertical_uncertainty`, `horizontal_uncertainty`, `beam_angle`.

**Read the two uncertainty fields carefully: despite the names, they are
VARIANCES in m², at one sigma, with no confidence-interval scaling applied** —
`cube::Sounding::vertical_error` / `horizontal_error` verbatim (see
`include/cube_bathymetry/sounding.h`). A consumer that treats
`vertical_uncertainty` as a σ in metres reads the wrong number, and in either
direction: a variance and its own square root agree only at 1 m². Below that
the raw value is the smaller of the two and the band is understated (0.25 m²
read as 0.25 m against a true σ of 0.5 m); above it the raw value is larger
and the band is overstated (4 m² — what the default `Vessel::gps_drms` of 2 m
contributes to `horizontal_error` on its own — read as 4 m against a true σ of
2 m). Take the square root. Any 95%/99% figure is produced downstream by
scaling that square root. `vertical_uncertainty` is a one-dimensional error about depth;
`horizontal_uncertainty` is radial (drms-derived), so its square root is a radius
in the horizontal plane rather than an error along a single axis. `beam_angle` is
the beam's incidence angle relative to nadir, in radians. `intensity` is the
per-beam acoustic backscatter copied from `SonarDetections.intensities` — usually
uncalibrated, but reflectivity in dB for the Kongsberg M3 (via `kongsberg_em_bridge`);
it is `NaN` when the source omits intensities. The positions are in the detections'
own frame (`header.frame_id`); `cube_bathymetry_node` georeferences them with a TF
lookup `map_frame ← header.frame_id` at the ping stamp. Consumers read fields **by
name** (`cube_bathymetry_node` and `bag_to_geotiff` both use named PointCloud2
iterators), so the field order is not load-bearing.

## The `grid` `elevation` convention

`elevation` is the **WGS84 ellipsoid height of the seabed in metres, positive up**.
It is not a depth below the water surface and it is not map-frame z (`map` is a
tangent plane at the datum). A seabed is therefore **below the water surface's
ellipsoid height**, and whether that makes `elevation` negative depends on where the
surface sits relative to the ellipsoid. Near sea level it is negative, shifted by the
geoid separation (tens of metres; about 28 m at the UNH pier). On a lake whose
surface is above the ellipsoid it is positive: Lake Massabesic's full pool is
+48.88 m ellipsoidal, so a seabed 10 m deep is at about +38.9 m. Do not read the
sign as "depth". This is not a new convention, only newly stated for this message: the node takes each sounding's height from the
ECEF -> geodetic conversion of its `earth`-frame position (`GeoSounding` keeps
`point[2]`, `grid_projection.cpp` copies it into the layer unchanged), stored depths
are WGS84 ellipsoidal heights (uma ADR-0002 §D4, see *Depth-adaptive multi-level
stores* below), and `ssp_ray_tracer.h` documents the same quantity as "an
elevation, positive up". `test_publish_equivalence` pins it
(`ElevationIsEllipsoidHeightPositiveUp`).

`uncertainty` is the 95 % confidence half-width, `1.96 sigma` in metres
(`stddev_to_confidence_interval_scale` in `parameters.h`).

**Depth below the water surface** is `h_surface - elevation`, where `h_surface` is
the ellipsoid height of the water surface, positive for a seabed below it.
`h_surface` must be an ellipsoid height too: do not subtract the `z` of a `map`
frame. The `map -> map_tide` transform carries only a z translation, which is the
boat's average odometry z, a tangent-plane coordinate at the boat's position. Read
as an ellipsoid height it is low by about `d^2 / 2R`, with `d` the boat's horizontal
distance from its map origin and `R` the earth radius: about 8 cm at 1 km, 0.3 m at
2 km and 2 m at 5 km. Fine near the first fix, wrong at long range; a consumer that
needs better must bound `d` or model the surface.

## Starting a fresh sheet without restarting

A running node can start a new live session in two ways, and
`scripts/reset_cube_nodes.sh` (installed to `lib/cube_bathymetry`) picks the right one
for each node:

| Way | What it does | `ros2` calls |
|---|---|---|
| the `~/reset` service (`std_srvs/Trigger`) | replaces the session state in RAM; the node keeps its lifecycle state; **no tile is written to disk** (loading a prior store can rename a legacy `survey/` directory in it to `processed/`, exactly as `configure` does) | 1 `service call` per node, plus 1 `service list` for all of them |
| the lifecycle walk | `deactivate` -> `cleanup` -> `configure` -> `activate` | about 17 per CUBE node, about 14 per consumer |

The time goes into the `ros2` command-line start-up (3 to 8 s per call under load), not
into the transitions, which is why the service path is faster: four boats and the
assessor take 6 calls instead of about 80. Measured on the four-boat simulation on
2026-10-13 (the full stack running on one host), two back-to-back resets took 19.2 s and
17.7 s (about 3 s per call), where the walk took 318 to 324 s.

```bash
# the common case: services where the nodes have them, the walk where they do not
ros2 run cube_bathymetry reset_cube_nodes.sh --consumer /coverage_assessment \
  /boat1/cube_bathymetry /boat2/cube_bathymetry /boat3/cube_bathymetry /boat4/cube_bathymetry

# or one node by hand
ros2 service call /boat1/cube_bathymetry/reset std_srvs/srv/Trigger '{}'
```

### The `~/reset` services

| Node | Service | Answers in | Refuses (`success: false`, nothing changed) when |
|---|---|---|---|
| `cube_bathymetry_node` | `~/reset`, e.g. `/boat1/cube_bathymetry/reset` | `inactive`, `active` | the node was configured with a `draft_dir`; or the running session primed prior tiles and the rebuild primes none (the prior directory is missing, unmounted, empty or of the wrong level); or the prior cannot be loaded; or nothing is configured |
| `coverage_assessment_node` | `~/reset`, e.g. `/coverage_assessment/reset` | `inactive`, `active` | the map frame has moved since the raster georeference was captured; or nothing is configured |

A service exists only while its node is configured (it is created last in `configure` and
released with the other entities), so a missing service means an unconfigured or finalized
node, or an older binary. The response says what was reset, for example
`3 tile(s) dropped from RAM; prior_store_dir unset; node is active; no tile written to disk (loading a prior store renames a legacy survey/ directory, as configure does)`;
an `inactive` node is reset too, and the message says it publishes no `grid` until it is
activated (`reset_cube_nodes.sh` then activates it, see below).

**`cube_bathymetry_node`.** It builds the new session aside and swaps it in only when
complete, so a failure (the response says why) leaves the running session untouched. The
new session has the same sheet-level settings as the old one (`capture_spacing_scale`, the
backscatter curve that arrived on `sonar_info`, which is not redelivered) and re-reads
`prior_store_dir`, so the blunder gate is active from the first ping. A prior that cannot be
loaded **fails the reset** (the running session, which has the gate, is kept and the response
says why), whereas a `configure` only warns and runs ungated: replacing a gated session
with an ungated one is worse than keeping the old one. The same holds for a prior that
loads **without error but primes nothing** (the directory is missing or unmounted, empty,
or holds only tiles of another level): the node remembers how many prior tiles the running
session primed, and when that is more than zero and the rebuild primes none, the reset
is refused and the running session kept. A node whose prior never primed anything (an
unset `prior_store_dir`, or one that was already empty at `configure`) has no gate to lose
and is reset as usual. Like `configure`, it
starts a new sheet and tile catalog and clears the evicted-tile markers, the disk-serve
queue, the refresh tracker, the saving ledger and the publish cursor. Publishers,
subscriptions, timers and parameters are not touched. The prime is a whole-store disk read
that **blocks the node's executor while it runs**, with the node active (a `configure` does
not): pings that arrive meanwhile are dropped once five are queued (the subscription is
best-effort, depth 5). That is one reason not to reset underway. Two more: a reset while
the node is `active` makes the next collision-avoidance `grid` window hold only the new
session's cells, and unknown cells in the window are `NaN`, which the costmap treats as
lethal (`unsurveyed_is_lethal`) around the vessel; and the **peak RAM** is the old sheet, plus
the whole prior store, plus the new sheet, all at once. With `draft_dir` empty, the only case
the service serves, the old sheet never evicts, so it is at its largest exactly when a reset
is wanted. (The old sheet is not dropped first, because a failed build must leave it
intact; running out of memory while loading the prior fails the reset and keeps the old
session.)

**On a boat with `draft_dir` set, the service does not help.** It refuses, because the saved
tiles would reload (the sheet would not be fresh) and dropping the unsaved ones would break
"persist before drop". Real-boat configurations are of this kind
(`bizzyboat_project11/config/bizzyboat.yaml` sets `draft_dir: /home/field/data/world/depths`),
so the 2026-08-27 operator request to start a new live session on a boat stays open
until the on-disk session work in rolker/unh_marine_autonomy#391 lands. The service serves
the simulation and the demo (no `draft_dir`) and boats run without persistence (which never
evict, so RAM grows).

**`coverage_assessment_node`.** It empties every source's union (and a grid it kept because
its frame could not be resolved: otherwise the old cells would come back on the next tick),
clears the `max_held_cells` latch, and (when active, with the areas projected) publishes the
emptied `coverage_assessment` and `coverage_grid` itself, so the zero shows at once and the
old run's coverage does not stay latched on either topic; the next tick publishes again. It keeps the survey areas and their projections, the subscriptions, TF, **the raster
georeference**, and the counters, which stay cumulative. Because it keeps the georeference
it refuses when the map frame has moved: that state needs the lifecycle walk, which captures
the georeference again, and the script falls back to the walk for that consumer. An inactive node
publishes the emptied result when it is next activated, which the script does right after
the reset. A node whose areas are not yet projected publishes nothing at reset time; until its
first tick that can assess, the old messages stay latched.

### The script

`reset_cube_nodes.sh [--allow-persistence] [--consumer NODE]... NODE...`. Name each node
once: a node given twice, or as both a CUBE node and a `--consumer` (`/a/b`, `a/b` and
`/a/b/` are the same node), is refused before anything is touched. In order:

1. **classifies** every node with one `ros2 service list -t`: a node that lists
   `<node>/reset [std_srvs/srv/Trigger]` is reset by service, any other node takes the walk
   (a `/reset` of another type is not called: a `Trigger` request to it would hang the
   client or mean something else). An empty or failed
   list is retried and then stops the script before any change; it is never read as "no
   node has a service". `--allow-persistence` sends every CUBE node down the walk;
2. **checks everything that can refuse**, before the first service call: every walk-class
   node and `--consumer` must answer and be in a state the script can walk (`active`,
   `inactive` or `unconfigured`; a mistyped name or a finalized node stops the run here),
   and a walk-class CUBE node with a `draft_dir` is refused (see below). These checks
   change nothing, with one exception: a walk-class CUBE node that was never configured is
   configured and cleaned up to read its `draft_dir`, and if that probe fails part way the
   node can be left `inactive`;
3. **resets the CUBE nodes that have the service**, in the order given. The service
   refuses inside the node, so a boat with a `draft_dir` is found when the script reaches
   it: boats before it are already reset (a reset only replaces RAM state, so this
   costs nothing), the script stops at the first refusal, prints which nodes were reset and
   which refused, touches nothing after it and exits 1. Re-running is safe;
4. walks the other CUBE nodes to `unconfigured`;
5. resets each `--consumer`, by service or by the walk (`unconfigured` and back to
   `active`). A consumer whose service refuses (the map frame moved) falls back to the walk,
   after printing the reason;
6. configures and activates the walked CUBE nodes, and prints the states of the walked
   nodes. A service node is reported by its own response, with no extra call.

A node whose service answers `inactive` (configured but not activated, as a failed run
can leave it) was reset but would publish nothing, so the script **activates it right after
its reset**, names it, and ends with `reset done; activated after the reset (they were
inactive): <nodes>`. That makes the command converge: a node that a failed walk or a failed
probe left `inactive` is configured, so it serves `~/reset`, and re-running the same command
resets it and activates it. If the activation fails, the script fails (exit 1) with the
state listing and the re-run hint.

Exit status: 0 reset done, every node active; 1 a transition, read or service call failed,
or a CUBE node refused (a consumer that refuses is walked instead, step 5); 2 usage.

**Ordering, honestly.** The walk quiesced every CUBE node before the consumer was reset,
so no `grid` crossed the reset in either direction. With services the CUBE nodes keep
publishing. A `grid` that an old sheet published just before its reset, and that is still
queued in the assessor's subscription (reliable, depth 10, at most about one per node per
5 s), would be folded into the emptied union and stay, because unions only grow. In the
other direction, cells a reset CUBE node accumulates before the assessor is reset are wiped
from the union and come back only through that node's next 200 m window (about 5 s later),
so cells outside the window are lost. Both windows are the gap between the CUBE call and the
consumer call. The script resets the consumer only after every CUBE node has answered, which
bounds the first, and relies on the boats **not being underway**, which makes the second
harmless. A guard on the assessor against the first is possible (it would drop grids stamped
before its reset) but is not made: the `grid` stamp is the stamp of the latest ping that
changed a tile, so the guard would need the sonar stamps and the assessor's clock to share a
time base across hosts, and it does nothing for the second. A consumer that takes the walk is
immune: its subscription is destroyed.

**Time bounds and retries.** Every `ros2` call is bounded by `timeout --foreground` (so
Ctrl-C still reaches `ros2`): `RESET_CUBE_GET_TIMEOUT` (default 30 s) for reads,
`RESET_CUBE_SET_TIMEOUT` (default 300 s) for transitions, and `RESET_CUBE_CALL_TIMEOUT`
for a service call, because the reset reads the prior store from disk. The call timeout
defaults to the transition timeout, so a prior store that a `configure` can load within
its bound is not failed by the service path. A
read (`ros2 service list`, `ros2 lifecycle get`, `ros2 param get`) that fails or times out is
retried, 3 attempts in all (`RESET_CUBE_GET_TRIES`) with a 2 s pause
(`RESET_CUBE_RETRY_PAUSE`); only when every attempt fails does it fail the script, and the
preflight reads do that before anything has changed. A service call that fails fast (the
CLI exits non-zero, or answers with no `Trigger` response) is retried too, with its own
budget of 2 attempts (`RESET_CUBE_CALL_TRIES`): a reset only replaces RAM state, so
repeating it is harmless. A call that **times out is not retried**: its request is still
queued in the node, and a second one would run a second whole-store prior load behind it
with the executor blocked again. The script stops, names the node and says it may or may not have been reset
(the first node's timeout does not read "nothing was changed").
Whether a node has the service is decided only by the classification, never by the outcome
of a call, so a slow node is never walked after it already reset. A transition is never retried: a repeated configure or cleanup is not the
same operation as the one that failed. The script ignores SIGHUP and SIGPIPE so a dropped
ssh session cannot kill it between the quiesce and the bring-up; run it under `tmux` or
`nohup` anyway if the link is unreliable, because a script killed midway leaves walked boats
without `grid` until it is run again.

### The lifecycle walk

The sheet is also rebuilt on every `configure`, so any node can start a new survey by being
walked through `deactivate` -> `cleanup` -> `configure` -> `activate`, by hand (`ros2
lifecycle set <node> deactivate`, `cleanup`, `configure`, `activate`) or by the script. It
is the fallback for a node without the service (an older binary, a node that was never
configured), the way to reload `draft_dir` deliberately (`--allow-persistence`), and the
only way to recover an assessor whose map frame moved. The script stops at the first failed
transition and names the node and the transition. After the first change, a failure also
prints the state of every walked node and says to re-run; running it again is safe (a node
that is already reset is simply reset or cycled again) and recovers a node the failure left
`inactive`: it is configured, so the re-run resets it by service and activates it. The preflight warns for every walked
CUBE node that is not `active` (an earlier run may have stopped part way), and a failing
probe of a never-configured node is reported as a failed probe, not as a failed reset. A
node that was never configured has no readable `draft_dir` yet (parameters are declared at
configure), so once every other check has passed the script configures it, reads the
parameters and puts it back to `unconfigured`, one node at a time; a refusal at that point
has still reset nothing. If a transition or a read fails during that probe, the node may be
left `inactive` (configured); the script says so. A configured node serves `~/reset`, so
re-running the same command resets it by service and activates it.

**Do not run it underway.** On the walk, between the quiesce and the bring-up the walked
boats publish no `grid`, which is the collision-avoidance grid, and each `ros2` call takes
seconds, so the gap is not short. A service reset blocks the node's executor for as long as
the prior store takes to read, and the ordering note above assumes stationary boats.

**What a fresh sheet is.** The service and `configure` both build a new `GeoMapSheet` and
reset the tile catalog, the sub-window refresh tracker, the evicted-tile markers, the
disk-serve queue and the publish cursor (last publish time, cached `map <- earth`
transform and vessel fix). The walk's `cleanup` also releases the publishers,
subscriptions and the TF listener and buffer, so nothing of the old configuration keeps
running. With `draft_dir` and `prior_store_dir` empty, the first `grid` after the reset
contains none of the previous window's cells (`test_node_lifecycle`). Both paths run the same
two functions (`buildSession` and `adoptSession`), and `test_node_lifecycle` checks the
same list of session fields after each, so a field one path forgets fails in both.

**What is not cleared.**

- **Saved tiles when `draft_dir` is set.** `deactivate` and `cleanup` flush the
  dirty tiles to `<draft_dir>/draft/`, and `configure` reloads everything saved
  there (and under `processed/`) into the new sheet. That is deliberate (ADR-0001: never
  lose survey data), and it means the sheet is **not** fresh. The live tile store is a
  derived product for the live view, not the record: the raw bags and offline
  processing are the authoritative path, which supersedes the ADR's wording that the store
  is the product of record. How a session is identified on disk is rolker/unh_marine_autonomy#391.
  The service and the script refuse when `draft_dir` is set. For a fresh sheet that keeps
  persistence and eviction on, point `draft_dir` at a NEW, empty directory
  (`ros2 param set <node> draft_dir /path/to/new/empty/dir`; it is writable, and the
  next `configure` reads it) and run the script with `--allow-persistence`: `cleanup`
  flushes the old sheet to the old directory, and `configure` finds nothing to reload
  in the new one. Do **not** set it to `''`. An empty `draft_dir` means no persistence
  and no eviction, so the new sheet's RAM grows without bound on a long survey, and
  `cleanup` flushes against the directory the node was *configured with*, not the
  value set afterwards, so it can still write to (and be refused by) the old one. (The
  service likewise judges the directory the node was configured with, so clearing the
  parameter does not unlock it.)
  Nothing on disk is ever deleted by a reset. If the final flush fails (disk full,
  permissions), `cleanup` fails and the node stays `inactive` with its sheet, rather
  than discard tiles that were never written; fix the cause and run the script again. While the cleanup
  is refused the save timer is stopped and nothing retries the write: the tiles stay in
  RAM until cleanup is requested again. A `shutdown` transition (from `unconfigured`,
  `inactive` or `active`) makes one last attempt to write them and then completes
  whether or not it succeeded, releasing the node's timers, subscriptions and
  publishers as cleanup does. A process stopped with SIGINT or SIGTERM (Ctrl-C, or
  `ros2 launch` stopping its nodes) makes the same last attempt after it stops
  spinning, and logs `Stopping: wrote the N tile(s)...` (or, on failure, how many
  were lost). Only a hard stop (SIGKILL, a crash, power loss) loses whatever the
  periodic save has not written; `ros2 launch` sends SIGKILL to a node that has not
  exited after its SIGINT and SIGTERM timeouts, so a flush slower than that is cut off.
  Tile writes are not atomic yet: a stop that lands mid-write can leave that tile
  file damaged, not just unsaved.
- **Nothing is saved when `draft_dir` is empty.** That is what makes the sheet fresh,
  and it also means the reset **permanently discards the in-RAM survey**: the old
  window exists nowhere afterwards. Save or export what you want to keep first. With
  an empty `draft_dir` the node also never evicts cold tiles, so RAM grows for as long
  as the survey runs.
- **`prior_store_dir`.** It seeds a predicted surface for the blunder gate, not
  coverage, and is read again on every configure and on every `~/reset`. The script only
  prints a note for a walked node; a reset by service reports what was primed in its
  response.

**Parameters between cycles.** Parameters are declared once and survive `cleanup`,
so the value a `configure` reads is the one set last, whether by launch file or by
`ros2 param set` between cycles. `read_only` parameters keep refusing runtime sets,
so a wrong `capture_spacing_scale` or `publish_dirty_subwindow` cannot be corrected
by a reset: it takes a restart of the node with the right launch value. The service reads
no parameter again: it re-applies the values the last `configure` read.

**Event and demo configurations.** Some project11 configurations set `draft_dir` for
the CUBE node: `bizzyboat_project11/config/bizzyboat.yaml` sets
`draft_dir: /home/field/data/world/depths` under `/**/cube_bathymetry`, and
`bizzyboat_sim_core_launch.py` layers that file, so a launch that loads it will
reload the saved tiles on every walked reset and refuse the service. A demo that wants a
fresh sheet between runs must leave `draft_dir` empty for the CUBE nodes (the refusal tells
you when it is not), which is fine for a short demo but never evicts, or give each run a
new empty `draft_dir` and use `--allow-persistence`, as described above.

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

## Coverage assessment (`coverage_assessment`)

The `coverage_assessment` package, next to this one, scores target areas against a
survey standard from one or more `grid` topics. Design, rules, frames and the
water-surface assumption are in [`coverage_assessment/README.md`](coverage_assessment/README.md);
every parameter is `read_only` (set it from YAML or a launch file):

| Parameter | Default | Meaning |
|---|---|---|
| `grid_topics` | `[]` (required) | One CUBE `grid` topic per source; the order defines `sources` in every message. Empty or duplicated fails configure. |
| `source_names` | `[]` | Per-source label; an empty entry or list falls back to the topic. Same length as `grid_topics` or empty. |
| `survey_area_topic` | `survey_area` | `SurveyArea` input (transient_local). |
| `assessment_period` | `1.0` s | Timer period. |
| `cell_size` | `1.0` m | Assessment lattice; should equal this node's `cell_size`. |
| `map_frame` | `map` | Frame of the assessment raster and of both outputs; sources in other frames are transformed into it. |
| `water_surface_frame` | `map_tide` | One boat's tide frame (e.g. `boat1/map_tide`); applied to every source. |
| `water_surface_boat_frame` | `base_link` | That boat's base frame, for the distance bound. |
| `water_surface_max_boat_distance` | `1500.0` m | Beyond this, scoring is skipped with a WARN. |
| `water_surface_max_age` | `30.0` s | Older than this, the tide (or the tide boat's frame) is refused and scoring is skipped with a WARN. |
| `max_raster_cells` | `1000000` | Per-area raster cap. |
| `max_pass_cell_checks` | `16000000` | Work budget of one pass: the bounding-box cells of all held areas times the number of sources; an area that would exceed it is refused with an ERROR. |
| `max_held_cells` | `100000000` | Most cells held for all sources together (about 800 MB); once over it nothing more is folded in and nothing is published, with an ERROR, until reset (the `~/reset` service, or the lifecycle walk). |
| `max_area_vertices` | `10000` | Most vertices in one area polygon; a definition with more is refused on arrival with an ERROR. |
| `max_areas` | `20` | Most areas held at once (1 to 100); a new id beyond it is refused with an ERROR. |

Because it keeps a union over time, a CUBE reset must also reset the assessor: pass it
to `reset_cube_nodes.sh` as `--consumer` (see *Starting a fresh sheet without restarting*),
or call its `~/reset` service (`std_srvs/Trigger`, e.g. `/coverage_assessment/reset`), which
empties the unions and keeps the areas, the raster georeference and the counters, and
refuses when the map frame has moved.

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
share `bag_to_geotiff`'s projection pipeline and write the **`processed`** layer
(the store's on-disk layers are `processed/`, `draft/`, `reference/` and
`chart/` — uma ADR-0010 D8 re-split the fused survey layer into `processed` +
`draft`, and a legacy `survey/` auto-migrates to `processed/` on load; the
off-boat re-run is authoritative).

| Tool | RAM | Output | Use when |
|---|---|---|---|
| `import_bag` | bounded by `--max-resident-tiles` (persist-then-drop eviction, #92); with `--depth-adaptive` the budget is the **store-wide total across levels** (#143) | lossless, but a tile evicted mid-disambiguation has a slightly re-derived depth **uncertainty** (depth value faithful) | streaming / very large surveys where RAM is the constraint |
| `batch_regen_bag` | bounded by one tile's soundings | **bit-exact** vs a whole-survey-in-RAM build (depth, uncertainty, and backscatter); with `--level-plan`, bit-exact vs `import_bag --depth-adaptive` | the authoritative off-boat product |

Both tools take `--capture-spacing-scale <k>` (default 0.71): the node capture
distance is `max(0.05 × |depth|, k × node spacing)`. CUBE's fixed 0.5 m floor
(`Capture_Distance_Minimum`) is gone (#143) — the spacing term replaces it, so a
fixed-level run at level 10 now gathers within 0.71 m instead of 0.5 m below
~14 m of water, and deeper the depth term dominates as before. (0.71 m, not
0.64 m: `Parameters::distance_scale` takes the **requested** cell size, which is
`-r 1.0` by default, so `k × spacing` is 0.71 m on a default fixed-level run.
The depth-adaptive sheets are built at each level's nominal cell
(`requestedCellSizeFor`), so a level-10 sheet there gathers within
0.71 × ~0.906 = 0.64 m.) The live node
applies the same gate at its fixed level and takes the same multiplier as the
ROS parameter `capture_spacing_scale` (default 0.71), so a deployment can pin
it — to `0.5 / cell_size` to reproduce the pre-#143 gate exactly, say — without
rebuilding. That parameter is **`read_only`**: it is read once at `configure`
and pushed into the map sheet there, so set it from the launch file or a YAML
overrides file — a runtime `ros2 param set capture_spacing_scale` is rejected
rather than silently ignored (a set that "succeeded" while the sheet kept the
old gate would be worse than a refusal).

### Depth-adaptive multi-level stores (`--depth-adaptive`, #143)

`import_bag --depth-adaptive` writes a store whose native tiles sit at
different GGGS levels depending on the water: shallow ground gets fine tiles,
deep ground coarse ones, and **coarse parent tiles stay estimated in full under
their children** — the parent is the LOD level above them and the native tile
for the unrefined remainder. The decisions are recorded in the
[ADR-0002 amendment](cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md);
in short:

- A **recon pass** over the bags counts every sounding into a fine count grid
  (`--count-level`, default = the ladder's finest level, 14) and spills the
  projected soundings to scratch as **one chronological file** (`--scratch-dir`,
  default beside the `-o` store, never `/tmp`; ~64 B per sounding). Free space
  is checked before the pass starts, budgeting that spill plus an allowance of
  the same size for the count-tile spill below — an allowance, not a bound: the
  count term follows the ground covered, which nothing knows up front. The
  count grid is bounded too: `--count-resident-tiles`
  (default 256, ~460 MB of level-14 tiles) caps what stays in RAM and colder
  tiles go to the same scratch directory as UInt16 GeoTIFFs, reloaded on demand;
  the plan report states the resident peak. One recon structure is **not**
  bounded by that budget: the per-level-14-grid decision-depth histograms, which
  are all needed when the plan reads each grid's percentile and so are never
  spilled. They are a small term beside the count tiles (≤ ~50 kB per grid
  against a count tile's 1.8 MB — roughly 17 MB/km² against ~630 MB/km²) but
  they grow with the ground covered, so the recon reports their resident size
  next to the count grid's rather than letting the bounded-RAM claim cover them
  silently.
- Each tile's level is the **coarser** of what the depth *requires* (the
  uma#369 ladder: cell = `--depth-adaptive-scale` × depth, clamped to
  `--depth-adaptive-coarsest`..`--depth-adaptive-finest`, default 8..14, and
  the finest level must be ≤ 14, the survey index's footprint level) and what
  the data *achieves* (Calder's level of aggregation: the finest spacing at
  which each node still gathers `--min-obs-per-node` soundings, inflated by
  `--blunder-allowance`). Where the depth requires finer than the data achieves
  the plan reports a **coverage deficit**.
- Both decisions read a **percentile**, not an extremum, so one flier cannot
  drive a tile's level:
  - `--decision-depth-percentile <p>` (default 2) is the percentile of a
    level-14 grid's *shallowest* soundings that sets its depth — the flier
    guard on the depth side. What is measured is the **water depth under the
    transducer** (`-|sonar_relative_position.z|`, negative-down), *not* the
    sounding's stored depth: stored depths are WGS84 ellipsoidal heights
    (uma ADR-0002 §D4) and the geoid runs tens of metres from the ellipsoid
    (~28 m at the UNH pier), while the ladder's argument is a footprint
    argument — a beam's footprint scales with its range below the transducer.
    Feeding the ellipsoidal height would coarsen every tile by one to two
    levels. Transducer draft is deliberately ignored: sub-metre, against a
    ladder whose levels are a factor of two apart.
    The recon holds a **depth histogram** per grid
    (0.25 m bins, sparse), so the percentile is honoured at survey density —
    a level-14 grid of a real survey line holds ~200 k soundings and its
    water-column fliers run to many hundreds. The answer is the shallow edge
    of the bin the rank falls in: under a bin-width shallower than the true
    percentile, never deeper. Any **0 < p ≤ 100** is accepted; 0 is rejected
    (it would make a single flier the decision depth).
  - `--achieved-percentile <p>` (default 95) is the percentile of the level of
    aggregation across a tile that sets its achieved level, so a few
    under-covered nodes do not coarsen the whole tile. It votes over the
    tile's **occupied** count cells only — a cell the survey never ensonified
    says nothing about the resolution the surveyed part supports, whereas a
    gap *between* lines does (the occupied cells around it see the gap in
    their boxes). It is a **conservative tail** statistic, which is what
    Calder's 0.95–0.99 range is for: the answer is the level that 95 % of the
    surveyed ground reaches *or beats*, not the level the typical node
    reaches.
    That tail is why a dense survey still reports a coarse achieved level, and
    the number is not a symptom. On the 2026-06-09 UNH pier bag (12.1 M
    soundings over 0.0085 km², ~1,400 soundings/m², ~4.6 per level-14 cell)
    the level-of-aggregation histogram over tile `10/17801/13988`'s 764 k
    occupied cells is: 37 % reach level 14 outright (λ = 0), a further 54 %
    reach level 12 (λ = 1) — 91 % of the ground at level 12 or finer — and the
    remaining 9 % runs out to λ = 43 over the thin fringe of the swath and the
    gaps between lines. p50/p75/p90 are all level 12; **p95 is level 11** and
    p99 level 10. The neighbouring tile `10/17801/13989` is thinner (3.3
    soundings per occupied cell against 7.8) and reaches 93.4 % at level 11,
    just short of the rank, so its p95 lands at level 10. Raising
    `--achieved-percentile` toward 50 would report the modal level 12 — and
    would store a tile whose outer tenth has no estimator support. The default
    stays conservative; what the report calls a coverage deficit here is
    real thin coverage at the swath edges, not a mis-measurement.
- `--level-plan-out <file>` is **recon only**: it writes the plan and prints
  its report — per level, the tile count, the **surveyed ground** those tiles
  cover and the surveyed ground for which they are the finest tile
  (`native`), and the storage (dense and at the observed ~2.4 MB/tile fill);
  then the estimate-count multiplier that parents-alive costs, the coverage
  deficit, and the ground stored coarser than level 10 (resolution lost
  against today's stores in >36 m water; inherent to the pinned ladder).
  Every area is ground the survey ensonified, measured from the count grid's
  occupied cells — not a tile footprint, which for a level-8 parent over one
  survey line is three orders of magnitude larger — and the coarser-than-10
  line counts only ground whose *finest* tile is coarser than 10, so
  parents-alive tiles over ground that also has a level-10 tile do not
  register as lost resolution. Then it exits, so the operator can approve before a multi-hour
  import. `--level-plan <file>` reuses that plan for the import;
  `--count-grid-out <dir>` persists the count grid (mergeable into a later run).
- Phase two replays the spill front to back — arrival order, the order the
  fixed-level path saw the pings, which is what makes the byte-identity below
  hold — into **one CUBE accumulator per level**;
  `--max-resident-tiles` is the store-wide total, evicted coldest-first across
  levels. Every import writes `build_fingerprint.json`
  ([ADR-0003 amendment](cube_bathymetry/docs/decisions/0003-staleness-fingerprint.md)).
- A later import at other levels is **additive**, like any other import: a
  re-import over covered ground under a different policy leaves the earlier
  finer tiles in place, and a fine-LOD reader prefers them.
- With the policy pinned to one level the depth-adaptive path is
  **byte-identical** to the fixed path (`test_mixed_level_import`).
- The two-phase path is also exercised **end to end on a real bag**
  (`test_real_bag_smoke`): the recon's reported numbers, byte-identity between
  `import_bag --depth-adaptive` and `batch_regen_bag --level-plan` on real
  soundings, and the three documented failure outcomes with a fault injected —
  a truncated spill and a failed `finalize()` both abort through
  `abortDirtyReplay` (exit 1, "NOT empty and NOT complete"), while an
  unwritable `build_fingerprint.json` over a store that IS complete is exit 2
  and says so. Every other test drives the library directly, so nothing else
  reaches `import_bag`'s `main()` past argument validation. It runs against a
  3,000-ping excerpt of the 2026-06-09 UNH pier M3 bag, which is **survey data
  referenced by path**, not a committed fixture (18 MB); it lives beside its
  source bag in the archive and `scripts/make_bag_excerpt.py` rebuilds it.
  Point `CUBE_REAL_BAG_EXCERPT` elsewhere to override the path. Where the
  excerpt is unreachable — a CI container, a host with no archive mount —
  every case **SKIPS with the reason printed, and never passes**;
  `.agents/ci_local_extra.sh` re-states that skip as its own CI line so a run
  that never exercised the path cannot read as a green one.
- `--bs-store` is **refused** on both mixed-level paths (`import_bag
  --depth-adaptive` and `batch_regen_bag --level-plan`) for now.
  `marine_mbes_backscatter_store` is single-level by construction ("All tiles
  live at a single GGGS level"; `loadTile(path, level)` rejects a tile written
  at another level), while every level is handed the same store root — so a
  mixed-level run would write a backscatter store nothing can load. The
  per-level layout is
  [uma#383](https://github.com/rolker/unh_marine_autonomy/issues/383); the PR
  that consumes it lifts the refusal. Mixed-level backscatter is gated on that
  work, not abandoned: take backscatter from a separate fixed-level run
  meanwhile.

`batch_regen_bag --level-plan <file>` rebuilds a depth-adaptive store from the
same plan (each tile gathered at its own level), and its `--index-db` dry run
then rolls the dirty set up to the emitted tile at every plan level. The plan
records the `--capture-spacing-scale` its import ran with, so the rebuild uses
that gate by default and **refuses** an explicit value that disagrees —
otherwise "bit-exact vs `import_bag --depth-adaptive`" would hold only when the
two runs happened to share a CLI default. `import_bag --level-plan` applies the
same rule.

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
conservative margin, rolls it up to the store's tiles via the GGGS parent
hierarchy (the fixed store level, or with `--level-plan` every emitted level of
the plan), and lists each dirty tile with the bags + pass intervals that
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

The JSON's top-level level field follows the mode: a fixed-level run emits
`"store_level": <L>`, and a `--level-plan` run emits `"store_levels": [<L>, …]`,
the plan's emitted level set. A plan-driven dirty set carries **mixed** per-tile
`"level"`s, so there is no single store level to report, and emitting one
derived from `-r` would have a consumer rebuild at the wrong level.

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

### Deriving a curve (`derive_angular_response.py`)

`ros2 run cube_bathymetry derive_angular_response.py` bins each beam's
intensity by `|rx_angle|` over one or more bags and writes the curve CSV that
`--backscatter-curve` (and the SonarInfo curve delivery) consumes. With
`--remove-tl` (tier-2, #87) each beam's two-way transmission loss
`40 log10(R) + 2 alpha R` is added back before binning, so the residual curve
transfers across depths, and alpha is written into the header for the
estimator to apply verbatim.

```bash
# fresh water (a lake): alpha from the full Francois-Garrison model at S = 0
derive_angular_response.py <bags...> -t <detections topic> -o curve.csv \
    --remove-tl --water-temp-c 24

# salt water: give the salinity too (pH defaults to 8.0)
derive_angular_response.py <bags...> -t <detections topic> -o curve.csv \
    --remove-tl --water-temp-c 15.5 --salinity 31

# or a path-averaged alpha worked out from the site's casts
derive_angular_response.py <bags...> -t <detections topic> -o curve.csv \
    --remove-tl --absorption-db-per-m 0.130

# only the pings inside a site box, from bags that also hold transit
derive_angular_response.py <bags...> -t <detections topic> -o curve.csv \
    --box 42.95 -70.65 43.00 -70.58 --remove-tl --water-temp-c 15.5 --salinity 31
```

**Choosing the absorption (#170).** alpha matters at multibeam frequencies: at
500 kHz fresh water at 24 C gives 0.049 dB/m, while the Isles of Shoals water
in August 2026 (about 31 psu, 11 to 20 C) gave 0.12 to 0.14 dB/m. Using the
freshwater value there under-compensates the two-way loss by about 3 dB at
20 m slant range, so the swath edges in deeper water render dark. Salinity is
the dominant term in salt water; temperature is second order.

- `--salinity` (psu, default 0 = fresh water), `--ph` (default 8.0, no effect
  in fresh water), `--water-temp-c` and `--depth-m` feed the full
  Francois-Garrison (1982) model: boric acid, MgSO4 and pure water. At
  salinity 0 it gives exactly the freshwater value earlier versions computed,
  so an existing freshwater invocation writes the same data rows.
- Take temperature and salinity from CTD or sound-speed casts at the site,
  not from the boat's surface temperature alone. In a stratified water column
  work out a path-averaged alpha over the depths surveyed and pass it with
  `--absorption-db-per-m`. The override needs `--remove-tl` and cannot be
  combined with `--salinity`, `--ph` or `--depth-m`; `--water-temp-c` is then
  optional and only recorded.
- Derive a curve for each site from that site's own bags, and use `--box` when
  those bags also cover transit or another site. A freshwater curve applied to
  a salt-water survey carries the wrong alpha in its header, and the estimator
  applies whatever alpha the header says.

**`--box LATMIN LONMIN LATMAX LONMAX`** (WGS84 degrees) keeps only pings whose
sensor position is inside the box. Positions come from the bags' own TF tree,
the way `import_bag` georeferences:

- All the bags on the command line are read as one time-ordered stream into
  one TF buffer, so split files work whether they are given together or in any
  order, and a `/tf_static` recorded only in the first split still applies to
  the rest.
- `/tf` and `/tf_static` from every namespace go into that one buffer. In a
  bag that records several vehicles their TF trees share it, so their frame
  names must not collide (`import_bag` has the same limitation).
- Each ping waits until the TF stream is 3 s past its stamp, so the transforms
  on both sides of it have been read, and `earth -> <detections frame>` is then
  looked up exactly at the ping stamp, interpolated between them. There is no
  fallback to the latest transform. Pings still waiting when the bags end are
  looked up against what was read.
- The ECEF position of the sensor is converted to latitude/longitude.

The run prints how many pings were kept and how many were dropped, by reason:
outside the box, no earth transform for their frame, a stamp outside the
earth TF coverage (before the first earth transform, after the last, or in a
gap longer than the 60 s buffer), an unset (zero) header stamp, which tf2
would otherwise read as "the latest transform", or an earth transform that is
not a geocentric position (more than 50 km from the ellipsoid, such as a local
map frame named `earth`). A box with no pings in it is an error. A box
across the antimeridian is not supported.

**Header provenance.** Every line above the CSV column row is a
`# key: value` comment. The loader (and the marine_tools SonarInfo mirror)
reads only `tl_removed` and `absorption_db_per_m` and ignores the rest:

| Key | Written | Meaning |
|---|---|---|
| `tl_removed` | always | `true` for a tier-2 curve |
| `absorption_db_per_m` | tier-2 | the alpha the estimator applies |
| `absorption_source` | tier-2 | `computed` (Francois-Garrison) or `override` |
| `water_temp_c` | tier-2, when known | temperature used or recorded |
| `salinity_psu`, `ph`, `depth_m`, `frequency_khz` | computed alpha | the model inputs (frequency = median per-ping `ping_info.frequency`) |
| `tl_model` | tier-2 | the TL formula |
| `box` | with `--box` | `LATMIN LONMIN LATMAX LONMAX` |
| `source_bags` | always | comma-separated basenames of the input bags |

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
| `roll`, `pitch` (dominant per-beam term) | TF: `level_frame ← base_link_frame` at the ping stamp (interpolated to the exact time, not "latest within 1 s"). **Pitch is negated at the producer** (#144): `tf2::getEulerYPR` returns bow-down-positive pitch in FLU, while `cube::Platform` keeps Calder's bow-up-positive convention. Roll needs no flip — a right-handed rotation about +x already lifts port, which is Calder's sense |
| `heave` | TF: `tide_frame ← base_link_frame` at the ping stamp, **negated** (#144): the transform's `z` is the boat above the tide-corrected surface (REP-103, +up), while `cube::Platform::heave` is Calder's +down. Enters the budget only squared, so the sign is numerically inert and it defaults to `0` if the transform is absent |
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

## Projection diagnostics (what the warnings mean)

`detections_to_pointcloud` emits these throttled; `import_bag`, `batch_regen_bag`
and `bag_to_geotiff` fold the same counts into their end-of-run summary line and
warn once when either is non-zero.

| Signal | What it means | What to do |
|---|---|---|
| `N of M beams took the generic device across-track beamwidth` | The ping reported no usable per-beam `rx_beamwidths` for those beams (absent, too short, or non-finite / non-positive / ≥ π rad), so the **angular** part of the uncertainty budget came from `Device::across_track_beamwidth` — a hardcoded generic value belonging to no particular sonar. The soundings are still produced and are still used. | Nothing operationally, but read the uncertainty as approximate. **Today this is 100% of M3 beams**: `kongsberg_em_bridge` deliberately leaves `rx_beamwidths` empty, so every M3 sounding's angular term is the generic 2°. Tracked in [`marine_tools#82`](https://github.com/rolker/marine_tools/issues/82); the reasoning is in [`divergences_from_calder.md`](cube_bathymetry/docs/divergences_from_calder.md). |
| `N of M beams reported no usable receive angle` | `rx_angles` was absent, shorter than the beam count, or non-finite for those beams. The beam angle is deliberately `NaN` rather than `0` (which would be a nadir beam that was never measured), so the sounding's position and uncertainty are `NaN` and the range gate drops it. | A driver/message problem, **not** a frame or range-gate problem. If this equals the beam count, the run produces no soundings at all and the summary says so explicitly instead of pointing at the frame overrides. |

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
