# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

"""
Tests for scripts/derive_angular_response.py (cube_bathymetry#170).

Covers the Francois-Garrison absorption, the CLI validation (alpha override,
--box), the CSV header provenance, the byte-identity of freshwater data rows
with the pre-#170 tool, the ECEF -> geodetic conversion and an end-to-end run
over a synthetic bag with the --box site filter.

The golden curve test/data/derived_curve_tier2_seawater.csv is written by
write_csv here and loaded by the C++ loader in test_angular_response_curve.cpp,
which closes the Python -> C++ round trip. If the header format changes on
purpose, regenerate it with GOLDEN_REGENERATE=1 and re-run both tests.
"""

import importlib.util
import math
import os
import pathlib
import re

import pytest

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE.parent / 'scripts' / 'derive_angular_response.py'
GOLDEN = HERE / 'data' / 'derived_curve_tier2_seawater.csv'

_spec = importlib.util.spec_from_file_location('derive_angular_response', SCRIPT)
dar = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(dar)


def pre170_freshwater_absorption_db_per_m(freq_khz, temp_c, depth_m=0.0):
    """Verbatim copy of the freshwater function the tool used before #170."""
    t = temp_c
    if t > 20.0:
        a3 = (3.964e-4 - 1.146e-5 * t + 1.45e-7 * t ** 2 - 6.5e-10 * t ** 3)
    else:
        a3 = (4.937e-4 - 2.590e-5 * t + 9.110e-7 * t ** 2 - 1.500e-8 * t ** 3)
    p3 = 1.0 - 3.83e-5 * depth_m + 4.9e-10 * depth_m ** 2
    alpha_db_per_km = a3 * p3 * freq_khz ** 2
    return alpha_db_per_km / 1000.0


fg = dar.francois_garrison_absorption_db_per_m


# --------------------------------------------------------------------------
# Absorption
# --------------------------------------------------------------------------

def test_fresh_water_500khz_24c():
    """Fresh water, 500 kHz, 24 C: 0.049 dB/m to 3 significant figures."""
    alpha = fg(500.0, 24.0)
    assert float('%.3g' % alpha) == 0.049
    assert alpha == pytest.approx(0.0489736, abs=1e-9)


@pytest.mark.parametrize('freq_khz', [12.0, 200.0, 300.0, 400.0, 500.0, 700.0, 1000.0])
@pytest.mark.parametrize('temp_c', [-2.0, 0.0, 4.3, 10.0, 19.99, 20.0, 20.01, 24.0, 39.0])
@pytest.mark.parametrize('depth_m', [0.0, 5.0, 25.0, 3000.0])
def test_salinity_zero_is_bit_identical_to_pre170(freq_khz, temp_c, depth_m):
    """Salinity 0 reproduces the pre-#170 freshwater alpha exactly."""
    # Exact equality, not approx: freshwater curves must keep byte-identical
    # data rows, and alpha feeds every row.
    for ph in (7.5, 8.0, 8.5):
        assert (fg(freq_khz, temp_c, 0.0, depth_m, ph)
                == pre170_freshwater_absorption_db_per_m(freq_khz, temp_c, depth_m))


def test_seawater_500khz_15c_32psu():
    """Seawater, 500 kHz, 15 C, 32 psu: about 0.126 dB/m."""
    assert fg(500.0, 15.0, 32.0) == pytest.approx(0.126, abs=0.001)


@pytest.mark.parametrize('temp_c,salinity,depth_m,expected', [
    (19.8, 31.2, 0.0, 0.137),   # Isles of Shoals casts, Aug 2026 (cube#170)
    (16.9, 30.8, 8.0, 0.128),
    (11.5, 31.4, 20.0, 0.121),
])
def test_seawater_matches_issue_cast_table(temp_c, salinity, depth_m, expected):
    """The Isles of Shoals cast values quoted in cube#170 are reproduced."""
    assert fg(500.0, temp_c, salinity, depth_m, 8.0) == pytest.approx(expected, abs=0.0006)


