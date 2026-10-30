#!/usr/bin/env python3
# Copyright 2025 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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

r"""
Derive an empirical backscatter angular-response curve from a bag.

Reads a marine_acoustic_msgs/SonarDetections topic, bins each beam's
``intensities`` (dB) by ``|rx_angles|`` (converted to degrees) into fixed-width
bins, and writes a CSV with the columns consumed by the CUBE estimator's
empirical ARA correction (cube_bathymetry#81):

    abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir

``db_relative_to_nadir`` is ``mean_bs_db - nadir_bin_mean_bs_db`` so the nadir
bin is ~0 and off-nadir bins are <= 0; the estimator subtracts this column from
each beam's raw dB. NaN/inf intensities and beams flagged bad are skipped.

Tier-2 (cube_bathymetry#87): with ``--remove-tl`` the per-beam 2-way
transmission loss ``TL = 40*log10(R) + 2*alpha*R`` (R = ``twtt*c/2`` per beam)
is compensated -- ADDED BACK -- for each beam BEFORE binning (a distant return
lost more energy, so it is boosted to recover range-independent backscatter,
TVG-style), so the curve becomes a TL-removed residual that is depth/range
transferable. The CSV columns are unchanged; the header records
``tl_removed``/``absorption_db_per_m`` so the C++ estimator applies the
identical TL (and never recomputes alpha).

The absorption alpha (cube_bathymetry#170) is either

* computed with the full Francois-Garrison (1982) model -- boric acid, MgSO4
  and pure water -- from ``--water-temp-c``, ``--salinity`` (psu, default 0 =
  fresh water), ``--ph`` (default 8.0), ``--depth-m`` and the median per-ping
  frequency (``absorption_source: computed``); or
* given directly with ``--absorption-db-per-m``, e.g. a path-averaged value
  worked out from the site's CTD / sound-speed casts
  (``absorption_source: override``).

Take T and S from casts at the site, not from the boat's surface temperature
alone; in salt water salinity is the dominant term.

Provenance (cube_bathymetry#170): the header also records how alpha was
obtained (``absorption_source``, ``salinity_psu``, ``ph``, ``depth_m``,
``frequency_khz``) and ``source_bags``, the comma-separated basenames of every
input bag, so a per-site curve can be traced to that site's data. All header
lines are ``# key: value`` comments; the C++ loader reads only ``tl_removed``
and ``absorption_db_per_m`` and ignores the rest.

Site box (cube_bathymetry#170): with ``--box LATMIN LONMIN LATMAX LONMAX``
(degrees, WGS84) only pings whose sensor position lies inside the box are
binned, so a per-site curve can be derived from bags that mix survey and
transit. Each ping is positioned from the bags' own TF tree the way import_bag
georeferences it:

* every bag on the command line is read in ONE time-ordered pass (merged on the
  bag timestamp) into ONE tf2 buffer, so a ``/tf_static`` recorded only in the
  first split file still serves the later ones. ``/tf`` and ``/tf_static`` from
  every namespace go into that buffer; in a bag recording several vehicles
  their trees share it, so their frame names must not collide (import_bag
  behaves the same);
* each ping waits until the TF stream has advanced 3 s past its stamp, so the
  transforms on both sides of it are in the buffer, then ``earth ->
  <header.frame_id>`` is looked up exactly at the ping stamp, interpolated
  between them. There is no fallback to the latest transform: a ping outside
  the earth TF coverage (before the first earth transform, after the last, or
  in a gap wider than the 60 s buffer) is dropped, and so is a ping with an
  unset (zero) stamp, which tf2 would read as "the latest transform". Pings
  still waiting when the bags end are looked up against what was read.

The ECEF translation of the sensor origin is converted to latitude/longitude in
closed form; a translation that is not a geocentric position (more than 50 km
from the ellipsoid) drops the ping. Pings outside the box and pings that cannot be positioned are
dropped, counted by reason and reported, and the box is recorded in the header.
Without ``--box`` no TF is read.

Usage:
    derive_angular_response.py <bag> [<bag> ...] -t <topic> -o <out.csv>
    derive_angular_response.py <bag> ... -t <topic> -o <out.csv> \\
        --remove-tl --water-temp-c 24.0
    derive_angular_response.py <bag> ... -t <topic> -o <out.csv> \\
        --remove-tl --water-temp-c 15.5 --salinity 31
    derive_angular_response.py <bag> ... -t <topic> -o <out.csv> \\
        --remove-tl --absorption-db-per-m 0.130
    derive_angular_response.py <bag> ... -t <topic> -o <out.csv> \\
        --box 42.95 -70.65 43.00 -70.58 --remove-tl --water-temp-c 15.5 --salinity 31
"""

import argparse
import collections
import heapq
import math
import os
import stat
import sys
import tempfile

DEFAULT_PH = 8.0

# Plausibility bounds for the absorption inputs. They are wider than the
# Francois-Garrison fit's validity range (T -2..22 C, S 30..35 psu, pH 7.7..8.3)
# so lakes, estuaries and warm water still compute, but narrow enough to catch a
# unit slip -- degF for degC, a dropped decimal point in a salinity or pH.
TEMP_C_RANGE = (-2.0, 40.0)
SALINITY_PSU_RANGE = (0.0, 50.0)
PH_RANGE = (6.0, 10.0)
DEPTH_M_RANGE = (0.0, 12000.0)
# An explicit alpha above this is almost certainly dB/km typed as dB/m: pure
# water at 2 MHz and 10 C is ~1.2 dB/m, and multibeam echosounders run lower.
MAX_ABSORPTION_DB_PER_M = 5.0
# Plausible median ping_info.frequency for the computed alpha, kHz. Multibeam
# and single-beam echosounders run from ~12 kHz to ~1.5 MHz; a value outside
# this is a unit slip (kHz stored as Hz gives a near-zero alpha) or garbage.
FREQUENCY_KHZ_RANGE = (1.0, 2000.0)

