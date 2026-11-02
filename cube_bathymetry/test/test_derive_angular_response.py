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

import errno
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


@pytest.mark.parametrize('extra', [
    ['--water-temp-c', '15'], ['--depth-m', '20'], ['--ph', '8.1']])
def test_absorption_inputs_without_remove_tl_warn(extra, capsys):
    """Every absorption input given without --remove-tl warns, none silently."""
    dar.parse_args(BASE + extra)
    err = capsys.readouterr().err
    assert extra[0] in err and 'ignored without --remove-tl' in err


@pytest.mark.parametrize('freq_hz', [500.0, 0.9e3, 2.1e6])
def test_computed_alpha_refuses_implausible_frequency(freq_hz):
    """A median frequency outside 1 kHz..2 MHz (kHz stored as Hz) is refused."""
    args = dar.parse_args(BASE + ['--remove-tl', '--water-temp-c', '15'])
    with pytest.raises(SystemExit) as exc:
        dar.resolve_absorption(args, [freq_hz])
    assert 'ping_info.frequency' in str(exc.value)


@pytest.mark.parametrize('freq_hz', [1e3, 12e3, 500e3, 2e6])
def test_computed_alpha_accepts_frequency_range(freq_hz):
    """The range edges and real echosounder frequencies compute."""
    args = dar.parse_args(BASE + ['--remove-tl', '--water-temp-c', '15'])
    alpha, source, freq_khz = dar.resolve_absorption(args, [freq_hz])
    assert source == 'computed' and freq_khz == freq_hz / 1000.0 and alpha > 0.0


def test_override_ignores_implausible_frequency():
    """With --absorption-db-per-m the frequency is not used, so not checked."""
    args = dar.parse_args(BASE + ['--remove-tl', '--absorption-db-per-m', '0.13'])
    assert dar.resolve_absorption(args, [500.0])[:2] == (0.13, 'override')


@pytest.mark.parametrize('width', ['0', '-2', 'nan', 'inf', '90.5'])
def test_bin_width_rejected(width, capsys):
    """A zero, negative, non-finite or wider-than-90 degree bin is refused."""
    assert '--bin-width-deg' in parse_error(['--bin-width-deg', width], capsys)


@pytest.mark.parametrize('width', ['0.1', '2', '90'])
def test_bin_width_accepted(width):
    """Positive widths up to 90 degrees are accepted."""
    assert dar.parse_args(BASE + ['--bin-width-deg', width]).bin_width_deg == float(width)


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


def test_check_distinct_bags(tmp_path):
    """The same bag twice, however spelled, is refused; distinct bags pass."""
    (tmp_path / 'run_1').mkdir()
    (tmp_path / 'run_1' / 'run_1_0.mcap').write_bytes(b'')
    (tmp_path / 'run_2').mkdir()
    (tmp_path / 'link').symlink_to(tmp_path / 'run_1')
    a = str(tmp_path / 'run_1')
    dar.check_distinct_bags([a, str(tmp_path / 'run_2')])
    for twice in ([a, a + '/'],
                  [a, str(tmp_path / 'run_2' / '..' / 'run_1')],
                  [a, str(tmp_path / 'link')],
                  [a, str(tmp_path / 'run_1' / 'run_1_0.mcap')]):
        with pytest.raises(SystemExit) as exc:
            dar.check_distinct_bags(twice)
        assert 'give each bag once' in str(exc.value)


def test_main_refuses_the_same_bag_twice(tmp_path):
    """main() refuses a repeated bag before reading anything."""
    bag = tmp_path / 'site_bag'
    write_synthetic_bag(bag)
    with pytest.raises(SystemExit) as exc:
        dar.main([str(bag), str(bag) + '/', '-t', TOPIC, '-o', str(tmp_path / 'x.csv')])
    assert 'give each bag once' in str(exc.value)
    assert not (tmp_path / 'x.csv').exists()


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


def test_write_csv_failure_keeps_the_existing_curve(tmp_path):
    """A failure while writing leaves the old curve intact and no temp file."""
    out = tmp_path / 'curve.csv'
    out.write_text('a good curve\n')
    with pytest.raises(TypeError):
        dar.write_csv(BINS, 2.0, str(out), False, 0.0, ['# ok', None])
    assert out.read_text() == 'a good curve\n'
    assert [q.name for q in tmp_path.iterdir()] == ['curve.csv']


def test_atomic_write_cleans_up_when_the_replace_fails(tmp_path, monkeypatch):
    """A failure after the temp file is written removes it; the old file stays."""
    out = tmp_path / 'curve.csv'
    out.write_text('a good curve\n')

    def fail(_src, _dst):
        raise OSError('disk full')

    monkeypatch.setattr(dar.os, 'replace', fail)
    with pytest.raises(SystemExit, match='cannot write .*curve.csv .disk full.; it is unchanged'):
        dar.write_csv(BINS, 2.0, str(out), False, 0.0, ['# new'])
    assert out.read_text() == 'a good curve\n'
    assert [q.name for q in tmp_path.iterdir()] == ['curve.csv']


def test_write_csv_replaces_and_keeps_permissions(tmp_path):
    """A successful write replaces the file and keeps its permission bits."""
    out = tmp_path / 'curve.csv'
    out.write_text('old\n')
    out.chmod(0o640)
    dar.write_csv(BINS, 2.0, str(out), False, 0.0, ['# new'])
    assert out.read_text().startswith('# new\nabs_angle_deg_center,')
    assert (out.stat().st_mode & 0o777) == 0o640
    assert [q.name for q in tmp_path.iterdir()] == ['curve.csv']


def test_write_csv_writes_through_a_symlink(tmp_path):
    """A symlinked output keeps the link and replaces its target."""
    real_dir = tmp_path / 'curves'
    real_dir.mkdir()
    target = real_dir / 'site.csv'
    target.write_text('old\n')
    target.chmod(0o640)
    link = tmp_path / 'current.csv'
    link.symlink_to(target)
    dar.write_csv(BINS, 2.0, str(link), False, 0.0, ['# new'])
    assert link.is_symlink() and os.readlink(link) == str(target)
    assert target.read_text().startswith('# new\n')
    assert (target.stat().st_mode & 0o777) == 0o640
    assert sorted(q.name for q in real_dir.iterdir()) == ['site.csv']
    assert sorted(q.name for q in tmp_path.iterdir()) == ['current.csv', 'curves']


def test_write_csv_through_a_dangling_symlink_creates_the_target(tmp_path):
    """A link to a file that does not exist yet creates that file, as open('w')."""
    link = tmp_path / 'current.csv'
    link.symlink_to(tmp_path / 'new.csv')
    dar.write_csv(BINS, 2.0, str(link), False, 0.0, ['# new'])
    assert link.is_symlink()
    assert (tmp_path / 'new.csv').read_text().startswith('# new\n')


@pytest.mark.skipif(os.geteuid() == 0, reason='root writes into read-only directories')
@pytest.mark.parametrize('through_link', [False, True])
def test_write_csv_in_a_read_only_directory_writes_in_place(tmp_path, capsys, through_link):
    """
    A writable file in a read-only directory is written in place, with a warning.

    No temp file can be created beside it, so it is written as open('w')
    would, directly or through a symlink from a writable directory.
    """
    ro_dir = tmp_path / 'readonly'
    ro_dir.mkdir()
    target = ro_dir / 'site.csv'
    target.write_text('old\n')
    out = target
    if through_link:
        out = tmp_path / 'current.csv'
        out.symlink_to(target)
    ro_dir.chmod(0o555)
    try:
        dar.write_csv(BINS, 2.0, str(out), False, 0.0, ['# new'])
    finally:
        ro_dir.chmod(0o755)
    assert target.read_text().startswith('# new\nabs_angle_deg_center,')
    assert sorted(q.name for q in ro_dir.iterdir()) == ['site.csv']
    err = capsys.readouterr().err
    assert 'warning: cannot create a temp file in %s' % ro_dir in err
    assert 'writing %s in place' % target in err


@pytest.mark.skipif(os.geteuid() == 0, reason='root writes into read-only directories')
def test_write_csv_in_place_failure_says_the_file_may_be_truncated(tmp_path, monkeypatch):
    """A write that fails part-way through the in-place fallback says the file may be truncated."""
    ro_dir = tmp_path / 'readonly'
    ro_dir.mkdir()
    target = ro_dir / 'site.csv'
    target.write_text('old\n')
    real_open = open

    class FullDisk:
        """open(path, 'w') that truncates the file, then fails to write."""

        def __init__(self, path, mode):
            self.f = real_open(path, mode)

        def __enter__(self):
            return self

        def __exit__(self, *exc):
            self.f.close()

        def write(self, _text):
            raise OSError(errno.ENOSPC, 'No space left on device')

    monkeypatch.setattr(dar, 'open', FullDisk, raising=False)
    ro_dir.chmod(0o555)
    try:
        with pytest.raises(SystemExit) as exc:
            dar.write_csv(BINS, 2.0, str(target), False, 0.0, ['# new'])
    finally:
        ro_dir.chmod(0o755)
    assert str(exc.value) == (
        'error: cannot write %s (No space left on device); it may now be truncated' % target)


@pytest.mark.skipif(os.geteuid() == 0, reason='root writes into read-only directories')
def test_write_csv_in_a_read_only_directory_without_the_file_is_a_clean_error(tmp_path):
    """A new file, or a read-only one, in a read-only directory exits naming the directory."""
    ro_dir = tmp_path / 'readonly'
    ro_dir.mkdir()
    locked = ro_dir / 'locked.csv'
    locked.write_text('old\n')
    locked.chmod(0o444)
    ro_dir.chmod(0o555)
    try:
        for name in ('new.csv', 'locked.csv'):
            with pytest.raises(SystemExit) as exc:
                dar.write_csv(BINS, 2.0, str(ro_dir / name), False, 0.0, ['# new'])
            assert str(exc.value) == (
                'error: cannot write %s: cannot create a file in %s (Permission denied)'
                % (ro_dir / name, ro_dir))
    finally:
        ro_dir.chmod(0o755)
    assert locked.read_text() == 'old\n'
    assert sorted(q.name for q in ro_dir.iterdir()) == ['locked.csv']


def test_write_csv_into_a_missing_directory_is_a_clean_error(tmp_path):
    """-o in a directory that does not exist exits naming it, no traceback."""
    out = tmp_path / 'no_such_dir' / 'curve.csv'
    with pytest.raises(SystemExit, match='cannot create a file in %s' % re.escape(
            str(out.parent))):
        dar.write_csv(BINS, 2.0, str(out), False, 0.0, ['# new'])


def test_write_csv_to_a_non_regular_file_writes_directly(tmp_path):
    """/dev/null and a FIFO are written in place; no temp file is attempted."""
    dar.write_csv(BINS, 2.0, '/dev/null', False, 0.0, ['# new'])
    import threading
    fifo = tmp_path / 'pipe'
    os.mkfifo(fifo)
    got = []
    reader = threading.Thread(target=lambda: got.append(fifo.read_text()), daemon=True)
    reader.start()
    dar.write_csv(BINS, 2.0, str(fifo), False, 0.0, ['# new'])
    reader.join(timeout=10)
    assert got and got[0].startswith('# new\nabs_angle_deg_center,')
    assert sorted(q.name for q in tmp_path.iterdir()) == ['pipe']


