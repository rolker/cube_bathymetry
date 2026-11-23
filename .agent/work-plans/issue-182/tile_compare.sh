#!/usr/bin/env bash
# tile_compare.sh: does a Rolling build of cube_bathymetry turn the same soundings
# into the same depth tiles as a Jazzy build? (rolker/cube_bathymetry#182, Part 2)
#
# Instructions for running it: tile_compare.md, beside this file.
#
# Modes
#   tile_compare.sh jazzy   --src <checkout> --out <dir>
#       With a Jazzy workspace sourced (cube_bathymetry built and installed there):
#       writes the synthetic bags, imports them, records versions and checksums, and
#       packs <dir>/tile_compare_jazzy.tar.gz (the one file to send back).
#   tile_compare.sh rolling --from <tile_compare_jazzy.tar.gz> --src <checkout> --out <dir>
#       With a Rolling workspace sourced: imports the bags Jazzy wrote, with the same
#       commands, and applies the pass rule to Rolling's tiles against Jazzy's.
#       Prints the report and PASS or FAIL; exit 0 on PASS, 1 on FAIL.
#   tile_compare.sh rolling --dry-run --src <checkout> --out <dir>
#       Rolling writes and reads its own bags (proves the script, records Rolling's
#       checksums); no Jazzy comparison. Packs <dir>/tile_compare_rolling.tar.gz.
#   tile_compare.sh compare <result dir A> <result dir B>
#       The pass rule between two result directories (e.g. two dry runs).
#
# <checkout> is the cube_bathymetry repository (the directory holding
# cube_bathymetry/test/test_import_bag_slope.py). <dir> must not exist or be empty.
#
# What is run (the commands of cube_bathymetry/test/test_import_bag_slope.py):
#   bags     four passes over a tilted plane, write_plane_bag(heading 0/90/180/270,
#            t0 = 1000 + 100 k), mcap; plus the incidence table of that test.
#   depth    import_bag <common args> -o depth --bs-store beam_bs
#            --backscatter-correction none <four bags>
#   slope    import_bag <common args> --backscatter-angle-basis incidence
#            --slope-surface depth --bs-store slope_bs --backscatter-correction
#            empirical --backscatter-curve incidence_table.csv
#            --backscatter-mode-fallback exclude <four bags>
#   real     only if the real 3,000-ping M3 excerpt is reachable (CUBE_REAL_BAG_EXCERPT
#            or the default path of test_real_bag_smoke.cpp): it is copied into the
#            archive and imported with that test's projector arguments,
#            import_bag -o real_depth <projector args> -r 1.0 <excerpt>.
#   batch_regen_bag is not run: no run of it in the slope test writes tiles.
#
# Pass rule (fixed before any run; .agent/work-plans/issue-182/plan.md):
#   depth stores (depth, real_depth; 2-band Float64, NaN nodata):
#     1. the same set of tile files (relative paths);
#     2. per tile and band, the same nodata (NaN) mask;
#     3. every finite cell within 1 mm (0.001, metres); an integer band exactly equal.
#   backscatter stores (beam_bs, slope_bs; Float32 dB): compared and reported only.
#   Always reported: tiles affected, cells that differ at all and beyond 1 mm, the
#   largest difference with its tile, band and cell, a histogram of differences,
#   whether cells beyond 1 mm are isolated or contiguous, byte equality of each
#   tile file, and the non-tile files (manifests) with write times and build
#   fingerprints removed. A widespread or smooth difference beyond 1 mm is a
#   different computation to be explained, not a hypothesis switch to accept.

set -eo pipefail
# Importing the test modules by path must leave no __pycache__ in the checkout.
export PYTHONDONTWRITEBYTECODE=1

usage() {
  sed -n '2,20p' "$0" >&2
  exit 2
}

die() {
  echo "tile_compare: $*" >&2
  exit 2
}

DEFAULT_EXCERPT=/mnt/nadata/map2026asv/logs/gabby/logs/bizzy_m3/bag_2026-06-09T14.51.50_m3_detections_3000ping_excerpt

MODE=${1:-}
[ -n "$MODE" ] || usage
shift

SRC=""
OUT=""
FROM=""
DRY=0
if [ "$MODE" = compare ]; then
  [ $# -eq 2 ] || usage
else
  while [ $# -gt 0 ]; do
    case "$1" in
      --src) SRC=$2; shift 2 ;;
      --out) OUT=$2; shift 2 ;;
      --from) FROM=$2; shift 2 ;;
      --dry-run) DRY=1; shift ;;
      *) usage ;;
    esac
  done