# A bin mean from fewer samples than this is noise. The same threshold as
# marine_perception_tools' cube_lab self-calibration (kMinPerBin), which refuses
# a curve whose nadir reference bin is below it or which has fewer than
# MIN_POPULATED_BINS bins at or above it. This tool warns on those conditions
# instead and still writes the curve (every row carries its n).
MIN_SAMPLES_PER_BIN = 200
MIN_POPULATED_BINS = 3

# WGS84 ellipsoid (semi-major axis a, flattening f).
WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_B = WGS84_A * (1.0 - WGS84_F)
WGS84_E2 = WGS84_F * (2.0 - WGS84_F)                       # first eccentricity^2
WGS84_EP2 = (WGS84_A ** 2 - WGS84_B ** 2) / WGS84_B ** 2   # second eccentricity^2

# --box TF lookups, the same constants as import_bag (kCacheWindowSec,
# kGuardSec). The bags are read in time order, so the buffer only has to hold
# the transforms bracketing the pings waiting to be positioned; a ping waits
# until the dynamic TF stream has advanced TF_GUARD_SEC past its stamp, so the
# transform after it is in the buffer too, and is then looked up exactly.
TF_CACHE_SEC = 60.0
TF_GUARD_SEC = 3.0
EARTH_FRAME = 'earth'
# Ellipsoid heights a ping's sensor can plausibly have. A survey sensor is
# within a few km of the ellipsoid; an ``earth`` frame that is not geocentric
# (a local map frame named earth, a placeholder at the origin) puts it thousands
# of km below it, and its latitude/longitude would be meaningless.
EARTH_HEIGHT_RANGE_M = (-50000.0, 50000.0)


def check_absorption_inputs(temp_c, salinity_psu, ph, depth_m):
    """Raise ValueError naming the first non-finite or implausible input."""
    for name, value, (lo, hi) in (
            ('--water-temp-c', temp_c, TEMP_C_RANGE),
            ('--salinity', salinity_psu, SALINITY_PSU_RANGE),
            ('--ph', ph, PH_RANGE),
            ('--depth-m', depth_m, DEPTH_M_RANGE)):
        if not math.isfinite(value) or not lo <= value <= hi:
            raise ValueError('%s %r is outside the plausible range [%g, %g]'
                             % (name, value, lo, hi))


def ecef_to_geodetic(x, y, z):
    """
    Convert WGS84 ECEF metres to ``(lat_deg, lon_deg, height_m)``, closed form.

    Heikkinen's exact closed-form solution (Zhu 1993, "Conversion of
    Earth-centered Earth-fixed coordinates to geodetic coordinates"); no
    iteration and no pyproj. Accurate to well under a millimetre at the
    Earth's surface. On the polar axis (p = 0) it returns lat = +/-90 directly.
    """
    a = WGS84_A
    b = WGS84_B
    e2 = WGS84_E2
    p = math.hypot(x, y)
    lon = math.degrees(math.atan2(y, x))
    if p < 1e-9:
        lat = 90.0 if z >= 0.0 else -90.0
        return lat, lon, abs(z) - b
    z2 = z * z
    big_f = 54.0 * b * b * z2
    g = p * p + (1.0 - e2) * z2 - e2 * (a * a - b * b)
    c = e2 * e2 * big_f * p * p / (g ** 3)
    s = (1.0 + c + math.sqrt(c * c + 2.0 * c)) ** (1.0 / 3.0)
    k = s + 1.0 + 1.0 / s
    big_p = big_f / (3.0 * k * k * g * g)
    q = math.sqrt(1.0 + 2.0 * e2 * e2 * big_p)
    r0 = (-big_p * e2 * p / (1.0 + q)
          + math.sqrt(0.5 * a * a * (1.0 + 1.0 / q)
                      - big_p * (1.0 - e2) * z2 / (q * (1.0 + q))
                      - 0.5 * big_p * p * p))
    u = math.sqrt((p - e2 * r0) ** 2 + z2)
    v = math.sqrt((p - e2 * r0) ** 2 + (1.0 - e2) * z2)
    z0 = b * b * z / (a * v)
    height = u * (1.0 - b * b / (a * v))
    lat = math.degrees(math.atan2(z + WGS84_EP2 * z0, p))
    return lat, lon, height


def earth_position_to_lat_lon(x, y, z):
    """
    Return ``(lat_deg, lon_deg)`` for an ECEF sensor position, or None.

    None when the position is not a plausible geocentric one: non-finite, the
    closed form fails on it (a point near the centre of the Earth raises in
    ecef_to_geodetic), or its ellipsoid height is outside EARTH_HEIGHT_RANGE_M
    -- (0, 0, 0) would otherwise map to the North Pole.
    """
    if not all(math.isfinite(v) for v in (x, y, z)):
        return None
    try:
        lat, lon, height = ecef_to_geodetic(x, y, z)
    except (ValueError, ZeroDivisionError, OverflowError):
        return None
    if not (math.isfinite(lat) and math.isfinite(lon) and math.isfinite(height)
            and EARTH_HEIGHT_RANGE_M[0] <= height <= EARTH_HEIGHT_RANGE_M[1]):
        return None
    return lat, lon