def test_salinity_increases_absorption_monotonically():
    """More salt, more absorption, at fixed T and f."""
    alphas = [fg(500.0, 15.0, s) for s in (0.0, 5.0, 10.0, 20.0, 30.0, 35.0, 40.0)]
    assert all(b > a for a, b in zip(alphas, alphas[1:]))
    # Salt water absorbs much more than fresh at 500 kHz.
    assert alphas[-2] > 2.0 * alphas[0]


def test_ph_only_acts_through_salt():
    """The pH has no effect in fresh water and raises alpha in salt water."""
    assert fg(500.0, 15.0, 0.0, 0.0, 7.6) == fg(500.0, 15.0, 0.0, 0.0, 8.4)
    # Boric acid term grows with pH.
    assert fg(500.0, 15.0, 32.0, 0.0, 8.4) > fg(500.0, 15.0, 32.0, 0.0, 7.6)


def test_depth_reduces_absorption():
    """The pressure terms lower alpha at depth."""
    assert fg(500.0, 10.0, 32.0, 1000.0) < fg(500.0, 10.0, 32.0, 0.0)


def test_low_frequency_seawater_magnitude():
    """The chemical terms have the right magnitude at 10 kHz."""
    # Boric acid and MgSO4 dominate at 10 kHz; Francois-Garrison gives about
    # 1 dB/km in 4 C, 35 psu, pH 8 surface water.
    alpha_db_per_km = fg(10.0, 4.0, 35.0, 0.0, 8.0) * 1000.0
    assert 0.9 < alpha_db_per_km < 1.3


@pytest.mark.parametrize('freq_khz', [0.0, -1.0, float('nan'), float('inf')])
def test_absorption_rejects_bad_frequency(freq_khz):
    """A zero, negative or non-finite frequency raises ValueError."""
    with pytest.raises(ValueError):
        fg(freq_khz, 15.0, 32.0)


@pytest.mark.parametrize('temp_c,salinity,ph,depth_m', [
    (75.0, 0.0, 8.0, 0.0),           # degF typed as degC
    (float('nan'), 0.0, 8.0, 0.0),
    (15.0, 310.0, 8.0, 0.0),         # dropped decimal point
    (15.0, -1.0, 8.0, 0.0),
    (15.0, 31.0, 80.0, 0.0),
    (15.0, 31.0, 8.0, -5.0),
])
def test_check_absorption_inputs_rejects(temp_c, salinity, ph, depth_m):
    """Unit slips and non-finite inputs are rejected."""
    with pytest.raises(ValueError):
        dar.check_absorption_inputs(temp_c, salinity, ph, depth_m)


# --------------------------------------------------------------------------
# Argument validation
# --------------------------------------------------------------------------

BASE = ['bag_a', '-t', '/sonar/detections', '-o', 'out.csv']


def parse_error(argv, capsys):
    """Run parse_args expecting an argparse error; return its stderr."""
    with pytest.raises(SystemExit) as exc:
        dar.parse_args(BASE + argv)
    assert exc.value.code == 2
    return capsys.readouterr().err


def test_defaults_fresh_water():
    """Without --salinity/--ph the computed alpha is fresh water at pH 8."""
    args = dar.parse_args(BASE + ['--remove-tl', '--water-temp-c', '24'])
    assert (args.salinity, args.ph, args.depth_m) == (0.0, 8.0, 0.0)
    assert args.absorption_db_per_m is None
    assert args.box is None


def test_seawater_accepted():
    """--salinity > 0 is no longer refused."""
    args = dar.parse_args(BASE + ['--remove-tl', '--water-temp-c', '15.5',
                                  '--salinity', '31', '--ph', '8.1'])
    assert (args.salinity, args.ph) == (31.0, 8.1)


def test_remove_tl_needs_temperature(capsys):
    """The computed alpha needs --water-temp-c."""
    assert '--water-temp-c is required' in parse_error(['--remove-tl'], capsys)


def test_override_requires_remove_tl(capsys):
    """--absorption-db-per-m without --remove-tl is an error."""
    err = parse_error(['--absorption-db-per-m', '0.13'], capsys)
    assert 'requires --remove-tl' in err


@pytest.mark.parametrize('extra', [['--salinity', '31'], ['--ph', '8'],
                                   ['--depth-m', '10'], ['--salinity', '0']])
