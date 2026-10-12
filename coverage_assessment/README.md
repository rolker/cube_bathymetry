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
| in | each of `grid_topics` | `grid_map_msgs/GridMap` (`elevation`, `uncertainty`) | reliable, volatile, depth 10 (what `cube_bathymetry`'s `grid` publisher offers) |
| in | `survey_area_topic` (`survey_area`) | `marine_interfaces/SurveyArea` | reliable, **transient_local**, depth 100 |
| in | `/tf`, `/tf_static` | | |
| out | `coverage_assessment` | `marine_interfaces/CoverageAssessment`, one per area per assessment | reliable, **transient_local**, depth 100 |
| out | `coverage_grid` | `grid_map_msgs/GridMap`, one layer `meets_standard` | reliable, **transient_local**, depth 1 |

Relative names resolve in the node's namespace. `header.frame_id` of both outputs is
`map_frame`; `header.stamp` is the assessment time.

`coverage_assessment` is latched so a scoreboard that starts late gets the areas. The
publisher's history is the last 100 messages (a fixed depth, like `survey_area`'s), not
one per area: a late consumer receives up to the last 100 assessments, oldest first, so
it may see older repeats of an area before the newest. Keep the latest per `area.id`.

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
| `grid_topics` | `[]` (required) | CUBE `grid` topics, one per source; the order defines the order of `CoverageAssessment.sources`. Configure fails when empty, when it holds an empty entry, or when a topic is listed twice. |
| `source_names` | `[]` | Per-source label (platform name). Empty list, or an empty entry, falls back to that source's topic string. Same length as `grid_topics` or empty, else configure fails. Names must be unique, so a duplicate fails configure too. |
| `survey_area_topic` | `survey_area` | `SurveyArea` input. |
| `assessment_period` | `1.0` s | Timer period; 0.001 s to 86400 s, else configure fails. |
| `cell_size` | `1.0` m | The assessment lattice. Should equal the sources' cell size (CUBE's `cell_size`); a source whose message resolution differs by more than 1e-6 m is resampled and warned about once per source. |
| `map_frame` | `map` | Frame of the assessment raster and of both outputs. Each grid arrives in its own frame (each boat has its own `<boat>/map`) and is transformed into this one. |
| `water_surface_frame` | `map_tide` | The frame whose origin is the water surface. One boat's, e.g. `boat1/map_tide`; the same surface is applied to every source. |
| `water_surface_boat_frame` | `base_link` | The tide-publishing boat (e.g. `boat1/base_link`), for the distance bound below. |
| `water_surface_max_boat_distance` | `1500.0` m | Beyond this the surface is refused: warn and skip scoring. |
| `water_surface_max_age` | `30.0` s | Oldest acceptable water-surface transform. |
| `max_raster_cells` | `1000000` | Per-area raster cap in lattice cells; guards a swapped latitude/longitude. A union of areas that exceeds it cannot be drawn as `coverage_grid` (WARN). |

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
activates it once it is inactive. `config/coverage_assessment.yaml` is an example for
the four-boat simulation (it needs editing for anything else; `grid_topics` is
required). It is not part of the `unh_marine_simulation` demo launch.

Starting a fresh assessment between runs: the assessor is the `--consumer` of
`reset_cube_nodes.sh` (see the `cube_bathymetry` README, *Starting a fresh sheet without
restarting*), `--consumer /coverage_assessment`. The sequence deactivate, cleanup,
configure, activate releases everything (the union of every source, the areas, the
subscriptions, TF) and starts empty. The areas come back from the latched
`survey_area` topic; nothing else is replayed. A consumer that is not reset keeps the
old union and would score the previous run's coverage.

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

For each message the node does one `lookupTransform(grid frame <- map_frame)` (latest
available) and hands the core its horizontal part, so lattice centre to grid frame is
that transform at z = 0. `elevation` is an ellipsoid height, independent of frame, so
it is never transformed. Dropping the z term changes the horizontal placement only by
the tangent-plane tilt between two frames times a depth: a millimetre or two for frames a
few hundred metres apart, far below a 1 m cell.

The area polygon is projected with exactly the chain `cube_bathymetry` uses to place
its cell centres (`grid_projection.cpp`): `GeoPointLatLongDegrees(lat, lon, 0)` to ECEF
to the `map_frame <- earth` transform to x, y (`src/frame_chain.hpp`, pinned against
`cube::geoGridsToGridMap` by `test_frame_chain`). The polygon is re-projected whenever
that transform changes.

**A source that cannot be placed withholds the assessment.** `CoverageAssessment` cannot
represent a failed source, and zeros would read as "uncovered", so:

- a source that has **sent no grid yet** scores zero, which is true, and keeps its
  slot in `sources`;
- a source whose **latest grid could not be placed** (no transform to its frame) makes
  the node publish **no** `CoverageAssessment` and no `coverage_grid` for any area, with
  a throttled WARN naming the source, its topic and the frame, until one of that
  source's grids is placed again. A source that stops publishing while flagged keeps the
  node withholding.

`earth` is absent from TF for about the first 10 s after the simulation launches. So the
assessor starts by withholding, with a WARN that says this is normal.

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
   skips (fail closed).

A change of less than 1 cm in the surface does not trigger a new assessment.

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
  is checked first and nothing meets it, whatever the uncertainty;
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

A timer (`assessment_period`) assesses only when a grid was folded in, an area changed
or was re-projected, or the surface moved by more than 1 cm; so `coverage_grid` is
published at most once per period. It does nothing, with a WARN, while any source is
unplaced, while `map_frame <- earth` is missing, or while the water surface is refused.
The first tick after activation always assesses.

### Invalid input

A `SurveyArea` is rejected with an ERROR, and an earlier definition of that id is kept,
when it has fewer than 3 vertices, a vertex that is not a finite latitude in
[-90, 90] and longitude in [-180, 180], a negative or non-finite `a` or `b`, zero area, or
a raster over `max_raster_cells`. An area whose stamp is older than the one held for that
id is ignored; on an equal stamp the last received wins. `a = b = 0` is accepted and
published as "not set". A grid message that cannot be converted, has no `elevation` layer
or has unusable geometry is dropped with a throttled ERROR; a source without an
`uncertainty` layer can cover but never meets a standard (warned once).

## Status line

Every 10 s the node logs the cells held (with memory), per-counter grid and area totals,
and why ticks did not assess (unplaced source, no `earth`, water surface unavailable,
stale or too far), plus the last above-surface count. The same counters back the tests.

## Tests

`colcon test --packages-select coverage_assessment` (build `cube_bathymetry` first; the
package has a `test_depend` on it so `colcon build --packages-up-to coverage_assessment`
does). The core has gtest suites for the polygon fill, the accumulator, the assessor and
registry, and the coverage raster. The wrapper has `test_frame_chain` (the polygon
projection against `cube::geoGridsToGridMap`) and `test_coverage_assessment_node`, which
runs the real node over a static TF broadcaster and synthetic grids on its own
`ROS_DOMAIN_ID` (86) with localhost-only discovery and checks exact numbers, two sources
in different (one rotated) frames, withholding for an unplaced source, a silent source,
a stale tide, a tide boat beyond the bound, the reset, the configure refusals and the
read-only parameters.