fi

# The Python half, written next to the results so the script is one file.
write_python() {
  cat > "$1" <<'PYEOF'
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

TOL = 1e-3            # 1 mm, in the band's units (metres for depth and uncertainty)
DEPTH_STORES = ('depth', 'real_depth')
REPORT_ONLY_STORES = ('beam_bs', 'slope_bs')
REAL_ARGS = ['-d', '/bizzy/sensors/m3/detections', '--odom-topic', '/bizzy/odom',
             '--base-link-frame', 'bizzy/base_link',
             '--level-frame', 'bizzy/base_link_north_up', '--tide-frame', 'bizzy/map_tide',
             '--platform', 'bizzyboat', '--sensor', 'kongsberg-m3',
             '--campaign', 'massabesic-jun2026', '-r', '1.0']


def slope_module(src):
    test_dir = pathlib.Path(src) / 'cube_bathymetry' / 'test'
    if not (test_dir / 'test_import_bag_slope.py').is_file():
        sys.exit('tile_compare: no cube_bathymetry/test/test_import_bag_slope.py under %s'
                 % src)
    sys.path.insert(0, str(test_dir))
    import test_import_bag_slope as slope   # noqa: E402
    return slope


def cmd_bags(src, bag_dir):
    slope = slope_module(src)
    bag_dir = pathlib.Path(bag_dir)
    bag_dir.mkdir(parents=True)
    for k, heading in enumerate(slope.HEADINGS):
        slope.write_plane_bag(bag_dir / ('pass_%03d' % heading), heading, t0=1000.0 + 100.0 * k)
    slope.write_incidence_table(bag_dir / 'incidence_table.csv')


def run(cmd, log):
    with open(log, 'w') as f:
        f.write('$ ' + ' '.join(cmd) + '\n')
        f.flush()
        rc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT).returncode
    print('  %s -> exit %d (log %s)' % (os.path.basename(log), rc, log))
    if rc != 0:
        sys.exit('tile_compare: %s failed with exit %d; see %s' % (cmd[0], rc, log))


def cmd_import(src, import_bag, bag_dir, stores, logs):
    slope = slope_module(src)
    bag_dir, stores, logs = pathlib.Path(bag_dir), pathlib.Path(stores), pathlib.Path(logs)
    stores.mkdir(parents=True)
    logs.mkdir(parents=True, exist_ok=True)
    bags = [str(bag_dir / ('pass_%03d' % h)) for h in slope.HEADINGS]
    run([import_bag] + slope._common_args(
        ['-o', str(stores / 'depth'), '--bs-store', str(stores / 'beam_bs'),
         '--backscatter-correction', 'none']) + bags, logs / 'import_depth.log')
    run([import_bag] + slope._common_args(
        slope.slope_args(stores / 'depth', stores / 'slope_bs',
                         bag_dir / 'incidence_table.csv')) + bags, logs / 'import_slope.log')
    real = bag_dir / 'real_excerpt'
    if real.is_dir():
        run([import_bag, '-o', str(stores / 'real_depth')] + REAL_ARGS + [str(real)],
            logs / 'import_real.log')


# ----------------------------------------------------------------------------------
# Comparison
# ----------------------------------------------------------------------------------

VOLATILE = ('written', 'time', 'date', 'fingerprint', 'build', 'host', 'created',
            'modified', 'version', 'git', 'compiler')


def scrub(obj):
    if isinstance(obj, dict):
        return {k: scrub(v) for k, v in obj.items()
                if not any(w in k.lower() for w in VOLATILE)}
    if isinstance(obj, list):
        return [scrub(v) for v in obj]
    return obj


def files_of(store):
    store = pathlib.Path(store)
    if not store.is_dir():
        return {}
    return {str(p.relative_to(store)): p for p in sorted(store.rglob('*'))
            if p.is_file() and '.incoming' not in str(p) and p.name != '.slope-run.lock'}