def test_override_excludes_computed_inputs(extra, capsys):
    """--absorption-db-per-m excludes every computed-alpha input."""
    err = parse_error(['--remove-tl', '--absorption-db-per-m', '0.13'] + extra, capsys)
    assert 'mutually exclusive' in err
    assert extra[0] in err


def test_override_without_temperature():
    """With the override, --water-temp-c is optional."""
    args = dar.parse_args(BASE + ['--remove-tl', '--absorption-db-per-m', '0.130'])
    assert args.absorption_db_per_m == 0.13
    assert args.water_temp_c is None


def test_override_with_temperature_recorded():
    """With the override, a given --water-temp-c is kept."""
    args = dar.parse_args(BASE + ['--remove-tl', '--absorption-db-per-m', '0.13',
                                  '--water-temp-c', '15.5'])
    assert args.water_temp_c == 15.5


@pytest.mark.parametrize('value', ['130', '0', '-0.1', 'nan', 'inf'])
def test_override_value_checked(value, capsys):
    """An override must be finite, > 0 and not look like dB/km."""
    err = parse_error(['--remove-tl', '--absorption-db-per-m', value], capsys)
    assert '--absorption-db-per-m' in err


def test_override_temperature_checked(capsys):
    """A temperature given with the override is still range-checked."""
    err = parse_error(['--remove-tl', '--absorption-db-per-m', '0.13',
                       '--water-temp-c', '75'], capsys)
    assert '--water-temp-c' in err


def test_salinity_out_of_range(capsys):
    """A salinity typo is rejected."""
    err = parse_error(['--remove-tl', '--water-temp-c', '15', '--salinity', '310'], capsys)
    assert '--salinity' in err


def test_salinity_without_remove_tl_warns(capsys):
    """--salinity without --remove-tl warns that it is ignored."""
    args = dar.parse_args(BASE + ['--salinity', '31'])
    assert not args.remove_tl
    assert 'ignored without --remove-tl' in capsys.readouterr().err


def test_box_parsed():
    """--box takes four floats."""
    args = dar.parse_args(BASE + ['--box', '42.95', '-70.65', '43.0', '-70.58'])
    assert args.box == [42.95, -70.65, 43.0, -70.58]


@pytest.mark.parametrize('box', [
    ['43.0', '-70.65', '42.95', '-70.58'],   # LATMIN > LATMAX (corners swapped)
    ['42.95', '-70.58', '43.0', '-70.65'],   # LONMIN > LONMAX
    ['42.95', '179.0', '43.0', '-179.0'],    # across the antimeridian
    ['-91', '0', '0', '1'],
    ['0', '0', '1', '181'],
    ['0', '0', 'nan', '1'],
])
def test_box_rejected(box, capsys):
    """Swapped corners, antimeridian boxes and bad values are rejected."""
    assert '--box' in parse_error(['--box'] + box, capsys)


# --------------------------------------------------------------------------
# Header provenance and CSV writing
# --------------------------------------------------------------------------

# Synthetic bins: index -> [sum_adjusted, sum_R, count] as accumulate builds.
BINS = {
    0: [-40.2 * 100 + 40 * 100 * 1.0, 1000.0, 100],
    1: [-41.0 * 80 + 3000.0, 1200.0, 80],
    2: [-42.5 * 50 + 2000.0, 900.0, 50],
    7: [-48.25 * 10 + 500.0, 400.0, 10],
}

# Data rows the pre-#170 tool wrote for BINS (fresh water, 500 kHz, 24 C, and
# tier-1). Captured from the unmodified script; they must not change.
PRE170_TIER2_FRESH_ROWS = [
    'abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir',
    '1.0,0.779,100,+0.000',
    '3.0,-2.031,80,-2.810',
    '5.0,-0.737,50,-1.516',
    '15.0,5.668,10,+4.888',
]
PRE170_TIER1_ROWS = [
    'abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir',
    '1.0,-0.200,100,+0.000',
    '3.0,-3.500,80,-3.300',
    '5.0,-2.500,50,-2.300',
    '15.0,1.750,10,+1.950',
]

# The C++ loader's rule (matchHeaderComment): '#', key, first ':', value.
HEADER_LINE = re.compile(r'^# ([a-z_0-9]+): (\S.*)$')


