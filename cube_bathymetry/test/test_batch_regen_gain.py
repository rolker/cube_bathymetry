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
batch_regen_bag and the gain-state declaration (#176).

A regen must never rebuild tiles uncorrected inside a corrected store, so
batch_regen takes the same declaration as import_bag and applies it at the
same point. batch_regen has no SonarInfo source and refuses a per-mode table,
so the paths it shares with import_bag are ``--backscatter-correction none``
and a single-curve CSV; the parity checks run on those.

Runs only when CMake hands over the built binaries in ``BATCH_REGEN_EXE`` and
``IMPORT_BAG_EXE``.
"""

import os
import pathlib
import subprocess
import sys

import pytest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_import_bag_mode_table as ib  # noqa: E402  (the synthetic bags and declaration helpers)

helpers = ib.helpers
BATCH_REGEN = os.environ.get('BATCH_REGEN_EXE', '')

pytestmark = pytest.mark.skipif(
    not (BATCH_REGEN and ib.IMPORT_BAG),
    reason='BATCH_REGEN_EXE / IMPORT_BAG_EXE unset (CMake sets them to the built tools)')


@pytest.fixture(autouse=True)
def _tools_are_built():
    assert os.access(BATCH_REGEN, os.X_OK), (
        'BATCH_REGEN_EXE=%r is not an executable: the batch_regen_bag target moved or was not '
        'built' % BATCH_REGEN)
    assert os.access(ib.IMPORT_BAG, os.X_OK), ib.IMPORT_BAG


def _regen(store, bag, extra):
    """Run batch_regen_bag over ``bag`` into ``store``; return (returncode, output)."""
    cmd = [BATCH_REGEN, '-o', str(store), '-d', helpers.TOPIC,
           '--base-link-frame', 'base_link', '--level-frame', 'base_link',
           '--tide-frame', 'base_link', '-r', '1.0'] + list(extra) + [str(bag)]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, timeout=300)
    return proc.returncode, proc.stdout


@pytest.fixture(scope='module')
def flat_step_bag(tmp_path_factory):
    bag = tmp_path_factory.mktemp('flatbag') / 'flat_bag'
    ib._write_flat_step_bag(bag)
    return bag


def _curve_with_gain_keys(tmp_path, sha=None, reference='pre_step'):
    base = (HERE / 'data' / 'derived_curve_tier2_seawater.csv').read_text()
    curve = tmp_path / 'curve.csv'
    keys = ''
    if sha is not None:
        keys = '# gain_reference_state: %s\n# gain_declaration_sha256: %s\n' % (reference, sha)
    curve.write_text(keys + base)
    return curve


def _step_sha():
    return ib._canonical_sha(
        'pre_step', [('gain_step', ib.STEP_START_NS, ib.STEP_END_NS, ib.GAIN_STEP_DB)])


def test_declaration_without_curve_shifts_and_reports(tmp_path, flat_step_bag):
    """The shift needs no curve; the 23 dB seam is gone and the per-segment counts print."""
    decl = ib._step_declaration(tmp_path)
    bs_store = tmp_path / 'bs'
    rc, out = _regen(tmp_path / 'store', flat_step_bag, [
        '--backscatter-gain-declaration', str(decl), '--bs-store', str(bs_store)])
    assert rc == 0, out
    means, spread = ib._spread_and_means(bs_store)
    assert all(min(abs(m - ib.REF_LEVEL), abs(m - ib.REF_LEVEL + ib.OBLIQUE_DROP)) < 1.0
               for m in means), (min(means), max(means))
    assert max(spread) < 1.0, (min(spread), max(spread))
    assert 'gain_step' in out and ': 200 pings' in out, out
    assert 'undeclared (at reference): 200 pings' in out, out

    # Control: without it the seam is in the rebuilt tiles.
    control = tmp_path / 'bs_control'
    rc, out = _regen(tmp_path / 'store_control', flat_step_bag, ['--bs-store', str(control)])
    assert rc == 0, out
    _means, spread = ib._spread_and_means(control)
    assert min(spread) > ib.GAIN_STEP_DB / 2 - 2.0, (min(spread), max(spread))


@pytest.mark.parametrize('use_curve', [False, True])
def test_bit_exact_parity_with_import_bag(tmp_path, flat_step_bag, use_curve):
    """Same bag, same declaration, same correction: the two tools write the same backscatter."""
    decl = ib._step_declaration(tmp_path)
    extra = ['--backscatter-gain-declaration', str(decl)]
    if use_curve:
        curve = _curve_with_gain_keys(tmp_path, _step_sha())
        extra += ['--backscatter-correction', 'empirical', '--backscatter-curve', str(curve)]
    else:
        extra += ['--backscatter-correction', 'none']
    imported = tmp_path / 'bs_import'
    regen = tmp_path / 'bs_regen'
    rc, out = ib._run(tmp_path / 'store_import', flat_step_bag,
                      extra + ['--bs-store', str(imported)])
    assert rc == 0, out
    rc, out = _regen(tmp_path / 'store_regen', flat_step_bag, extra + ['--bs-store', str(regen)])
    assert rc == 0, out
    for band in (1, 2, 3):
        a = sorted(ib._band_values(imported, band))
        b = sorted(ib._band_values(regen, band))
        assert a and a == b, (band, a[:3], b[:3])


def test_curve_without_gain_header_refused(tmp_path, flat_step_bag):
    decl = ib._step_declaration(tmp_path)
    curve = _curve_with_gain_keys(tmp_path)         # no gain keys
    rc, out = _regen(tmp_path / 'store', flat_step_bag, [
        '--backscatter-correction', 'empirical', '--backscatter-curve', str(curve),
        '--backscatter-gain-declaration', str(decl)])
    assert rc != 0, out
    assert 'records no gain-state declaration' in out, out
    assert 'reading messages' not in out, out


def test_curve_fitted_under_a_declaration_refused_without_one(tmp_path, flat_step_bag):
    curve = _curve_with_gain_keys(tmp_path, _step_sha())
    rc, out = _regen(tmp_path / 'store', flat_step_bag, [
        '--backscatter-correction', 'empirical', '--backscatter-curve', str(curve)])
    assert rc != 0, out
    assert 'no gain-state declaration was given' in out, out


def test_curve_fitted_under_another_declaration_refused(tmp_path, flat_step_bag):
    decl = ib._step_declaration(tmp_path)
    other = ib._canonical_sha(
        'pre_step', [('gain_step', ib.STEP_START_NS, ib.STEP_END_NS, 22.0)])
    curve = _curve_with_gain_keys(tmp_path, other)
    rc, out = _regen(tmp_path / 'store', flat_step_bag, [
        '--backscatter-correction', 'empirical', '--backscatter-curve', str(curve),
        '--backscatter-gain-declaration', str(decl)])
    assert rc != 0, out
    assert 'they are different corrections' in out, out


def test_a_per_mode_table_is_still_refused(tmp_path, flat_step_bag):
    decl = ib._step_declaration(tmp_path)
    rc, out = _regen(tmp_path / 'store', flat_step_bag, [
        '--backscatter-correction', 'empirical',
        '--backscatter-curve', str(HERE / 'data' / 'angular_response_table_v1.csv'),
        '--backscatter-gain-declaration', str(decl)])
    assert rc != 0, out
    assert 'per-pulse-mode table' in out, out


def test_a_bad_declaration_is_refused_before_any_bag_is_read(tmp_path, flat_step_bag):
    bad = tmp_path / 'bad.csv'
    bad.write_text('# format: gain-state-declaration v1\n# reference_state: r\n'
                   'segment,start_utc,end_utc,offset_db,provenance,provenance_note\n'
                   'a,1970-01-01T00:00:00Z,1970-01-02T00:00:00Z,5.0,guessed,n\n')
    rc, out = _regen(tmp_path / 'store', flat_step_bag, ['--backscatter-gain-declaration', str(bad)])
    assert rc != 0, out
    assert 'could not be loaded' in out and 'reading messages' not in out, out