def components(mask):
    """Sizes of the 8-connected components of True cells in a 2-D boolean array."""
    import numpy as np
    seen = np.zeros(mask.shape, dtype=bool)
    sizes = []
    rows, cols = mask.shape
    for r0, c0 in zip(*np.nonzero(mask)):
        if seen[r0, c0]:
            continue
        stack = [(r0, c0)]
        seen[r0, c0] = True
        n = 0
        while stack:
            r, c = stack.pop()
            n += 1
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    rr, cc = r + dr, c + dc
                    if 0 <= rr < rows and 0 <= cc < cols and mask[rr, cc] and not seen[rr, cc]:
                        seen[rr, cc] = True
                        stack.append((rr, cc))
        sizes.append(n)
    return sizes


BINS = [(0.0, 0.0, '== 0'), (0.0, 1e-12, '(0, 1e-12]'), (1e-12, 1e-9, '(1e-12, 1e-9]'),
        (1e-9, 1e-6, '(1e-9, 1e-6]'), (1e-6, 1e-3, '(1e-6, 1e-3]'),
        (1e-3, 1e-2, '(1e-3, 1e-2]'), (1e-2, 1e-1, '(1e-2, 1e-1]'),
        (1e-1, float('inf'), '> 1e-1')]


def compare_store(name, a_root, b_root, gated, out):
    import numpy as np
    from osgeo import gdal
    gdal.UseExceptions()
    ok = True
    a_files, b_files = files_of(a_root), files_of(b_root)
    a_tifs = {k for k in a_files if k.endswith('.tif')}
    b_tifs = {k for k in b_files if k.endswith('.tif')}
    rule = 'PASS RULE (1 mm)' if gated else 'reported only'
    out.append('')
    out.append('=== store %s (%s): %d tiles in A, %d in B' % (name, rule, len(a_tifs),
                                                            len(b_tifs)))
    if not a_files and not b_files:
        out.append('  absent on both sides')
        return True
    if a_tifs != b_tifs:
        out.append('  tile lists DIFFER: only in A %s; only in B %s'
                   % (sorted(a_tifs - b_tifs), sorted(b_tifs - a_tifs)))
        ok = False
    else:
        out.append('  tile lists: same')
    hist = {}
    total = {'cells': 0, 'valid': 0, 'differ': 0, 'beyond': 0, 'mask': 0}
    worst = (0.0, None)
    tiles_differ, tiles_beyond, bytes_equal = set(), set(), 0
    comp_sizes = []
    for rel in sorted(a_tifs & b_tifs):
        if a_files[rel].read_bytes() == b_files[rel].read_bytes():
            bytes_equal += 1
        da, db = gdal.Open(str(a_files[rel])), gdal.Open(str(b_files[rel]))
        if (da.RasterCount, da.RasterXSize, da.RasterYSize) != \
                (db.RasterCount, db.RasterXSize, db.RasterYSize):
            out.append('  %s: shape or band count DIFFERS' % rel)
            ok = False
            continue
        if da.GetGeoTransform() != db.GetGeoTransform():
            out.append('  %s: geotransform DIFFERS %s vs %s'
                       % (rel, da.GetGeoTransform(), db.GetGeoTransform()))
            ok = False
        for band in range(1, da.RasterCount + 1):
            ba, bb = da.GetRasterBand(band), db.GetRasterBand(band)
            xa = ba.ReadAsArray().astype(np.float64)
            xb = bb.ReadAsArray().astype(np.float64)
            is_int = gdal.GetDataTypeName(ba.DataType).lower().startswith(('byte', 'int',
                                                                           'uint'))
            nda, ndb = ba.GetNoDataValue(), bb.GetNoDataValue()
            ma = np.isnan(xa) | ((xa == nda) if nda is not None else False)
            mb = np.isnan(xb) | ((xb == ndb) if ndb is not None else False)
            mask_diff = int(np.count_nonzero(ma != mb))
            total['cells'] += xa.size
            total['mask'] += mask_diff
            if mask_diff:
                out.append('  %s band %d: nodata mask DIFFERS in %d cells' % (rel, band,
                                                                           mask_diff))
                ok = False
                tiles_beyond.add(rel)
            both = ~ma & ~mb
            total['valid'] += int(np.count_nonzero(both))
            d = np.where(both, np.abs(xa - xb), 0.0)
            ndiff = int(np.count_nonzero(d > 0))
            limit = 0.0 if is_int else TOL
            beyond = both & (d > limit)
            nbeyond = int(np.count_nonzero(beyond))
            total['differ'] += ndiff
            total['beyond'] += nbeyond
            if ndiff:
                tiles_differ.add(rel)
            if nbeyond:
                tiles_beyond.add(rel)
                comp_sizes += components(beyond)
            for lo, hi, label in BINS:
                if lo == hi == 0.0:
                    n = int(np.count_nonzero(both & (d == 0)))
                else:
                    n = int(np.count_nonzero(both & (d > lo) & (d <= hi)))
                hist[label] = hist.get(label, 0) + n
            if d.size and d.max() > worst[0]:
                r, c = np.unravel_index(int(np.argmax(d)), d.shape)
                worst = (float(d.max()), '%s band %d cell (row %d, col %d): A %.12g, B %.12g'
                         % (rel, band, r, c, xa[r, c], xb[r, c]))
        del da, db
    out.append('  tile files byte-identical: %d of %d' % (bytes_equal, len(a_tifs & b_tifs)))
    out.append('  cells: %d (%d valid on both sides); nodata-mask differences: %d'
               % (total['cells'], total['valid'], total['mask']))
    out.append('  cells that differ at all: %d, in %d tiles' % (total['differ'],
                                                                len(tiles_differ)))
    out.append('  cells beyond 1 mm: %d, in %d tiles' % (total['beyond'], len(tiles_beyond)))
    out.append('  largest difference: %.6g%s' % (worst[0], (' at ' + worst[1]) if worst[1]
                                                 else ''))
    out.append('  histogram of |A - B| over cells valid on both sides:')
    for _, _, label in BINS:
        out.append('    %-16s %d' % (label, hist.get(label, 0)))
    if comp_sizes:
        iso = sum(1 for s in comp_sizes if s == 1)
        out.append('  cells beyond 1 mm form %d groups (8-connected): %d isolated cells, '
                   'largest group %d cells' % (len(comp_sizes), iso, max(comp_sizes)))
    else:
        out.append('  cells beyond 1 mm: none, so no groups')
    if gated and total['beyond']:
        ok = False
    # Non-tile files: reported, not required.
    others = sorted((set(a_files) | set(b_files)) - a_tifs - b_tifs)
    for rel in others:
        if rel not in a_files or rel not in b_files:
            out.append('  other file %s only in %s' % (rel, 'A' if rel in a_files else 'B'))
            continue
        ra, rb = a_files[rel].read_bytes(), b_files[rel].read_bytes()
        if ra == rb:
            out.append('  other file %s: byte-identical' % rel)
            continue
        try:
            same = scrub(json.loads(ra)) == scrub(json.loads(rb))
            out.append('  other file %s: differs in bytes; %s once write times, versions and '
                       'build fingerprints are removed' % (rel, 'equal' if same else 'DIFFERS'))
        except ValueError:
            out.append('  other file %s: differs in bytes (not JSON; not compared further)'
                       % rel)
    if gated:
        out.append('  store verdict: %s' % ('PASS' if ok else 'FAIL'))
    return ok or not gated