def header_dict(lines):
    """Parse header lines (after the title) as the C++ loader would."""
    out = {}
    for line in lines[1:]:   # line 0 is the title comment
        m = HEADER_LINE.match(line)
        assert m, 'not a "# key: value" line: %r' % line
        assert m.group(1) not in out, 'duplicate key %s' % m.group(1)
        out[m.group(1)] = m.group(2)
    return out


def data_rows(path):
    """Return the non-comment lines of a curve CSV."""
    return [ln for ln in path.read_text().splitlines() if not ln.startswith('#')]


def computed_header(**kw):
    """Build a computed fresh-water tier-2 header, with overrides."""
    args = {'alpha': fg(500.0, 24.0), 'absorption_source': 'computed',
            'water_temp_c': 24.0, 'salinity_psu': 0.0, 'ph': 8.0, 'depth_m': 0.0,
            'frequency_khz': 500.0}
    args.update(kw)
    return dar.build_header(True, ['bag_a', 'bag_b'], **args)


def test_header_computed():
    """A computed alpha records all of its inputs and the source bags."""
    lines = computed_header()
    assert lines[0].startswith('# Empirical angular response')
    assert header_dict(lines) == {
        'tl_removed': 'true',
        'absorption_db_per_m': '0.0489736',
        'absorption_source': 'computed',
        'water_temp_c': '24',
        'salinity_psu': '0',
        'ph': '8',
        'depth_m': '0',
        'frequency_khz': '500',
        'tl_model': '40*log10(R) + 2*alpha*R   (R = twtt*c/2, metres)',
        'source_bags': 'bag_a,bag_b',
    }


def test_header_override_omits_unknowns():
    """An override records no salinity, pH, depth or frequency."""
    lines = dar.build_header(True, ['shoals_1'], alpha=0.13,
                             absorption_source='override')
    hd = header_dict(lines)
    assert hd['absorption_db_per_m'] == '0.13'
    assert hd['absorption_source'] == 'override'
    for key in ('water_temp_c', 'salinity_psu', 'ph', 'depth_m', 'frequency_khz'):
        assert key not in hd
    assert hd['source_bags'] == 'shoals_1'


def test_header_override_with_temperature():
    """A temperature given with the override is recorded unrounded."""
    lines = dar.build_header(True, ['b'], alpha=0.13, absorption_source='override',
                             water_temp_c=19.85)
    assert header_dict(lines)['water_temp_c'] == '19.85'


def test_header_tier1_and_box():
    """Tier-1 headers carry the box and source bags too."""
    lines = dar.build_header(False, ['b1', 'b2'], box=[42.95, -70.65, 43.0, -70.58])
    assert header_dict(lines) == {
        'tl_removed': 'false',
        'box': '42.95 -70.65 43 -70.58',
        'source_bags': 'b1,b2',
    }


def test_bag_basenames():
    """Basenames ignore a trailing slash and keep file extensions."""
    assert dar.bag_basenames(['/data/logs/run_1/', 'rel/run_2', 'x/y_0.mcap']) == \
        ['run_1', 'run_2', 'y_0.mcap']


@pytest.mark.parametrize('bad', ['a,b', 'a\nb', 'a\rb', '/'])
def test_bag_basenames_refuses_unrecordable(bad):
    """A name that would break the one-line list is refused."""
    with pytest.raises(SystemExit):
        dar.bag_basenames([bad])


def test_fresh_tier2_rows_byte_identical_to_pre170(tmp_path):
    """Fresh-water tier-2 data rows match the pre-#170 tool byte for byte."""
    out = tmp_path / 'c.csv'
    dar.write_csv(BINS, 2.0, str(out), True, fg(500.0, 24.0, 0.0, 0.0, 8.0),
                  computed_header())
    assert data_rows(out) == PRE170_TIER2_FRESH_ROWS


def test_tier1_rows_byte_identical_to_pre170(tmp_path):
    """Tier-1 data rows match the pre-#170 tool byte for byte."""
    out = tmp_path / 'c.csv'
    dar.write_csv(BINS, 2.0, str(out), False, 0.0, dar.build_header(False, ['b']))
    assert data_rows(out) == PRE170_TIER1_ROWS


