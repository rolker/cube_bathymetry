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

pytestmark = pytest.mark.skipif(
    not (IMPORT_BAG and os.access(IMPORT_BAG, os.X_OK)),
    reason='IMPORT_BAG_EXE is not an executable (CMake sets it to the built import_bag)')

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


def _write_bag(path):
    """
    Write the helpers' two-mode bag with a small deterministic jitter per beam.

    As in ``helpers.write_mode_bag``: each SonarInfo is stamped before, and
    recorded after, the first ping of its mode, and the second mode reads
    12 dB brighter. The jitter is needed because a noise-free stream gives
    the derive tool angle bins with a variance under its 0.0005 dB^2 floor,
    which it writes as unweighable and import_bag then refuses to correct.
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
                          text=True, timeout=600)
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