def check_box(box):
    """Raise ValueError unless ``box`` is a valid (latmin, lonmin, latmax, lonmax)."""
    lat_min, lon_min, lat_max, lon_max = box
    if not all(math.isfinite(v) for v in box):
        raise ValueError('--box values must be finite, got %r' % (box,))
    if not (-90.0 <= lat_min < lat_max <= 90.0):
        raise ValueError('--box needs -90 <= LATMIN < LATMAX <= 90, got %g %g'
                         % (lat_min, lat_max))
    if not (-180.0 <= lon_min < lon_max <= 180.0):
        raise ValueError('--box needs -180 <= LONMIN < LONMAX <= 180, got %g %g '
                         '(a box across the antimeridian is not supported)'
                         % (lon_min, lon_max))


def in_box(lat, lon, box):
    """Return True when (lat, lon) lies inside ``box`` (edges inclusive)."""
    lat_min, lon_min, lat_max, lon_max = box
    return lat_min <= lat <= lat_max and lon_min <= lon <= lon_max


def is_tf_topic(name):
    """Return 'tf', 'tf_static' or None for a (possibly namespaced) topic name."""
    if name == '/tf' or name.endswith('/tf'):
        return 'tf'
    if name == '/tf_static' or name.endswith('/tf_static'):
        return 'tf_static'
    return None


def stamp_ns(stamp):
    """Return a builtin_interfaces/Time as integer nanoseconds."""
    return int(stamp.sec) * 1000000000 + int(stamp.nanosec)


class PingLocator:
    """
    Position pings from the bags' own TF tree (``earth -> sensor frame``).

    The same guard-then-exact pattern as import_bag. Feed every ``/tf`` and
    ``/tf_static`` message with add_tf and every ping with push, in bag order.
    drain then yields the pings whose stamp the dynamic TF stream (the
    *frontier*, the newest dynamic transform stamp seen) has passed by
    ``guard_sec``, so the transforms on both sides of the ping are in the
    buffer, each looked up exactly at its stamp. ``drain(flush=True)`` at the
    end of the stream yields whatever is still waiting.
    """

    def __init__(self, cache_sec=TF_CACHE_SEC, guard_sec=TF_GUARD_SEC):
        """Create an empty buffer holding ``cache_sec`` of dynamic transforms."""
        import tf2_ros
        from rclpy.duration import Duration

        self._tf2_ros = tf2_ros
        self._buffer = tf2_ros.Buffer(cache_time=Duration(seconds=cache_sec))
        self._guard_ns = int(round(guard_sec * 1e9))
        self._frontier_ns = None
        self._pending = collections.deque()   # (ping stamp ns, ping), arrival order

    def add_tf(self, tf_message, is_static):
        """Insert every transform of a tf2_msgs/TFMessage into the buffer."""
        for t in tf_message.transforms:
            if is_static:
                self._buffer.set_transform_static(t, 'bag')
            else:
                self._buffer.set_transform(t, 'bag')
                ns = stamp_ns(t.header.stamp)
                if self._frontier_ns is None or ns > self._frontier_ns:
                    self._frontier_ns = ns

    def push(self, ping):
        """Queue a ping (anything with ``header.frame_id``/``header.stamp``)."""
        self._pending.append((stamp_ns(ping.header.stamp), ping))

    @property
    def pending(self):
        """Number of pings waiting for the TF frontier."""
        return len(self._pending)

    def drain(self, flush=False):
        """
        Yield ``(ping, position, reason)`` for each ping that is ready.

        A ping is ready once ``ping stamp <= frontier - guard`` (as import_bag,
        first in first out: a ping waits behind an earlier one), or for every
        waiting ping with ``flush``. ``position`` and ``reason`` are locate's.
        """
        while self._pending:
            ping_ns, ping = self._pending[0]
            if not flush and (self._frontier_ns is None
                              or ping_ns > self._frontier_ns - self._guard_ns):
                return
            self._pending.popleft()
            position, reason = self.locate(ping.header.frame_id, ping.header.stamp)
            yield ping, position, reason

    def locate(self, frame_id, stamp):
        """
        Return ``((lat_deg, lon_deg), None)`` or ``(None, reason)``.

        ``stamp`` is a builtin_interfaces/Time, looked up exactly (tf2
        interpolates between the transforms on either side; there is no
        latest-transform fallback). ``reason`` is ``'bad_stamp'`` for an unset
        (zero) or negative stamp -- tf2 reads time zero as "the latest
        transform", which would position the ping wherever the vessel happened
        to be -- ``'no_tf'`` when the frame has no earth transform at all, and
        ``'extrapolation'`` when it has one but the stamp is outside the
        buffered coverage, and ``'bad_position'`` when the transform is not a
        plausible geocentric position (see earth_position_to_lat_lon).
        """
        from rclpy.time import Time

        if stamp_ns(stamp) <= 0:
            return None, 'bad_stamp'
        tf2_ros = self._tf2_ros
        try:
            t = self._buffer.lookup_transform_core(
                EARTH_FRAME, frame_id, Time.from_msg(stamp))
        except tf2_ros.ExtrapolationException:
            return None, 'extrapolation'
        except tf2_ros.TransformException:
            return None, 'no_tf'
        tr = t.transform.translation
        position = earth_position_to_lat_lon(tr.x, tr.y, tr.z)
        if position is None:
            return None, 'bad_position'
        return position, None


