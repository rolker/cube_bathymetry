#!/usr/bin/env python3
# Copyright 2026 University of New Hampshire
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""
The slope run of import_bag over synthetic bags on a tilted plane (#173).

Four bags cross one tilted plane on four headings, so the same ground is seen
from four sides and the angle the seabed sees differs between them by the
slope. Each beam's raw level is A + f(true incidence) with f a straight line
in degrees. The ordinary import gives the depth store; a slope run (incidence
against that store, an incidence table that is f) puts every beam back at A,
whichever way it was looking. The site is centred on a GGGS tile edge, so every
bag reaches both tiles and the stencils cross the edge.

Runs only when CMake hands over the built binary in ``IMPORT_BAG_EXE``.
"""

import hashlib
import json
import math
import os
import pathlib
import shutil
import subprocess
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_derive_angular_response as helpers  # noqa: E402  (bag writer, modes)

IMPORT_BAG = os.environ.get('IMPORT_BAG_EXE', '')
BATCH_REGEN = os.environ.get('BATCH_REGEN_EXE', '')

pytestmark = pytest.mark.skipif(
    not IMPORT_BAG, reason='IMPORT_BAG_EXE unset (CMake sets it to the built import_bag)')


@pytest.fixture(autouse=True)
def _import_bag_is_built():
    assert os.access(IMPORT_BAG, os.X_OK), (
        'IMPORT_BAG_EXE=%r is not an executable: the import_bag target moved or was not '
        'built' % IMPORT_BAG)


# The site: lat 43.0 is a level-10 GGGS tile edge (8 deg / 2^10 = 0.0078125 deg
# divides it), so the centre of every track sits on the boundary between two tiles.
LAT0, LON0 = 43.0, -70.6
SENSOR_HEIGHT = 25.0           # m above the plane at the origin
LEVEL_A = -25.0                # dB: the true, slope-free level
DB_PER_DEG = -0.2              # raw = A + DB_PER_DEG * incidence_deg
SOUND_SPEED = 1500.0
HEADINGS = (0.0, 90.0, 180.0, 270.0)
TABLE_TOP_DEG = 61.0           # the outermost bin centre of the incidence table
TILES = ('10_17791_14003.tif', '10_17792_14003.tif')    # the two tiles at the site


class Plane:
    """The seabed: z = -H + dh/de * east + dh/dn * north, about the site centre."""

    def __init__(self, slope_deg, aspect_deg):
        t = math.tan(math.radians(slope_deg))
        a = math.radians(aspect_deg)
        self.de, self.dn = t * math.sin(a), t * math.cos(a)

    def incidence_deg(self, theta_deg, heading_deg):
        """Incidence of a beam at rx angle theta (starboard +) on this plane."""
        th, h = math.radians(theta_deg), math.radians(heading_deg)
        d = (math.sin(th) * math.cos(h), -math.sin(th) * math.sin(h), -math.cos(th))
        n = (-self.de, -self.dn, 1.0)
        norm = math.sqrt(sum(c * c for c in n))
        cosine = -sum(d[i] * n[i] for i in range(3)) / norm
        return math.degrees(math.acos(max(-1.0, min(1.0, cosine))))

    def range_m(self, theta_deg, heading_deg, e0, n0):
        """Slant range from the sensor at (e0, n0) along the beam to the plane, or None."""
        th, h = math.radians(theta_deg), math.radians(heading_deg)
        denominator = math.cos(th) + math.sin(th) * (self.de * math.cos(h) - self.dn * math.sin(h))
        numerator = SENSOR_HEIGHT - self.de * e0 - self.dn * n0
        if denominator <= 1e-6 or numerator <= 0:
            return None
        return numerator / denominator


PLANE = Plane(15.0, 60.0)


def _enu_axes():
    lat, lon = math.radians(LAT0), math.radians(LON0)
    east = (-math.sin(lon), math.cos(lon), 0.0)
    north = (-math.sin(lat) * math.cos(lon), -math.sin(lat) * math.sin(lon), math.cos(lat))
    up = (math.cos(lat) * math.cos(lon), math.cos(lat) * math.sin(lon), math.sin(lat))
    return east, north, up


def _rotation_quaternion(heading_deg):
    """Quaternion of the ECEF <- sonar rotation: x forward, y starboard, z down."""
    east, north, up = _enu_axes()
    h = math.radians(heading_deg)
    f = [math.sin(h) * east[i] + math.cos(h) * north[i] for i in range(3)]
    s = [math.cos(h) * east[i] - math.sin(h) * north[i] for i in range(3)]
    d = [-up[i] for i in range(3)]
    m = [[f[0], s[0], d[0]], [f[1], s[1], d[1]], [f[2], s[2], d[2]]]
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0:
        k = math.sqrt(trace + 1.0) * 2
        return ((m[2][1] - m[1][2]) / k, (m[0][2] - m[2][0]) / k, (m[1][0] - m[0][1]) / k,
                0.25 * k)
    i = max(range(3), key=lambda j: m[j][j])
    j, k_ = (i + 1) % 3, (i + 2) % 3
    r = math.sqrt(m[i][i] - m[j][j] - m[k_][k_] + 1.0) * 2
    q = [0.0, 0.0, 0.0]
    q[i] = 0.25 * r
    q[j] = (m[j][i] + m[i][j]) / r
    q[k_] = (m[k_][i] + m[i][k_]) / r
    return (q[0], q[1], q[2], (m[k_][j] - m[j][k_]) / r)


def _earth_tf(t_sec, heading_deg, e0, n0):
    from geometry_msgs.msg import TransformStamped
    from tf2_msgs.msg import TFMessage
    east, north, up = _enu_axes()
    origin = helpers.geodetic_to_ecef(LAT0, LON0, 0.0)
    t = TransformStamped()
    t.header.frame_id = 'earth'
    t.header.stamp = helpers._stamp(t_sec)
    t.child_frame_id = 'base_link'
    pos = [origin[i] + e0 * east[i] + n0 * north[i] for i in range(3)]
    t.transform.translation.x, t.transform.translation.y, t.transform.translation.z = pos
    qx, qy, qz, qw = _rotation_quaternion(heading_deg)
    t.transform.rotation.x, t.transform.rotation.y = qx, qy
    t.transform.rotation.z, t.transform.rotation.w = qz, qw
    return TFMessage(transforms=[t])


def _static_identity():
    from geometry_msgs.msg import TransformStamped
    from tf2_msgs.msg import TFMessage
    t = TransformStamped()
    t.header.frame_id = 'base_link'
    t.header.stamp = helpers._stamp(0.0)
    t.child_frame_id = 'mbes'
    t.transform.rotation.w = 1.0
    return TFMessage(transforms=[t])


def _ping(t_sec, plane, heading_deg, e0, n0, thetas, level_fn):
    from marine_acoustic_msgs.msg import SonarDetections
    msg = SonarDetections()
    msg.header.stamp = helpers._stamp(t_sec)
    msg.header.frame_id = 'mbes'
    msg.ping_info.frequency = 500000.0
    msg.ping_info.sound_speed = SOUND_SPEED
    rx, tx, ttt, intensities = [], [], [], []
    for theta in thetas:
        r = plane.range_m(theta, heading_deg, e0, n0)
        if r is None:
            continue
        rx.append(math.radians(theta))
        tx.append(0.0)
        ttt.append(2.0 * r / SOUND_SPEED)
        intensities.append(level_fn(theta, heading_deg))
    msg.rx_angles, msg.tx_angles, msg.two_way_travel_times = rx, tx, ttt
    msg.intensities = intensities
    return msg


FAN = [-45.0 + 0.5 * k for k in range(181)]


def write_plane_bag(path, heading_deg, t0, plane=PLANE, length_m=60.0, speed=3.0, ping_hz=10.0,
                    thetas=None, level_fn=None, sensor_mode=helpers.REF_KEY):
    """One pass through the site centre on ``heading_deg``, starting at time t0."""
    thetas = thetas if thetas is not None else FAN
    if level_fn is None:
        def level_fn(theta, heading):
            return LEVEL_A + DB_PER_DEG * plane.incidence_deg(theta, heading)
    n_pings = int(length_m / speed * ping_hz)
    h = math.radians(heading_deg)
    events = [(t0 - 2.0, '/bizzy/tf_static', _static_identity()),
              (t0 - 1.0, helpers.INFO_TOPIC, helpers._sonar_info(t0 - 1.0, sensor_mode))]
    for i in range(-20, n_pings + 60):          # TF runs past the last ping
        ts = t0 + i / ping_hz
        s = -length_m / 2 + speed * (ts - t0)
        events.append((ts, '/bizzy/tf', _earth_tf(ts, heading_deg, s * math.sin(h),
                                                  s * math.cos(h))))
    for i in range(n_pings):
        ts = t0 + i / ping_hz
        s = -length_m / 2 + speed * (ts - t0)
        events.append((ts + 0.001, helpers.TOPIC,
                       _ping(ts, plane, heading_deg, s * math.sin(h), s * math.cos(h), thetas,
                             level_fn)))
    events.sort(key=lambda e: e[0])
    helpers.write_bag(path, events, helpers.MODE_BAG_TOPICS)


def write_incidence_table(path, top_deg=TABLE_TOP_DEG, response_db_per_deg=DB_PER_DEG,
                          records=True, slope_baseline=5.0, slope_layers='processed',
                          shift_db=0.0):
    """Write a one-mode incidence table whose angle term is the synthetic response."""
    rows = []
    centre = 1.0
    while centre <= top_deg + 1e-9:
        rows.append('%s,angle,%g,,,-20,100,9,%.3f,0.3,0.000,0.000,true,UNKNOWN,nan' % (
            helpers.REF_KEY, centre, response_db_per_deg * centre + shift_db))
        centre += 2.0
    lines = ['# format: angular-response-table v2'] if records else [
        '# format: angular-response-table v1']
    if records:
        lines += ['# angle_basis: incidence', '# slope_baseline_deg: %g' % slope_baseline,
                  '# slope_layers: %s' % slope_layers]
    lines += ['# reference_mode: %s' % helpers.REF_KEY, '# range_ref_m: 20',
              '# angle_bin_width_deg: 2',
              '# mode_summary: %s pings=100 beams=10000 declared_tvg=UNKNOWN' % helpers.REF_KEY,
              'mode,term,x,angle_lo_deg,angle_hi_deg,value_db,n,var_db2,db_relative_to_nadir,'
              'se_db,offset_db_vs_ref,offset_se_db,offset_anchored,declared_tvg_model,'
              'declared_tvg_absorption_db_per_km'] + rows
    pathlib.Path(path).write_text('\n'.join(lines) + '\n')
    return path


def _common_args(extra=()):
    # The synthetic bags have no attitude or heave frames; naming base_link for
    # both makes those lookups the identity.
    return ['-d', helpers.TOPIC, '--sonar-info-topic', helpers.INFO_TOPIC,
            '--base-link-frame', 'base_link', '--level-frame', 'base_link',
            '--tide-frame', 'base_link', '-r', '1.0'] + list(extra)


def run_import(args, bags, timeout=600):
    cmd = [IMPORT_BAG] + _common_args(args) + [str(b) for b in bags]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          timeout=timeout)
    return proc.returncode, proc.stdout


def slope_args(surface, bs_store, table=None, extra=()):
    args = ['--backscatter-angle-basis', 'incidence', '--slope-surface', str(surface),
            '--bs-store', str(bs_store)]
    if table is not None:
        args += ['--backscatter-correction', 'empirical', '--backscatter-curve', str(table),
                 '--backscatter-mode-fallback', 'exclude']
    else:
        args += ['--backscatter-correction', 'none']
    return args + list(extra)


def cell_means(store, band=1):
    """{(tile name, row, col): value} of every finite cell of ``band`` (1-based)."""
    from osgeo import gdal
    gdal.UseExceptions()
    cells = {}
    for tif in sorted(pathlib.Path(store).rglob('*.tif')):
        if '.incoming' in str(tif):
            continue
        ds = gdal.Open(str(tif))
        arr = ds.GetRasterBand(band).ReadAsArray()
        nodata = ds.GetRasterBand(band).GetNoDataValue()
        rows, cols = arr.shape
        for r in range(rows):
            for c in range(cols):
                v = float(arr[r, c])
                if v == v and (nodata is None or v != nodata):
                    cells[(tif.name, r, c)] = v
        del ds
    return cells


def store_files(root):
    """{relative path: sha256 of content} of every file under ``root``, lock file left out."""
    out = {}
    for p in sorted(pathlib.Path(root).rglob('*')):
        if p.is_file() and p.name != '.slope-run.lock':
            out[str(p.relative_to(root))] = hashlib.sha256(p.read_bytes()).hexdigest()
    return out


def tile_files(root):
    return {k: v for k, v in store_files(root).items() if k.endswith('.tif')}


def manifest(store):
    return json.loads((pathlib.Path(store) / 'derived_tiles.json').read_text())


def scratch_dirs(parent):
    return sorted(p.name for p in pathlib.Path(parent).glob('slope-run-*'))


def median(values):
    ordered = sorted(values)
    return ordered[len(ordered) // 2]


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def bag_identity(bag):
    return {'name': pathlib.Path(bag).name,
            'metadata_sha256': sha(pathlib.Path(bag) / 'metadata.yaml')}


@pytest.fixture(scope='module')
def world(tmp_path_factory):
    """Four plane bags, the ordinary import of all of them, and the incidence table."""
    root = tmp_path_factory.mktemp('slope_world')
    bags = {}
    for k, heading in enumerate(HEADINGS):
        bags[heading] = root / ('pass_%03d' % heading)
        write_plane_bag(bags[heading], heading, t0=1000.0 + 100.0 * k)
    # Starboard-only passes over the same ground: the slope run reads these in the
    # aspect test, because a symmetric fan averages the slope away.
    side = {}
    for k, heading in enumerate(HEADINGS):
        side[heading] = root / ('side_%03d' % heading)
        write_plane_bag(side[heading], heading, t0=2000.0 + 100.0 * k,
                        thetas=[15.0 + 0.5 * j for j in range(61)])
    depth = root / 'depth'
    beam_bs = root / 'beam_bs'
    rc, out = run_import(['-o', str(depth), '--bs-store', str(beam_bs),
                          '--backscatter-correction', 'none'], list(bags.values()))
    assert rc == 0, out
    table = write_incidence_table(root / 'incidence_table.csv')
    return {'root': root, 'bags': bags, 'side': side, 'depth': depth, 'beam_bs': beam_bs,
            'table': table,
            'ordinary_output': out}


@pytest.fixture(scope='module')
def all_run(world):
    """Run the slope run over all four bags with the table; give its store and output."""
    store = world['root'] / 'all_run'
    rc, out = run_import(slope_args(world['depth'], store, world['table']),
                         list(world['bags'].values()))
    assert rc == 0, out
    return {'store': store, 'out': out}


def fresh(tmp_path):
    """Return a fresh bs store path under a tmp dir that holds nothing else of the run."""
    return tmp_path / 'bs'


# ---------------------------------------------------------------------------------------
# The correction itself
# ---------------------------------------------------------------------------------------

def test_the_ordinary_import_wrote_the_depths_and_the_beam_layer(world):
    assert (pathlib.Path(world['depth']) / 'processed' / TILES[0]).exists()
    assert (pathlib.Path(world['depth']) / 'processed' / TILES[1]).exists()
    assert cell_means(world['beam_bs'])


def test_corrected_level_does_not_depend_on_the_aspect(world, tmp_path):
    """Per heading: the slope run lands on the true level; the uncorrected level does not."""
    corrected, raw = {}, {}
    for heading, bag in world['side'].items():
        store = tmp_path / ('c%03d' % heading)
        rc, out = run_import(slope_args(world['depth'], store, world['table']), [bag])
        assert rc == 0, out
        corrected[heading] = list(cell_means(store).values())
        store = tmp_path / ('r%03d' % heading)
        rc, out = run_import(slope_args(world['depth'], store, None), [bag])
        assert rc == 0, out
        raw[heading] = list(cell_means(store).values())
    for heading in HEADINGS:
        assert len(corrected[heading]) > 1000, (heading, len(corrected[heading]))
        centre = median(corrected[heading])
        assert abs(centre - LEVEL_A) < 0.1, (heading, centre)
        within = sum(1 for v in corrected[heading] if abs(v - LEVEL_A) < 1.0)
        assert within > 0.85 * len(corrected[heading]), heading
    # Negative control: without the correction the four headings read different levels.
    medians = [median(raw[h]) for h in HEADINGS]
    assert max(medians) - min(medians) > 4.0, medians
    assert all(m < LEVEL_A - 1.0 for m in medians), medians


# ---------------------------------------------------------------------------------------
# What a slope run leaves behind
# ---------------------------------------------------------------------------------------

def test_manifest_records_inputs_contributors_and_depth_hashes(world, all_run):
    m = manifest(all_run['store'])
    assert m['format'] == 'derived-tiles v1' and m['schema_version'] == 1
    assert m['inputs'] == {
        'angle_basis': 'incidence', 'slope_baseline_deg': 5.0, 'slope_layers': 'processed',
        'correction': 'table:' + sha(world['table']), 'gain_declaration_sha256': '',
        'gain_reference_state': '', 'level': 10}
    assert sorted(m['tiles']) == sorted(TILES)
    bags = sorted((bag_identity(b) for b in world['bags'].values()), key=lambda b: b['name'])
    depth = pathlib.Path(world['depth']) / 'processed'
    expected_depth = [{'path': 'processed/' + name, 'sha256': sha(depth / name)} for name in TILES]
    for name in TILES:
        t = m['tiles'][name]
        # Two bags (here all four) over one tile edge are contributors of both tiles.
        assert t['contributors'] == bags, name
        assert t['depth_tiles'] == expected_depth, name
        assert t['stencil_margin_m'] > 2.0, t
        assert t['code_version'] == 'slope-run-1'
        assert t['written'].endswith('Z') and len(t['written']) == 20
    assert m['last_publish']['written'] == sorted(TILES)
    assert m['last_publish']['removed'] == []
    # Only backscatter tiles in the destination; no depths, no build fingerprint.
    files = store_files(all_run['store'])
    assert sorted(f for f in files if f.endswith('.tif')) == ['survey/' + t for t in sorted(TILES)]
    assert 'build_fingerprint.json' not in files
    assert not [f for f in files if '.incoming' in f or f.endswith('.pending')]
    assert 'Published 2 backscatter tile(s)' in all_run['out']
    assert 'differencing baseline' in all_run['out'] and 'at the one-cell floor' in all_run['out']


def test_no_depth_tile_anywhere_and_the_scratch_is_gone_after_ok_refused_and_failed(
        world, tmp_path):
    bags = list(world['bags'].values())
    bs = fresh(tmp_path)
    rc, out = run_import(slope_args(world['depth'], bs, world['table']), bags)
    assert rc == 0, out
    stray = [t for t in tmp_path.rglob('*.tif') if bs not in t.parents]
    assert not stray, 'a depth tile was written'
    assert scratch_dirs(tmp_path) == [], 'the scratch directory was left behind'

    # Refused (a subset of the bags): same.
    before = store_files(bs)
    rc, out = run_import(slope_args(world['depth'], bs, world['table']), bags[:2])
    assert rc != 0
    assert store_files(bs) == before
    assert scratch_dirs(tmp_path) == []

    # Failed (a damaged surface tile fails at the first lookup, mid-run).
    bad = tmp_path / 'depth_bad'
    shutil.copytree(world['depth'], bad)
    (bad / 'processed' / TILES[0]).write_bytes(b'not a tile')
    bs2 = tmp_path / 'bs2'
    rc, out = run_import(slope_args(bad, bs2, world['table']), bags)
    assert rc == 1, out
    assert 'slope surface could not be read' in out, out
    assert 'nothing was published' in out, out
    assert not list(bs2.rglob('*.tif')) and not (bs2 / 'derived_tiles.json').exists()
    assert scratch_dirs(tmp_path) == []
    assert not [t for t in tmp_path.rglob('*.tif') if bs not in t.parents and bad not in t.parents]


def test_a_second_run_with_the_same_bags_gives_identical_tiles(world, all_run, tmp_path):
    bags = list(world['bags'].values())
    other = tmp_path / 'other'
    rc, out = run_import(slope_args(world['depth'], other, world['table']), bags)
    assert rc == 0, out
    assert tile_files(other) == tile_files(all_run['store'])
    # And again into the same store: tiles unchanged, manifest equal but for the times.
    again = tmp_path / 'again'
    shutil.copytree(all_run['store'], again)
    first = tile_files(again)
    rc, out = run_import(slope_args(world['depth'], again, world['table']), bags)
    assert rc == 0, out
    assert tile_files(again) == first
    m1, m2 = manifest(all_run['store']), manifest(again)
    for m in (m1, m2):
        for t in m['tiles'].values():
            t.pop('written')
        m.pop('last_publish')
    assert m1 == m2


def test_a_run_with_a_subset_of_the_bags_is_refused_at_first_touch(world, all_run, tmp_path):
    store = tmp_path / 'subset'
    shutil.copytree(all_run['store'], store)
    before = store_files(store)
    bags = list(world['bags'].values())
    rc, out = run_import(slope_args(world['depth'], store, world['table']), bags[:3])
    assert rc == 1, out
    assert 'was built from 4 bag(s)' in out and 'pass_270' in out, out
    assert 'Nothing has been published' in out, out
    assert store_files(store) == before, 'the store must be byte-identical after a refusal'
    assert scratch_dirs(tmp_path) == []


def test_a_run_adding_a_bag_succeeds_when_it_is_given_all_the_recorded_ones(world, tmp_path):
    store = tmp_path / 'grow'
    bags = list(world['bags'].values())
    rc, out = run_import(slope_args(world['depth'], store, world['table']), bags[:3])
    assert rc == 0, out
    assert len(manifest(store)['tiles'][TILES[0]]['contributors']) == 3
    rc, out = run_import(slope_args(world['depth'], store, world['table']), bags)
    assert rc == 0, out
    assert len(manifest(store)['tiles'][TILES[0]]['contributors']) == 4


def test_contributors_are_right_under_forced_eviction(world, all_run, tmp_path):
    store = tmp_path / 'evict'
    rc, out = run_import(slope_args(world['depth'], store, world['table'],
                                    ['--max-resident-tiles', '1']), list(world['bags'].values()))
    assert rc == 0, out
    m = manifest(store)
    assert {n: t['contributors'] for n, t in m['tiles'].items()} == {
        n: t['contributors'] for n, t in manifest(all_run['store'])['tiles'].items()}
    # Two tiles, one resident at a time: the scratch stores really evicted and reloaded.
    written = int(out.split('Scratch stores: ')[1].split(' depth')[0])
    assert written > 10, out
    # The cells are the same cells, at the same level to within the effect of
    # re-deriving the depth hypotheses on every reload (a tile evicted between
    # every ping flushes its median pre-filter each time).
    a, b = cell_means(store), cell_means(all_run['store'])
    assert a.keys() == b.keys()
    diffs = sorted(abs(a[k] - b[k]) for k in a)
    print('eviction differences: median', diffs[len(diffs) // 2], 'max', diffs[-1])
    assert diffs[len(diffs) // 2] < 0.02 and diffs[-1] < 0.5


@pytest.mark.parametrize('what', ['baseline', 'table', 'gain'])
def test_a_run_with_other_inputs_is_refused_before_any_bag_is_read(
        world, all_run, tmp_path, what):
    store = tmp_path / 'inputs'
    shutil.copytree(all_run['store'], store)
    before = store_files(store)
    bags = list(world['bags'].values())
    table = world['table']
    extra = []
    expect = ''
    if what == 'baseline':
        extra, expect = ['--slope-baseline-deg', '6'], 'the slope baseline angle differs'
    elif what == 'table':
        table = write_incidence_table(tmp_path / 'other_table.csv', shift_db=1.0)
        expect = 'the correction differs'
    else:
        table, expect = None, 'the correction differs'
    rc, out = run_import(slope_args(world['depth'], store, table, extra), bags)
    assert rc == 1, out
    assert expect in out, out
    assert 'One store holds one correction' in out, out
    assert 'projecting detections' not in out, 'the refusal must come before any bag is read'
    assert store_files(store) == before
    assert scratch_dirs(tmp_path) == []


def test_a_different_gain_declaration_is_refused(world, tmp_path):
    store = tmp_path / 'gain'
    bag = world['side'][0.0]
    rc, out = run_import(slope_args(world['depth'], store, None), [bag])
    assert rc == 0, out
    declaration = HERE / 'data' / 'gain_declaration_v1.csv'
    rc, out = run_import(slope_args(world['depth'], store, None,
                                    ['--backscatter-gain-declaration', str(declaration)]), [bag])
    assert rc == 1, out
    assert 'the gain declaration differs' in out, out
    assert 'projecting detections' not in out


def test_bags_with_one_name_are_refused(world, tmp_path):
    twin = tmp_path / 'elsewhere' / world['bags'][0.0].name
    shutil.copytree(world['bags'][0.0], twin)
    rc, out = run_import(slope_args(world['depth'], fresh(tmp_path), world['table']),
                         [world['bags'][0.0], twin])
    assert rc != 0
    assert "two bags are named 'pass_000'" in out, out
    assert 'projecting detections' not in out


@pytest.mark.parametrize('extra,expect', [
    (['-l', '10'], '-l is refused'),
    (['-o', '/tmp/x_store'], '-o is refused'),
    (['--depth-adaptive'], '--depth-adaptive is refused'),
    (['--slope-baseline-deg', '0'], 'must be in (0, 30]'),
    (['--slope-baseline-deg', '31'], 'must be in (0, 30]'),
])
def test_slope_run_requests_that_are_refused(world, tmp_path, extra, expect):
    rc, out = run_import(slope_args(world['depth'], fresh(tmp_path), world['table'], extra),
                         [world['bags'][0.0]])
    assert rc != 0
    assert expect in out, out
    assert 'projecting detections' not in out and not list(tmp_path.rglob('*.tif'))


def test_slope_run_needs_its_surface_and_store(world, tmp_path):
    bag = world['bags'][0.0]
    rc, out = run_import(['--backscatter-angle-basis', 'incidence', '--bs-store',
                          str(fresh(tmp_path)), '--backscatter-correction', 'none'], [bag])
    assert rc != 0 and '--slope-surface' in out, out
    rc, out = run_import(['--backscatter-angle-basis', 'incidence', '--slope-surface',
                          str(world['depth']), '--backscatter-correction', 'none'], [bag])
    assert rc != 0 and '--bs-store' in out, out
    rc, out = run_import(['--backscatter-angle-basis', 'sideways'], [bag])
    assert rc != 0 and "must be 'beam' or 'incidence'" in out, out
    # The slope flags mean nothing on the beam basis.
    rc, out = run_import(['-o', str(tmp_path / 'o'), '--slope-surface', str(world['depth'])],
                         [bag])
    assert rc != 0 and 'needs --backscatter-angle-basis incidence' in out, out
    rc, out = run_import(['-o', str(tmp_path / 'o'), '--slope-baseline-deg', '4'], [bag])
    assert rc != 0 and 'needs --backscatter-angle-basis incidence' in out, out


def test_a_single_curve_and_a_beam_table_are_refused_on_incidence(world, tmp_path):
    bag = world['bags'][0.0]
    curve = tmp_path / 'curve.csv'
    curve.write_text('abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n'
                     '1,-20,100,0\n31,-26,100,-6\n')
    rc, out = run_import(slope_args(world['depth'], fresh(tmp_path), curve), [bag])
    assert rc != 0
    assert 'is a single curve; a single curve has no incidence form' in out, out
    beam_table = write_incidence_table(tmp_path / 'beam_table.csv', records=False)
    rc, out = run_import(slope_args(world['depth'], fresh(tmp_path), beam_table), [bag])
    assert rc != 0
    assert 'is a beam-angle table' in out and 'incidence angle' in out, out
    # ... and an incidence table on an ordinary (beam) import.
    rc, out = run_import(['-o', str(tmp_path / 'o'), '--backscatter-correction', 'empirical',
                          '--backscatter-curve', str(world['table'])], [bag])
    assert rc != 0 and 'is an incidence-angle table' in out, out
    assert not list(tmp_path.rglob('*.tif'))


def test_a_beam_angle_store_is_not_a_slope_store_and_the_reverse(world, tmp_path):
    bag = world['bags'][0.0]
    rc, out = run_import(slope_args(world['depth'], world['beam_bs'], world['table']), [bag])
    assert rc == 1 and 'beam-angle backscatter store' in out, out
    slope_store = tmp_path / 'slope_store'
    rc, out = run_import(slope_args(world['depth'], slope_store, world['table']), [bag])
    assert rc == 0, out
    rc, out = run_import(['-o', str(tmp_path / 'o'), '--bs-store', str(slope_store)], [bag])
    assert rc != 0 and 'holds derived_tiles.json' in out, out
    if BATCH_REGEN:
        proc = subprocess.run(
            [BATCH_REGEN, '-o', str(tmp_path / 'o2'), '--bs-store', str(slope_store), '-d',
             helpers.TOPIC, str(bag)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        assert proc.returncode != 0 and 'holds derived_tiles.json' in proc.stdout, proc.stdout


def test_a_held_lock_refuses_a_second_run_at_once(world, tmp_path):
    import fcntl
    store = fresh(tmp_path)
    store.mkdir()
    with open(store / '.slope-run.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        rc, out = run_import(slope_args(world['depth'], store, world['table']),
                             [world['bags'][0.0]])
    assert rc == 1 and 'another slope run holds' in out, out
    assert 'projecting detections' not in out


def test_an_abandoned_publish_is_cleaned_up_and_leftover_scratch_is_named(world, tmp_path):
    store = fresh(tmp_path)
    (store / 'survey' / '.incoming-slope-run-dead').mkdir(parents=True)
    (store / 'survey' / '.incoming-slope-run-dead' / TILES[0]).write_bytes(b'half a tile')
    (tmp_path / 'slope-run-AbCdEf').mkdir()
    rc, out = run_import(slope_args(world['depth'], store, world['table']),
                         [world['side'][0.0]])
    assert rc == 0, out
    assert 'a slope-run publish that never committed' in out, out
    assert 'slope-run-AbCdEf' in out and 'can be deleted' in out, out
    assert not list((store / 'survey').glob('.incoming*'))
    assert (tmp_path / 'slope-run-AbCdEf').exists(), 'a leftover is named, never deleted'


def test_scratch_dir_chooses_where_the_scratch_lives(world, tmp_path):
    scratch_root = tmp_path / 'fast_disk'
    rc, out = run_import(slope_args(world['depth'], fresh(tmp_path), world['table'],
                                    ['--scratch-dir', str(scratch_root)]), [world['side'][0.0]])
    assert rc == 0, out
    assert str(scratch_root / 'slope-run-') in out, out
    assert scratch_dirs(scratch_root) == [] and scratch_dirs(tmp_path) == []


def test_a_changed_depth_tile_changes_the_recorded_hash(world, all_run, tmp_path):
    from osgeo import gdal
    gdal.UseExceptions()
    depth2 = tmp_path / 'depth2'
    shutil.copytree(world['depth'], depth2)
    victim = depth2 / 'processed' / TILES[0]
    ds = gdal.Open(str(victim), gdal.GA_Update)
    band = ds.GetRasterBand(1)
    arr = band.ReadAsArray()
    arr[arr == arr] += 0.01          # a centimetre everywhere
    band.WriteArray(arr)
    del band, ds
    store = tmp_path / 'changed'
    rc, out = run_import(slope_args(depth2, store, world['table']), list(world['bags'].values()))
    assert rc == 0, out
    new, old = manifest(store), manifest(all_run['store'])
    for name in TILES:
        n = {d['path']: d['sha256'] for d in new['tiles'][name]['depth_tiles']}
        o = {d['path']: d['sha256'] for d in old['tiles'][name]['depth_tiles']}
        assert n.keys() == o.keys()
        assert n['processed/' + TILES[0]] != o['processed/' + TILES[0]]
        assert n['processed/' + TILES[1]] == o['processed/' + TILES[1]]


# ---------------------------------------------------------------------------------------
# The table accounting is on the incidence angle
# ---------------------------------------------------------------------------------------

@pytest.fixture(scope='module')
def accounting(tmp_path_factory):
    """
    Build a cross-track slope, a surface from a dense fan and a four-beam bag.

    The slope is 15 deg, rising to the port side of a heading-0 pass. The four beams are
    chosen so that the beam angle and the incidence put them on opposite sides of the
    table's edge at 32 deg: -40 and -30 deg look at the face turned toward the vessel
    (incidence 25 and 15), 20 and 25 deg look at the far side (incidence 35 and 40).
    """
    root = tmp_path_factory.mktemp('slope_accounting')
    plane = Plane(15.0, 270.0)
    dense = root / 'dense'
    write_plane_bag(dense, 0.0, 1000.0, plane=plane)
    four = root / 'four'
    write_plane_bag(four, 0.0, 2000.0, plane=plane, thetas=[-40.0, -30.0, 20.0, 25.0])
    depth = root / 'depth'
    rc, out = run_import(['-o', str(depth), '--backscatter-correction', 'none'], [dense])
    assert rc == 0, out
    table = write_incidence_table(root / 'table_to_31.csv', top_deg=31.0)
    return {'root': root, 'four': four, 'depth': depth, 'table': table, 'plane': plane}


def test_table_accounting_is_on_the_incidence_angle(accounting):
    plane = accounting['plane']
    assert [round(plane.incidence_deg(t, 0.0)) for t in (-40, -30, 20, 25)] == [25, 15, 35, 40]
    store = accounting['root'] / 'bs'
    rc, out = run_import(slope_args(accounting['depth'], store, accounting['table']),
                         [accounting['four']])
    assert rc == 0, out
    import re
    m = re.search(r'beams of resolved pings: (\d+) correctable, (\d+) not', out)
    assert m, out
    correctable, not_correctable = int(m.group(1)), int(m.group(2))
    outside = re.search(r'(\d+) beam\(s\), beam angle outside the written angle bins', out)
    assert outside, out
    # On the incidence angle: -40 and -30 are inside the table, 20 and 25 are beyond its edge:
    # about half the beams. On the sonar-frame angle it would be 3 inside and 1 outside, and
    # with the stamping after the accounting every beam would have no angle at all.
    total = correctable + not_correctable
    assert total == 800, out
    assert 340 <= not_correctable <= 460, out
    assert 340 <= correctable <= 460, out
    assert int(outside.group(1)) == not_correctable, out
    assert 'no finite beam angle' not in out, out
    assert "are on each beam's incidence angle" in out, out
