# coverage_assessment

Scores how much of each target area one or more bathymetry grids cover, and how much
of that meets a survey standard, with credit per source (per boat). It is a prototype
under [unh_marine_autonomy#403](https://github.com/rolker/unh_marine_autonomy/issues/403)
and was built for the Ocean Discovery Day scoreboard; the message family
(`SurveyArea`, `SurveyStandard`, `CoverageAssessment`, `CoverageContribution`) is in
`marine_interfaces` and its comments are the contract. Where this text and a message
comment differ, the message wins.

The node is generic by name: it reads any `grid_map_msgs/GridMap` that carries an
`elevation` and an `uncertainty` layer, not only CUBE's. It is a lifecycle node,
`coverage_assessment`, executable `coverage_assessment_node`.

The package holds a plain-C++ core (`coverage_assessment_core`: polygon rasterising, a
per-source accumulator, the judging rules, the coverage raster; no ROS, no TF, no
message types, unit-tested on its own) and the ROS wrapper
(`src/coverage_assessment_node.hpp`, private to the package, not installed) that
converts messages and frames into the core's structs.

## Topics

| Direction | Topic (default) | Type | QoS |
|---|---|---|---|
| in | each of `grid_topics` | `grid_map_msgs/GridMap` (`elevation`, `uncertainty`) | reliable, volatile, depth 10 (what `cube_bathymetry`'s `grid` publisher offers); a publisher with an incompatible QoS (best-effort, say) is reported with an ERROR naming the topic |
| in | `survey_area_topic` (`survey_area`) | `marine_interfaces/SurveyArea` | reliable, **transient_local**, depth 100; a **volatile** publisher cannot reach it and is reported with an ERROR naming the topic, instead of failing silently |
| in | `/tf`, `/tf_static` | | |
| out | `coverage_assessment` | `marine_interfaces/CoverageAssessment`, one per area per assessment | reliable, **transient_local**, depth 100 |
| out | `coverage_grid` | `grid_map_msgs/GridMap`, one layer `meets_standard` | reliable, **transient_local**, depth 1 |

Relative names resolve in the node's namespace. `header.frame_id` of both outputs is
`map_frame`; `header.stamp` is the assessment time.

| Service | Type | Purpose |
|---|---|---|
| `~/reset` (e.g. `/coverage_assessment/reset`) | `std_srvs/Trigger` | Starts a new assessment session in RAM, with no lifecycle round trip; see *Starting a fresh assessment between runs* below. Exists while the node is configured (inactive or active). |

`coverage_assessment` is latched so a scoreboard that starts late gets the areas. The
publisher's history is the last 100 messages (a fixed depth, like `survey_area`'s), not
one per area: a late consumer receives up to the last 100 assessments, oldest first, so
it may see older repeats of an area before the newest. Keep the latest per `area.id`.
`max_areas` (default 20, at most 100) is what makes that safe: one pass publishes one
message per held area, so with at most 100 areas the newest assessment of every area is
always inside the history.

`coverage_grid` covers the bounding box of all areas on the assessment lattice. Layer
`meets_standard`: NaN where a cell is outside every area or not covered, `0` covered but
below the standard, `1` meets it. Where areas overlap, a cell is `1` only if it meets the
standard of every area containing it.

## Parameters

All are `read_only` (declared once, in the constructor, so a second configure never meets
`ParameterAlreadyDeclared`): a runtime `ros2 param set` is rejected rather than accepted
and ignored. Set them from a launch file or YAML before the node starts.

| Parameter | Default | Meaning |
|---|---|---|
| `grid_topics` | `[]` (required) | CUBE `grid` topics, one per source; the order defines the order of `CoverageAssessment.sources`. Configure fails when empty, when it holds an empty entry, or when two entries resolve to the same topic (namespace expansion and remapping included: `grid` and `/<ns>/grid` are one topic, which would otherwise be credited as two sources). |
| `source_names` | `[]` | Per-source label (platform name). Empty list, or an empty entry, falls back to that source's topic string. Same length as `grid_topics` or empty, else configure fails. Names must be unique, so a duplicate fails configure too. |
| `survey_area_topic` | `survey_area` | `SurveyArea` input. |
| `assessment_period` | `1.0` s | Timer period; 0.001 s to 86400 s, else configure fails. |
| `cell_size` | `1.0` m | The assessment lattice. Should equal the sources' cell size (CUBE's `cell_size`); a source whose message resolution differs by more than 1e-6 m is resampled and warned about once per source. |
| `map_frame` | `map` | Frame of the assessment raster and of both outputs. Each grid arrives in its own frame (each boat has its own `<boat>/map`) and is transformed into this one. |
| `water_surface_frame` | `map_tide` | The frame whose origin is the water surface. One boat's, e.g. `boat1/map_tide`; the same surface is applied to every source. |
| `water_surface_boat_frame` | `base_link` | The tide-publishing boat (e.g. `boat1/base_link`), for the distance bound below. |
| `water_surface_max_boat_distance` | `1500.0` m | Beyond this the surface is refused: warn and skip scoring. |
| `water_surface_max_age` | `30.0` s | Oldest acceptable transform, for both the water-surface frame and the tide boat's frame (the distance bound). |
| `max_raster_cells` | `1000000` | Per-area raster cap in lattice cells; guards a swapped latitude/longitude. A union of areas that exceeds it cannot be drawn as `coverage_grid` (WARN). |
| `max_pass_cell_checks` | `16000000` | Work budget of one assessment pass (read_only, at least the number of sources). A pass checks every held area's whole bounding box once per source, so the boxes of **all** held areas, in lattice cells, times the number of sources may not exceed it: 16e6 is 4e6 cells with 4 sources, or 1e6 with 16. At the measured ~0.1 s per 1e6 cells x 4 sources that keeps a pass near 0.4 s, where 20 areas of `max_raster_cells` each would take ~12 s and block the single-threaded node. An area that would take a pass over it is refused with an ERROR naming the budget, the source count and the cells already held; when the per-pass allowance (this over the source count) is smaller than `max_raster_cells`, it is also the cap the area's raster is built against, so an area too large for a pass is refused before its mask is allocated; an update of a held id is judged with its old size removed. The 10 s status line reports the last pass duration, the cells held and the source count. |
| `max_held_cells` | `100000000` | Most lattice cells held for all sources together (read_only, at least 1), counted as allocated: 64 x 64 blocks, 8 bytes per cell, so 1e8 is about 800 MB. Each source keeps every cell it has mapped until the node is reset, inside the survey areas or not, so the memory grows with the area surveyed. Once over it (by at most one grid message), no further grid is folded in and **nothing is published**, with a throttled ERROR naming the parameter, until the node is reset (the `~/reset` service, or deactivate, cleanup, configure, activate) or restarted with a larger value: from there a score could only under-report coverage. The 10 s status line reports the allocated cells against the limit. |
| `max_area_vertices` | `10000` | Most vertices in one SurveyArea polygon (read_only, at least 3). A definition with more is refused on arrival with an ERROR: filling a polygon scans every edge on every row, and queued definitions keep every vertex. |
| `max_areas` | `20` | Most survey areas held at once; 1 to 100, else configure fails. A SurveyArea with a **new** id beyond it is refused with an ERROR (an update of a held id still applies). Bounds the cost of an assessment pass and keeps the 100-deep latched history holding the newest assessment of every area. |