# --box drop reasons, in report order, with the words the run report uses.
BOX_DROP_REASONS = (
    ('outside', 'outside the box'),
    ('no_tf', 'with no earth transform for their frame'),
    ('extrapolation', 'outside the earth TF coverage at their stamp'),
    ('bad_stamp', 'with an unset (zero) header stamp'),
    ('bad_position', 'with an earth transform that is not a geocentric position'),
)


def new_box_stats():
    """Return zeroed --box ping counters: 'kept' plus one per drop reason."""
    stats = {'kept': 0}
    stats.update((key, 0) for key, _words in BOX_DROP_REASONS)
    return stats


def box_report(box, stats, n_pings):
    """Return the one-line --box summary printed after the pass."""
    return 'box %s: kept %d of %d pings; dropped %s' % (
        ' '.join('%g' % v for v in box), stats['kept'], n_pings,
        ', '.join('%d %s' % (stats[key], words) for key, words in BOX_DROP_REASONS))


def parse_args(argv):
    """Parse CLI args (bag(s), topic, output, bin width, tier-2 TL options)."""
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('bag', nargs='+',
                   help='one or more rosbag2 directories (or .mcap/.db3 files); '
                        'a real survey calibration spans many bags')
    p.add_argument('-t', '--topic', required=True,
                   help='marine_acoustic_msgs/SonarDetections topic')
    p.add_argument('-o', '--output', required=True, help='output CSV path')
    p.add_argument('--bin-width-deg', type=float, default=2.0,
                   help='angular bin width in degrees (default 2.0)')
    p.add_argument('--remove-tl', action='store_true',
                   help='tier-2: remove per-beam 2-way transmission loss '
                        '40*log10(R) + 2*alpha*R before binning (cube#87)')
    p.add_argument('--water-temp-c', type=float, default=None,
                   help='water temperature, degC (REQUIRED with --remove-tl '
                        'unless --absorption-db-per-m is given); feeds the '
                        'computed absorption alpha. Use a cast value '
                        'representative of the water column, not the surface '
                        'temperature alone. With --absorption-db-per-m it is '
                        'only recorded in the header')
    p.add_argument('--salinity', type=float, default=None,
                   help='salinity, psu (default 0 = fresh water); feeds the '
                        'Francois-Garrison boric-acid and MgSO4 terms. Take it '
                        'from a CTD cast at the site')
    p.add_argument('--ph', type=float, default=None,
                   help='pH for the boric-acid term (default %g; it has no '
                        'effect at salinity 0)' % DEFAULT_PH)
    p.add_argument('--depth-m', type=float, default=None,
                   help='nominal depth for the absorption pressure terms D '
                        '(default 0.0 -> P2=P3=1)')
    p.add_argument('--absorption-db-per-m', type=float, default=None,
                   help='use this absorption alpha (dB/m) instead of computing '
                        "it, e.g. a path-averaged value from the site's casts. "
                        'Requires --remove-tl; excludes --salinity, --ph and '
                        '--depth-m')
    p.add_argument('--box', type=float, nargs=4, default=None,
                   metavar=('LATMIN', 'LONMIN', 'LATMAX', 'LONMAX'),
                   help="keep only pings whose sensor position (from the bag's "
                        'TF tree, earth -> detections frame) lies inside this '
                        'WGS84 box, degrees; for deriving a per-site curve from '
                        'bags that mix survey and transit')
    args = p.parse_args(argv)
    if args.box is not None:
        try:
            check_box(args.box)
        except ValueError as e:
            p.error(str(e))
    override = args.absorption_db_per_m is not None
    if override:
        if not args.remove_tl:
            p.error('--absorption-db-per-m requires --remove-tl (alpha is only '
                    'used by the tier-2 TL compensation)')
        given = [flag for flag, value in (('--salinity', args.salinity),
                                          ('--ph', args.ph),
                                          ('--depth-m', args.depth_m))
                 if value is not None]
        if given:
            p.error('--absorption-db-per-m is mutually exclusive with %s: they '
                    'only feed the computed alpha' % ', '.join(given))
        alpha = args.absorption_db_per_m
        if not math.isfinite(alpha) or not 0.0 < alpha <= MAX_ABSORPTION_DB_PER_M:
            p.error('--absorption-db-per-m %r must be > 0 and <= %g dB/m (is it '
                    'in dB/km?)' % (alpha, MAX_ABSORPTION_DB_PER_M))
        if args.water_temp_c is not None and not (
                math.isfinite(args.water_temp_c)
                and TEMP_C_RANGE[0] <= args.water_temp_c <= TEMP_C_RANGE[1]):
            p.error('--water-temp-c %r is outside the plausible range [%g, %g]'
                    % ((args.water_temp_c,) + TEMP_C_RANGE))
        return args

    if args.remove_tl and args.water_temp_c is None:
        p.error('--water-temp-c is required when --remove-tl is set (or give '
                '--absorption-db-per-m)')
    if not args.remove_tl:
        ignored = [flag for flag, value in (('--water-temp-c', args.water_temp_c),
                                            ('--salinity', args.salinity),
                                            ('--ph', args.ph),
                                            ('--depth-m', args.depth_m))
                   if value is not None]
        if ignored:
            print('warning: %s only feed(s) the tier-2 absorption and is '
                  'ignored without --remove-tl' % ', '.join(ignored),
                  file=sys.stderr)
    if args.salinity is None:
        args.salinity = 0.0
    if args.ph is None:
        args.ph = DEFAULT_PH
    if args.depth_m is None:
        args.depth_m = 0.0
    if args.remove_tl:
        try:
            check_absorption_inputs(
                args.water_temp_c, args.salinity, args.ph, args.depth_m)
        except ValueError as e:
            p.error(str(e))
    return args