def golden_content(tmp_path):
    """Return the bytes write_csv produces for the golden curve."""
    alpha = fg(500.0, 15.5, 31.0, 0.0, 8.0)
    header = dar.build_header(
        True, ['shoals_2026-08-26_a', 'shoals_2026-08-26_b'], alpha=alpha,
        absorption_source='computed', water_temp_c=15.5, salinity_psu=31.0,
        ph=8.0, depth_m=0.0, frequency_khz=500.0,
        box=[42.95, -70.65, 43.0, -70.58])
    out = tmp_path / 'golden.csv'
    dar.write_csv(BINS, 2.0, str(out), True, alpha, header)
    return out.read_bytes()


def test_golden_seawater_curve_for_cpp_round_trip(tmp_path):
    """The golden curve the C++ loader test reads is current."""
    content = golden_content(tmp_path)
    if os.environ.get('GOLDEN_REGENERATE') == '1':
        GOLDEN.write_bytes(content)
    assert GOLDEN.read_bytes() == content, (
        '%s no longer matches write_csv output; if the change is intended, '
        're-run with GOLDEN_REGENERATE=1 and check test_angular_response_curve '
        'still passes' % GOLDEN)
    text = content.decode()
    assert '# absorption_db_per_m: 0.125363\n' in text
    assert '# absorption_source: computed\n' in text


# --------------------------------------------------------------------------
# ECEF -> geodetic, box predicate, TF topic matching
# --------------------------------------------------------------------------

def geodetic_to_ecef(lat_deg, lon_deg, h):
    """Textbook forward WGS84 conversion, used to round-trip the inverse."""
    lat = math.radians(lat_deg)
    lon = math.radians(lon_deg)
    n = dar.WGS84_A / math.sqrt(1.0 - dar.WGS84_E2 * math.sin(lat) ** 2)
    return ((n + h) * math.cos(lat) * math.cos(lon),
            (n + h) * math.cos(lat) * math.sin(lon),
            (n * (1.0 - dar.WGS84_E2) + h) * math.sin(lat))


def test_ecef_known_value():
    """ECEF of 43.0 N, 70.6 W, h 0 converts back to that point."""
    # ECEF of lat 43.0, lon -70.6, h 0 (WGS84), from PROJ (EPSG:4979 -> 4978).
    lat, lon, h = dar.ecef_to_geodetic(1551841.299521614, -4406692.218763631,
                                       4327501.759196973)
    assert lat == pytest.approx(43.0, abs=1e-9)
    assert lon == pytest.approx(-70.6, abs=1e-9)
    assert h == pytest.approx(0.0, abs=1e-4)


@pytest.mark.parametrize('lat,lon,h', [
    (43.0, -70.6, 0.0), (42.97, -70.61, -25.0), (-33.9, 151.2, 100.0),
    (0.0, 0.0, 0.0), (89.999, 10.0, 0.0), (-60.0, -120.0, 5000.0)])
def test_ecef_round_trip(lat, lon, h):
    """Forward then inverse returns the input."""
    got = dar.ecef_to_geodetic(*geodetic_to_ecef(lat, lon, h))
    assert got[0] == pytest.approx(lat, abs=1e-9)
    assert got[1] == pytest.approx(lon, abs=1e-9)
    assert got[2] == pytest.approx(h, abs=1e-4)


def test_ecef_pole():
    """The polar axis is handled without dividing by zero."""
    lat, _lon, h = dar.ecef_to_geodetic(0.0, 0.0, -dar.WGS84_B)
    assert lat == -90.0
    assert h == pytest.approx(0.0, abs=1e-6)


def test_in_box_edges_inclusive():
    """Points on the box edge are inside."""
    box = [42.95, -70.65, 43.0, -70.58]
    assert dar.in_box(42.97, -70.6, box)
    assert dar.in_box(43.0, -70.58, box)
    assert not dar.in_box(43.01, -70.6, box)
    assert not dar.in_box(42.97, -70.5, box)