def bag_messages(path):
    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py import message_to_ordereddict
    from rosidl_runtime_py.utilities import get_message
    reader = rosbag2_py.SequentialReader()
    reader.open(rosbag2_py.StorageOptions(uri=str(path), storage_id='mcap'),
                rosbag2_py.ConverterOptions('cdr', 'cdr'))
    types = {t.name: t.type for t in reader.get_all_topics_and_types()}
    out = []
    while reader.has_next():
        topic, data, stamp = reader.read_next()
        msg = deserialize_message(data, get_message(types[topic]))
        # repr of the field dict: NaN fields compare equal, unlike msg == msg.
        out.append((topic, stamp, repr(message_to_ordereddict(msg))))
    return out


def compare_bags(a_dir, b_dir):
    """Whether the bags of two sides hold the same messages, decoded (CDR padding and
    file names left out)."""
    names = sorted(p.name for p in pathlib.Path(a_dir).glob('pass_*'))
    if names != sorted(p.name for p in pathlib.Path(b_dir).glob('pass_*')):
        return 'DIFFERENT bag sets'
    differ = []
    total = 0
    for name in names:
        ma, mb = bag_messages(pathlib.Path(a_dir) / name), bag_messages(pathlib.Path(b_dir) / name)
        total += len(ma)
        if ma != mb:
            differ.append(name)
    if differ:
        return 'messages DIFFER in %s' % ', '.join(differ)
    return 'the same %d messages (topic, stamp, values) in %d synthetic bags' % (total,
                                                                               len(names))