def francois_garrison_absorption_db_per_m(
        freq_khz, temp_c, salinity_psu=0.0, depth_m=0.0, ph=DEFAULT_PH):
    """
    Francois-Garrison (1982) sound absorption alpha in sea or fresh water, dB/m.

    Sum of the boric-acid, magnesium-sulphate and pure-water relaxation terms
    (f in kHz, T in degC, S in psu, D in metres, c in m/s), each in dB/km:

        c       = 1412 + 3.21 T + 1.19 S + 0.0167 D
        boric   = A1 P1 f1 f**2 / (f1**2 + f**2)
                  A1 = 8.86 / c * 10**(0.78 pH - 5),  P1 = 1
                  f1 = 2.8 sqrt(S / 35) 10**(4 - 1245 / (273 + T))
        MgSO4   = A2 P2 f2 f**2 / (f2**2 + f**2)
                  A2 = 21.44 S / c (1 + 0.025 T)
                  P2 = 1 - 1.37e-4 D + 6.2e-9 D**2
                  f2 = 8.17 10**(8 - 1990 / (273 + T)) / (1 + 0.0018 (S - 35))
        water   = A3 P3 f**2
                  A3 = temperature-split cubic (T <= 20 / T > 20)
                  P3 = 1 - 3.83e-5 D + 4.9e-10 D**2

    At S = 0 both chemical terms are exactly zero (f1 = 0 and A2 = 0), so the
    result is the pure-water term alone, bit-for-bit the freshwater value the
    tool computed before cube_bathymetry#170 (f=500 kHz, T=24 C, D=0 ->
    0.0489736 dB/m). Sanity in salt water: f=500 kHz, T=15 C, S=32, pH 8, D=0
    -> ~0.126 dB/m. Returns alpha_dB/km / 1000.
    """
    if not math.isfinite(freq_khz) or freq_khz <= 0.0:
        raise ValueError('frequency must be finite and > 0 kHz, got %r' % freq_khz)
    t = temp_c
    s = salinity_psu
    d = depth_m
    f2_khz2 = freq_khz ** 2
    c = 1412.0 + 3.21 * t + 1.19 * s + 0.0167 * d

    # Boric acid (dominant below ~1 kHz; small at multibeam frequencies).
    a1 = 8.86 / c * 10.0 ** (0.78 * ph - 5.0)
    f1 = 2.8 * math.sqrt(s / 35.0) * 10.0 ** (4.0 - 1245.0 / (273.0 + t))
    boric = a1 * f1 * f2_khz2 / (f1 ** 2 + f2_khz2)

    # Magnesium sulphate (dominant from ~10 to a few hundred kHz).
    a2 = 21.44 * s / c * (1.0 + 0.025 * t)
    p2 = 1.0 - 1.37e-4 * d + 6.2e-9 * d ** 2
    f2 = 8.17 * 10.0 ** (8.0 - 1990.0 / (273.0 + t)) / (1.0 + 0.0018 * (s - 35.0))
    mgso4 = a2 * p2 * f2 * f2_khz2 / (f2 ** 2 + f2_khz2)

    # Pure water. Written exactly as the pre-#170 freshwater function so S = 0
    # reproduces its floating-point result (and therefore its CSV rows).
    if t > 20.0:
        a3 = (3.964e-4 - 1.146e-5 * t + 1.45e-7 * t ** 2 - 6.5e-10 * t ** 3)
    else:
        a3 = (4.937e-4 - 2.590e-5 * t + 9.110e-7 * t ** 2 - 1.500e-8 * t ** 3)
    p3 = 1.0 - 3.83e-5 * d + 4.9e-10 * d ** 2
    water = a3 * p3 * freq_khz ** 2

    alpha_db_per_km = boric + mgso4 + water
    return alpha_db_per_km / 1000.0


def open_reader(bag_path):
    """Open a rosbag2 SequentialReader, auto-detecting the storage backend."""
    import rosbag2_py

    storage_id = ''
    if bag_path.endswith('.mcap'):
        storage_id = 'mcap'
    elif bag_path.endswith('.db3'):
        storage_id = 'sqlite3'
    storage_options = rosbag2_py.StorageOptions(uri=bag_path, storage_id=storage_id)
    converter_options = rosbag2_py.ConverterOptions(
        input_serialization_format='cdr', output_serialization_format='cdr')
    reader = rosbag2_py.SequentialReader()
    reader.open(storage_options, converter_options)
    return reader


def merged_messages(readers):
    """
    Yield ``(reader_index, topic, data, t_ns)`` from every reader in time order.

    A k-way merge on each message's bag timestamp, so several bags -- split
    files of one recording, or bags recorded side by side -- are read as one
    chronological stream, as import_bag's BagReaders does. Ties go to the
    reader given first.
    """
    heap = []
    for i, reader in enumerate(readers):
        if reader.has_next():
            name, data, t_ns = reader.read_next()
            heap.append((t_ns, i, name, data))
    heapq.heapify(heap)
    while heap:
        # (t_ns, i) is unique: each reader has at most one message in the heap.
        t_ns, i, name, data = heapq.heappop(heap)
        yield i, name, data, t_ns
        reader = readers[i]
        if reader.has_next():
            name, data, t_ns = reader.read_next()
            heapq.heappush(heap, (t_ns, i, name, data))