`earth_frame` is hard-coded `earth`.

Namespaced multi-boat setup: `water_surface_frame` and `water_surface_boat_frame` must
name the same boat (`bluefin/map_tide`, `bluefin/base_link`). The unqualified defaults
exist only in a single-boat graph.

## Launch

```bash
ros2 launch coverage_assessment coverage_assessment_launch.py \
  params_file:=/path/to/my_params.yaml [namespace:=<ns>]
```

The launch file starts one node, configures it as soon as the process is up and
activates it once, right after that first configure (the handler fires for the first
`configuring` -> `inactive` transition only, so the later configure of
`reset_cube_nodes.sh --consumer` (deactivate, cleanup, configure, activate) is not followed
by a second activation; `test_launch_lifecycle.py` runs that sequence). `config/coverage_assessment.yaml` is an example for
the four-boat simulation (it needs editing for anything else; `grid_topics` is
required). It is not part of the `unh_marine_simulation` demo launch.

### Starting a fresh assessment between runs

Call the `~/reset` service (`std_srvs/Trigger`), or let `reset_cube_nodes.sh
--consumer /coverage_assessment` do it after the CUBE nodes (see the `cube_bathymetry`
README, *Starting a fresh sheet without restarting*, which also says what order the two
resets rely on: boats not underway, the assessor reset after every CUBE node):