def test_is_tf_topic():
    """Namespaced /tf and /tf_static topics are recognised."""
    assert dar.is_tf_topic('/tf') == 'tf'
    assert dar.is_tf_topic('/bizzy/tf') == 'tf'
    assert dar.is_tf_topic('/tf_static') == 'tf_static'
    assert dar.is_tf_topic('/bizzy/tf_static') == 'tf_static'
    assert dar.is_tf_topic('/bizzy/tfx') is None
    assert dar.is_tf_topic('/sonar/detections') is None


# --------------------------------------------------------------------------
# End to end over a synthetic bag (rosbag2 + tf2, as the script runs)
# --------------------------------------------------------------------------

TOPIC = '/bizzy/sonar/detections'
INSIDE = (42.97, -70.61)     # inside the test box
OUTSIDE = (43.10, -70.61)    # north of it
TEST_BOX = ['42.95', '-70.65', '43.0', '-70.58']


def _stamp(t_sec):
    """Return a builtin_interfaces/Time for t_sec."""
    from builtin_interfaces.msg import Time
    sec = int(math.floor(t_sec))
    return Time(sec=sec, nanosec=int(round((t_sec - sec) * 1e9)))


def _transform(parent, child, t_sec, xyz):
    """Return a TransformStamped parent -> child at t_sec."""
    from geometry_msgs.msg import TransformStamped
    t = TransformStamped()
    t.header.frame_id = parent
    t.header.stamp = _stamp(t_sec)
    t.child_frame_id = child
    t.transform.translation.x, t.transform.translation.y, t.transform.translation.z = xyz
    t.transform.rotation.w = 1.0
    return t


def _ping(t_sec, frame_id, intensity):
    """Return a two-beam SonarDetections ping."""
    from marine_acoustic_msgs.msg import SonarDetections
    msg = SonarDetections()
    msg.header.stamp = _stamp(t_sec)
    msg.header.frame_id = frame_id
    msg.ping_info.frequency = 500000.0
    msg.ping_info.sound_speed = 1500.0
    msg.rx_angles = [0.0, math.radians(31.0)]       # bins 0 and 15 at 2 deg
    msg.intensities = [intensity, intensity - 5.0]
    msg.two_way_travel_times = [0.02, 0.03]         # 15 m and 22.5 m
    return msg


def write_synthetic_bag(path):
    """
    Bag with earth TF and four pings.

      ping 1, t=100.5: inside the box (latest-transform fallback, 0.5 s old)
      ping 2, t=102.5: outside (logged after the 103 s TF -> exact lookup)
      ping 3, t=200.0: latest earth TF is 97 s old -> stale_tf
      ping 4, t=101.0: frame with no earth transform -> no_tf
    """
    import rosbag2_py
    from rclpy.serialization import serialize_message
    from tf2_msgs.msg import TFMessage

    writer = rosbag2_py.SequentialWriter()
    writer.open(rosbag2_py.StorageOptions(uri=str(path), storage_id='mcap'),
                rosbag2_py.ConverterOptions('cdr', 'cdr'))
    for i, (name, type_name) in enumerate([
            ('/bizzy/tf', 'tf2_msgs/msg/TFMessage'),
            ('/bizzy/tf_static', 'tf2_msgs/msg/TFMessage'),
            (TOPIC, 'marine_acoustic_msgs/msg/SonarDetections')]):
        writer.create_topic(rosbag2_py.TopicMetadata(i, name, type_name, 'cdr'))

    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    outside = geodetic_to_ecef(OUTSIDE[0], OUTSIDE[1], 0.0)

    def earth(t_sec, xyz):
        return TFMessage(transforms=[_transform('earth', 'base_link', t_sec, xyz)])

    events = [
        (99.0, '/bizzy/tf_static',
         TFMessage(transforms=[_transform('base_link', 'mbes', 0.0, (0.0, 0.0, -0.5))])),
        (100.0, '/bizzy/tf', earth(100.0, inside)),
        (100.5, TOPIC, _ping(100.5, 'mbes', -30.0)),
        (101.0, '/bizzy/tf', earth(101.0, inside)),
        (101.0, TOPIC, _ping(101.0, 'orphan_frame', -31.0)),
        (102.0, '/bizzy/tf', earth(102.0, outside)),
        (103.0, '/bizzy/tf', earth(103.0, outside)),
        (103.2, TOPIC, _ping(102.5, 'mbes', -32.0)),
        (200.0, TOPIC, _ping(200.0, 'mbes', -33.0)),
    ]
    for t_sec, topic, msg in events:
        writer.write(topic, serialize_message(msg), int(round(t_sec * 1e9)))
    del writer