def accumulate(readers, topic, bin_width_deg, remove_tl, bins, freqs,
               box=None, box_stats=None):
    """
    Accumulate per-bin sums on `topic` over every reader into `bins`.

    Each bin slot is ``[sum_adjusted, sum_R, count]``:

    * tier-1 (``remove_tl`` False): ``sum_adjusted`` is the raw dB sum; ``sum_R``
      stays 0 (unused).
    * tier-2 (``remove_tl`` True): the range-INDEPENDENT-of-alpha part of the TL
      (the spreading term ``40*log10(R)``) is compensated (added back) inline, so
      ``sum_adjusted`` is ``sum(db + 40*log10(R))`` and ``sum_R`` is ``sum(R)``. The
      absorption term ``2*alpha*R`` is added in write_csv once alpha is known from the
      median frequency -- a single pass over the bags.

    `freqs` collects each ping's ``ping_info.frequency`` (Hz) for the median.

    Without `box` the readers are read one after another; the binned sums do not
    depend on the order.

    With `box` (latmin, lonmin, latmax, lonmax) each reader is filtered to the
    detections topic plus every /tf and /tf_static topic in it, and all readers
    are merged into ONE time-ordered pass (merged_messages) feeding ONE
    PingLocator, as import_bag does: a /tf_static recorded only in the first
    split file still positions the pings of the later splits, and a bag holding
    only TF positions the pings of a bag recorded beside it. Each ping is queued
    until the TF frontier is TF_GUARD_SEC past its stamp, looked up exactly,
    and binned only if its sensor position lies inside the box. `box_stats` (see
    new_box_stats) counts kept and dropped pings; pings that are dropped
    contribute neither samples nor a frequency.
    Returns the number of messages read on `topic`.
    """
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py.utilities import get_message

    type_maps = []
    for reader in readers:
        type_map = {t.name: t.type for t in reader.get_all_topics_and_types()}
        if topic not in type_map:
            raise SystemExit(
                "error: topic '%s' not in bag (have: %s)"
                % (topic, ', '.join(sorted(type_map)) or 'none'))
        type_maps.append(type_map)
    msg_types = [get_message(type_map[topic]) for type_map in type_maps]

    def add_ping(msg):
        bin_ping(msg, bin_width_deg, remove_tl, bins, freqs)

    n_msgs = 0
    if box is None:
        for i, reader in enumerate(readers):
            while reader.has_next():
                name, data, _stamp = reader.read_next()
                if name != topic:
                    continue
                n_msgs += 1
                add_ping(deserialize_message(data, msg_types[i]))
        return n_msgs

    import rosbag2_py

    tf_topics = []    # per reader: {topic name: 'tf' | 'tf_static'}
    for reader, type_map in zip(readers, type_maps):
        kinds = {name: kind for name, type_name in type_map.items()
                 for kind in (is_tf_topic(name),)
                 if kind is not None and type_name == 'tf2_msgs/msg/TFMessage'}
        reader.set_filter(rosbag2_py.StorageFilter(topics=[topic] + sorted(kinds)))
        tf_topics.append(kinds)
    tf_msg_type = get_message('tf2_msgs/msg/TFMessage')
    locator = PingLocator()   # one buffer for every bag on the command line

    def take_ready(flush):
        for msg, position, reason in locator.drain(flush):
            if position is None:
                box_stats[reason] += 1
            elif not in_box(position[0], position[1], box):
                box_stats['outside'] += 1
            else:
                box_stats['kept'] += 1
                add_ping(msg)

    for i, name, data, _t_ns in merged_messages(readers):
        kind = tf_topics[i].get(name)
        if kind is not None:
            locator.add_tf(deserialize_message(data, tf_msg_type), kind == 'tf_static')
            take_ready(False)
        elif name == topic:
            n_msgs += 1
            locator.push(deserialize_message(data, msg_types[i]))
    take_ready(True)   # end of the stream: position whatever is still waiting
    return n_msgs