```bash
ros2 service call /coverage_assessment/reset std_srvs/srv/Trigger '{}'
```

The service empties the union of every source, together with a grid it kept because its
frame could not be resolved (otherwise the old cells would return on the next tick), and
clears the `max_held_cells` latch and the change flags. When the node is active and the
areas are projected, it publishes the emptied result itself, before it answers: the
`coverage_assessment` messages and the latched `coverage_grid` show nothing covered at once,
whatever the water surface is doing (with nothing held the result does not depend on the
surface height). The next tick then publishes again, even if the water surface did not
change. It **keeps** the
survey areas and their projections (they are not re-sent: the latched `survey_area` topic
is not replayed either), the subscriptions, TF, the raster georeference, and the
counters, which stay cumulative (they also count areas and faults, which a reset that keeps
the areas must not zero; a one-line INFO logs what was dropped). It answers in `inactive` and
`active`, and says which in the response; an inactive node assesses nothing until it is
activated. An inactive node cannot publish, so it publishes the emptied result when it is
next activated (the script activates it), again whatever the water surface is doing. A node
whose areas are not yet projected (no georeference yet, or a definition queued) publishes
nothing at reset time; the first tick that can assess publishes. If the water surface is
unavailable then, the old messages stay latched on both topics until that tick succeeds,
because a tick returns before assessing.

**It refuses when the map frame has moved** since the raster georeference was captured
(`success: false`, nothing changed), because the service keeps the georeference and so
cannot undo the move. That state, and a node that is not configured, are recovered by the
lifecycle walk: deactivate, cleanup, configure, activate releases everything (the union of
every source, the areas, the subscriptions, TF), captures the georeference again and
starts empty; the areas come back from the latched `survey_area` topic and nothing else is
replayed. `reset_cube_nodes.sh` falls back to the walk for a consumer whose service
refuses. A consumer that is not reset keeps the old union and would score the previous
run's coverage.

## How it works

### Input: a latched union over time, per source

`grid` is a vessel-centred 200 m window, so a snapshot of it would forget everything
the boat has left behind. The node therefore keeps, for every source, the union over
time of all its grids. It does not read `coverage_tiles` or the store: the live `grid`
is what every source already publishes, sim boats and real ones alike.

Per source, a lattice anchored in the **raster frame** (cell `(i, j)` covers
`[i*cs, (i+1)*cs)`) holds elevation and uncertainty in paged 64 x 64 blocks of `float`
(about 8 bytes per surveyed cell plus the block table; memory grows with the surveyed
footprint and is logged every 10 s). Each incoming message is resampled **pull-style**:
for every lattice centre whose image in the message's frame falls inside the message,
take the message cell containing it. Pull is needed because each CUBE message sizes its
geometry from its own extent, so successive messages are not on one lattice, and a
push would alias into pinholes.

**A NaN never erases, per field.** A finite elevation overwrites the held elevation, a
finite uncertainty overwrites the held uncertainty, and a NaN in either field leaves
that field alone, so a finite elevation with NaN uncertainty (which the simulation
produces before TF settles) cannot wipe a good uncertainty. The accumulator holds
elevation and uncertainty only; coverage is judged at assessment time.

### Frames

For each message the node does one `lookupTransform(grid frame <- earth)` (latest
available), composes it with the captured raster georeference (below), and hands the core
the horizontal part of the result, so lattice centre to grid frame is that transform at
z = 0. `elevation` is an ellipsoid height, independent of frame, so
it is never transformed. Dropping the z term changes the horizontal placement only by
the tangent-plane tilt between two frames times a depth: a millimetre or two for frames a
few hundred metres apart, far below a 1 m cell.

The area polygon is projected with exactly the chain `cube_bathymetry` uses to place
its cell centres (`grid_projection.cpp`): `GeoPointLatLongDegrees(lat, lon, 0)` to ECEF
to the `map_frame <- earth` transform to x, y (`src/frame_chain.hpp`, pinned against
`cube::geoGridsToGridMap` by `test_frame_chain`).

