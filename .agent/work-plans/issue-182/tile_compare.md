# Tile comparison, Jazzy side (issue 182, Part 2)

`tile_compare.sh` (beside this file) checks whether a Rolling build of
`cube_bathymetry` turns the same soundings into the same depth tiles as a Jazzy
build. Jazzy writes the bags and its tiles; Rolling later reads the same bags.
This page is for the Jazzy side, run on deadpool (Ubuntu 24.04, Jazzy) without
an agent session. It takes a few minutes.

## What it needs

- A Jazzy environment in which `cube_bathymetry` at `jazzy` `0b70b46` builds:
  `/opt/ros/jazzy` plus the workspace that holds the upstream `marine_*`
  packages (rolker/unh_marine_autonomy `jazzy`).
- `python3-gdal`, `python3-numpy` and `python3-pytest` (the bag writer lives in
  the package's pytest modules), `git`, `gcc`, `gdal-config`.
- Your clone of rolker/cube_bathymetry, with `feature/issue-182` fetched.

Nothing in your clone's working tree changes: the `0b70b46` build is a
separate git worktree in a separate colcon workspace, removed at the end.

## Commands

Set the first two lines to your paths, then paste the rest as it stands.

```bash
REPO=~/path/to/cube_bathymetry              # your clone of rolker/cube_bathymetry
UPSTREAM_SETUP=~/path/to/jazzy_ws/install/setup.bash   # sources /opt/ros/jazzy and the marine_* packages

source "$UPSTREAM_SETUP"
git -C "$REPO" fetch origin
mkdir -p ~/tile_compare_ws/src
git -C "$REPO" worktree add --detach ~/tile_compare_ws/src/cube_bathymetry 0b70b46
cd ~/tile_compare_ws
MAKEFLAGS=-j8 colcon build --packages-select cube_bathymetry --parallel-workers 4 \
  --cmake-args -DBUILD_TESTING=OFF
source ~/tile_compare_ws/install/setup.bash
git -C "$REPO" show origin/feature/issue-182:.agent/work-plans/issue-182/tile_compare.sh \
  > ~/tile_compare.sh
bash ~/tile_compare.sh jazzy --src ~/tile_compare_ws/src/cube_bathymetry --out ~/tile_compare
```

The last line ends with `send back: ~/tile_compare/tile_compare_jazzy.tar.gz`.

If the real 3,000-ping M3 excerpt is reachable (at
`/mnt/nadata/map2026asv/logs/gabby/logs/bizzy_m3/bag_2026-06-09T14.51.50_m3_detections_3000ping_excerpt`,
or wherever `CUBE_REAL_BAG_EXCERPT` points), the script copies it into the
archive and imports it too, so the archive grows by its size. To leave it out,
run the last line with `CUBE_REAL_BAG_EXCERPT=/nonexistent` in front.

## What it writes

Everything goes under `~/tile_compare/jazzy/`:

- `bags/`: the four synthetic plane bags (`pass_000` to `pass_270`, mcap), the
  incidence table, and the real excerpt if it was reachable;
- `stores/depth` (the depth store, 2-band Float64: the one the pass rule
  covers), `stores/beam_bs` and `stores/slope_bs` (backscatter, reported only),
  `stores/real_depth` if the excerpt was imported;
- `logs/`: each `import_bag` command line and its output;
- `versions.txt`: OS, `gcc`, GDAL, glibc, the tf2, rosbag2 and GDAL package
  versions, the source commit and the `import_bag` binary's sha256;
- `bags.sha256`, `stores.sha256`: checksums of every file.

## What to send back

The one file `~/tile_compare/tile_compare_jazzy.tar.gz` (a few MB without the
real excerpt). The Rolling side then runs

```bash
bash tile_compare.sh rolling --from tile_compare_jazzy.tar.gz --src <checkout> --out <dir>
```

which imports your bags on Rolling and prints the report and `PASS` or `FAIL`.

## Cleaning up

```bash
git -C "$REPO" worktree remove --force ~/tile_compare_ws/src/cube_bathymetry
rm -rf ~/tile_compare_ws ~/tile_compare.sh
```

Keep `~/tile_compare` until the comparison has been recorded.

## The pass rule (fixed before any run)

For the depth stores: the same tile files; per tile and band, the same NaN
mask; every finite cell within 1 mm. The backscatter stores are compared and
reported only. The report always gives the tiles affected, the cells that
differ at all and beyond 1 mm, the largest difference with its tile and cell,
a histogram of differences, and whether cells beyond 1 mm are isolated or
contiguous. A widespread or smooth difference beyond 1 mm is a different
computation to be explained, not a hypothesis switch to accept.