def bin_ping(msg, bin_width_deg, remove_tl, bins, freqs):
    """Add one SonarDetections ping's beams to `bins` (see accumulate)."""
    intensities = msg.intensities
    rx_angles = msg.rx_angles
    twtt = msg.two_way_travel_times
    sound_speed = msg.ping_info.sound_speed
    freq = msg.ping_info.frequency
    if remove_tl and math.isfinite(freq) and freq > 0.0:
        freqs.append(freq)  # only needed for the tier-2 absorption alpha
    flags = getattr(msg, 'flags', None)
    count = min(len(intensities), len(rx_angles))
    for i in range(count):
        db = intensities[i]
        if not math.isfinite(db):
            continue
        # Skip beams flagged bad (flag != 0) when flags are present and aligned.
        if flags is not None and i < len(flags):
            flag_val = getattr(flags[i], 'flag', 0)
            if flag_val != 0:
                continue
        ang = rx_angles[i]
        if not math.isfinite(ang):
            continue

        range_m = 0.0
        if remove_tl:
            # R = twtt * c / 2; skip beams with a non-positive / NaN range
            # (log10 undefined) so they never poison the residual. NOTE: this
            # DROPS such beams from the derivation entirely, whereas the C++
            # estimator RETAINS them and just skips the TL term (tier-1 fallback
            # for that beam) -- an intentional asymmetry: a beam with no valid
            # range can't inform the TL-removed residual, but its raw value is
            # still a usable backscatter sample at apply time.
            if i >= len(twtt):
                continue
            range_m = twtt[i] * sound_speed / 2.0
            if not math.isfinite(range_m) or range_m <= 0.0:
                continue
            adjusted = db + 40.0 * math.log10(range_m)
        else:
            adjusted = db

        abs_deg = abs(math.degrees(ang))
        idx = int(abs_deg // bin_width_deg)
        slot = bins.setdefault(idx, [0.0, 0.0, 0])
        slot[0] += adjusted
        slot[1] += range_m
        slot[2] += 1


def bag_basenames(bag_paths):
    """
    Return the basenames of ``bag_paths`` for the ``source_bags`` header line.

    A trailing slash on a bag directory is ignored. A name that cannot be
    recorded unambiguously on one comma-separated header line (it contains a
    comma, a line break, or is empty) is refused rather than mangled.
    """
    names = []
    for path in bag_paths:
        name = os.path.basename(os.path.normpath(path))
        if not name or name in ('.', '..') or any(ch in name for ch in ',\r\n'):
            raise SystemExit(
                'error: bag name %r cannot be recorded in the source_bags '
                'header (empty, or contains a comma or line break)' % path)
        names.append(name)
    return names


def build_header(remove_tl, source_bags, alpha=None, absorption_source=None,
                 water_temp_c=None, salinity_psu=None, ph=None, depth_m=None,
                 frequency_khz=None, box=None):
    """
    Return the curve CSV's comment header lines (without newlines).

    Every provenance line is a ``# key: value`` comment. The C++ loader
    (``loadAngularResponseCurveWithHeader``) and its marine_tools Python mirror
    read only ``tl_removed`` and ``absorption_db_per_m``; every other key is
    provenance for people and is ignored by them. Tier-2 lines whose value is
    unknown (None) are omitted -- with an alpha override there is no salinity,
    pH, depth or frequency behind alpha, and the temperature is optional.
    ``box`` (the --box site filter) is recorded as
    ``# box: LATMIN LONMIN LATMAX LONMAX`` when given.
    """
    lines = ['# Empirical angular response (derive_angular_response.py, cube#81)']
    if remove_tl:
        # Self-describing tier-2 header (cube#87): the estimator reads
        # tl_removed + absorption_db_per_m and applies the identical TL.
        lines.append('# tl_removed: true')
        lines.append('# absorption_db_per_m: %.6g' % alpha)
        lines.append('# absorption_source: %s' % absorption_source)
        for key, value in (('water_temp_c', water_temp_c),
                           ('salinity_psu', salinity_psu),
                           ('ph', ph),
                           ('depth_m', depth_m),
                           ('frequency_khz', frequency_khz)):
            if value is not None:
                lines.append('# %s: %.6g' % (key, value))
        lines.append('# tl_model: 40*log10(R) + 2*alpha*R   (R = twtt*c/2, metres)')
    else:
        lines.append('# tl_removed: false')
    if box is not None:
        lines.append('# box: %s' % ' '.join('%.9g' % v for v in box))
    lines.append('# source_bags: %s' % ','.join(source_bags))
    return lines


def thin_curve_warning(bins, bin_width_deg):
    """
    Return a warning string when the curve rests on too few samples, else None.

    Thin means the nadir (reference) bin has fewer than MIN_SAMPLES_PER_BIN
    samples, or fewer than MIN_POPULATED_BINS bins reach it: the conditions
    under which cube_lab refuses to self-calibrate.
    """
    if not bins:
        return None
    nadir_idx = min(bins)
    nadir_n = bins[nadir_idx][2]
    populated = sum(1 for slot in bins.values() if slot[2] >= MIN_SAMPLES_PER_BIN)
    if nadir_n >= MIN_SAMPLES_PER_BIN and populated >= MIN_POPULATED_BINS:
        return None
    return ('warning: thin curve: the nadir reference bin (%.1f deg) has %d '
            'samples and %d of %d bins have at least %d; every row is relative '
            'to the nadir bin, and cube_lab refuses a curve below %d samples '
            'there or with fewer than %d such bins'
            % ((nadir_idx + 0.5) * bin_width_deg, nadir_n, populated, len(bins),
               MIN_SAMPLES_PER_BIN, MIN_SAMPLES_PER_BIN, MIN_POPULATED_BINS))


def write_csv(bins, bin_width_deg, out_path, remove_tl, alpha, header_lines):
    """
    Write ``header_lines``, then the binned means and db_relative_to_nadir.

    For tier-2 (`remove_tl`), the absorption term ``2*alpha*R`` is added here
    (compensated) using the per-bin mean range, completing the TL compensation
    started in accumulate. ``header_lines`` (see build_header) records the TL
    provenance so the C++ estimator applies the identical model.
    """
    if not bins:
        raise SystemExit('error: no finite intensity/angle samples found')

    # Per-bin mean (TL-removed when tier-2) dB at the bin centre.
    rows = []
    for idx in sorted(bins):
        s_adj, s_range, n = bins[idx]
        center = (idx + 0.5) * bin_width_deg
        mean = s_adj / n
        if remove_tl:
            # Compensate the absorption part of the TL: + 2*alpha*mean_R.
            mean += 2.0 * alpha * (s_range / n)
        rows.append([center, mean, n])

    nadir_mean = rows[0][1]  # lowest-angle (nadir-most) bin
    lines = list(header_lines)
    lines.append('abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir')
    for center, mean_db, n in rows:
        db_rel = mean_db - nadir_mean
        lines.append('%.1f,%.3f,%d,%+.3f' % (center, mean_db, n, db_rel))
    write_text_atomically(out_path, ''.join(line + '\n' for line in lines))
    return len(rows)


def write_text_atomically(path, text):
    """
    Replace ``path`` with ``text`` in one step (temp file + os.replace).

    A failure part-way leaves an existing file untouched and no partial file
    behind. The new file keeps an existing file's permission bits, else gets
    the umask default, as open(path, 'w') would.
    """
    directory = os.path.dirname(os.path.abspath(path))
    try:
        mode = stat.S_IMODE(os.stat(path).st_mode)
    except FileNotFoundError:
        umask = os.umask(0)
        os.umask(umask)
        mode = 0o666 & ~umask
    fd, tmp_path = tempfile.mkstemp(
        dir=directory, prefix='.%s.' % os.path.basename(path), suffix='.tmp')
    try:
        with os.fdopen(fd, 'w') as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp_path, mode)
        os.replace(tmp_path, path)
    except BaseException:
        try:
            os.unlink(tmp_path)
        except FileNotFoundError:
            pass
        raise