**The raster georeference is captured once.** At the first successful lookup of
`map_frame <- earth` after configure the node keeps that transform and uses it for
**both** the polygon projection and the placement of every source (source frame to
`earth` to the captured raster), so the target and the accumulated history are always in
the same coordinates. Each timer tick compares the live `map_frame <- earth` with the
captured one: the distance between the two map origins (in earth coordinates, so a tiny
rotation of the frame about its own origin is not read as a shift of the 6.4e6 m
translation) against 1 cm, and the rotation between them against 1e-6 rad. If either is
exceeded, the node logs an ERROR naming
the frame and the shift and **withholds** everything (no `CoverageAssessment`, no
`coverage_grid`, counted as `withheld_map_moved`) until it is reconfigured
(deactivate, cleanup, configure, activate), which recaptures; the `~/reset` service keeps
the georeference, so it refuses in this state. Nothing is re-projected
silently: cells already accumulated cannot be moved with the frame. Grids keep
accumulating in the captured frame meanwhile. A live lookup that fails is not movement;
the captured transform stays in use.

**Startup wait for `earth`.** Nothing can be captured, projected or placed before
`earth` is in TF, which is about the first 10 s after the simulation launches. Until then
the node withholds with a throttled WARN "waiting for `<map_frame>` <- `earth`" that says
this is normal, counted as `withheld_no_earth` (checked before anything about the
sources); the latest grid of each source that arrives meanwhile is kept and placed once the
frame resolves. Such a grid is not counted as unplaced and does not raise the
unplaced-source WARN below, which is for a grid whose own frame cannot be reached once the
georeference exists.

**A source that cannot be placed withholds the assessment.** `CoverageAssessment` cannot
represent a failed source, and zeros would read as "uncovered", so:

- a source that has **sent no grid yet** scores zero, which is true, and keeps its
  slot in `sources`;
- a source whose **latest grid could not be placed** (no transform to its frame) makes
  the node publish **no** `CoverageAssessment` and no `coverage_grid` for any area, with
  a throttled WARN naming the source, its topic and the frame. Every timer tick retries
  the lookup of that frame: when it resolves, the grid the node kept is folded in and
  scoring resumes, so a boat that has gone silent does not halt the scoreboard for
  everyone.

That a source which has **sent no grid yet** scores zero is by design (decided
2026-10-12): the message keeps its slot, and its zero is true, not a failure.

### Water surface

`h_surface` is the geodetic altitude of the origin of `earth <- water_surface_frame`.
The depth of a cell is `h_surface - elevation`, metres, positive down (the sign was
checked against the chart grid in the simulation: depths came out positive and within
about 0.2 m median of it).

**Which boat's tide.** One, named by `water_surface_frame`, applied to every source. That
is an assumption, valid over an area of a few kilometres in one body of water. Its
consequences:

1. a source on a boat far from the tide boat sees a surface only as good as that
   assumption;
2. **when that boat's estimator withholds `map_tide`** (older than
   `water_surface_max_age`, or not published), the assessor stops scoring **everyone**:
   it skips the assessment with a throttled WARN, keeps accumulating grids, and the last
   assessment stays on the transient_local topic (the scoreboard shows an old value, the
   log says why). A static transform has no stamp and no age. A water surface whose
   transform cannot be looked up is the same skip;
3. the **curvature bound**: a tangent plane at the origin of the tide's parent frame sits
   `d^2 / 2R` above the ellipsoid at distance `d` (R is the earth radius, 6.371e6 m):
   about 0.18 m at 1500 m and 0.02 m at 500 m. The node measures `d`, the horizontal
   distance of `water_surface_boat_frame` from the origin of the tide frame's parent
   (as TF reports the parent), and above `water_surface_max_boat_distance` it warns and
   skips scoring, like (2). If the parent or the boat frame cannot be found, it also
   skips (fail closed). The boat's transform is held to `water_surface_max_age` like the
   tide's: a dead boat's last position does not pass the bound for ever (counted with the
   stale refusals). If the tide frame's parent is `earth` itself, the distance would be
   measured from the centre of the Earth (about 6.4e6 m) and could never pass; the node
   says so with an ERROR (counted separately) and names the fix: a tide frame under a
   local map frame, such as `bluefin/map_tide` under `bluefin/map`.