def cmd_compare(a, b, report):
    a, b = pathlib.Path(a), pathlib.Path(b)
    out = ['tile_compare report', 'A: %s' % a, 'B: %s' % b]
    for side, root in (('A', a), ('B', b)):
        v = root / 'versions.txt'
        if v.is_file():
            out.append('--- %s versions.txt' % side)
            out += ['  ' + line for line in v.read_text().splitlines()]
    # The bags each side read.
    sa = (a / 'bags.sha256').read_text() if (a / 'bags.sha256').is_file() else ''
    sb = (b / 'bags.sha256').read_text() if (b / 'bags.sha256').is_file() else ''
    out.append('')
    if sa and sa == sb:
        out.append('bags read: the same bytes (bags.sha256 equal)')
    else:
        out.append('bags read: NOT the same bytes (bags.sha256 differ); decoded: %s'
                   % compare_bags(a / 'bags', b / 'bags'))
    verdict = True
    for name in DEPTH_STORES + REPORT_ONLY_STORES:
        if name == 'real_depth' and not ((a / 'stores' / name).is_dir()
                                         and (b / 'stores' / name).is_dir()):
            out.append('')
            out.append('=== store real_depth: not compared (real excerpt imported on %s)'
                       % ('neither side' if not (a / 'stores' / name).is_dir()
                          and not (b / 'stores' / name).is_dir() else 'one side only'))
            continue
        verdict &= compare_store(name, a / 'stores' / name, b / 'stores' / name,
                                 name in DEPTH_STORES, out)
    out.append('')
    out.append('VERDICT (depth stores, 1 mm rule): %s' % ('PASS' if verdict else 'FAIL'))
    text = '\n'.join(out) + '\n'
    pathlib.Path(report).write_text(text)
    print(text, end='')
    return 0 if verdict else 1


if __name__ == '__main__':
    what = sys.argv[1]
    if what == 'bags':
        cmd_bags(sys.argv[2], sys.argv[3])
    elif what == 'import':
        cmd_import(*sys.argv[2:7])
    elif what == 'compare':
        sys.exit(cmd_compare(sys.argv[2], sys.argv[3], sys.argv[4]))
PYEOF
}

find_import_bag() {
  local p
  IFS=: read -r -a prefixes <<< "${AMENT_PREFIX_PATH:-}"
  for p in "${prefixes[@]}"; do
    if [ -x "$p/lib/cube_bathymetry/import_bag" ]; then
      echo "$p/lib/cube_bathymetry/import_bag"
      return 0
    fi
  done
  return 1
}

sums() {
  # sha256 of every file under $1, by path relative to $1, sorted.
  (cd "$1" && find . -type f ! -name '.slope-run.lock' -print0 | sort -z |
     xargs -0 -r sha256sum)
}

record_versions() {
  local f=$1 src=$2 import_bag=$3
  {
    echo "side: $4"
    echo "date: $(date -Iseconds)"
    echo "host: $(hostname)"
    echo "os: $(. /etc/os-release && echo "$PRETTY_NAME")"
    echo "ROS_DISTRO: ${ROS_DISTRO:-unset}"
    echo "source: $(git -C "$src" rev-parse HEAD 2>/dev/null || echo unknown)$(
      git -C "$src" diff --quiet -- cube_bathymetry 2>/dev/null || echo ' (cube_bathymetry/ modified)')"
    echo "import_bag: $import_bag"
    echo "import_bag sha256: $(sha256sum "$import_bag" | cut -d' ' -f1)"
    echo "gcc: $(gcc --version 2>/dev/null | head -1)"
    echo "gdal-config: $(gdal-config --version 2>/dev/null || echo unknown)"
    echo "python gdal: $(python3 -c 'from osgeo import gdal; print(gdal.__version__)' 2>/dev/null)"
    echo "glibc: $(ldd --version 2>/dev/null | head -1)"
    echo "libgdal linked: $(ldd "$import_bag" | grep -o 'libgdal[^ ]*' | head -1)"
    echo "packages:"
    dpkg-query -W -f '  ${Package} ${Version}\n' "ros-${ROS_DISTRO}-tf2*" \
      "ros-${ROS_DISTRO}-rosbag2*" "ros-${ROS_DISTRO}-geometry2" "ros-${ROS_DISTRO}-mcap*" \
      'libgdal*' 'libproj*' 'libc6' 2>/dev/null | grep -v ' $' || true
  } > "$f"
}