def run_main(tmp_path, extra, capsys):
    """Run main() over the synthetic bag; return the CSV path and stdout."""
    bag = tmp_path / 'site_bag'
    if not bag.exists():
        write_synthetic_bag(bag)
    out = tmp_path / ('curve_%d.csv' % len(list(tmp_path.glob('curve_*.csv'))))
    assert dar.main([str(bag), '-t', TOPIC, '-o', str(out)] + extra) == 0
    return out, capsys.readouterr().out


def counts_by_angle(path):
    """Map each bin centre to its sample count."""
    rows = data_rows(path)[1:]
    return {float(r.split(',')[0]): int(r.split(',')[2]) for r in rows}


def test_end_to_end_without_box_reads_every_ping(tmp_path, capsys):
    """Without --box every ping is binned and no box is recorded."""
    out, stdout = run_main(tmp_path, [], capsys)
    assert counts_by_angle(out) == {1.0: 4, 31.0: 4}
    hd = header_dict([ln for ln in out.read_text().splitlines() if ln.startswith('#')])
    assert hd['source_bags'] == 'site_bag'
    assert 'box' not in hd
    assert 'box ' not in stdout


def test_end_to_end_box_keeps_only_site_pings(tmp_path, capsys):
    """--box keeps only the inside ping and reports each drop reason."""
    out, stdout = run_main(
        tmp_path, ['--box'] + TEST_BOX + ['--remove-tl', '--water-temp-c', '15.5',
                                          '--salinity', '31'], capsys)
    # Only ping 1 survives: one sample per bin.
    assert counts_by_angle(out) == {1.0: 1, 31.0: 1}
    assert ('kept 1 of 4 pings; dropped 1 outside the box, 1 with no earth '
            'transform, 1 with an earth transform more than 1 s from the ping') in stdout
    lines = out.read_text().splitlines()
    hd = header_dict([ln for ln in lines if ln.startswith('#')])
    assert hd['box'] == '42.95 -70.65 43 -70.58'
    assert hd['absorption_source'] == 'computed'
    assert hd['salinity_psu'] == '31'
    assert hd['frequency_khz'] == '500'
    assert float(hd['absorption_db_per_m']) == pytest.approx(fg(500.0, 15.5, 31.0), rel=1e-5)
    # Ping 1 nadir beam: -30 dB + 40 log10(15) + 2 alpha 15.
    alpha = float(hd['absorption_db_per_m'])
    nadir = [ln for ln in lines if ln.startswith('1.0,')][0]
    expected = -30.0 + 40.0 * math.log10(15.0) + 2.0 * alpha * 15.0
    assert float(nadir.split(',')[1]) == pytest.approx(expected, abs=2e-3)


def test_end_to_end_box_with_override(tmp_path, capsys):
    """--box combines with the alpha override."""
    extra = ['--box'] + TEST_BOX + ['--remove-tl', '--absorption-db-per-m', '0.130']
    out, _ = run_main(tmp_path, extra, capsys)
    hd = header_dict([ln for ln in out.read_text().splitlines() if ln.startswith('#')])
    assert hd['absorption_db_per_m'] == '0.13'
    assert hd['absorption_source'] == 'override'
    assert 'salinity_psu' not in hd and 'water_temp_c' not in hd


def test_end_to_end_box_with_no_site_pings_fails(tmp_path, capsys):
    """A box with no pings in it fails without writing a curve."""
    bag = tmp_path / 'site_bag'
    write_synthetic_bag(bag)
    with pytest.raises(SystemExit) as exc:
        dar.main([str(bag), '-t', TOPIC, '-o', str(tmp_path / 'x.csv'),
                  '--box', '10', '10', '11', '11'])
    assert 'no pings inside --box' in str(exc.value)
    assert not (tmp_path / 'x.csv').exists()