def resolve_absorption(args, freqs):
    """
    Return ``(alpha_db_per_m, source, freq_khz)`` for the tier-2 TL.

    ``source`` is ``'override'`` when ``--absorption-db-per-m`` was given (alpha
    used verbatim) and ``'computed'`` otherwise (Francois-Garrison at the median
    per-ping frequency). ``freq_khz`` is that median, or None when no finite
    frequency was found -- which is an error only for the computed path, as
    is a median outside FREQUENCY_KHZ_RANGE.
    """
    freq_khz = median(freqs) / 1000.0 if freqs else None
    if args.absorption_db_per_m is not None:
        return args.absorption_db_per_m, 'override', freq_khz
    if freq_khz is None:
        raise SystemExit(
            'error: --remove-tl needs a finite ping_info.frequency to compute '
            'alpha; none found (or give --absorption-db-per-m)')
    if not FREQUENCY_KHZ_RANGE[0] <= freq_khz <= FREQUENCY_KHZ_RANGE[1]:
        raise SystemExit(
            'error: the median ping_info.frequency is %g kHz, outside the '
            'plausible %g..%g kHz; it should be in Hz (is it in kHz?). Give '
            '--absorption-db-per-m instead of computing alpha from it'
            % ((freq_khz,) + FREQUENCY_KHZ_RANGE))
    alpha = francois_garrison_absorption_db_per_m(
        freq_khz, args.water_temp_c, args.salinity, args.depth_m, args.ph)
    return alpha, 'computed', freq_khz


def median(values):
    """Plain median of a non-empty list (no numpy dependency)."""
    s = sorted(values)
    m = len(s)
    mid = m // 2
    if m % 2 == 1:
        return s[mid]
    return 0.5 * (s[mid - 1] + s[mid])


def main(argv=None):
    """Read the bag(s), bin by |rx_angles| and write the curve CSV (one line)."""
    args = parse_args(sys.argv[1:] if argv is None else argv)
    source_bags = bag_basenames(args.bag)  # refuse a bad name before reading

    bins = {}      # bin_index -> [sum_adjusted, sum_R, count], merged across bags
    freqs = []     # per-ping ping_info.frequency (Hz)
    box_stats = new_box_stats() if args.box is not None else None
    readers = [open_reader(bag_path) for bag_path in args.bag]
    n_msgs = accumulate(
        readers, args.topic, args.bin_width_deg, args.remove_tl, bins, freqs,
        box=args.box, box_stats=box_stats)
    if box_stats is not None:
        print(box_report(args.box, box_stats, n_msgs))
        if box_stats['kept'] == 0:
            raise SystemExit('error: no pings inside --box; nothing to derive '
                             '(check the box corners are LATMIN LONMIN LATMAX '
                             'LONMAX and the bags carry earth TF)')

    warning = thin_curve_warning(bins, args.bin_width_deg)
    if warning is not None:
        if args.box is not None:
            warning += ('; --box kept %d pings: widen the box or add bags'
                        % box_stats['kept'])
        print(warning, file=sys.stderr)

    alpha = 0.0
    if args.remove_tl:
        alpha, source, freq_khz = resolve_absorption(args, freqs)
        if source == 'override':
            print('tier-2: alpha=%.6g dB/m (--absorption-db-per-m override)'
                  % alpha)
            header = build_header(
                True, source_bags, alpha=alpha, absorption_source=source,
                water_temp_c=args.water_temp_c, box=args.box)
        else:
            print('tier-2: median frequency %.1f kHz, T=%.1f C, S=%g psu, '
                  'pH=%g, D=%.1f m -> alpha=%.6g dB/m'
                  % (freq_khz, args.water_temp_c, args.salinity, args.ph,
                     args.depth_m, alpha))
            header = build_header(
                True, source_bags, alpha=alpha, absorption_source=source,
                water_temp_c=args.water_temp_c, salinity_psu=args.salinity,
                ph=args.ph, depth_m=args.depth_m, frequency_khz=freq_khz,
                box=args.box)
    else:
        header = build_header(False, source_bags, box=args.box)

    n_rows = write_csv(
        bins, args.bin_width_deg, args.output, args.remove_tl, alpha, header)
    print('read %d pings across %d bag(s) on %s; wrote %d-bin curve to %s'
          % (n_msgs, len(args.bag), args.topic, n_rows, args.output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
