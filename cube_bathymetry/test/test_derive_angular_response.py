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
    A 150 s bag whose header stamps run ``offset_sec`` ahead of its bag times.

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
    """A transform stamped further ahead of its bag time than its frame moves the frontier only to that time."""
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