The distance bound checks **the tide boat only**: it is the one-surface assumption made
explicit (one boat's tide for the whole area, an area of a few kilometres), not a check
of where the other boats are.

A change of less than 1 cm in the surface does not trigger a new assessment.

**Clocks.** The assessment timer is a wall timer, but every age (the tide's, the boat's)
is measured against the node clock. With `use_sim_time:=true` the node clock follows
`/clock` while the timer keeps wall time, so for a simulation set `use_sim_time` on this
node as on the simulation's other nodes (the stamps in TF are simulation time); with
real boats leave it `false`. A paused simulation freezes ages, not the timer.

### Rasterising and judging

A polygon (raster-frame metres) is filled by scanline, even-odd; a cell belongs to the
area when its **centre** is inside, with a half-open rule: a centre on a lower or left
edge is in, on an upper or right edge is out. So a `w` metre rectangle holds exactly
`w / cell_size` columns at any lattice offset, and polygons sharing an edge partition the
cells. `area_total` is the cell count times `cell_size^2`: the rasterised polygon, not
its geodetic area. A polygon smaller than a cell is valid and has `area_total` 0.

The judging rules are those of the message comments:

- a source **covers** a cell when it has a finite depth >= 0 there (a cell above the
  surface has no survey depth; uncertainty is not needed for coverage); the cell is
  covered when any source covers it;
- to judge the standard only sources with a finite depth >= 0 **and** a finite
  uncertainty >= 0 compete; the one with the smallest uncertainty supplies the depth, ties
  going to the shallower depth (fail closed, since `allowed(depth)` grows with depth);
  the cell meets when `u <= sqrt(a^2 + (b*depth)^2)`. A covered cell where nothing
  competes does not meet;
- a standard that is **not set** (`a = b = 0`, or `a` or `b` NaN, negative or infinite)
  is checked first and nothing meets it, whatever the uncertainty. Such an area is
  accepted, scored for coverage, and published with `area_meeting_standard` 0, as
  `SurveyStandard.msg` defines it;
- a cell exactly at the standard meets: the grids carry `float` uncertainty, so the
  allowed value is rounded to `float` before the comparison (`0.1f` against `a = 0.1`
  meets; one float step above does not);
- per source, covered and meeting are judged on its own grid with its own depth;
  `area_unique` is the cells only that source covers.

Output fields are `area_total`, `area_covered`, `area_meeting_standard` and the
per-source `area_covered`, `area_meeting_standard`, `area_unique`, all in m^2. The
invariants `meeting <= covered <= total` and `unique <= covered` hold. `sources` keeps
the order of `grid_topics` in every message. `SurveyArea.priority` is not used.

### Negative depths are never hidden

A cell whose depth is negative (the seabed "above" the surface) is not covered. Each
assessment counts the cells with depth below -1 m inside the areas and logs a throttled
WARN with the count and the surface height used, so a wrong sign, datum or stale tide
cannot pose as a plausible verdict. The count is in the status line.

### When it assesses

A timer (`assessment_period`) assesses only when a grid was folded in, an area was
added or changed, or the surface moved by more than 1 cm; so `coverage_grid` is
published at most once per period. It does nothing, with a WARN, while any source is
unplaced, while `map_frame <- earth` has not been captured, or while the water surface is
refused; when `map_frame <- earth` has moved off the capture it does nothing, with an
ERROR. The first tick after activation always assesses.

### Invalid input

A `SurveyArea` is rejected with an ERROR, and any earlier definition of that id is kept,
when it has fewer than 3 vertices or more than `max_area_vertices`, a vertex that is not a finite latitude in [-90, 90]
and longitude in [-180, 180], zero area, a raster over `max_raster_cells`, edges that cross more than 1e8 lattice rows in total (the fill visits only the edges spanning each row, so its cost is those crossings; a comb with thousands of teeth across a tall area has too many), a vertex more
than 1e7 m (in x or y, in the raster frame) from the raster origin, a definition that would take the bounding
boxes of all held areas, times the number of sources, over `max_pass_cell_checks`, or a new id beyond `max_areas`. The standard is **not** a reason to reject: a NaN, negative or infinite `a`
or `b` means "not set" (above).

Definitions are validated as they arrive and queued per id; the registry sees them in
stamp order once `earth` is known. The checks that need no transform run on arrival: the
vertex count and ranges, and the shape (zero area, collinear vertices), measured on the
plane tangent to the WGS84 ellipsoid at the polygon's first vertex (latitude/longitude to
ECEF to east/north: the same kind of plane as the map frame, anchored at the polygon because
the map frame is not known yet; it holds at a pole and across the antimeridian). Points on
one great circle are collinear; three points on one parallel are a thin real triangle, as
in the real projection, where it rasterises to no cells. So only shape-valid definitions
are queued, and a burst of degenerate ones cannot push a valid one out. The checks that
depend on the map frame run when the definition is projected: the 1e7 m bound and
`max_raster_cells`, which bounds the raster in the map frame (an east/north bounding box can
be far larger than the map-frame one when map is rotated, so it is not applied on arrival). The queue holds at most 8 definitions per id; beyond that the one with the lowest
stamp is dropped with a throttled WARN naming the id and the stamp. The node keeps, per id,
the last **accepted** definition and its stamp, apart from the queue (it is never evicted): a
rejected update never displaces it, and an update older than it is ignored with a throttled
WARN (on an equal stamp the last received wins). So a valid area followed by a newer invalid
one, both arriving before the first assessment, still scores the valid one.