# One side's run: bags (written here or taken from elsewhere), imports, sums.
side_run() {
  local side=$1 res=$2 bags_from=$3
  local import_bag
  import_bag=$(find_import_bag) || die "no lib/cube_bathymetry/import_bag on AMENT_PREFIX_PATH; source the workspace's install/setup.bash first"
  [ -n "$SRC" ] && [ -f "$SRC/cube_bathymetry/test/test_import_bag_slope.py" ] ||
    die "--src must be the cube_bathymetry checkout (holding cube_bathymetry/test/)"
  mkdir -p "$res/logs"
  local py="$res/tile_compare.py"
  write_python "$py"
  if [ -z "$bags_from" ]; then
    echo "writing the synthetic bags ($side) ..."
    python3 "$py" bags "$SRC" "$res/bags"
    local excerpt=${CUBE_REAL_BAG_EXCERPT:-$DEFAULT_EXCERPT}
    if [ -f "$excerpt/metadata.yaml" ]; then
      echo "copying the real excerpt $excerpt ($(du -sh "$excerpt" | cut -f1)) ..."
      cp -r "$excerpt" "$res/bags/real_excerpt"
    else
      echo "real excerpt not reachable at $excerpt: synthetic bags only"
    fi
  else
    echo "using the bags in $bags_from"
    cp -r "$bags_from" "$res/bags"
  fi
  record_versions "$res/versions.txt" "$SRC" "$import_bag" "$side"
  echo "importing ($side, $import_bag) ..."
  python3 "$py" import "$SRC" "$import_bag" "$res/bags" "$res/stores" "$res/logs"
  sums "$res/bags" > "$res/bags.sha256"
  sums "$res/stores" > "$res/stores.sha256"
  echo "$side: $(wc -l < "$res/bags.sha256") bag files, $(grep -c '\.tif$' "$res/stores.sha256") tile files"
}

fresh_out() {
  [ -n "$OUT" ] || die "--out <dir> is required"
  if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT" 2>/dev/null)" ]; then
    die "$OUT exists and is not empty"
  fi
  mkdir -p "$OUT"
  OUT=$(cd "$OUT" && pwd)
}

case "$MODE" in
  jazzy)
    fresh_out
    [ "${ROS_DISTRO:-}" = jazzy ] || echo "warning: ROS_DISTRO is '${ROS_DISTRO:-unset}', not jazzy" >&2
    side_run jazzy "$OUT/jazzy" ""
    tar -C "$OUT" -czf "$OUT/tile_compare_jazzy.tar.gz" jazzy
    echo
    echo "send back: $OUT/tile_compare_jazzy.tar.gz ($(du -h "$OUT/tile_compare_jazzy.tar.gz" | cut -f1))"
    ;;
  rolling)
    fresh_out
    [ "${ROS_DISTRO:-}" = rolling ] || echo "warning: ROS_DISTRO is '${ROS_DISTRO:-unset}', not rolling" >&2
    if [ "$DRY" = 1 ]; then
      side_run rolling "$OUT/rolling" ""
      tar -C "$OUT" -czf "$OUT/tile_compare_rolling.tar.gz" rolling
      echo "dry run: Rolling wrote and read its own bags; no Jazzy comparison."
      echo "checksums: $OUT/rolling/bags.sha256, $OUT/rolling/stores.sha256"
    else
      [ -f "$FROM" ] || die "--from <tile_compare_jazzy.tar.gz> is required (or --dry-run)"
      tar -C "$OUT" -xzf "$FROM"
      [ -d "$OUT/jazzy/bags" ] || die "$FROM holds no jazzy/bags"
      side_run rolling "$OUT/rolling" "$OUT/jazzy/bags"
      echo
      write_python "$OUT/tile_compare.py"
      set +e
      python3 "$OUT/tile_compare.py" compare "$OUT/jazzy" "$OUT/rolling" "$OUT/report.txt"
      rc=$?
      set -e
      echo "report: $OUT/report.txt"
      exit $rc
    fi
    ;;
  compare)
    A=$1
    B=$2
    py=$(mktemp "${TMPDIR:-/tmp}/tile_compare.XXXXXX.py")
    trap 'rm -f "$py"' EXIT
    write_python "$py"
    set +e
    python3 "$py" compare "$A" "$B" "$B/report_vs_$(basename "$A").txt"
    rc=$?
    set -e
    exit $rc
    ;;
  *)
    usage
    ;;
esac
