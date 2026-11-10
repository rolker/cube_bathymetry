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
import_bag over a synthetic two-mode bag with a per-pulse-mode table (#172).

PR #175 review: the CLI tests ran the table path only over the pier excerpt,
which records no SonarInfo, so they exercised exit 4 and never the main-pass
association -- the topic filter, the pending-ping hold and the TF-driven
drain order. The resolver unit tests feed states by hand and cannot catch a
mistake there. This test writes a bag whose SonarInfo is stamped just before
the first ping of its mode but RECORDED after it, derives the table from that
same bag with derive_angular_response.py --by-mode, imports the bag with the
table, and reads the store back: every corrected cell sits at the reference
mode's level on the fixed path (float backscatter store); the depth-adaptive
path (recon spill + replay) is run over the same bag and checked for the same
resolution outcome and fingerprint, since it persists no backscatter yet.

Runs only when CMake hands over the built binary in ``IMPORT_BAG_EXE``.
"""

import hashlib
import json
import os
import pathlib
import subprocess
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_derive_angular_response as helpers  # noqa: E402  (the synthetic bag + modes)

IMPORT_BAG = os.environ.get('IMPORT_BAG_EXE', '')

# Skip only on a bare `pytest` run with nothing handed over. When CMake DID
# set the variable and the binary is not there (a renamed target, a moved
# output directory), that is a failure, not a quiet skip of every test here.
pytestmark = pytest.mark.skipif(
    not IMPORT_BAG, reason='IMPORT_BAG_EXE unset (CMake sets it to the built import_bag)')


@pytest.fixture(autouse=True)
def _import_bag_is_built():
    assert os.access(IMPORT_BAG, os.X_OK), (
        'IMPORT_BAG_EXE=%r is not an executable: the import_bag target moved or was not '
        'built' % IMPORT_BAG)


REF_LEVEL = -30.0      # the reference mode's nadir level written into every ping
CW_STEP = 12.0         # the second mode reads this much brighter (its offset)
OBLIQUE_DROP = 5.0     # the 31-degree beam reads this much below nadir in both modes


def _derive_table(tmp_path, bag):
    """Derive the per-mode table from ``bag`` as the operator would."""
    table = tmp_path / 'table.csv'
    assert helpers.dar.main([str(bag), '-t', helpers.TOPIC, '-o', str(table), '--by-mode']) == 0
    text = table.read_text()
    assert text.startswith('# format: angular-response-table v1')
    import re
    offsets = {m.group(1): float(m.group(2)) for m in
               re.finditer(r'# mode_summary: (\S+) .* offset_db=(-?[0-9.]+)', text)}
    assert abs(offsets[helpers.CW_KEY] - CW_STEP) < 0.05, offsets   # jitter is 0.2 dB
    assert offsets[helpers.REF_KEY] == 0.0, offsets
    return table


def _write_bag(path, low_from=None, low_db=0.0):
    """
    Write the helpers' two-mode bag with a small deterministic jitter per beam.

    As in ``helpers.write_mode_bag``: each SonarInfo is stamped before, and
    recorded after, the first ping of its mode, and the second mode reads
    12 dB brighter. The jitter is needed because a noise-free stream gives
    the derive tool angle bins with a variance under its 0.0005 dB^2 floor,
    which it writes as unweighable and import_bag then refuses to correct.

    ``low_from`` / ``low_db`` (#176): from ping ``low_from`` on, every ping reads
    ``low_db`` lower, as a receiver whose gain stepped without telling anyone.
    """
    inside = helpers.geodetic_to_ecef(helpers.INSIDE[0], helpers.INSIDE[1], 0.0)
    events = [(99.0, '/bizzy/tf_static', helpers._mbes_static())]
    events += [(99.0 + k, '/bizzy/tf', helpers._earth_tf(99.0 + k, inside)) for k in range(30)]
    for i in range(400):
        ts = 100.0 + 0.05 * i
        mode = helpers.CW_KEY if (i // 50) % 2 else helpers.REF_KEY
        if i == 5 or (i % 50 == 0 and i):
            events.append((ts + 0.01, helpers.INFO_TOPIC, helpers._sonar_info(ts - 0.001, mode)))
        jitter = ((i * 7) % 5 - 2) * 0.1          # -0.2 .. +0.2 dB, zero mean
        level = REF_LEVEL + (CW_STEP if mode == helpers.CW_KEY else 0.0) + jitter
        if low_from is not None and i >= low_from:
            level -= low_db
        events.append((ts, helpers.TOPIC, helpers._ping(ts, 'mbes', level)))
    events.sort(key=lambda e: e[0])
    helpers.write_bag(path, events, helpers.MODE_BAG_TOPICS)


def _run(store, bag, extra):
    """Run import_bag over ``bag`` into ``store``; return (returncode, output)."""
    # The synthetic bag has no attitude or heave frames; naming base_link for
    # both makes those lookups the identity (level, no heave) instead of a
    # missing transform, which the estimator's gate refuses.
    cmd = [IMPORT_BAG, '-o', str(store), '-d', helpers.TOPIC,
           '--sonar-info-topic', helpers.INFO_TOPIC,
           '--base-link-frame', 'base_link', '--level-frame', 'base_link',
           '--tide-frame', 'base_link', '-r', '1.0'] + list(extra) + [str(bag)]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, timeout=300)
    return proc.returncode, proc.stdout


def _table_args(table, fallback='exclude'):
    return ['--backscatter-correction', 'empirical', '--backscatter-curve', str(table),
            '--backscatter-mode-fallback', fallback]


def _band_values(store, band):
    """
    Every finite value of ``band`` (1-based) over the store's tiles, decoded.

    The backscatter store's tiles are 3-band Float32 (mean, standard error,
    sample sd; NaN = no data). Scale/offset/no-data are honoured so a
    quantized band would decode the same way.
    """
    from osgeo import gdal
    gdal.UseExceptions()
    values = []
    for tif in sorted(pathlib.Path(store).rglob('*.tif')):
        ds = gdal.Open(str(tif))
        assert ds is not None, tif
        assert ds.RasterCount >= band, (tif, ds.RasterCount)
        b = ds.GetRasterBand(band)
        raw = b.ReadAsArray()
        nodata = b.GetNoDataValue()
        scale = b.GetScale() if b.GetScale() is not None else 1.0
        offset = b.GetOffset() if b.GetOffset() is not None else 0.0
        for v in raw.flatten():
            if nodata is not None and v == nodata:
                continue
            x = float(v) * scale + offset
            if x == x:   # finite (NaN is the Float32 no-data)
                values.append(x)
        del ds
    return values


def _fingerprint(store):
    return json.loads((pathlib.Path(store) / 'build_fingerprint.json').read_text())


@pytest.fixture(scope='module')
def mode_bag(tmp_path_factory):
    bag = tmp_path_factory.mktemp('bags') / 'mode_bag'
    _write_bag(bag)
    return bag


@pytest.fixture(scope='module')
def table(tmp_path_factory, mode_bag):
    return _derive_table(tmp_path_factory.mktemp('table'), mode_bag)


def test_fixed_path_corrects_every_ping_to_the_reference_level(tmp_path, mode_bag, table):
    """The float backscatter store holds one level: the modes' offset is gone."""
    store = tmp_path / 'store'
    bs_store = tmp_path / 'bs_store'
    rc, out = _run(store, mode_bag, _table_args(table) + ['--bs-store', str(bs_store)])
    assert rc == 0, out
    assert '5 ping(s) before the first SonarInfo: exclude' in out, out
    assert 'done!' in out, out

    means = _band_values(bs_store, 1)
    assert means, 'the backscatter store wrote no cells'
    # Both modes land on the reference mode's nadir level: the second mode's
    # 12 dB step and the 5 dB oblique drop are both removed by the table. The
    # vessel does not move, so every cell averages beams of BOTH modes; the
    # within-cell spread (band 3, the beams' sample sd) is the jitter only.
    assert max(abs(m - REF_LEVEL) for m in means) < 1.0, (min(means), max(means))
    spread = _band_values(bs_store, 3)
    assert spread and max(spread) < 1.0, (min(spread), max(spread))  # jitter is 0.2 dB
    assert 'beams of resolved pings: 790 correctable, 0 not (exclude)' in out, out

    fp = _fingerprint(store)
    bc = fp['backscatter_correction']
    assert bc['kind'] == 'table', bc
    assert bc['fallback'] == 'exclude', bc
    assert bc['sha256'] == hashlib.sha256(table.read_bytes()).hexdigest(), bc


def test_plain_curve_fingerprint_hashes_the_bytes_parsed(tmp_path, mode_bag):
    """The legacy single-curve branch fingerprints the file it applied (one read)."""
    curve = HERE / 'data' / 'derived_curve_tier2_seawater.csv'
    store = tmp_path / 'store'
    rc, out = _run(store, mode_bag, ['--backscatter-correction', 'empirical',
                                     '--backscatter-curve', str(curve)])
    assert rc == 0, out
    bc = _fingerprint(store)['backscatter_correction']
    assert bc['kind'] == 'curve', bc
    assert bc['source'] == str(curve), bc
    assert bc['sha256'] == hashlib.sha256(curve.read_bytes()).hexdigest(), bc


def test_without_the_table_the_modes_stay_apart(tmp_path, mode_bag):
    """Control: the same bag with no correction keeps the 12 dB step inside every cell."""
    store = tmp_path / 'store'
    bs_store = tmp_path / 'bs_store'
    rc, out = _run(store, mode_bag, ['--backscatter-correction', 'none',
                                     '--bs-store', str(bs_store)])
    assert rc == 0, out
    # Each cell averages beams of both modes (the vessel does not move): the
    # means sit between the two levels and the within-cell spread carries the
    # 12 dB step, about 6 dB of sample sd. Neither is anywhere near the
    # corrected run's one level and 0.2 dB jitter.
    means = _band_values(bs_store, 1)
    assert means
    assert max(means) > REF_LEVEL + 3.0, (min(means), max(means))
    spread = _band_values(bs_store, 3)
    assert spread and min(spread) > CW_STEP / 2 - 1.0, (min(spread), max(spread))


def test_depth_adaptive_replay_path_resolves_the_same_way(tmp_path, mode_bag, table):
    """
    The recon pass resolves every ping and the spill replays them.

    The depth-adaptive path writes no backscatter store yet (import_bag
    refuses --bs-store with --depth-adaptive; the per-level backscatter layout
    is uma#383), so the corrected VALUES cannot be read back on this path
    today. What can be checked is that the main-pass association ran over the
    same bag with the same outcome, the spill replayed every sounding, and the
    fingerprint names the table it applied. The index-carrying spill record is
    covered cell-for-cell by test_recon.
    """
    store = tmp_path / 'store'
    scratch = tmp_path / 'scratch'
    scratch.mkdir()
    rc, out = _run(store, mode_bag, _table_args(table) + [
        '--depth-adaptive', '--scratch-dir', str(scratch), '--min-obs-per-node', '1'])
    assert rc == 0, out
    assert '395 of 400 pings resolved, 8 SonarInfo message(s) seen' in out, out
    assert '5 ping(s) before the first SonarInfo: exclude' in out, out
    assert 'beams of resolved pings: 790 correctable, 0 not (exclude)' in out, out
    assert 'Recon pass: 800 soundings counted, 800 spilled' in out, out
    assert 'Replayed 800 soundings' in out, out
    assert list(pathlib.Path(store).rglob('*.tif')), 'the depth-adaptive store wrote no tiles'
    bc = _fingerprint(store)['backscatter_correction']
    assert bc['kind'] == 'table', bc
    assert bc['fallback'] == 'exclude', bc
    assert bc['sha256'] == hashlib.sha256(table.read_bytes()).hexdigest(), bc


# ---------------------------------------------------------------------------
# Gain-state declaration (#176)
# ---------------------------------------------------------------------------

GAIN_STEP_DB = 23.0       # the receiver reads this much LOW inside the step
STEP_START_NS = 110 * 10**9     # ping 200 of the bag (100 s + 200 * 50 ms)
STEP_END_NS = 3600 * 10**9      # past the last ping (120 s)
EPOCH = __import__('datetime').datetime(1970, 1, 1, tzinfo=__import__('datetime').timezone.utc)


def _utc(ns):
    """Return ``ns`` since the epoch as the declaration's UTC stamp text."""
    import datetime
    t = EPOCH + datetime.timedelta(microseconds=ns // 1000)
    text = t.strftime('%Y-%m-%dT%H:%M:%S')
    frac = ns % 10**9
    if frac:
        text += ('.%09d' % frac).rstrip('0')
    return text + 'Z'


def _canonical_sha(reference, segments):
    """SHA-256 of the canonical form (the spec in GainDeclaration::canonicalForm)."""
    text = 'gain-state-declaration v1\nreference_state=%s\n' % reference
    for _id, start, end, offset in sorted(segments, key=lambda s: s[1]):
        text += '%d,%d,%.6f\n' % (start, end, offset)
    return hashlib.sha256(text.encode()).hexdigest()


def _declaration(tmp_path, segments, reference='pre_step', name='gain.csv'):
    """Write a declaration of ``(id, start_ns, end_ns, offset_db)`` segments; return its path."""
    lines = ['# format: gain-state-declaration v1', '# reference_state: %s' % reference,
             'segment,start_utc,end_utc,offset_db,provenance,provenance_note']
    for seg_id, start, end, offset in segments:
        lines.append('%s,%s,%s,%s,measured,synthetic test bag' %
                     (seg_id, _utc(start), _utc(end), offset))
    path = tmp_path / name
    path.write_text('\n'.join(lines) + '\n')
    return path


def _step_declaration(tmp_path, offset=GAIN_STEP_DB, start=STEP_START_NS, end=STEP_END_NS,
                      name='gain.csv'):
    return _declaration(tmp_path, [('gain_step', start, end, offset)], name=name)


def _with_gain_keys(table, tmp_path, sha, reference='pre_step'):
    """Copy ``table`` with the ``# gain_*`` lines a derivation under a declaration writes."""
    lines = table.read_text().split('\n')
    out = [lines[0], '# gain_reference_state: %s' % reference,
           '# gain_declaration_sha256: %s' % sha] + lines[1:]
    path = tmp_path / ('table_gain_%s.csv' % sha[:8])
    path.write_text('\n'.join(out))
    return path


def _spread_and_means(bs_store):
    means = _band_values(bs_store, 1)
    spread = _band_values(bs_store, 3)
    assert means and spread, 'the backscatter store wrote no cells'
    return means, spread


@pytest.fixture(scope='module')
def step_bag(tmp_path_factory):
    """Write the two-mode bag whose second half reads 23 dB low (an unrecorded gain step)."""
    bag = tmp_path_factory.mktemp('stepbag') / 'step_bag'
    _write_bag(bag, low_from=200, low_db=GAIN_STEP_DB)
    return bag


def _write_flat_step_bag(bag):
    """One mode, no SonarInfo, the second half 23 dB low: no table is possible or needed."""
    inside = helpers.geodetic_to_ecef(helpers.INSIDE[0], helpers.INSIDE[1], 0.0)
    events = [(99.0, '/bizzy/tf_static', helpers._mbes_static())]
    events += [(99.0 + k, '/bizzy/tf', helpers._earth_tf(99.0 + k, inside)) for k in range(30)]
    for i in range(400):
        ts = 100.0 + 0.05 * i
        level = REF_LEVEL + ((i * 7) % 5 - 2) * 0.1 - (GAIN_STEP_DB if i >= 200 else 0.0)
        events.append((ts, helpers.TOPIC, helpers._ping(ts, 'mbes', level)))
    helpers.write_bag(bag, sorted(events, key=lambda e: e[0]), helpers.BAG_TOPICS)


@pytest.fixture(scope='module')
def flat_step_bag(tmp_path_factory):
    bag = tmp_path_factory.mktemp('flatbag') / 'flat_bag'
    _write_flat_step_bag(bag)
    return bag


def test_declared_step_restores_reference_level(tmp_path, step_bag, table):
    """With the declaration the table's output is in the reference state, step or no step."""
    decl = _step_declaration(tmp_path)
    sha = _canonical_sha('pre_step', [('gain_step', STEP_START_NS, STEP_END_NS, GAIN_STEP_DB)])
    gtable = _with_gain_keys(table, tmp_path, sha)
    bs_store = tmp_path / 'bs_store'
    rc, out = _run(tmp_path / 'store', step_bag, _table_args(gtable) + [
        '--backscatter-gain-declaration', str(decl), '--bs-store', str(bs_store)])
    assert rc == 0, out
    means, spread = _spread_and_means(bs_store)
    assert max(abs(m - REF_LEVEL) for m in means) < 1.0, (min(means), max(means))
    assert max(spread) < 1.0, (min(spread), max(spread))
    assert ('gain_step [1970-01-01T00:01:50Z, 1970-01-01T01:00:00Z) +23.000 dB (measured): 200'
            in out), out
    assert 'undeclared (at reference): 200 pings' in out, out

    # Control: the same bag without the declaration shows the 23 dB seam, so
    # this test can fail.
    bs_control = tmp_path / 'bs_control'
    rc, out = _run(tmp_path / 'store_control', step_bag,
                   _table_args(table) + ['--bs-store', str(bs_control)])
    assert rc == 0, out
    means, spread = _spread_and_means(bs_control)
    assert min(spread) > GAIN_STEP_DB / 2 - 2.0, (min(spread), max(spread))
    assert min(means) < REF_LEVEL - GAIN_STEP_DB / 2 + 2.0, (min(means), max(means))


def test_wrong_sign_would_double_the_step(tmp_path, step_bag, table):
    """A declaration with the sign flipped makes the seam ~46 dB: why the sign is pinned."""
    decl = _step_declaration(tmp_path, offset=-GAIN_STEP_DB)
    sha = _canonical_sha('pre_step', [('gain_step', STEP_START_NS, STEP_END_NS, -GAIN_STEP_DB)])
    gtable = _with_gain_keys(table, tmp_path, sha)
    bs_store = tmp_path / 'bs_store'
    rc, out = _run(tmp_path / 'store', step_bag, _table_args(gtable) + [
        '--backscatter-gain-declaration', str(decl), '--bs-store', str(bs_store)])
    assert rc == 0, out
    _means, spread = _spread_and_means(bs_store)
    assert min(spread) > 2 * GAIN_STEP_DB / 2 - 2.0, (min(spread), max(spread))   # ~32 dB sd


@pytest.mark.parametrize('correction', [[], ['--backscatter-correction', 'none']])
def test_declaration_without_table_shifts(tmp_path, flat_step_bag, correction):
    """The shift is a receiver calibration: it needs no table, no curve and no resolved mode."""
    decl = _step_declaration(tmp_path)
    bs_store = tmp_path / 'bs_store'
    rc, out = _run(tmp_path / 'store', flat_step_bag, correction + [
        '--backscatter-gain-declaration', str(decl), '--bs-store', str(bs_store)])
    assert rc == 0, out
    means, spread = _spread_and_means(bs_store)
    # No table: the nadir cell reads REF_LEVEL and the oblique one 5 dB below,
    # in both halves of the bag.
    assert all(min(abs(m - REF_LEVEL), abs(m - REF_LEVEL + OBLIQUE_DROP)) < 1.0
               for m in means), (min(means), max(means))
    assert max(spread) < 1.0, (min(spread), max(spread))
    # Control: no declaration, the seam is there.
    bs_control = tmp_path / 'bs_control'
    rc, out = _run(tmp_path / 'store_control', flat_step_bag,
                   correction + ['--bs-store', str(bs_control)])
    assert rc == 0, out
    _means, spread = _spread_and_means(bs_control)
    assert min(spread) > GAIN_STEP_DB / 2 - 2.0, (min(spread), max(spread))


def test_stamp_equal_to_start_ns_is_shifted(tmp_path):
    """
    The half-open boundary through the real import path.

    Ten pings, the last stamped one nanosecond BEFORE the segment start; ten
    more, the first stamped exactly AT it. The report must count 10 and 10,
    and the cell holding all twenty must average (10 * -30 + 10 * -7) / 20.
    """
    inside = helpers.geodetic_to_ecef(helpers.INSIDE[0], helpers.INSIDE[1], 0.0)
    events = [(99.0, '/bizzy/tf_static', helpers._mbes_static())]
    events += [(99.0 + k, '/bizzy/tf', helpers._earth_tf(99.0 + k, inside)) for k in range(30)]
    from builtin_interfaces.msg import Time
    for i in range(20):
        stamp_ns = STEP_START_NS + (i - 10) * 50 * 10**6        # i == 10 is AT the start
        if i == 9:
            stamp_ns = STEP_START_NS - 1
        msg = helpers._ping(0.0, 'mbes', REF_LEVEL + ((i * 7) % 5 - 2) * 0.1)
        msg.header.stamp = Time(sec=stamp_ns // 10**9, nanosec=stamp_ns % 10**9)
        events.append((stamp_ns / 1e9, helpers.TOPIC, msg))
    bag = tmp_path / 'edge_bag'
    helpers.write_bag(bag, sorted(events, key=lambda e: e[0]), helpers.BAG_TOPICS)
    decl = _step_declaration(tmp_path)
    bs_store = tmp_path / 'bs_store'
    rc, out = _run(tmp_path / 'store', bag, [
        '--backscatter-correction', 'none', '--backscatter-gain-declaration', str(decl),
        '--bs-store', str(bs_store)])
    assert rc == 0, out
    assert 'gain_step' in out and ': 10 pings' in out, out
    assert 'undeclared (at reference): 10 pings' in out, out
    means, _spread = _spread_and_means(bs_store)
    expected = (10 * REF_LEVEL + 10 * (REF_LEVEL + GAIN_STEP_DB)) / 20.0     # beam 0 cell
    assert any(abs(m - expected) < 0.3 for m in means), (expected, sorted(means))


def test_declaration_report_counts_and_warns_on_empty_segment(tmp_path, flat_step_bag):
    """A segment stamped a day late catches no ping: the report says so, loudly."""
    late = 86400 * 10**9
    decl = _declaration(tmp_path, [('first', STEP_START_NS, STEP_END_NS, GAIN_STEP_DB),
                                   ('late', STEP_END_NS + late, STEP_END_NS + 2 * late, 5.0)])
    rc, out = _run(tmp_path / 'store', flat_step_bag, [
        '--backscatter-correction', 'none', '--backscatter-gain-declaration', str(decl)])
    assert rc == 0, out
    assert ': 200 pings' in out and 'undeclared (at reference): 200 pings' in out, out
    assert '(measured): 0 pings' in out, out
    assert "segment 'late' is declared but no ping fell in it" in out, out


def test_single_curve_without_gain_header_refused(tmp_path, flat_step_bag):
    """A curve with no gain keys, run under a declaration, may have been fitted across the step."""
    curve = HERE / 'data' / 'derived_curve_tier2_seawater.csv'
    decl = _step_declaration(tmp_path)
    rc, out = _run(tmp_path / 'store', flat_step_bag, [
        '--backscatter-correction', 'empirical', '--backscatter-curve', str(curve),
        '--backscatter-gain-declaration', str(decl)])
    assert rc != 0, out
    assert 'records no gain-state declaration' in out, out
    assert not (tmp_path / 'store' / 'build_fingerprint.json').exists()


def test_single_curve_with_matching_gain_header_accepted(tmp_path, flat_step_bag):
    sha = _canonical_sha('pre_step', [('gain_step', STEP_START_NS, STEP_END_NS, GAIN_STEP_DB)])
    curve = tmp_path / 'curve.csv'
    base = (HERE / 'data' / 'derived_curve_tier2_seawater.csv').read_text()
    curve.write_text('# gain_reference_state: pre_step\n# gain_declaration_sha256: %s\n%s' %
                     (sha, base))
    decl = _step_declaration(tmp_path)
    rc, out = _run(tmp_path / 'store', flat_step_bag, [
        '--backscatter-correction', 'empirical', '--backscatter-curve', str(curve),
        '--backscatter-gain-declaration', str(decl)])
    assert rc == 0, out
    # ... and the same curve with NO declaration is refused: it was fitted under one.
    rc, out = _run(tmp_path / 'store2', flat_step_bag, [
        '--backscatter-correction', 'empirical', '--backscatter-curve', str(curve)])
    assert rc != 0, out
    assert 'no gain-state declaration was given' in out, out


def test_auto_with_declaration_warns(tmp_path):
    """The SonarInfo-delivered curve records no gain state: say so, loudly."""
    from marine_interfaces.msg import SonarInfo
    inside = helpers.geodetic_to_ecef(helpers.INSIDE[0], helpers.INSIDE[1], 0.0)
    info = helpers._sonar_info(99.5, helpers.REF_KEY)
    info.angular_response_angle_deg = [0.0, 60.0]
    info.angular_response_db_rel_nadir = [0.0, -3.0]
    info.angular_response_tl = SonarInfo.ANGULAR_RESPONSE_TL_IN
    events = [(99.0, '/bizzy/tf_static', helpers._mbes_static()),
              (99.5, helpers.INFO_TOPIC, info)]
    events += [(99.0 + k, '/bizzy/tf', helpers._earth_tf(99.0 + k, inside)) for k in range(30)]
    events += [(100.0 + 0.05 * i, helpers.TOPIC, helpers._ping(100.0 + 0.05 * i, 'mbes', -30.0))
               for i in range(40)]
    bag = tmp_path / 'auto_bag'
    helpers.write_bag(bag, sorted(events, key=lambda e: e[0]), helpers.MODE_BAG_TOPICS)
    decl = _step_declaration(tmp_path)
    rc, out = _run(tmp_path / 'store', bag, ['--backscatter-gain-declaration', str(decl)])
    assert rc == 0, out
    assert 'its gain state is UNKNOWN' in out, out


def test_table_without_declaration_hash_refused(tmp_path, mode_bag, table):
    """A pre-#176 table (no gain keys) under a declaration is refused before any bag is read."""
    decl = _step_declaration(tmp_path)
    store = tmp_path / 'store'
    rc, out = _run(store, mode_bag, _table_args(table) + [
        '--backscatter-gain-declaration', str(decl)])
    assert rc != 0, out
    assert 'records no gain-state declaration' in out, out
    assert 'zero-segment declaration' in out, out
    assert 'calculating total time' not in out, out
    assert not store.exists() or not (store / 'build_fingerprint.json').exists()


def test_hash_mismatch_refused(tmp_path, mode_bag, table):
    """A table fitted under another declaration (or reference label) is refused, both named."""
    decl = _step_declaration(tmp_path)
    other = _canonical_sha('pre_step', [('gain_step', STEP_START_NS, STEP_END_NS, 22.0)])
    for sha, ref in ((other, 'pre_step'),
                     (_canonical_sha('pre_step', [('gain_step', STEP_START_NS, STEP_END_NS,
                                                  GAIN_STEP_DB)]), 'other_state')):
        gtable = _with_gain_keys(table, tmp_path, sha, reference=ref)
        rc, out = _run(tmp_path / 'store', mode_bag, _table_args(gtable) + [
            '--backscatter-gain-declaration', str(decl)])
        assert rc != 0, out
        assert 'they are different corrections' in out, out
        assert sha in out, out


def test_table_with_declaration_but_none_supplied_refused(tmp_path, mode_bag, table):
    sha = _canonical_sha('pre_step', [('gain_step', STEP_START_NS, STEP_END_NS, GAIN_STEP_DB)])
    gtable = _with_gain_keys(table, tmp_path, sha)
    rc, out = _run(tmp_path / 'store', mode_bag, _table_args(gtable))
    assert rc != 0, out
    assert 'no gain-state declaration was given' in out, out


def test_zero_segment_declaration_is_the_escape_hatch(tmp_path, mode_bag, table):
    """A season that never crossed a step imports with a zero-segment declaration."""
    decl = _declaration(tmp_path, [])
    gtable = _with_gain_keys(table, tmp_path, _canonical_sha('pre_step', []))
    rc, out = _run(tmp_path / 'store', mode_bag, _table_args(gtable) + [
        '--backscatter-gain-declaration', str(decl)])
    assert rc == 0, out
    assert 'declares only the reference state' in out, out
    assert 'undeclared (at reference): 400 pings' in out, out


def test_declaration_that_is_not_one_is_refused(tmp_path, mode_bag, table):
    rc, out = _run(tmp_path / 'store', mode_bag, _table_args(table) + [
        '--backscatter-gain-declaration', str(table)])
    assert rc != 0, out
    assert 'is a per-pulse-mode table' in out, out


def test_declaration_handed_as_the_curve_is_refused_naming_the_flag(tmp_path, flat_step_bag):
    decl = _step_declaration(tmp_path)
    rc, out = _run(tmp_path / 'store', flat_step_bag, [
        '--backscatter-correction', 'empirical', '--backscatter-curve', str(decl),
        '--backscatter-gain-declaration', str(decl)])
    assert rc != 0, out
    assert "--backscatter-curve" in out and 'is a gain-state declaration' in out, out
    assert '--backscatter-gain-declaration takes the declaration' in out, out
    assert not (tmp_path / 'store' / 'build_fingerprint.json').exists()


def test_derived_table_under_the_declaration_imports_end_to_end(tmp_path, step_bag):
    """
    The whole chain: derive across the step with the declaration, import with it.

    The table comes from the step bag itself (half of it 23 dB low), so it is only
    right if the derive tool shifted before fitting; the import then shifts the same
    pings, and the digest the Python tool wrote is the one the C++ import recomputes.
    """
    decl = _step_declaration(tmp_path)
    table = tmp_path / 'table.csv'
    assert helpers.dar.main([str(step_bag), '-t', helpers.TOPIC, '-o', str(table), '--by-mode',
                             '--gain-declaration', str(decl)]) == 0
    text = table.read_text()
    sha = _canonical_sha('pre_step', [('gain_step', STEP_START_NS, STEP_END_NS, GAIN_STEP_DB)])
    assert '# gain_declaration_sha256: %s' % sha in text
    bs_store = tmp_path / 'bs_store'
    rc, out = _run(tmp_path / 'store', step_bag, _table_args(table) + [
        '--backscatter-gain-declaration', str(decl), '--bs-store', str(bs_store)])
    assert rc == 0, out
    means, spread = _spread_and_means(bs_store)
    assert max(abs(m - REF_LEVEL) for m in means) < 1.0, (min(means), max(means))
    assert max(spread) < 1.0, (min(spread), max(spread))
    # The same table without the declaration is refused: it was fitted under one.
    rc, out = _run(tmp_path / 'store2', step_bag, _table_args(table))
    assert rc != 0, out
    assert 'no gain-state declaration was given' in out, out


def test_fingerprint_records_the_declaration_fixed_and_depth_adaptive(tmp_path, flat_step_bag):
    """Schema 3: the reference state and canonical digest, null without a declaration."""
    decl = _step_declaration(tmp_path)
    sha = _canonical_sha('pre_step', [('gain_step', STEP_START_NS, STEP_END_NS, GAIN_STEP_DB)])
    store = tmp_path / 'store'
    rc, out = _run(store, flat_step_bag, ['--backscatter-gain-declaration', str(decl)])
    assert rc == 0, out
    fp = _fingerprint(store)
    assert fp['schema_version'] == 3, fp
    assert fp['gain_declaration'] == {'reference_state': 'pre_step', 'sha256': sha,
                                      'source': str(decl)}, fp

    scratch = tmp_path / 'scratch'
    scratch.mkdir()
    adaptive = tmp_path / 'adaptive'
    rc, out = _run(adaptive, flat_step_bag, [
        '--backscatter-gain-declaration', str(decl), '--depth-adaptive',
        '--scratch-dir', str(scratch), '--min-obs-per-node', '1'])
    assert rc == 0, out
    assert _fingerprint(adaptive)['gain_declaration']['sha256'] == sha

    plain = tmp_path / 'plain'
    rc, out = _run(plain, flat_step_bag, [])
    assert rc == 0, out
    assert _fingerprint(plain)['gain_declaration'] is None