A grid message that cannot be converted, has no `elevation` layer or has unusable geometry
is dropped with a throttled ERROR, and so is one whose footprint reaches more than 1e7 m
(or 1e12 lattice cells) from the raster origin. One whose footprint (the lattice box around
its corners, every index it touches) spans more than 2001 x 2001 = 4,004,001 lattice cells
(`SourceAccumulator::kMaxFootprintCells`) is dropped with a throttled WARN naming the cell
count. That admits CUBE's axis-aligned 200 m window at 0.1 m at any alignment (2000 cells
wide, 2001 indices when an edge lies on a lattice line); a window rotated 45 degrees spans
twice its own cells, so lattices finer than about 0.15 m are not supported. A
source without an `uncertainty` layer can cover but never meets a standard (warned once).
A finite negative uncertainty is not a measurement and never overwrites a held value, so
the union cannot shrink.

### Faults

No exception can leave a callback: the grid, survey-area and timer callbacks catch
`std::exception` and anything else, log a throttled ERROR, count it (`callback_exceptions`
in the status line) and keep running. `main()` catches anything that escapes the spin,
logs FATAL and returns non-zero.

## Status line

Every 10 s the node logs the cells held (with memory), per-counter grid and area totals,
and why ticks did not assess (unplaced source, no `earth`, water surface unavailable,
stale, too far, a tide frame under `earth`, or the map frame moved), the last
above-surface count and the number of caught callback exceptions. The same counters back the tests.

## Tests

`colcon test --packages-select coverage_assessment` (build `cube_bathymetry` first; the
package has a `test_depend` on it so `colcon build --packages-up-to coverage_assessment`
does). The core has gtest suites for the polygon fill, the accumulator, the assessor and
registry, and the coverage raster. The wrapper has `test_frame_chain` (the polygon
projection against `cube::geoGridsToGridMap`) and `test_coverage_assessment_node`, which
runs the real node over a static TF broadcaster and synthetic grids on its own
`ROS_DOMAIN_ID` (86) with localhost-only discovery and checks exact numbers, two sources
in different (one rotated) frames, withholding for an unplaced source, a silent source,
a stale tide and a stale tide boat, a tide boat beyond the bound, a tide frame under
`earth`, the reset by lifecycle walk and by the `~/reset` service (the union, a kept unplaced grid,
the held-limit latch, the cumulative counters, the refusals for a moved map frame, before
the first georeference and when unconfigured), the map frame moving after the capture, the retry of a silent
unplaced source, the startup wait for `earth`, area arrival ordering and queue overflow, unset standards, `max_areas`, injected callback
exceptions, incompatible QoS, the configure refusals and the read-only parameters. The
launch file has its own `launch_testing` test (`test_launch_lifecycle.py`, own
`ROS_DOMAIN_ID` 85).