def test_output_to_dev_stdout(tmp_path):
    """-o /dev/stdout writes the curve to standard output."""
    import subprocess
    import sys
    bag = tmp_path / 'site_bag'
    write_synthetic_bag(bag)
    proc = subprocess.run(
        [sys.executable, str(SCRIPT), str(bag), '-t', TOPIC, '-o', '/dev/stdout'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
    assert proc.returncode == 0, proc.stderr
    assert '\nabs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n' in proc.stdout
    assert '1.0,' in proc.stdout and 'wrote 2-bin curve to /dev/stdout' in proc.stdout


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
SOUTH = (42.90, -70.61)      # just south of the box
NORTH = (43.04, -70.61)      # just north of it; the SOUTH-NORTH midpoint is inside
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


def _earth_tf(t_sec, xyz):
    """Return a TFMessage holding earth -> base_link at t_sec."""
    from tf2_msgs.msg import TFMessage
    return TFMessage(transforms=[_transform('earth', 'base_link', t_sec, xyz)])


def _mbes_static():
    """Return the /tf_static TFMessage base_link -> mbes."""
    from tf2_msgs.msg import TFMessage
    return TFMessage(transforms=[_transform('base_link', 'mbes', 0.0, (0.0, 0.0, -0.5))])


BAG_TOPICS = (
    ('/bizzy/tf', 'tf2_msgs/msg/TFMessage'),
    ('/bizzy/tf_static', 'tf2_msgs/msg/TFMessage'),
    (TOPIC, 'marine_acoustic_msgs/msg/SonarDetections'),
)


def write_bag(path, events, topics=BAG_TOPICS):
    """Write ``(t_sec, topic, msg)`` events to an mcap bag declaring ``topics``."""
    import rosbag2_py
    from rclpy.serialization import serialize_message

    writer = rosbag2_py.SequentialWriter()
    writer.open(rosbag2_py.StorageOptions(uri=str(path), storage_id='mcap'),
                rosbag2_py.ConverterOptions('cdr', 'cdr'))
    for i, (name, type_name) in enumerate(topics):
        writer.create_topic(rosbag2_py.TopicMetadata(i, name, type_name, 'cdr'))
    for t_sec, topic, msg in events:
        data = msg if isinstance(msg, bytes) else serialize_message(msg)   # bytes: raw
        writer.write(topic, data, int(round(t_sec * 1e9)))
    del writer


def write_synthetic_bag(path):
    """
    Bag with earth TF and six pings, positioned as import_bag would.

      ping 99.5:  before the first earth transform (100 s)  -> extrapolation
      ping 100.5: between two inside transforms              -> kept
      ping 101.0: frame with no earth transform              -> no_tf
      ping 102.5: between two outside transforms (logged after the 103 s one)
                                                             -> outside
      ping 105.0: between a SOUTH (104 s) and a NORTH (106 s) transform, both
                  outside the box; only the interpolated position is inside,
                  so the latest or the nearest transform would drop it
                                                             -> kept
      ping 200.0: after the last earth transform (109 s); still waiting when
                  the bag ends, flushed and looked up         -> extrapolation
      ping 0.0:   unset header stamp, logged at 101.5 s while inside
                  transforms exist (tf2 would read it as "latest")
                                                             -> bad_stamp
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    outside = geodetic_to_ecef(OUTSIDE[0], OUTSIDE[1], 0.0)
    south = geodetic_to_ecef(SOUTH[0], SOUTH[1], 0.0)
    north = geodetic_to_ecef(NORTH[0], NORTH[1], 0.0)
    earth = _earth_tf
    events = [
        (99.0, '/bizzy/tf_static', _mbes_static()),
        (99.5, TOPIC, _ping(99.5, 'mbes', -35.0)),
        (100.0, '/bizzy/tf', earth(100.0, inside)),
        (100.5, TOPIC, _ping(100.5, 'mbes', -30.0)),
        (101.0, '/bizzy/tf', earth(101.0, inside)),
        (101.0, TOPIC, _ping(101.0, 'orphan_frame', -31.0)),
        (101.5, TOPIC, _ping(0.0, 'mbes', -36.0)),
        (102.0, '/bizzy/tf', earth(102.0, outside)),
        (103.0, '/bizzy/tf', earth(103.0, outside)),
        (103.2, TOPIC, _ping(102.5, 'mbes', -32.0)),
        (104.0, '/bizzy/tf', earth(104.0, south)),
        (105.0, TOPIC, _ping(105.0, 'mbes', -34.0)),
        (106.0, '/bizzy/tf', earth(106.0, north)),
        (109.0, '/bizzy/tf', earth(109.0, north)),
        (200.0, TOPIC, _ping(200.0, 'mbes', -33.0)),
    ]
    write_bag(path, events)


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
    assert counts_by_angle(out) == {1.0: 7, 31.0: 7}
    hd = header_dict([ln for ln in out.read_text().splitlines() if ln.startswith('#')])
    assert hd['source_bags'] == 'site_bag'
    assert 'box' not in hd
    assert 'box ' not in stdout


def test_end_to_end_box_keeps_only_site_pings(tmp_path, capsys):
    """--box keeps the pings positioned inside and reports each drop reason."""
    out, stdout = run_main(
        tmp_path, ['--box'] + TEST_BOX + ['--remove-tl', '--water-temp-c', '15.5',
                                          '--salinity', '31'], capsys)
    # Pings 100.5 and 105.0 survive: two samples per bin.
    assert counts_by_angle(out) == {1.0: 2, 31.0: 2}
    assert ('kept 2 of 7 pings; dropped 1 outside the box, 1 with no earth '
            'transform for their frame, 2 outside the earth TF coverage at '
            'their stamp, 1 with an unset (zero) header stamp or one more than '
            '3600 s ahead of the TF stream, 0 with an earth transform that is '
            'not a geocentric position; at most 3 waited for TF') in stdout
    lines = out.read_text().splitlines()
    hd = header_dict([ln for ln in lines if ln.startswith('#')])
    assert hd['box'] == '42.95 -70.65 43 -70.58'
    assert hd['absorption_source'] == 'computed'
    assert hd['salinity_psu'] == '31'
    assert hd['frequency_khz'] == '500'
    assert float(hd['absorption_db_per_m']) == pytest.approx(fg(500.0, 15.5, 31.0), rel=1e-5)
    # Nadir beams of the two kept pings (-30 and -34 dB, both at 15 m):
    # their mean + 40 log10(15) + 2 alpha 15.
    alpha = float(hd['absorption_db_per_m'])
    nadir = [ln for ln in lines if ln.startswith('1.0,')][0]
    expected = -32.0 + 40.0 * math.log10(15.0) + 2.0 * alpha * 15.0
    assert float(nadir.split(',')[1]) == pytest.approx(expected, abs=2e-3)


def test_end_to_end_box_warns_about_a_thin_curve(tmp_path, capsys):
    """A box keeping only a few pings warns, and still writes the curve."""
    bag = tmp_path / 'site_bag'
    write_synthetic_bag(bag)
    out = tmp_path / 'thin.csv'
    assert dar.main([str(bag), '-t', TOPIC, '-o', str(out), '--box'] + TEST_BOX) == 0
    err = capsys.readouterr().err
    assert 'thin curve' in err and '--box kept 2 pings' in err
    assert counts_by_angle(out) == {1.0: 2, 31.0: 2}


def _bins(counts):
    """Synthetic bins {index: [sum_adjusted, sum_R, n]} from {index: n}."""
    return {idx: [-30.0 * n, 0.0, n] for idx, n in counts.items()}


def test_thin_curve_warning():
    """Warn on a thin nadir bin or too few populated bins, not on a good curve."""
    n = dar.MIN_SAMPLES_PER_BIN
    assert dar.thin_curve_warning(_bins({0: n, 1: n, 2: n, 30: 1}), 2.0) is None
    thin_nadir = dar.thin_curve_warning(_bins({0: n - 1, 1: n, 2: n, 3: n}), 2.0)
    assert 'nadir reference bin (1.0 deg) has %d samples' % (n - 1) in thin_nadir
    narrow = dar.thin_curve_warning(_bins({0: n, 1: n, 2: n - 1}), 2.0)
    assert '2 of 3 bins have at least %d' % n in narrow
    assert dar.thin_curve_warning({}, 2.0) is None


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


def write_split_bags(tmp_path):
    """
    One recording split in two files; /tf_static is only in the first.

    Every ping is inside the box and bracketed by earth transforms, and the last
    ping of split 0 is bracketed by a transform recorded in split 1.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    split0 = tmp_path / 'survey_0'
    split1 = tmp_path / 'survey_1'
    write_bag(split0, [
        (99.0, '/bizzy/tf_static', _mbes_static()),
        (100.0, '/bizzy/tf', _earth_tf(100.0, inside)),
        (100.5, TOPIC, _ping(100.5, 'mbes', -30.0)),
        (101.0, '/bizzy/tf', _earth_tf(101.0, inside)),
        (101.5, TOPIC, _ping(101.5, 'mbes', -30.0)),
    ])
    write_bag(split1, [
        (102.0, '/bizzy/tf', _earth_tf(102.0, inside)),
        (102.5, TOPIC, _ping(102.5, 'mbes', -30.0)),
        (103.0, '/bizzy/tf', _earth_tf(103.0, inside)),
        (103.5, TOPIC, _ping(103.5, 'mbes', -30.0)),
        (104.0, '/bizzy/tf', _earth_tf(104.0, inside)),
        (107.0, '/bizzy/tf', _earth_tf(107.0, inside)),
    ])
    return split0, split1


@pytest.mark.parametrize('reverse', [False, True])
def test_end_to_end_box_over_split_bags_shares_one_tf_buffer(tmp_path, capsys, reverse):
    """
    Split files share one TF buffer and are read as one chronological stream.

    The /tf_static in split 0 positions split 1's pings, and the order the
    files are given on the command line does not matter.
    """
    bags = [str(b) for b in write_split_bags(tmp_path)]
    if reverse:
        bags.reverse()
    out = tmp_path / 'curve.csv'
    assert dar.main(bags + ['-t', TOPIC, '-o', str(out), '--box'] + TEST_BOX) == 0
    assert 'kept 4 of 4 pings' in capsys.readouterr().out
    assert counts_by_angle(out) == {1.0: 4, 31.0: 4}
    hd = header_dict([ln for ln in out.read_text().splitlines() if ln.startswith('#')])
    assert hd['source_bags'] == ','.join(os.path.basename(b) for b in bags)


def write_tf_and_detection_bags(tmp_path):
    """
    Write a TF-only bag and a detections-only bag recorded side by side.

    The TF bag declares only /tf and /tf_static; the detections bag declares
    only the detections topic. Every ping is inside the box.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    tf_bag = tmp_path / 'nav_tf'
    det_bag = tmp_path / 'mbes_only'
    write_bag(tf_bag, [
        (99.0, '/bizzy/tf_static', _mbes_static()),
        (100.0, '/bizzy/tf', _earth_tf(100.0, inside)),
        (101.0, '/bizzy/tf', _earth_tf(101.0, inside)),
        (102.0, '/bizzy/tf', _earth_tf(102.0, inside)),
        (105.0, '/bizzy/tf', _earth_tf(105.0, inside)),
    ], topics=BAG_TOPICS[:2])
    write_bag(det_bag, [
        (100.5, TOPIC, _ping(100.5, 'mbes', -30.0)),
        (101.5, TOPIC, _ping(101.5, 'mbes', -30.0)),
    ], topics=BAG_TOPICS[2:])
    return tf_bag, det_bag


def test_end_to_end_box_tf_only_bag_positions_a_detections_bag(tmp_path, capsys):
    """With --box a TF-only bag beside a detections bag positions its pings."""
    tf_bag, det_bag = write_tf_and_detection_bags(tmp_path)
    out = tmp_path / 'curve.csv'
    assert dar.main([str(tf_bag), str(det_bag), '-t', TOPIC, '-o', str(out),
                     '--box'] + TEST_BOX) == 0
    assert 'kept 2 of 2 pings' in capsys.readouterr().out
    assert counts_by_angle(out) == {1.0: 2, 31.0: 2}


def write_clock_offset_bag(path, offset_sec):
    """
    Write a 150 s bag whose header stamps run ``offset_sec`` ahead of its bag times.

    earth -> base_link at 1 Hz, inside the box, and a ping every 0.5 s; every
    message is recorded at its stamp minus ``offset_sec``, as by a recorder
    whose clock lags the publishers'. Longer than the 60 s buffer, so a ping
    held back by the offset would fall out of it.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    events = [(0.0, '/bizzy/tf_static', _mbes_static())]
    for k in range(150):
        stamp = 100.0 + k
        events.append((stamp - offset_sec, '/bizzy/tf', _earth_tf(stamp, inside)))
        for half in (0.25, 0.75):
            events.append((stamp + half - offset_sec, TOPIC,
                           _ping(stamp + half, 'mbes', -30.0)))
    write_bag(path, sorted(events, key=lambda e: e[0]))


@pytest.mark.parametrize('offset_sec', [90.0, 57.0])
def test_end_to_end_box_with_a_recorder_clock_offset(tmp_path, capsys, offset_sec):
    """A constant clock offset between publisher and recorder keeps the same pings."""
    outputs = {}
    for offset in (0.0, offset_sec):
        bag = tmp_path / ('offset_%g' % offset) / 'site_bag'
        bag.parent.mkdir()
        write_clock_offset_bag(bag, offset)
        out = tmp_path / ('curve_%g.csv' % offset)
        assert dar.main([str(bag), '-t', TOPIC, '-o', str(out), '--box'] + TEST_BOX) == 0
        captured = capsys.readouterr()
        assert 'ignored' not in captured.err
        outputs[offset] = (out.read_bytes(),
                           re.search(r'kept \d+ of \d+ pings[^\n]*', captured.out).group(0))
    assert outputs[offset_sec] == outputs[0.0]
    # The two pings after the last transform (249 s) are the only ones lost.
    assert outputs[0.0][1].startswith('kept 298 of 300 pings; dropped 0 outside')


def test_without_box_a_bag_without_the_topic_is_an_error(tmp_path):
    """Without --box every bag must carry the detections topic."""
    tf_bag, det_bag = write_tf_and_detection_bags(tmp_path)
    with pytest.raises(SystemExit) as exc:
        dar.main([str(tf_bag), str(det_bag), '-t', TOPIC,
                  '-o', str(tmp_path / 'x.csv')])
    assert "topic '%s' not in bag" % TOPIC in str(exc.value)


def test_box_without_dynamic_tf_fails_before_reading(tmp_path):
    """With --box and no /tf topic in any bag the run stops up front."""
    _tf_bag, det_bag = write_tf_and_detection_bags(tmp_path)
    static_only = tmp_path / 'static_only'
    write_bag(static_only, [(99.0, '/bizzy/tf_static', _mbes_static())],
              topics=BAG_TOPICS[1:2])
    for bags in ([det_bag], [det_bag, static_only]):
        with pytest.raises(SystemExit) as exc:
            dar.main([str(b) for b in bags]
                     + ['-t', TOPIC, '-o', str(tmp_path / 'x.csv'), '--box'] + TEST_BOX)
        assert 'no bag has a /tf topic' in str(exc.value)
    assert not (tmp_path / 'x.csv').exists()


def test_ping_record_keeps_only_the_binning_fields():
    """A queued ping holds the beam arrays and flag bytes, not the message."""
    from marine_acoustic_msgs.msg import DetectionFlag
    msg = _ping(100.0, 'mbes', -30.0)
    msg.flags = [DetectionFlag(flag=0), DetectionFlag(flag=2)]
    rec = dar.PingRecord(msg)
    assert not hasattr(rec, '__dict__') and not hasattr(rec, 'header')
    assert rec.flags == bytes([0, 2])
    assert (rec.sound_speed, rec.frequency) == (1500.0, 500000.0)
    bins, freqs = {}, []
    dar.bin_ping(rec, 2.0, False, bins, freqs)
    assert bins == {0: [-30.0, 0.0, 1]}   # beam 1 is flagged bad


def test_box_with_no_bag_holding_the_topic_is_an_error(tmp_path):
    """With --box at least one bag must carry the detections topic."""
    tf_bag, _det_bag = write_tf_and_detection_bags(tmp_path)
    with pytest.raises(SystemExit) as exc:
        dar.main([str(tf_bag), '-t', TOPIC, '-o', str(tmp_path / 'x.csv'),
                  '--box'] + TEST_BOX)
    assert "topic '%s' not in any bag" % TOPIC in str(exc.value)
    assert not (tmp_path / 'x.csv').exists()


class _FakeReader:
    """A SequentialReader stand-in that records how many are open at once."""

    open_now = 0
    open_max = 0

    def __init__(self, messages):
        """Open over ``messages``, a list of ``(name, data, t_ns)``."""
        self._messages = list(messages)
        self.closed = False
        _FakeReader.open_now += 1
        _FakeReader.open_max = max(_FakeReader.open_max, _FakeReader.open_now)

    def has_next(self):
        """Return True while messages remain."""
        assert not self.closed
        return bool(self._messages)

    def read_next(self):
        """Return the next message."""
        return self._messages.pop(0)

    def close(self):
        """Close the reader (once)."""
        assert not self.closed
        self.closed = True
        _FakeReader.open_now -= 1


def _fake_sources(bags):
    """Return merged_messages sources over lists of (name, t_ns) messages."""
    _FakeReader.open_now = _FakeReader.open_max = 0
    return [(msgs[0][1] if msgs else 0,
             (lambda msgs=msgs: _FakeReader([(n, b'', t) for n, t in msgs])))
            for msgs in bags]


def test_merged_messages_opens_split_files_one_at_a_time():
    """A recording in many consecutive splits holds one reader open at a time."""
    bags = [[('/t', 100 * k + j) for j in range(5)] for k in range(300)]
    bags.reverse()   # the command-line order does not matter
    got = [t for _i, _n, _d, t in dar.merged_messages(_fake_sources(bags))]
    assert got == sorted(got) and len(got) == 1500
    assert _FakeReader.open_max == 1 and _FakeReader.open_now == 0


def test_merged_messages_interleaves_overlapping_bags():
    """Overlapping bags interleave in time order; ties go to the bag given first."""
    bags = [[('/a', 10), ('/a', 30), ('/a', 50)],
            [('/b', 20), ('/b', 30), ('/b', 40)],
            [],
            [('/c', 60)]]
    got = [(i, t) for i, _n, _d, t in dar.merged_messages(_fake_sources(bags))]
    assert got == [(0, 10), (1, 20), (0, 30), (1, 30), (1, 40), (0, 50), (3, 60)]
    assert _FakeReader.open_max == 2 and _FakeReader.open_now == 0


def test_merged_messages_closes_readers_when_abandoned():
    """A consumer that stops early still closes every open reader."""
    bags = [[('/a', 10), ('/a', 30)], [('/b', 20), ('/b', 40)]]
    stream = dar.merged_messages(_fake_sources(bags))
    next(stream)
    next(stream)
    assert _FakeReader.open_now == 2
    stream.close()
    assert _FakeReader.open_now == 0


CORRUPT = b'\x00\x01\x00\x00garbage, not CDR'


def write_bag_with_corrupt_messages(path):
    """Write a bag whose detections and TF topics each hold one corrupt message."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    write_bag(path, [
        (99.0, '/bizzy/tf_static', _mbes_static()),
        (100.0, '/bizzy/tf', _earth_tf(100.0, inside)),
        (100.5, TOPIC, _ping(100.5, 'mbes', -30.0)),
        (100.7, TOPIC, CORRUPT),
        (100.8, '/bizzy/tf', CORRUPT),
        (101.0, '/bizzy/tf', _earth_tf(101.0, inside)),
        (101.5, TOPIC, _ping(101.5, 'mbes', -30.0)),
        (102.0, '/bizzy/tf', _earth_tf(102.0, inside)),
        (105.0, '/bizzy/tf', _earth_tf(105.0, inside)),
    ])


@pytest.mark.parametrize('box', [False, True])
def test_corrupt_messages_are_skipped_and_counted(tmp_path, capsys, box):
    """A message that will not deserialize is counted and skipped; the run goes on."""
    bag = tmp_path / 'damaged'
    write_bag_with_corrupt_messages(bag)
    out = tmp_path / 'curve.csv'
    extra = ['--box'] + TEST_BOX if box else []
    assert dar.main([str(bag), '-t', TOPIC, '-o', str(out)] + extra) == 0
    captured = capsys.readouterr()
    if box:
        assert 'kept 2 of 2 pings' in captured.out
        assert ('skipped 2 corrupt message(s) that would not deserialize '
                '(1 on /bizzy/sonar/detections, 1 on /bizzy/tf)') in captured.err
    else:
        assert 'read 2 pings' in captured.out
        assert ('skipped 1 corrupt message(s) that would not deserialize '
                '(1 on /bizzy/sonar/detections)') in captured.err
    assert counts_by_angle(out) == {1.0: 2, 31.0: 2}


def _ns(sec, nanosec=0):
    """Return integer nanoseconds for a (sec, nanosec) stamp."""
    return sec * 1000000000 + nanosec


def _push(loc, sec, nanosec, frame_id='mbes'):
    """Queue a ping at (sec, nanosec) and return the token drain yields for it."""
    token = (sec, nanosec, frame_id)
    loc.push(_ns(sec, nanosec), frame_id, token)
    return token


def _earth_tf_at(sec, nanosec, xyz):
    """Return earth -> base_link at an exact (sec, nanosec) stamp."""
    from builtin_interfaces.msg import Time
    msg = _earth_tf(0.0, xyz)
    msg.transforms[0].header.stamp = Time(sec=sec, nanosec=nanosec)
    return msg


def test_ping_locator_guard_edge_and_exact_interpolation():
    """
    A ping is released once the TF frontier is exactly the guard past its stamp.

    One nanosecond later it waits. The released ping is positioned by
    interpolating between the transforms on either side of its stamp.
    """
    south = geodetic_to_ecef(SOUTH[0], SOUTH[1], 0.0)
    north = geodetic_to_ecef(NORTH[0], NORTH[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_earth_tf_at(100, 0, south), False)
    loc.add_tf(_earth_tf_at(103, 0, north), False)
    edge = _push(loc, 101, 0)    # stamp == frontier - guard once TF 104 is in
    late = _push(loc, 101, 1)    # one nanosecond later
    assert list(loc.drain()) == []           # frontier 103: nothing is ready
    loc.add_tf(_earth_tf_at(104, 0, north), False)
    ready = list(loc.drain())
    assert [r[0] for r in ready] == [edge]
    assert loc.pending == 1
    # One third of the way from SOUTH to NORTH (the ECEF chord's sag is metres).
    lat, lon = ready[0][1]
    assert lat == pytest.approx(SOUTH[0] + (NORTH[0] - SOUTH[0]) / 3.0, abs=1e-4)
    assert lon == pytest.approx(SOUTH[1], abs=1e-6)
    loc.add_tf(_earth_tf_at(104, 1, north), False)   # frontier moves 1 ns
    assert [r[0] for r in loc.drain()] == [late]
    assert loc.pending == 0


def test_ping_locator_flush_and_no_latest_fallback():
    """Flushed pings get the exact lookup too: no latest-transform fallback."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_earth_tf_at(100, 0, inside), False)
    loc.add_tf(_earth_tf_at(101, 0, inside), False)
    within = _push(loc, 100, 500000000)
    after = _push(loc, 101, 1)       # a nanosecond past the last transform
    orphan = _push(loc, 100, 500000000, frame_id='orphan')
    assert list(loc.drain()) == []   # frontier 101 < 100.5 + guard
    got = [(r[0], r[2]) for r in loc.drain(flush=True)]
    # In stamp order (ties in arrival order), not arrival order.
    assert got == [(within, None), (orphan, 'no_tf'), (after, 'extrapolation')]


def test_ping_locator_far_future_ping_does_not_block_the_queue():
    """Pings leave in stamp order: a far-future stamp holds back nothing."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_earth_tf_at(100, 0, inside), False)
    future = _push(loc, 100 + 1800, 0)   # half an hour ahead: queued
    first = _push(loc, 100, 500000000)
    second = _push(loc, 101, 0)
    loc.add_tf(_earth_tf_at(104, 0, inside), False)
    assert [(r[0], r[2]) for r in loc.drain()] == [(first, None), (second, None)]
    assert loc.pending == 1
    assert [(r[0], r[2]) for r in loc.drain(flush=True)] == [(future, 'extrapolation')]


def test_ping_locator_drops_a_ping_far_ahead_of_the_tf_stream():
    """A ping more than the future limit ahead of the frontier is a bad stamp."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    assert loc.push(_ns(5000), 'mbes', 'no frontier yet') is None   # cannot judge
    loc.add_tf(_earth_tf_at(100, 0, inside), False)
    limit_ns = int(dar.PING_FUTURE_LIMIT_SEC * 1e9)
    assert loc.push(_ns(100) + limit_ns, 'mbes', 'at the limit') is None
    assert loc.push(_ns(100) + limit_ns + 1, 'mbes', 'past it') == 'bad_stamp'
    assert loc.push(0, 'mbes', 'unset') == 'bad_stamp'
    assert loc.pending == 2


def _bad_tf(sec, xyz=(6378137.0, 0.0, 0.0), w=1.0, parent='earth', child='base_link'):
    """Return a TFMessage with one transform built from the given parts."""
    from tf2_msgs.msg import TFMessage
    t = _transform(parent, child, sec, xyz)
    t.transform.rotation.w = w
    return TFMessage(transforms=[t])


def test_ping_locator_keeps_invalid_transforms_out_and_counts_them(capfd):
    """NaN, zero-quaternion and bad-name transforms neither enter tf2 nor move the frontier."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_earth_tf_at(100, 0, inside), False, _ns(100))
    ping = _push(loc, 100, 0)
    for msg in (_bad_tf(110.0, xyz=(float('nan'), 0.0, 0.0)),
                _bad_tf(110.0, w=0.0),
                _bad_tf(110.0, parent=''),
                _bad_tf(110.0, parent='base_link'),
                _bad_tf(0.0, xyz=(float('inf'), 0.0, 0.0))):
        loc.add_tf(msg, False, _ns(110))
    loc.add_tf(_bad_tf(0.0, w=0.0), True)   # statics are checked too
    assert loc.tf_rejected == {'invalid': 6}
    assert list(loc.drain()) == []           # the frontier is still 100
    assert loc.pending == 1
    assert 'TF_' not in capfd.readouterr().err   # tf2 printed nothing
    assert dar.tf_rejected_warning(loc.tf_rejected).startswith(
        'warning: --box ignored 6 transform(s): 6 invalid')
    assert [r[0] for r in loc.drain(flush=True)] == [ping]


def test_ping_locator_old_transform_is_counted_not_printed(capfd):
    """A transform tf2 would refuse as TF_OLD_DATA is counted, silently."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_earth_tf_at(200, 0, inside), False, _ns(200))
    loc.add_tf(_earth_tf_at(139, 0, inside), False, _ns(201))   # 61 s older
    loc.add_tf(_earth_tf_at(141, 0, inside), False, _ns(201))   # within 60 s
    assert loc.tf_rejected == {'old': 1}
    assert 'TF_OLD_DATA' not in capfd.readouterr().err


def test_ping_locator_future_transform_cannot_purge_the_buffer():
    """A transform far ahead of its bag time is left out, so real ones still serve."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_earth_tf_at(100, 0, inside), False, _ns(100))
    loc.add_tf(_earth_tf_at(100 + 86400, 0, inside), False, _ns(100, 500000000))
    loc.add_tf(_earth_tf_at(101, 0, inside), False, _ns(101))
    assert loc.tf_rejected == {'future': 1}
    assert loc.locate('mbes', _ns(100, 500000000)) == ((pytest.approx(INSIDE[0]),
                                                        pytest.approx(INSIDE[1])), None)


def test_ping_locator_frontier_never_passes_the_bag_time():
    """
    A transform stamped further ahead of its bag time than its frame is moves the frontier less.

    Only to that bag time plus the frame's offset.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_earth_tf_at(100, 0, inside), False, _ns(100))   # offset 0
    early = _push(loc, 97, 500000000)
    late = _push(loc, 97, 500000001)
    loc.add_tf(_earth_tf_at(110, 0, inside), False, _ns(100, 500000000))   # 10 s jump
    assert loc.tf_rejected == {}
    assert [r[0] for r in loc.drain()] == [early]   # frontier 100.5, not 110
    assert loc.pending == 1 and late


@pytest.mark.parametrize('offset', [90, 57, -30])
def test_ping_locator_constant_clock_offset_changes_nothing(offset):
    """
    A constant stamp-minus-bag-time offset releases pings as a zero offset does.

    A recorder whose clock lags (or leads) the publisher's records every
    transform at stamp - offset; none is left out as 'future', and the
    frontier follows the stamps, not the bag time.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    first = _push(loc, 100, 0)
    second = _push(loc, 101, 0)
    for sec in (100, 101, 102, 103):
        loc.add_tf(_earth_tf_at(sec, 0, inside), False, _ns(sec - offset))
    assert [(r[0], r[2]) for r in loc.drain()] == [(first, None)]   # frontier 103
    loc.add_tf(_earth_tf_at(104, 0, inside), False, _ns(104 - offset))
    assert [(r[0], r[2]) for r in loc.drain()] == [(second, None)]
    assert loc.tf_rejected == {}


def test_ping_locator_future_jump_is_judged_per_frame():
    """
    A frame's offset is its own: one frame's hour jump is left out, the other's +90 s is not.

    The jump does not enter tf2, so the real transforms of its frame still
    serve, and it does not move the frontier.
    """
    from tf2_msgs.msg import TFMessage
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_earth_tf_at(100, 0, inside), False, _ns(100))   # base_link: 0
    ping = _push(loc, 97, 500000000)
    loc.add_tf(_earth_tf_at(100 + 3600, 0, inside), False, _ns(100, 500000000))
    assert list(loc.drain()) == []   # the frontier is still 100
    for sec in (190, 191):           # buoy: a constant +90 s, accepted
        relay = TFMessage(transforms=[_transform('earth', 'buoy', float(sec), inside)])
        loc.add_tf(relay, False, _ns(sec - 90))
    loc.add_tf(_earth_tf_at(101, 0, inside), False, _ns(101))
    assert loc.tf_rejected == {'future': 1}
    assert loc.locate('mbes', _ns(100, 500000000)) == (
        (pytest.approx(INSIDE[0]), pytest.approx(INSIDE[1])), None)
    assert [(r[0], r[2]) for r in loc.drain(flush=True)] == [(ping, 'extrapolation')]


BAG_SEC = 1700000000   # a realistic epoch, so a near-zero stamp is far off it


@pytest.mark.parametrize('first_sec, reason', [
    (0.0, 'invalid'),                 # unset stamp
    (0.0001, 'future'),               # positive, but decades behind the bag time
    (BAG_SEC + 86400.0, 'future'),    # a day ahead
])
def test_ping_locator_glitched_first_transform_is_not_a_clock_reference(first_sec, reason):
    """
    A first transform far off its bag time is left out and sets no reference.

    Its frame takes the offset of its next transform instead, and the glitch
    moves no frontier, so neither its own pings nor those of a second,
    healthy frame (earth -> buoy -> mbes2) are lost.
    """
    from tf2_msgs.msg import TFMessage
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(TFMessage(transforms=[_transform('buoy', 'mbes2', 0.0, (0.0, 0.0, 0.0))]),
               True)
    loc.add_tf(_link_tf('earth', 'base_link', first_sec, inside), False, _ns(BAG_SEC))
    assert loc.tf_rejected == {reason: 1}
    assert loc.chain_frontier('mbes') is None   # nothing accepted for base_link
    tokens = []
    got = []
    for k in range(1, 30):
        sec = BAG_SEC + k
        loc.add_tf(_link_tf('earth', 'base_link', float(sec), inside), False, _ns(sec))
        if k % 5 == 0:
            tokens.append(_push(loc, sec - 2, 0))
            tokens.append(_push(loc, sec + 1, 0, frame_id='mbes2'))
        got += [(r[0], r[2]) for r in loc.drain()]
        loc.add_tf(_link_tf('earth', 'buoy', float(sec), inside), False, _ns(sec))
        got += [(r[0], r[2]) for r in loc.drain()]
    got += [(r[0], r[2]) for r in loc.drain(flush=True)]
    assert loc.tf_rejected == {reason: 1}
    assert sorted(got) == sorted((token, None) for token in tokens)


def _link_tf(parent, child, t_sec, xyz=(0.0, 0.0, 0.0)):
    """Return a TFMessage holding one parent -> child transform at t_sec."""
    from tf2_msgs.msg import TFMessage
    return TFMessage(transforms=[_transform(parent, child, t_sec, xyz)])


def _feed_slow_earth_chain(loc, until_sec):
    """
    Feed earth -> map at 0.1 Hz (100 s, 110 s, ...) and map -> base_link at 10 Hz.

    Also earth -> buoy at 10 Hz, an unrelated frame. Fed in time order up to,
    but not including, ``until_sec``.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    for k in range(1000, int(round(until_sec * 10))):
        sec = k / 10.0
        if k % 100 == 0:
            loc.add_tf(_link_tf('earth', 'map', sec, inside), False, _ns(k // 10))
        loc.add_tf(_link_tf('map', 'base_link', sec), False, int(round(sec * 1e9)))
        loc.add_tf(_link_tf('earth', 'buoy', sec, inside), False, int(round(sec * 1e9)))


def test_ping_locator_waits_for_the_slowest_link_of_its_chain():
    """
    A ping waits for every dynamic link of its chain, not the newest stamp of any frame.

    The fast map -> base_link and the unrelated buoy pass the ping long before
    the 0.1 Hz earth -> map does; released then (import_bag's frontier) it
    would drop as extrapolation, with only the 100 s earth -> map read.
    """
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    ping = _push(loc, 101, 0)
    _feed_slow_earth_chain(loc, 110.0)
    assert list(loc.drain()) == []
    assert loc.chain_frontier('mbes') == _ns(100)
    _feed_slow_earth_chain(loc, 110.1)   # earth -> map at 110 s
    got = list(loc.drain())
    assert [(r[0], r[2]) for r in got] == [(ping, None)]
    assert got[0][1] == (pytest.approx(INSIDE[0]), pytest.approx(INSIDE[1]))


def test_ping_locator_releases_a_ping_held_by_a_stalled_link():
    """Once its chain's newest link stamp is cache - guard past a ping, it leaves anyway."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_link_tf('earth', 'map', 100.0, inside), False, _ns(100))
    ping = _push(loc, 101, 0)
    release = 101.0 + dar.TF_CACHE_SEC - dar.TF_GUARD_SEC
    for k in range(1000, int(round(release * 10))):   # earth -> map has stopped
        loc.add_tf(_link_tf('map', 'base_link', k / 10.0), False, int(round(k * 1e8)))
    assert list(loc.drain()) == []
    loc.add_tf(_link_tf('map', 'base_link', release), False, _ns(int(release)))
    assert [(r[0], r[2]) for r in loc.drain()] == [(ping, 'extrapolation')]


def test_ping_locator_release_bound_is_per_chain():
    """
    Another frame whose clock runs 90 s ahead does not release a chain's pings early.

    earth -> base_link at 1 Hz with no offset beside earth -> buoy at 10 Hz
    stamped 90 s ahead of the bag time: the 57 s release is measured on each
    chain's own links, so every ping on both chains is positioned.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    tokens = []
    got = []
    for k in range(1000, 1600):
        sec = k / 10.0
        bag = int(round(sec * 1e9))
        if k % 10 == 0:
            loc.add_tf(_link_tf('earth', 'base_link', sec, inside), False, bag)
        loc.add_tf(_link_tf('earth', 'buoy', sec + 90.0, inside), False, bag)
        if k % 10 == 5 and k < 1400:
            tokens.append(_push(loc, k // 10, 500000000))
            tokens.append(_push(loc, k // 10 + 90, 500000000, frame_id='buoy'))
        got += [(r[0], r[2]) for r in loc.drain()]
    got += [(r[0], r[2]) for r in loc.drain(flush=True)]
    assert loc.tf_rejected == {}
    assert sorted(got) == sorted((token, None) for token in tokens)


def test_ping_locator_static_chain_and_unconnected_frame():
    """
    A chain of static links is ready at once; an unconnected frame waits for the overall frontier.

    Static transforms never change, so nothing needs to be waited for; a frame
    with no path to earth yet is released as import_bag would and drops as no_tf.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_link_tf('earth', 'buoy', 0.0, inside), True)
    assert loc.chain_frontier('buoy') == math.inf
    buoy = _push(loc, 50, 0, frame_id='buoy')
    assert [(r[0], r[2]) for r in loc.drain()] == [(buoy, None)]
    orphan = _push(loc, 100, 0, frame_id='orphan')
    assert loc.chain_frontier('orphan') is None and list(loc.drain()) == []
    loc.add_tf(_earth_tf_at(103, 0, inside), False, _ns(103))
    assert [(r[0], r[2]) for r in loc.drain()] == [(orphan, 'no_tf')]


def test_ping_locator_chain_with_a_cycle_does_not_hang():
    """A parent loop in the transforms read so far reads as not connected to earth."""
    loc = dar.PingLocator()
    loc.add_tf(_link_tf('a', 'b', 100.0), False, _ns(100))
    loc.add_tf(_link_tf('b', 'a', 100.0), False, _ns(100))
    assert loc.chain_frontier('a') == _ns(100)   # the overall frontier


def test_ping_locator_rejects_unset_stamp():
    """A zero stamp is dropped as bad_stamp, never read as tf2's "latest"."""
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_earth_tf_at(100, 0, inside), False)
    assert loc.locate('mbes', 0) == (None, 'bad_stamp')
    assert loc.locate('mbes', _ns(100))[1] is None


@pytest.mark.parametrize('xyz', [
    (0.0, 0.0, 0.0),            # would be the North Pole, 6357 km down
    (1.0, 1.0, 1.0),            # ecef_to_geodetic raises on these
    (100.0, 100.0, 100.0),
    (1500.0, -2300.0, 4.0),     # local map coordinates in a frame named earth
    (float('nan'), 0.0, 0.0),
    (4.2e7, 0.0, 0.0),          # geostationary orbit
    (34721.27, 13579.63, -18942.10),   # ~42 km out: the closed form goes complex
    (0.0, 0.0, dar.WGS84_B - 50000.0 - 1.0),   # 1 m inside the radius band (pole)
    (dar.WGS84_A + 50000.0 + 1.0, 0.0, 0.0),   # 1 m outside it (equator)
])
def test_earth_position_rejects_non_geocentric(xyz):
    """Translations that are not a position near the Earth's surface give None."""
    assert dar.earth_position_to_lat_lon(*xyz) is None


def test_earth_position_radius_band_edges():
    """Just inside the radius band converts; the height bound still applies."""
    # 1 m above the inner edge on the polar axis: 49999 m below the ellipsoid.
    lat, _lon = dar.earth_position_to_lat_lon(0.0, 0.0, dar.WGS84_B - 49999.0)
    assert lat == 90.0
    # 1 m inside the outer edge at the equator: 49999 m above the ellipsoid.
    lat, lon = dar.earth_position_to_lat_lon(dar.WGS84_A + 49999.0, 0.0, 0.0)
    assert (lat, lon) == (pytest.approx(0.0, abs=1e-9), 0.0)
    # Inside the radius band but 71 km below the ellipsoid at the equator.
    assert dar.earth_position_to_lat_lon(dar.WGS84_B - 49999.0, 0.0, 0.0) is None


def test_earth_position_never_raises():
    """No geocentric radius from the centre to far outside the Earth raises."""
    import random
    rng = random.Random(170)
    for _ in range(20000):
        radius = rng.uniform(0.0, 7.0e6)
        v = [rng.gauss(0.0, 1.0) for _ in range(3)]
        norm = math.sqrt(sum(c * c for c in v)) or 1.0
        got = dar.earth_position_to_lat_lon(*(radius * c / norm for c in v))
        assert got is None or (-90.0 <= got[0] <= 90.0 and -180.0 <= got[1] <= 180.0)


def test_earth_position_accepts_survey_heights():
    """A sensor a little below or above the ellipsoid converts."""
    for h in (-120.0, 0.0, 35.0):
        lat, lon = dar.earth_position_to_lat_lon(*geodetic_to_ecef(42.97, -70.61, h))
        assert lat == pytest.approx(42.97, abs=1e-9)
        assert lon == pytest.approx(-70.61, abs=1e-9)


def test_ping_locator_drops_non_geocentric_earth_transform():
    """A non-geocentric earth transform drops the ping instead of aborting."""
    loc = dar.PingLocator()
    loc.add_tf(_mbes_static(), True)
    loc.add_tf(_earth_tf_at(100, 0, (1.0, 1.0, 1.0)), False)
    assert loc.locate('mbes', _ns(100)) == (None, 'bad_position')


# ==========================================================================
# --by-mode: per-pulse-mode table (cube_bathymetry#172)
# ==========================================================================

MODE_KEYS = HERE / 'data' / 'mode_keys.json'
MODE_TABLE_FIXTURE = HERE / 'data' / 'angular_response_table_v1.csv'
REF_KEY = 'FMU_1000us_24000Hz_500kHz'
CW_KEY = 'CW_50us_20000Hz_500kHz'


def _f32(value):
    """Return a JSON vector value as the float32 a message would carry, widened."""
    import struct
    if isinstance(value, str):
        return float(value)
    return struct.unpack('f', struct.pack('f', value))[0]


def _key_cases():
    import json
    return json.loads(MODE_KEYS.read_text())['cases']


@pytest.mark.parametrize('case', _key_cases(), ids=lambda c: c['name'])
def test_mode_key_vectors(case):
    """The shared key vectors (the spec PR 2's C++ must match) hold."""
    key = dar.canonical_mode_key([_f32(v) for v in case['pulse_lengths_s']],
                                 [_f32(v) for v in case['bandwidths_hz']],
                                 case['tx_signal_types'], _f32(case['frequency_hz']))
    assert key == case['key']
    # Letter first (the old loaders' numeric parse rejects the row), no field
    # or line separators, never the reserved '*'.
    assert key[0].isalpha()
    assert not any(c in key for c in ', \t\r\n*')


@pytest.mark.parametrize('value,scale,text', [
    (1000.0, 1.0, '1000'), (24000.0, 1.0, '24000'), (20.0, 1.0, '20'),
    (0.45, 1.0, '0.45'), (1.5, 1.0, '1.5'), (0.0200, 1.0, '0.02'),
    (9.996, 1.0, '10'), (123456.0, 1.0, '123000'), (0.001, 1e6, '1000'),
    (500000.0, 1e-3, '500'), (0.0, 1.0, '?'), (-1.0, 1.0, '?'),
    (float('nan'), 1.0, '?'), (float('inf'), 1.0, '?'), ('x', 1.0, '?'),
])
def test_key_number(value, scale, text):
    """Key numbers: 3 significant figures, plain decimal, '?' for bad raw values."""
    assert dar.key_number(value, scale) == text


def test_default_sonar_info_topic():
    """The SonarInfo topic defaults to the detections topic's sibling."""
    assert dar.default_sonar_info_topic('/bizzy/sensors/m3/detections') == \
        '/bizzy/sensors/m3/sonar_info'
    assert dar.default_sonar_info_topic('detections') == '/sonar_info'


def test_by_mode_defaults():
    """--by-mode fills the topic, reference and fit defaults."""
    args = dar.parse_args(BASE + ['--by-mode'])
    assert args.sonar_info_topic == '/sonar/sonar_info'
    assert args.reference_mode == REF_KEY
    assert (args.adjacency_window_s, args.specular_exclusion_deg,
            args.misfit_threshold_db, args.range_ref_m) == (2.0, 10.0, 1.5, None)
    assert args.range_angle_edges == (30.0, 50.0)


@pytest.mark.parametrize('text, edges', [('none', ()), ('30', (30.0,)),
                                         ('25,45,65', (25.0, 45.0, 65.0))])
def test_range_angle_edges_parse(text, edges):
    """--range-angle-edges: "none" is one group, else the edges in degrees."""
    args = dar.parse_args(BASE + ['--by-mode', '--range-angle-edges', text])
    assert args.range_angle_edges == edges
    b = dar.ModeTableBuilder(range_angle_edges_deg=edges)
    assert len(b.groups) == len(edges) + 1
    assert b.groups[0][0] == b.first_nonspecular_bin and b.groups[-1][1] == b.n_half


@pytest.mark.parametrize('text, reason', [
    ('30,30', 'must increase strictly'), ('50,30', 'must increase strictly'),
    ('abc', 'comma-separated'), ('10', 'empty group'), ('9', 'empty group'),
    ('29.9,30', 'empty group'), ('90', 'empty group'), ('nan', 'not a finite number')])
def test_range_angle_edges_rejected(text, reason, capsys):
    """An edge list that is not increasing, not numeric or leaves a group empty is an error."""
    assert reason in parse_error(['--by-mode', '--range-angle-edges', text], capsys)


def test_range_angle_groups_snap_to_bins():
    """Edges snap up to the next bin boundary; the snapped bounds are what the table says."""
    groups = dar.range_angle_groups(4.0, 10.0, (30.0, 50.0))
    assert groups == [(3, 8), (8, 13), (13, 23)]
    assert [dar.group_bounds_deg(g, 4.0) for g in groups] == [
        (12.0, 32.0), (32.0, 52.0), (52.0, 90.0)]
    bg = dar.bin_groups(groups, 23)
    assert list(bg[:3]) == [0, 0, 0] and bg[7] == 0 and bg[8] == 1 and bg[13] == 2


def test_by_mode_refuses_remove_tl(capsys):
    """A table never carries tl_removed: --by-mode --remove-tl is refused."""
    err = parse_error(['--by-mode', '--remove-tl', '--water-temp-c', '15'], capsys)
    assert '--by-mode is refused with --remove-tl' in err


@pytest.mark.parametrize('flag', [['--reference-mode', REF_KEY], ['--range-ref-m', '10'],
                                  ['--adjacency-window-s', '1'],
                                  ['--range-angle-edges', '30'],
                                  ['--accumulation-cache', 'a.pkl'],
                                  ['--diagnostics-out', 'd.json']])
def test_by_mode_flags_need_by_mode(flag, capsys):
    """The per-mode options are an error without --by-mode."""
    assert 'only applies with --by-mode' in parse_error(flag, capsys)


@pytest.mark.parametrize('flag', [['--range-ref-m', '0'], ['--adjacency-window-s', '-1'],
                                  ['--specular-exclusion-deg', '85'],
                                  ['--misfit-threshold-db', 'nan'],
                                  ['--reference-mode', 'a,b']])
def test_by_mode_values_checked(flag, capsys):
    """Out-of-range per-mode values are argparse errors."""
    parse_error(['--by-mode'] + flag, capsys)


class _Ping:
    """A PingRecord stand-in for synthetic streams."""

    __slots__ = ('intensities', 'rx_angles', 'two_way_travel_times', 'sound_speed',
                 'frequency', 'flags')


SYNTH_ANGLES_DEG = [a + 0.5 for a in range(-65, 65)]
MODES = {
    REF_KEY: {'pulse': 0.001, 'bw': 24000.0, 'type': 2},
    CW_KEY: {'pulse': 0.00005, 'bw': 20000.0, 'type': 1},
    'CW_20us_50000Hz_500kHz': {'pulse': 0.00002, 'bw': 50000.0, 'type': 1},
    'FMU_5000us_24000Hz_500kHz': {'pulse': 0.005, 'bw': 24000.0, 'type': 2},
}


def sweep_depth(t):
    """Depth that sweeps 5..25 m every 120 s (identifies the range term)."""
    return 15.0 + 10.0 * math.sin(2.0 * math.pi * t / 120.0)


def two_mode_switch(threshold=15.0, hysteresis=1.0, shallow=REF_KEY, deep=CW_KEY):
    """Return a mode chooser switching at ``threshold`` m depth with hysteresis."""
    def choose(_t, h, cur):
        if cur is None:
            return shallow if h < threshold else deep
        if cur == shallow and h > threshold + hysteresis:
            return deep
        if cur == deep and h < threshold - hysteresis:
            return shallow
        return cur
    return choose


def synth_stream(builder, mode_of, level, offsets=None, seconds=600.0, rate=20.0,
                 depth=sweep_depth, slope_deg=0.0, noise_db=3.0, seed=1, t0=100.0,
                 angles_deg=SYNTH_ANGLES_DEG, tvg=(0, float('nan'))):
    """
    Feed a synthetic multibeam stream into ``builder``.

    Flat or cross-track sloping bottom (``slope_deg`` > 0 deepens to
    starboard): a beam at angle theta meets it at R = h cos(s) / cos(theta + s)
    with incidence |theta + s|. Each beam's dB is ``level(incidence_deg, R,
    theta_deg, mode) + offsets[mode]`` plus Gaussian noise. A SonarInfo is
    added (stamped 1 us before the ping, declaring ``tvg`` = (model, alpha))
    whenever the mode changes.
    """
    import numpy as np
    rng = np.random.default_rng(seed)
    offsets = offsets or {}
    theta = np.radians(np.asarray(angles_deg, dtype=np.float64))
    slope = math.radians(slope_deg)
    cur = None
    for i in range(int(seconds * rate)):
        t = t0 + i / rate
        h = depth(t)
        mode = mode_of(t, h, cur)
        if mode != cur:
            m = MODES[mode]
            builder.add_sonar_info(int(round(t * 1e9)) - 1000, [m['pulse']], [m['bw']],
                                   [m['type']], tvg[0], tvg[1], 'synthetic')
            cur = mode
        rng_m = h * math.cos(slope) / np.cos(theta + slope)
        inc = np.abs(np.degrees(theta + slope))
        y = (level(inc, rng_m, np.degrees(theta), mode) + offsets.get(mode, 0.0)
             + rng.normal(0.0, noise_db, len(theta)))
        p = _Ping()
        p.intensities = y.astype(np.float32)
        p.rx_angles = theta.astype(np.float32)
        p.two_way_travel_times = (2.0 * rng_m / 1500.0).astype(np.float32)
        p.sound_speed = 1500.0
        p.frequency = 500000.0
        p.flags = None
        builder.add_ping(int(round(t * 1e9)), p)


def lambert_like(inc_deg):
    """Return a smooth incidence-angle response, dB."""
    import numpy as np
    return -20.0 - 0.15 * np.asarray(inc_deg)


def supported(group, min_support=2000.0):
    """Return ``(node, r, se)`` of one angle group's well-supported range nodes."""
    rows = zip(group['nodes'], group['r'], group['r_se'], group['r_support'])
    return [(x, r, se) for x, r, se, n in rows if n >= min_support]


def supported_all(fit, min_support=2000.0):
    """Return ``(group, node, r, se)`` over every angle group's well-supported nodes."""
    return [(g, x, r, se) for g in fit['groups'] for x, r, se in supported(g, min_support)]


def test_two_mode_step_offset_recovered():
    """A 12 dB step between two modes is recovered within 0.5 dB; directions agree."""
    b = dar.ModeTableBuilder()
    synth_stream(b, two_mode_switch(),
                 lambda inc, r, th, m: lambert_like(inc) - 10.0 * dar_log10(r),
                 offsets={CW_KEY: 12.0})
    table = b.finish(REF_KEY)
    assert table['reference_source'] == 'requested'
    off = table['offsets']
    assert off[REF_KEY] == {'offset_db': 0.0, 'se_db': 0.0, 'anchored': True}
    assert off[CW_KEY]['offset_db'] == pytest.approx(12.0, abs=0.5)
    assert off[CW_KEY]['anchored'] and off[CW_KEY]['se_db'] < 0.5
    (pair,) = table['pairs']
    assert pair['used'] and pair['count'] >= 6
    assert pair['directions_agree'] is True
    assert all(d['count'] >= 3 for d in pair['directions'].values())
    assert table['switches']['events'] == pair['count']
    # The table's meaning: raw - db_relative_to_nadir - r_g - offset puts both
    # modes on the reference mode's nadir level at R0 (the header's offset_meaning).
    levels = {}
    for key in (REF_KEY, CW_KEY):
        n, s, _ss = b.modes[key].cells()
        fit = table['modes'][key]
        n_half = n.shape[0] // 2
        abs_n, abs_s = n[:n_half] + n[n_half:], s[:n_half] + s[n_half:]
        r_bin = fit['range_db_per_cell'][b.bin_group]
        corrected = abs_s.sum() - (abs_n * (fit['rel_db_per_bin'][:, None] + r_bin)).sum()
        levels[key] = corrected / abs_n.sum() - off[key]['offset_db']
    assert levels[CW_KEY] == pytest.approx(levels[REF_KEY], abs=0.5)


def dar_log10(x):
    """Return numpy log10 of ``x`` (the synthetic level laws)."""
    import numpy as np
    return np.log10(x)


def test_offsets_chain_through_an_intermediate_mode_and_flag_unanchored():
    """A mode that only meets an intermediate mode is chained; an isolated one is unanchored."""
    deep = 'FMU_5000us_24000Hz_500kHz'
    iso = 'CW_20us_50000Hz_500kHz'

    def three(t, h, cur):
        # shallow REF (< 11 m), CW (11-19 m), deep FMU 5 ms (> 19 m), with
        # hysteresis, so the deep mode never borders the reference.
        if cur is None:
            return REF_KEY if h < 11 else (CW_KEY if h < 19 else deep)
        if cur == REF_KEY and h > 12:
            return CW_KEY
        if cur == CW_KEY and h < 10:
            return REF_KEY
        if cur == CW_KEY and h > 20:
            return deep
        if cur == deep and h < 18:
            return CW_KEY
        return cur

    b = dar.ModeTableBuilder()
    law = (lambda inc, r, th, m: lambert_like(inc) - 15.0 * dar_log10(r))
    synth_stream(b, three, law, offsets={CW_KEY: 8.0, deep: 17.0})
    # An isolated mode recorded after a 60 s gap: no switch path to the rest.
    synth_stream(b, lambda t, h, cur: iso, law, offsets={iso: -5.0}, seconds=60.0,
                 t0=800.0, seed=2)
    table = b.finish(REF_KEY)
    off = table['offsets']
    assert off[CW_KEY]['offset_db'] == pytest.approx(8.0, abs=0.5)
    # The deep mode never reaches R0: its range term is anchored at its node
    # nearest R0, and the offset carries the -15 dB/decade law between them.
    nodes = table['modes'][deep]['groups'][0]['nodes']
    anchor = min(max(0.0, nodes[0]), nodes[-1])
    assert off[deep]['offset_db'] == pytest.approx(17.0 - 15.0 * anchor, abs=0.5)
    assert off[deep]['anchored'] and off[deep]['se_db'] > off[CW_KEY]['se_db']
    assert not any(set(p['modes']) == {REF_KEY, deep} for p in table['pairs'])
    assert off[iso] == {'offset_db': 0.0, 'se_db': off[iso]['se_db'], 'anchored': False}
    assert math.isnan(off[iso]['se_db'])
    assert any('no switch path to the reference mode for %s' % iso in w
               for w in table['warnings'])
    assert table['switches']['discarded'].get('gap', 0) >= 1
    lines = dar.mode_table_lines(table, ['synthetic'])
    assert any(ln.startswith('# mode_summary: %s ' % iso) and 'anchored=false' in ln
               for ln in lines)


def test_reference_fallback_is_loud():
    """An absent reference mode falls back to the most-sampled one with a warning."""
    b = dar.ModeTableBuilder()
    synth_stream(b, two_mode_switch(threshold=12.0),
                 lambda inc, r, th, m: lambert_like(inc), seconds=240.0)
    table = b.finish('CW_1us_1Hz_1kHz')
    assert table['reference_mode'] in (REF_KEY, CW_KEY)
    assert 'NOT comparable across campaigns' in table['reference_source']
    assert any('NOT comparable' in w for w in table['warnings'])
    hd = table_header(dar.mode_table_lines(table, ['s']))
    assert 'NOT comparable across campaigns' in hd['reference_mode_source']


@pytest.mark.parametrize('slope_db_per_decade', [0.0, -20.0, 10.0])
def test_range_term_recovers_the_compensation_residual(slope_db_per_decade):
    """
    40logR-, 20logR- and over-compensated streams: the nodes recover the shape.

    A sonar compensating 40 log R leaves a flat level (0 dB/decade); one
    compensating 20 log R of a 40 log R loss leaves -20 dB/decade; no law is
    assumed by the fit.
    """
    b = dar.ModeTableBuilder()
    synth_stream(b, lambda t, h, cur: REF_KEY,
                 lambda inc, r, th, m: lambert_like(inc) + slope_db_per_decade * dar_log10(r))
    table = b.finish(REF_KEY)
    fit = table['modes'][REF_KEY]
    assert fit['range_status'] == 'fitted' and len(fit['groups']) == 3
    for g in fit['groups']:
        nodes = supported(g)
        assert len(nodes) >= 3, g['lo_deg']
        for x, r, _se in nodes:
            assert r == pytest.approx(slope_db_per_decade * x, abs=0.5), (g['lo_deg'], x, r)
        assert 0.0 in g['nodes']
        assert g['r'][g['nodes'].index(0.0)] == pytest.approx(0.0, abs=1e-9)
    assert not fit['misfit_exceeded']
    for g in fit['scalar_comparison']['groups']:
        assert g['slope_db_per_decade'] == pytest.approx(slope_db_per_decade, abs=0.5)
    # Intercepts are referred to R0: the angle curve is the incidence law.
    rows = {centre: rel for centre, _n, _a, _v, rel in fit['angle']}
    assert rows[40.5 if 40.5 in rows else 41.0] == pytest.approx(
        -0.15 * (41.0 - 1.0), abs=0.6)


def test_absorption_curvature_needs_the_binned_term():
    """An uncompensated 2*alpha*R loss: the nodes follow the curve, a scalar slope cannot."""
    alpha = 0.13  # dB/m: 500 kHz in 15 C, 31 psu seawater (cube_bathymetry#170)

    b = dar.ModeTableBuilder()
    synth_stream(b, lambda t, h, cur: REF_KEY,
                 lambda inc, r, th, m: lambert_like(inc) - 2.0 * alpha * r,
                 depth=lambda t: 22.5 + 17.5 * math.sin(2.0 * math.pi * t / 120.0))
    table = b.finish(REF_KEY)
    fit = table['modes'][REF_KEY]
    r0 = table['range_ref_m']
    assert supported_all(fit)
    for g, x, r, _se in supported_all(fit):
        if x in (g['nodes'][0], g['nodes'][-1]):
            # An end node has data on one side only, so its value is a chord
            # fit of the curve, not the curve's value; the misfit below is the
            # check that the term still follows the data there.
            continue
        # Linear between quarter-decade nodes: within 0.5 dB of the curve at the nodes.
        assert r == pytest.approx(-2.0 * alpha * r0 * (10.0 ** x - 1.0), abs=0.5), (
            g['lo_deg'], x, r)
    assert fit['misfit_max_db'] < table['misfit_threshold_db']
    assert not fit['misfit_exceeded']
    assert fit['scalar_comparison']['misfit_max_db'] > table['misfit_threshold_db']


def test_sloping_bottom_signed_bins_keep_the_slope_out_of_the_range_term():
    """
    A cross-track slope pairs shorter range with smaller incidence on one side.

    Signed angle bins recover the true range term; folding to |theta| bins
    (shown for comparison) is biased.
    """
    b = dar.ModeTableBuilder()
    synth_stream(b, lambda t, h, cur: REF_KEY,
                 lambda inc, r, th, m: -20.0 - 0.3 * inc - 10.0 * dar_log10(r),
                 slope_deg=12.0, depth=lambda t: 17.0 + 5.0 * math.sin(2.0 * math.pi * t / 120.0))
    table = b.finish(REF_KEY)
    fit = table['modes'][REF_KEY]
    assert supported_all(fit)
    for g, x, r, _se in supported_all(fit):
        assert r == pytest.approx(-10.0 * x, abs=0.5), (g['lo_deg'], x, r)
    n, s, ss = b.modes[REF_KEY].cells()
    folded = dar.fit_mode(n, s, ss, b.bin_width_deg, b.groups, table['log_r0'], fold=True)
    errors = [abs(r + 10.0 * x) for _g, x, r, _se in supported_all(folded)]
    assert max(errors) > 1.5


def test_specular_band_is_kept_out_of_the_range_fit():
    """A range-dependent artefact inside 10 deg stays out of r; without the exclusion it leaks."""
    law = (lambda inc, r, th, m: lambert_like(inc) - 10.0 * dar_log10(r)
           + dar_where(abs_array(th) < 10.0, 30.0 * dar_log10(r), 0.0))
    errors = {}
    for exclusion in (10.0, 0.0):
        b = dar.ModeTableBuilder(specular_exclusion_deg=exclusion)
        synth_stream(b, lambda t, h, cur: REF_KEY, law)
        fit = b.finish(REF_KEY)['modes'][REF_KEY]
        errors[exclusion] = max(abs(r + 10.0 * x) for _g, x, r, _se in supported_all(fit))
    assert errors[10.0] < 0.5
    assert errors[0.0] > 1.0


def dar_where(cond, a, b):
    """Return numpy where."""
    import numpy as np
    return np.where(cond, a, b)


def abs_array(x):
    """Return numpy abs."""
    import numpy as np
    return np.abs(x)


def test_misfit_flags_a_step_inside_a_node_interval():
    """A 6 dB step between nodes is structure the binned term cannot carry: flagged."""
    b = dar.ModeTableBuilder()
    synth_stream(b, lambda t, h, cur: REF_KEY,
                 lambda inc, r, th, m: lambert_like(inc) + dar_where(r > 21.0, -6.0, 0.0))
    table = b.finish(REF_KEY, range_ref_m=10.0)
    fit = table['modes'][REF_KEY]
    assert fit['misfit_exceeded'] and fit['misfit_max_db'] > 1.5
    assert any('misfit' in w and REF_KEY in w for w in table['warnings'])
    assert 'misfit_exceeded=yes' in '\n'.join(dar.mode_table_lines(table, ['s']))


def test_flat_single_depth_bottom_leaves_the_range_term_unidentified():
    """Angle and range are confounded on a flat single-depth bottom: r = 0 with a reason."""
    b = dar.ModeTableBuilder()
    synth_stream(b, lambda t, h, cur: REF_KEY,
                 lambda inc, r, th, m: lambert_like(inc) - 20.0 * dar_log10(r),
                 depth=lambda t: 20.0, seconds=120.0)
    fit = b.finish(REF_KEY)['modes'][REF_KEY]
    assert fit['range_status'] == 'zero'
    for g in fit['groups']:
        assert g['range_status'].startswith('zero: not identified')
        assert g['nodes'] == [] and g['within_bin_spread_decades'] < 0.03
    assert not any(ln.split(',')[1] == 'range' for ln in dar.mode_table_lines(
        b.finish(REF_KEY), ['s']) if not ln.startswith('#'))


def test_non_separable_range_term_needs_the_angle_groups():
    """
    One range term per mode fails on a range dependence that differs with angle.

    Inner and outer beams follow different log-range slopes: one term per
    mode leaves structure the misfit flags; one per angle group recovers each.
    """
    def slope(th):
        a = abs_array(th)
        return dar_where(a < 30.0, -10.0, dar_where(a < 50.0, -17.0, -25.0))

    law = (lambda inc, r, th, m: lambert_like(inc) + slope(th) * dar_log10(r))
    grouped = dar.ModeTableBuilder()
    synth_stream(grouped, lambda t, h, cur: REF_KEY, law)
    fit = grouped.finish(REF_KEY)['modes'][REF_KEY]
    assert [(g['lo_deg'], g['hi_deg']) for g in fit['groups']] == [
        (10.0, 30.0), (30.0, 50.0), (50.0, 90.0)]
    expected = dict(zip((10.0, 30.0, 50.0), (-10.0, -17.0, -25.0)))
    for g, x, r, _se in supported_all(fit):
        assert r == pytest.approx(expected[g['lo_deg']] * x, abs=0.5), (g['lo_deg'], x, r)
    assert not fit['misfit_exceeded'], fit['misfit_max_db']

    single = dar.ModeTableBuilder(range_angle_edges_deg=())
    synth_stream(single, lambda t, h, cur: REF_KEY, law)
    table = single.finish(REF_KEY)
    one = table['modes'][REF_KEY]
    assert len(one['groups']) == 1
    assert one['misfit_exceeded'] and one['misfit_max_db'] > 1.5
    # The pooled residual per range bin cancels; the half groups show it.
    halves = {k: v for k, v in one['binned_residuals'].items() if 'half' in k}
    assert len(halves) == 2
    assert max(abs(m) for rows in halves.values() for _x, _n, m in rows) > 1.5
    assert max(abs(m) for _x, _n, m in one['binned_residuals']['all']) < 1.0


def test_junk_end_node_is_trimmed_and_the_anchor_moves_to_the_span():
    """
    A sliver of junk at long range must not anchor the term (Appledore run 2).

    A mode working at 5-25 m that also records a few hundred detections at
    ~R0 * 4, 60 dB off: the end node those beams would hold is trimmed, the
    term is anchored at the nearest kept node, the junk is out of span and the
    good nodes keep the law.
    """
    def depth(t):
        # 3 junk pings of 12000 (390 of 1.56 M beams: over 200, under 0.1 %).
        return 250.0 if int(round(t * 20.0)) % 4000 == 0 else sweep_depth(t)

    def law(inc, r, th, m):
        return lambert_like(inc) - 10.0 * dar_log10(r) + dar_where(r > 150.0, 60.0, 0.0)

    b = dar.ModeTableBuilder()
    synth_stream(b, lambda t, h, cur: REF_KEY, law, depth=depth)
    table = b.finish(REF_KEY, range_ref_m=60.0)     # R0 far beyond the working range
    fit = table['modes'][REF_KEY]
    for g in fit['groups']:
        assert g['range_status'] == 'fitted', g
        assert g['trimmed_nodes'] and max(g['trimmed_nodes']) > 0.5, g['trimmed_nodes']
        assert g['out_of_span_samples'] > 0
        assert g['span'][1] < 0.5
        # Anchored at the nearest kept node: r = 0 there, 0 at no other node.
        assert g['r'][-1] == pytest.approx(0.0, abs=1e-9)
        for x, r, _se in supported(g):
            assert r == pytest.approx(-10.0 * (x - g['nodes'][-1]), abs=0.5), (x, r)
    assert not fit['misfit_exceeded'], fit['misfit_max_db']
    header = '\n'.join(dar.mode_table_lines(table, ['s']))
    assert 'out_of_span=' in header and 'trimmed=' in header


def test_misfit_ignores_bins_below_the_share_floor():
    """A 20 dB residual on a bin holding under 0.1 % of the group's beams is not reported."""
    import numpy as np
    x_rel = dar.cell_log10_range() - 1.0        # R0 = 10 m
    cells = dar.RANGE_CELL_COUNT
    nb = np.zeros((2, cells))
    sb = np.zeros((2, cells))
    # Two fit bins, a million good beams at -0.5..0 decade, mean 0 residual...
    good = (x_rel >= -0.5) & (x_rel < 0.0)
    nb[:, good] = 5e5 / good.sum()
    # ...and 300 junk beams at +0.5 decade reading 20 dB high.
    junk = int(np.argmin(np.abs(x_rel - 0.5)))
    nb[0, junk] = 300
    sb[0, junk] = 300 * 20.0
    a = np.zeros(2)
    r_bin = np.zeros((2, cells))
    groups = [('g', np.ones(2, dtype=bool))]
    without = dar._binned_residuals(nb, sb, a, r_bin, x_rel, groups, 200)
    assert any(abs(m - 20.0) < 1e-6 and n == 300 for _x, n, m in without['all'])
    with_floor = dar._binned_residuals(nb, sb, a, r_bin, x_rel, groups, 200,
                                       dar.MISFIT_MIN_SHARE)
    assert not any(n == 300 for _x, n, m in with_floor['all'])
    assert all(n >= dar.MISFIT_MIN_SHARE * 1e6 for _x, n, _m in with_floor['g'])
    assert dar._misfit_stats(with_floor)[0] == pytest.approx(0.0, abs=1e-9)


def test_accumulation_cache_round_trip(tmp_path, capsys):
    """A second run with --accumulation-cache skips the bag pass and writes the same table."""
    bag = tmp_path / 'mode_bag'
    write_mode_bag(bag)
    out1, out2 = tmp_path / 't1.csv', tmp_path / 't2.csv'
    cache = tmp_path / 'acc.pkl'
    base = [str(bag), '-t', TOPIC, '--by-mode', '--accumulation-cache', str(cache)]
    assert dar.main(base + ['-o', str(out1)]) == 0
    first = capsys.readouterr().out
    assert 'wrote the accumulated statistics to' in first and cache.is_file()
    assert dar.main(base + ['-o', str(out2)]) == 0
    second = capsys.readouterr().out
    assert 'bag pass skipped' in second
    strip = (lambda p: [ln for ln in p.read_text().splitlines()
                        if not ln.startswith('# source_bags')])
    assert strip(out1) == strip(out2)
    # A refit with other fit settings reuses the cache; other accumulation
    # settings (box, bin width, edges) do not.
    assert dar.main(base + ['-o', str(out2), '--misfit-threshold-db', '3']) == 0
    capsys.readouterr()
    with pytest.raises(SystemExit, match='different.*range_angle_edges'):
        dar.main(base + ['-o', str(out2), '--range-angle-edges', 'none'])
    with pytest.raises(SystemExit, match='not a derive-accumulation-cache'):
        (tmp_path / 'junk.pkl').write_bytes(b'\x80\x04N.')
        dar.main([str(bag), '-t', TOPIC, '--by-mode', '-o', str(out2),
                  '--accumulation-cache', str(tmp_path / 'junk.pkl')])


def test_variance_column_measures_the_beam_scatter():
    """The per-(mode, angle bin) variance is the scatter after the range term (noise^2)."""
    b = dar.ModeTableBuilder()
    synth_stream(b, lambda t, h, cur: REF_KEY,
                 lambda inc, r, th, m: lambert_like(inc) - 20.0 * dar_log10(r),
                 noise_db=2.0, seconds=300.0)
    fit = b.finish(REF_KEY)['modes'][REF_KEY]
    for centre, n, _level, var, _rel in fit['angle']:
        if centre > 12.0 and n > 2000:
            # Each 2 deg bin also spans ~0.3 dB of the incidence slope.
            assert var == pytest.approx(4.0, rel=0.15), centre


def test_sonar_info_history_dedupes_and_orders():
    """Heartbeats repeating a stamp dedupe; an out-of-order SonarInfo is inserted in place."""
    h = dar.SonarInfoHistory()
    h.add(100, 'A', ('UNKNOWN', None))
    h.add(100, 'A', ('UNKNOWN', None))
    h.add(300, 'C', ('UNKNOWN', None))
    h.add(200, 'B', ('40LOGR', 80.0))
    assert len(h) == 3
    assert h.at(99) is None
    assert h.at(100)[0] == 'A' and h.at(250)[0] == 'B' and h.at(10 ** 12)[0] == 'C'


def test_pings_before_the_first_sonar_info_are_counted_not_binned():
    """A ping with no SonarInfo at or before it gets no mode."""
    b = dar.ModeTableBuilder()
    p = _Ping()
    p.intensities, p.rx_angles = [-30.0], [0.3]
    p.two_way_travel_times, p.sound_speed, p.frequency, p.flags = [0.02], 1500.0, 5e5, None
    b.add_ping(50, p)
    b.add_sonar_info(60, [0.001], [24000.0], [2])
    b.add_ping(70, p)
    assert b.pings_without_info == 1
    assert list(b.modes) == [REF_KEY] and b.modes[REF_KEY].pings == 1


def test_declared_state_per_mode_and_mixed():
    """The declared TVG state is recorded per mode; two states in one mode are MIXED."""
    b = dar.ModeTableBuilder()
    synth_stream(b, lambda t, h, cur: REF_KEY, lambda inc, r, th, m: lambert_like(inc),
                 seconds=30.0)
    synth_stream(b, lambda t, h, cur: REF_KEY, lambda inc, r, th, m: lambert_like(inc),
                 seconds=30.0, t0=200.5, tvg=(3, 80.0))
    table = b.finish(REF_KEY)
    fit = table['modes'][REF_KEY]
    assert fit['declared_tvg_model'] == 'MIXED'
    assert {(s['model'], s['absorption_db_per_km']) for s in fit['declared_states']} == {
        ('UNKNOWN', None), ('40LOGR', 80.0)}
    assert any('declared TVG states' in w for w in table['warnings'])


def table_header(lines):
    """Map every ``# key: value`` comment of a table to its value (the title is skipped)."""
    out = {}
    for line in lines:
        m = HEADER_LINE.match(line)
        if m and line.startswith('#'):
            out.setdefault(m.group(1), m.group(2))
    return out


def hand_built_table():
    """Return a small deterministic table dict, the shape derive_mode_table returns."""
    nan = float('nan')

    def group(lo, hi, nodes, r, se, support, status='fitted', trimmed=(), out_of_span=0):
        return {'lo_deg': lo, 'hi_deg': hi, 'nodes': nodes, 'r': r, 'r_se': se,
                'r_support': support, 'range_status': status,
                'span': [nodes[0], nodes[-1]] if nodes else [0.0, 0.0],
                'trimmed_nodes': list(trimmed), 'out_of_span_samples': out_of_span}

    def mode(pings, angle, groups, declared=('UNKNOWN', nan), misfit=0.4):
        return {'pings': pings, 'beams': pings * 256, 'angle': angle, 'groups': groups,
                'range_status': dar.range_status_summary(groups),
                'declared_tvg_model': declared[0],
                'declared_tvg_absorption_db_per_km': declared[1],
                'misfit_max_db': misfit, 'misfit_exceeded': misfit > 1.5}
    unidentified = ('zero: not identified, the within-angle-bin spread of log10 R is '
                    '0.010 decade (< 0.03)')
    return {
        'reference_mode': REF_KEY, 'reference_source': 'requested',
        'range_ref_m': 18.9, 'range_ref_source': 'median slant range of the reference mode',
        'bin_width_deg': 2.0, 'specular_exclusion_deg': 10.0, 'window_sec': 2.0,
        'misfit_threshold_db': 1.5, 'range_angle_groups_deg': [(10.0, 30.0), (30.0, 90.0)],
        'modes': {
            REF_KEY: mode(93890, [(1.0, 5000, 25.5, 9.25, 0.0), (11.0, 4000, 24.7, 6.5, -0.8),
                                  (41.0, 3000, 18.3, 7.0, -7.2)],
                          [group(10.0, 30.0, [-0.25, 0.0, 0.25], [1.5, 0.0, -2.25],
                                 [0.1, 0.0, 0.12], [3000.0, 5000.0, 2500.0]),
                           group(30.0, 90.0, [0.0, 0.25], [0.0, -1.0], [0.0, 0.08],
                                 [4000.0, 1500.0], trimmed=(-0.25, 0.5), out_of_span=37)]),
            CW_KEY: mode(217255, [(1.0, 6000, 7.7, 12.0, 0.0), (41.0, 1, 2.1, nan, -5.6)],
                         [group(10.0, 30.0, [], [], [], [], status=unidentified),
                          group(30.0, 90.0, [], [], [], [],
                                status='zero: 120 non-specular samples, fewer than 200')],
                         declared=('40LOGR', 80.0), misfit=2.0),
        },
        'offsets': {REF_KEY: {'offset_db': 0.0, 'se_db': 0.0, 'anchored': True},
                    CW_KEY: {'offset_db': -17.25, 'se_db': 0.31, 'anchored': True}},
    }


def test_mode_table_format():
    """The table: format line first, mode first, angle and range rows, scalars per row."""
    lines = dar.mode_table_lines(hand_built_table(), ['bag_a', 'bag_b'],
                                 box=(42.976, -70.628, 43.002, -70.596))
    assert lines[0] == '# format: angular-response-table v1'
    hd = table_header(lines)
    assert hd['format'] == 'angular-response-table v1'
    assert hd['reference_mode'] == REF_KEY and hd['range_ref_m'] == '18.9'
    assert hd['box'] == '42.976 -70.628 43.002 -70.596'
    assert hd['source_bags'] == 'bag_a,bag_b'
    assert hd['range_angle_groups_deg'] == '10-30,30-90'
    assert 'offset_meaning' in hd and 'range_term_lookup' in hd
    assert 'tl_removed' not in hd and 'absorption_db_per_m' not in hd
    terms = [ln for ln in lines if ln.startswith('# range_term: ')]
    assert terms == ['# range_term: %s 10-30 %s' % (CW_KEY, 'zero: not identified, the '
                     'within-angle-bin spread of log10 R is 0.010 decade (< 0.03)'),
                     '# range_term: %s 30-90 zero: 120 non-specular samples, fewer than 200'
                     % CW_KEY,
                     '# range_term: %s 10-30 fitted nodes=3 span=-0.25..0.25 trimmed=none '
                     'out_of_span=0' % REF_KEY,
                     '# range_term: %s 30-90 fitted nodes=2 span=0..0.25 trimmed=-0.25,0.5 '
                     'out_of_span=37' % REF_KEY]
    rows = [ln for ln in lines if not ln.startswith('#')]
    assert rows[0] == ','.join(dar.TABLE_COLUMNS)
    assert rows[1] == ('CW_50us_20000Hz_500kHz,angle,1.0,,,7.700,6000,12.000,+0.000,'
                       '0.045,-17.250,0.310,true,40LOGR,80')
    assert rows[2] == ('CW_50us_20000Hz_500kHz,angle,41.0,,,2.100,1,,-5.600,,'
                       '-17.250,0.310,true,40LOGR,80')
    assert rows[-3] == ('FMU_1000us_24000Hz_500kHz,range,0.25,10,30,-2.250,2500,,,0.120,'
                        '0.000,0.000,true,UNKNOWN,nan')
    assert rows[-1] == ('FMU_1000us_24000Hz_500kHz,range,0.25,30,90,-1.000,1500,,,0.080,'
                        '0.000,0.000,true,UNKNOWN,nan')
    assert all(len(r.split(',')) == len(dar.TABLE_COLUMNS) for r in rows)
    assert not any(r.split(',')[0] == CW_KEY and r.split(',')[1] == 'range' for r in rows)


def test_mode_table_fixture_for_the_loader_tests():
    """The table fixture the old-loader tests (C++ and the marine_tools mirror) read is current."""
    content = ''.join(ln + '\n' for ln in dar.mode_table_lines(
        hand_built_table(), ['bag_a'], box=(42.976, -70.628, 43.002, -70.596))).encode()
    if os.environ.get('GOLDEN_REGENERATE') == '1':
        MODE_TABLE_FIXTURE.write_bytes(content)
    assert MODE_TABLE_FIXTURE.read_bytes() == content, (
        '%s no longer matches mode_table_lines output; if intended, re-run with '
        'GOLDEN_REGENERATE=1 and check test_angular_response_curve' % MODE_TABLE_FIXTURE)


def _load_marine_tools_mirror():
    """Import kongsberg_em_bridge.angular_response (read-only), or None."""
    try:
        from kongsberg_em_bridge import angular_response
        return angular_response
    except ImportError:
        pass
    path = (HERE.parents[2] / 'marine_tools' / 'kongsberg_em_bridge'
            / 'kongsberg_em_bridge' / 'angular_response.py')
    if not path.is_file():
        return None
    spec = importlib.util.spec_from_file_location('mt_angular_response', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_marine_tools_mirror_ignores_a_mode_table():
    """The SonarInfo-publishing mirror reads a table as an empty curve, never as garbage."""
    mirror = _load_marine_tools_mirror()
    if mirror is None:
        pytest.skip('marine_tools kongsberg_em_bridge is not in this workspace')
    points, tl_removed, alpha = mirror.load_angular_response_curve(str(MODE_TABLE_FIXTURE))
    assert points == []
    assert tl_removed is False and alpha is None


# --- end to end over a bag with SonarInfo ---------------------------------

INFO_TOPIC = '/bizzy/sonar/sonar_info'
MODE_BAG_TOPICS = BAG_TOPICS + ((INFO_TOPIC, 'marine_interfaces/msg/SonarInfo'),)


def _sonar_info(t_sec, mode_key):
    """Return a SonarInfo for one of MODES stamped at t_sec."""
    from marine_interfaces.msg import SonarInfo
    m = MODES[mode_key]
    msg = SonarInfo()
    msg.header.stamp = _stamp(t_sec)
    msg.header.frame_id = 'mbes'
    msg.sonar_model = 'synthetic'
    msg.pulse_lengths = [m['pulse']]
    msg.bandwidths = [m['bw']]
    msg.tx_signal_types = [m['type']]
    msg.tvg_absorption_db_per_km = float('nan')
    return msg


def write_mode_bag(path, with_tf=False):
    """
    Bag alternating two modes every 50 pings at 20 Hz, the second 12 dB brighter.

    The first five pings come before any SonarInfo. Each SonarInfo is stamped
    just before the first ping of its mode but RECORDED 10 ms after it, so a
    ping must be attributed by stamp, not by arrival order. ``with_tf`` adds a
    static mbes frame and an earth transform inside the test box every second.
    """
    inside = geodetic_to_ecef(INSIDE[0], INSIDE[1], 0.0)
    events = []
    if with_tf:
        events.append((99.0, '/bizzy/tf_static', _mbes_static()))
        events += [(99.0 + k, '/bizzy/tf', _earth_tf(99.0 + k, inside)) for k in range(30)]
    for i in range(400):
        ts = 100.0 + 0.05 * i
        mode = CW_KEY if (i // 50) % 2 else REF_KEY
        if i == 5 or (i % 50 == 0 and i):
            events.append((ts + 0.01, INFO_TOPIC, _sonar_info(ts - 0.001, mode)))
        events.append((ts, TOPIC, _ping(ts, 'mbes', -30.0 + (12.0 if mode == CW_KEY else 0.0))))
    events.sort(key=lambda e: e[0])
    write_bag(path, events, MODE_BAG_TOPICS)


def test_end_to_end_by_mode_attributes_pings_by_stamp(tmp_path, capsys):
    """--by-mode over a bag: every ping lands in its own mode, the step is the offset."""
    import json
    bag = tmp_path / 'mode_bag'
    write_mode_bag(bag)
    out = tmp_path / 'table.csv'
    diag = tmp_path / 'diag.json'
    assert dar.main([str(bag), '-t', TOPIC, '-o', str(out), '--by-mode',
                     '--diagnostics-out', str(diag)]) == 0
    stdout = capsys.readouterr().out
    assert '5 pings before the first SonarInfo were left out' in stdout
    lines = out.read_text().splitlines()
    assert lines[0] == '# format: angular-response-table v1'
    summaries = {ln.split()[2]: ln for ln in lines if ln.startswith('# mode_summary:')}
    assert set(summaries) == {REF_KEY, CW_KEY}
    assert ' pings=195 ' in summaries[REF_KEY] and ' pings=200 ' in summaries[CW_KEY]
    assert 'offset_db=12.000' in summaries[CW_KEY]
    d = json.loads(diag.read_text())
    assert d['pings_without_info'] == 5 and d['detections_read'] == 400
    assert d['modes'][CW_KEY]['declared_tvg_model'] == 'UNKNOWN'
    # The old loaders see nothing in the table.
    mirror = _load_marine_tools_mirror()
    if mirror is not None:
        assert mirror.load_angular_response_curve(str(out))[0] == []


def test_end_to_end_by_mode_with_box(tmp_path, capsys):
    """--by-mode --box positions the pings and still attributes every kept one."""
    bag = tmp_path / 'mode_bag_tf'
    write_mode_bag(bag, with_tf=True)
    out = tmp_path / 'table.csv'
    assert dar.main([str(bag), '-t', TOPIC, '-o', str(out), '--by-mode',
                     '--box'] + TEST_BOX) == 0
    stdout = capsys.readouterr().out
    assert 'kept 400 of 400 pings' in stdout
    text = out.read_text()
    assert '# box: 42.95 -70.65 43 -70.58' in text
    assert 'offset_db=12.000' in text


def test_by_mode_without_a_sonar_info_topic_is_an_error(tmp_path):
    """--by-mode needs SonarInfo in at least one bag."""
    bag = tmp_path / 'site_bag'
    write_synthetic_bag(bag)
    with pytest.raises(SystemExit, match='needs the SonarInfo topic'):
        dar.main([str(bag), '-t', TOPIC, '-o', str(tmp_path / 'x.csv'), '--by-mode'])


def test_switch_tracker_gap_discards_the_switch():
    """A switch across a gap wider than the window has no before side."""
    tr = dar.SwitchTracker(int(2e9), 45, dar.RANGE_CELL_COUNT)

    def summary():
        s = dar.OffsetSummary(45, dar.RANGE_CELL_COUNT)
        s.n, s.s, s.pings = 10, -300.0, 1
        return s
    tr.add(int(1e9), 'A', summary())
    tr.add(int(5e9), 'B', summary())          # 4 s gap: discarded
    tr.add(int(5.5e9), 'A', summary())        # adjacent: kept
    tr.add(int(5.4e9), 'A', summary())        # out of order: breaks the run
    tr.finish()
    assert len(tr.events) == 1 and tr.events[0].from_key == 'B'
    assert tr.discarded == {'gap': 1, 'out_of_order': 1}


def test_offset_summary_compact_gives_the_same_level():
    """A compacted (sparse) summary corrects to the same level as the dense one."""
    import numpy as np
    s = dar.OffsetSummary(45, dar.RANGE_CELL_COUNT)
    s.n, s.s, s.pings = 6, -180.0, 2
    s.angle_counts[[5, 20]] = (4, 2)
    s.cell_counts[[110, 111, 300]] = (1, 3, 2)
    rel = np.linspace(0.0, -9.0, 45)
    rng = np.linspace(5.0, -20.0, dar.RANGE_CELL_COUNT)
    dense = s.level(rel, rng)
    s.compact()
    assert isinstance(s.cell_counts, tuple) and len(s.cell_counts[0]) == 3
    assert s.level(rel, rng) == pytest.approx(dense, abs=1e-12)
