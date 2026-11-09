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

Gain-state declaration (cube_bathymetry#176): a sonar's receiver gain can step
without the sonar recording it, and a curve or table fitted across the step is
fitted over two gain states. With ``--gain-declaration FILE`` (a
``gain-state-declaration v1`` file, see the README) each ping's declared offset
-- the dB ADDED to the reported level to reach the declaration's reference
state, so a segment that reads 23 dB low is ``+23.0`` -- is added to its
intensities BEFORE binning (single curve and ``--by-mode``, and before the
tier-2 TL add-back), so one curve or table comes out in the reference state
from data on both sides of the step. The header then records
``gain_reference_state`` and ``gain_declaration_sha256`` (the digest of the
declaration's canonical form), which ``import_bag`` and ``batch_regen`` compare
with the declaration they are given and refuse a mismatch, and per-segment ping
and beam counts. A ping in no segment is at the reference; the run prints the
count, and warns about a declared segment no ping fell in.

Site box (cube_bathymetry#170): with ``--box LATMIN LONMIN LATMAX LONMAX``
(degrees, WGS84) only pings whose sensor position lies inside the box are
binned, so a per-site curve can be derived from bags that mix survey and
transit. Each ping is positioned from the bags' own TF tree the way import_bag
georeferences it:

* every bag on the command line is read in ONE time-ordered pass (merged on the
  bag timestamp) into ONE tf2 buffer, so a ``/tf_static`` recorded only in the
  first split file still serves the later ones, and a bag holding only TF may
  position the pings of a detections bag recorded beside it (at least one bag
  must have the detections topic; without ``--box`` every bag must).
  ``/tf`` and ``/tf_static`` from every namespace go into that buffer; in a
  bag recording several vehicles their trees share it, so their frame names
  must not collide (import_bag behaves the same);
* each ping waits until every dynamic link of ``earth -> ... -> <frame>`` has
  advanced 3 s past its stamp (but no longer than about 57 s after the
  newest transform of a link of that chain passes it, so every link of the
  chain must publish at least about once a minute), so the
  transforms on both sides of it are in the buffer, then ``earth ->
  <header.frame_id>`` is looked up exactly at the ping stamp, interpolated
  between them. There is no fallback to the latest transform: a ping outside
  the earth TF coverage (before the first earth transform, after the last, or
  in a gap wider than the 60 s buffer) is dropped, and so is a ping with an
  unset (zero) stamp, which tf2 would read as "the latest transform", or one
  stamped more than 1 h ahead of the TF stream. Waiting pings leave in stamp
  order, so one ping with a bad future stamp does not hold back the rest.
  Pings still waiting when the bags end are looked up against what was read.
  The TF stream's progress is the newest accepted dynamic transform stamp,
  never later than the time the bag recorded it plus its frame's clock offset
  (stamp minus bag time of the frame's first accepted transform, so a
  recorder whose clock lags or leads the publisher's works; only a transform
  within 1 h of its bag time can set it). Transforms tf2 would reject (NaN,
  zero quaternion, bad frame names, older than the 60 s buffer), dynamic
  transforms with an unset (zero) stamp, transforms whose stamp jumps more
  than 60 s further ahead of the bag time than their frame's offset, and,
  while a frame has no offset yet, transforms more than 1 h off their bag
  time are left out and counted; they move no frontier.

The ECEF translation of the sensor origin is converted to latitude/longitude in
closed form; a translation that is not a geocentric position (more than 50 km
from the ellipsoid) drops the ping. Pings outside the box and pings that cannot
be positioned are dropped, counted by reason and reported, and the box is
recorded in the header.
Without ``--box`` no TF is read.

Per-mode table (cube_bathymetry#172): with ``--by-mode`` each ping is attributed
to the latest ``marine_interfaces/SonarInfo`` at or before its stamp (one
time-ordered pass over detections, SonarInfo and, with ``--box``, TF) and keyed
by its pulse mode (canonical_mode_key: ``FMU_1000us_24000Hz_500kHz``). Per mode
the tool fits ``L = A(theta) + r_g(log10(R / R0))``: ``r_g`` per angle group
``g`` of ``|theta|`` (``--range-angle-edges``, default 10-30, 30-50 and 50-90
deg) on quarter-decade nodes of log10(R / R0), linear between them, fitted
with one intercept per SIGNED angle bin outside the specular band (so only
depth change identifies it); ``A`` per ``|theta|`` bin referred to R0, with
its sample variance; the
offset of each mode against ``--reference-mode`` from the beams either side of
every mode switch, chained by least squares. It writes an
``angular-response-table v1`` CSV (first line ``# format: angular-response-table
v1``, mode key in the first column) instead of a single curve, reports misfit,
offset and switch-direction diagnostics, and refuses ``--remove-tl``: the
measured range term replaces the modelled add-back. The estimator does not read
the table yet (cube_bathymetry#172 PR 2); the current single-curve loaders read
it as an empty curve.

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
    derive_angular_response.py <bag> ... -t <topic> -o <table.csv> --by-mode \\
        --box 42.976 -70.628 43.002 -70.596 --diagnostics-out <diag.json>
"""

import argparse
import collections
import errno
import functools
import heapq
import math
import os
import re
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
# until each dynamic link of its earth chain has advanced TF_GUARD_SEC past its
# stamp, so the transform after it is in the buffer too, and is then looked up
# exactly (see PingLocator).
TF_CACHE_SEC = 60.0
TF_GUARD_SEC = 3.0
# A ping stamped further than this ahead of the TF frontier when it arrives is
# dropped as a bad stamp (a clock glitch, a garbage header): it could only be
# looked up at the very end, and would sit in memory until then.
PING_FUTURE_LIMIT_SEC = 3600.0
EARTH_FRAME = 'earth'
# Ellipsoid heights a ping's sensor can plausibly have. A survey sensor is
# within a few km of the ellipsoid; an ``earth`` frame that is not geocentric
# (a local map frame named earth, a placeholder at the origin) puts it thousands
# of km below it, and its latitude/longitude would be meaningless.
EARTH_HEIGHT_RANGE_M = (-50000.0, 50000.0)
# Geocentric radii a sensor position can have: the ellipsoid's polar radius
# minus 50 km to its equatorial radius plus 50 km. A radius outside this is not
# a position near the Earth's surface, and is refused BEFORE the closed form,
# which goes complex (and raises TypeError) for some radii far inside the Earth.
EARTH_RADIUS_RANGE_M = (WGS84_B + EARTH_HEIGHT_RANGE_M[0],
                        WGS84_A + EARTH_HEIGHT_RANGE_M[1])


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

    None when the position is not a plausible geocentric one: non-finite, a
    geocentric radius outside EARTH_RADIUS_RANGE_M (checked before the closed
    form, which is only valid near the surface: (0, 0, 0) would map to the
    North Pole, and a point some 20 to 45 km from the centre makes its cube
    root complex), or an ellipsoid height outside EARTH_HEIGHT_RANGE_M. Any
    arithmetic failure in the closed form is also caught and gives None.
    """
    if not all(math.isfinite(v) for v in (x, y, z)):
        return None
    radius = math.sqrt(x * x + y * y + z * z)
    if not EARTH_RADIUS_RANGE_M[0] <= radius <= EARTH_RADIUS_RANGE_M[1]:
        return None
    try:
        lat, lon, height = ecef_to_geodetic(x, y, z)
    except (TypeError, ValueError, ZeroDivisionError, OverflowError):
        return None
    if not all(isinstance(v, float) for v in (lat, lon, height)):
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
    ``/tf_static`` message with add_tf and every ping's stamp and frame with
    push, in bag order.
    drain then yields the pings whose stamp every dynamic link of their chain
    (``earth -> ... -> <ping frame>``) has passed by ``guard_sec``, so the
    transforms on both sides of the ping are in the buffer for each link, each
    ping looked up exactly at its stamp. ``drain(flush=True)`` at the end of
    the stream yields whatever is still waiting.

    A link's *frontier* is the newest accepted stamp of its child frame, so a
    slow link (earth -> map at 0.1 Hz beside a 10 Hz map -> base_link) holds a
    ping until its next transform arrives; import_bag's single frontier, the
    newest stamp of any frame, releases it early and the ping drops as
    extrapolation. Two bounds keep a stalled link from holding pings forever:
    a ping whose chain does not reach ``earth`` yet waits for the newest stamp
    of any frame (the overall frontier), as in import_bag, and any other ping
    is released once the newest frontier among its own chain's dynamic links
    is ``cache_sec - guard_sec`` past it, since by then that link is about to
    drop the transforms around it from the cache (tf2 prunes per frame). Using
    the chain's own links, not the overall frontier, keeps another frame whose
    clock offset runs ahead from releasing this chain's pings early. So a
    dynamic link publishing less often than about once a minute still loses
    pings.
    """

    def __init__(self, cache_sec=TF_CACHE_SEC, guard_sec=TF_GUARD_SEC,
                 future_limit_sec=PING_FUTURE_LIMIT_SEC):
        """Create an empty buffer holding ``cache_sec`` of dynamic transforms."""
        import tf2_ros
        from rclpy.duration import Duration

        self._tf2_ros = tf2_ros
        self._buffer = tf2_ros.Buffer(cache_time=Duration(seconds=cache_sec))
        self._cache_ns = int(round(cache_sec * 1e9))
        self._guard_ns = int(round(guard_sec * 1e9))
        self._future_limit_ns = int(round(future_limit_sec * 1e9))
        self._frontier_ns = None   # overall: the newest of _link_frontier_ns
        self._link_frontier_ns = {}   # child frame -> its frontier (dynamic only)
        self._parent = {}   # child frame -> parent, from accepted transforms
        self._release_ns = self._cache_ns - self._guard_ns
        self._newest_ns = {}   # child frame -> newest dynamic stamp inserted
        # child frame -> stamp minus bag time of its first accepted dynamic
        # transform (within future_limit_sec of its bag time): the clock offset
        # between that frame's publisher and the recorder, against which later
        # transforms of the frame are judged.
        self._offset_ns = {}
        # Transforms not given to tf2, by reason (see TF_REJECT_REASONS).
        self.tf_rejected = collections.Counter()
        # Heap of (ping stamp ns, arrival number, frame id, item): the earliest
        # stamp leaves first, ties in arrival order.
        self._pending = []
        self._arrivals = 0
        self.max_pending = 0   # the most pings that waited at once

    def add_tf(self, tf_message, is_static, bag_ns=None):
        """
        Insert every transform of a tf2_msgs/TFMessage into the buffer.

        ``bag_ns`` is the time the bag recorded the message. Transforms tf2
        would reject, or that would corrupt the buffer, are not given to it:
        they are counted in ``tf_rejected`` (instead of tf2 printing a line for
        each) and do not move the frontier. See transform_reject_reason.

        The frontier advances to an accepted dynamic transform's stamp, but
        never past ``bag_ns`` plus its child frame's clock offset (the stamp
        minus bag time of that frame's first accepted transform): a transform
        stamped further ahead of the time it was recorded than its frame
        usually is (a glitch) must not release waiting pings before the
        transforms that really follow them have been read, while a constant
        offset between the publisher's clock and the recorder's (relayed
        topics, unsynchronised hosts) changes nothing.
        """
        for t in tf_message.transforms:
            reason = self.transform_reject_reason(t, is_static, bag_ns)
            if reason is not None:
                self.tf_rejected[reason] += 1
                continue
            child = t.child_frame_id
            self._parent[child] = t.header.frame_id
            if is_static:
                self._buffer.set_transform_static(t, 'bag')
                continue
            self._buffer.set_transform(t, 'bag')
            ns = stamp_ns(t.header.stamp)
            if ns > self._newest_ns.get(child, ns - 1):
                self._newest_ns[child] = ns
            if bag_ns is None:
                advance = ns
            else:
                offset = self._offset_ns.setdefault(child, ns - bag_ns)
                advance = min(ns, bag_ns + offset)
            if advance > self._link_frontier_ns.get(child, advance - 1):
                self._link_frontier_ns[child] = advance
            if self._frontier_ns is None or advance > self._frontier_ns:
                self._frontier_ns = advance

    def chain_frontier(self, frame_id):
        """
        Return the stamp up to which every link from ``earth`` to ``frame_id`` has been read.

        That is the oldest frontier among the chain's dynamic links; ``math.inf``
        when every link is static (the position never changes); the overall
        frontier (None before any dynamic transform) when the chain does not
        reach ``earth`` from what has been read so far.
        """
        links = self._chain_link_frontiers(frame_id)
        return self._frontier_ns if links is None else links[0]

    def _chain_link_frontiers(self, frame_id):
        """
        Return the oldest and newest frontier of the dynamic links from ``earth`` to ``frame_id``.

        Both ``math.inf`` when every link is static; None when the chain does
        not reach ``earth`` from what has been read so far (or loops).
        """
        oldest = newest = None
        frame = frame_id
        seen = set()
        while frame != EARTH_FRAME:
            parent = self._parent.get(frame)
            if parent is None or frame in seen:
                return None   # not (yet) connected to earth
            seen.add(frame)
            link = self._link_frontier_ns.get(frame)
            if link is not None:
                oldest = link if oldest is None else min(oldest, link)
                newest = link if newest is None else max(newest, link)
            frame = parent
        if oldest is None:
            return math.inf, math.inf
        return oldest, newest

    def transform_reject_reason(self, t, is_static, bag_ns=None):
        """
        Return why TransformStamped ``t`` must not enter the buffer, or None.

        ``'invalid'``: what tf2 itself rejects (and prints an error for) -- an
        empty or self-referencing frame name, a NaN or infinite value, or a
        zero quaternion (tf2 normalises any other) -- and, for a dynamic
        transform, an unset (zero) or negative stamp, as push treats it for
        pings. Dynamic transforms only: ``'old'``, more than the cache length
        older than the newest transform of the same child frame (tf2's
        TF_OLD_DATA); ``'future'``, a stamp minus ``bag_ns`` more than the
        cache length above the frame's clock reference, which tf2 would accept
        and then prune every real transform of that frame against. The
        reference is the stamp minus ``bag_ns`` of the frame's first accepted
        transform, so a constant clock offset between publisher and recorder
        passes. Only a transform whose offset is within ``future_limit_sec``
        (1 h) of its bag time, either way, can become the reference: one
        further off while the frame has none yet (a glitch) is also left out
        as 'future', and the reference stays unset until a transform within
        the bound arrives. A rejected transform never enters tf2, never
        becomes a reference and never moves any frontier, so a glitch on one
        frame costs no pings of another.
        """
        tr = t.transform.translation
        q = t.transform.rotation
        values = (tr.x, tr.y, tr.z, q.x, q.y, q.z, q.w)
        norm2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w
        if (not t.header.frame_id or not t.child_frame_id
                or t.header.frame_id == t.child_frame_id
                or not all(math.isfinite(v) for v in values)
                or not (math.isfinite(norm2) and norm2 > 0.0)):
            return 'invalid'
        if is_static:
            return None
        ns = stamp_ns(t.header.stamp)
        if ns <= 0:
            return 'invalid'
        offset = self._offset_ns.get(t.child_frame_id)
        if bag_ns is not None:
            if offset is None:
                if abs(ns - bag_ns) > self._future_limit_ns:
                    return 'future'
            elif ns - bag_ns > offset + self._cache_ns:
                return 'future'
        newest = self._newest_ns.get(t.child_frame_id)
        if newest is not None and newest > ns + self._cache_ns:
            return 'old'
        return None

    def push(self, ping_ns, frame_id, item):
        """
        Queue ``item`` (any object) for positioning at ``frame_id`` and ``ping_ns``.

        ``item`` comes back from drain. Queue only what the caller needs once
        the ping is positioned (accumulate queues a compact PingRecord, not the
        deserialized message), since pings wait here until the TF frontier
        passes them.

        Returns None when queued, or ``'bad_stamp'`` when the ping is dropped
        at once: an unset (zero) or negative stamp, or one more than
        ``future_limit_sec`` ahead of the TF frontier.
        """
        if ping_ns <= 0 or (self._frontier_ns is not None
                            and ping_ns > self._frontier_ns + self._future_limit_ns):
            return 'bad_stamp'
        heapq.heappush(self._pending, (ping_ns, self._arrivals, frame_id, item))
        self._arrivals += 1
        self.max_pending = max(self.max_pending, len(self._pending))
        return None

    @property
    def pending(self):
        """Return the number of pings waiting for the TF frontier."""
        return len(self._pending)

    def drain(self, flush=False):
        """
        Yield ``(item, position, reason)`` for each ping that is ready.

        A ping is ready once ``ping stamp <= chain_frontier - guard``, or
        ``ping stamp <= newest frontier of the chain's dynamic links - (cache -
        guard)`` (see the class docstring), or for every waiting ping with
        ``flush``. Pings leave in
        stamp order, not arrival order, so one ping with a far-future stamp
        does not hold back the pings that arrived after it (import_bag's
        first-in-first-out deque does); a ping waiting on a slow link holds
        back the later pings of other frames for at most that bound while the
        link keeps publishing. If every link of a chain stops for good, its
        pings wait until ``flush`` and hold back later pings of other chains
        with them (one sonar topic means one chain, so this costs nothing in
        practice).
        ``position`` and ``reason`` are locate's.
        """
        while self._pending:
            ping_ns, _arrival, frame_id, item = self._pending[0]
            if not flush and not self._ready(ping_ns, frame_id):
                return
            heapq.heappop(self._pending)
            position, reason = self.locate(frame_id, ping_ns)
            yield item, position, reason

    def _ready(self, ping_ns, frame_id):
        """Return True when the ping at ``ping_ns`` in ``frame_id`` can be looked up."""
        links = self._chain_link_frontiers(frame_id)
        if links is None:   # not connected to earth: the overall frontier
            return (self._frontier_ns is not None
                    and ping_ns <= self._frontier_ns - self._guard_ns)
        oldest, newest = links
        return (ping_ns <= oldest - self._guard_ns
                or ping_ns <= newest - self._release_ns)

    def locate(self, frame_id, ping_ns):
        """
        Return ``((lat_deg, lon_deg), None)`` or ``(None, reason)``.

        ``ping_ns`` (integer nanoseconds) is looked up exactly (tf2
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

        if ping_ns <= 0:
            return None, 'bad_stamp'
        tf2_ros = self._tf2_ros
        try:
            t = self._buffer.lookup_transform_core(
                EARTH_FRAME, frame_id, Time(nanoseconds=ping_ns))
        except tf2_ros.ExtrapolationException:
            return None, 'extrapolation'
        except tf2_ros.TransformException:
            return None, 'no_tf'
        tr = t.transform.translation
        position = earth_position_to_lat_lon(tr.x, tr.y, tr.z)
        if position is None:
            return None, 'bad_position'
        return position, None


# ---------------------------------------------------------------------------
# Gain-state declaration (cube_bathymetry#176)
#
# A sonar's receiver gain can step without the sonar recording it. The operator
# declares the steps in a ``gain-state-declaration v1`` file (see the README):
# a reference state plus half-open UTC segments with an additive dB offset (the
# value ADDED to the reported level to reach the reference state). The
# C++ loader (gain_declaration.cpp) implements the same format and the same
# checks; test/data/gain_declaration_vectors.json pins both.
# ---------------------------------------------------------------------------

GAIN_FORMAT_LINE = '# format: gain-state-declaration v1'
GAIN_FORMAT_PREFIX = '# format: gain-state-declaration'
GAIN_COLUMNS = 'segment,start_utc,end_utc,offset_db,provenance,provenance_note'
GAIN_PROVENANCE = ('settings_file', 'measured', 'operator_reported')
GAIN_MAX_ABS_OFFSET_DB = 60.0
GAIN_MAX_BYTES = 64 * 1024
GAIN_IDENTIFIER = re.compile(r'^[A-Za-z0-9_.-]+$')
GAIN_UTC = re.compile(r'^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,9}))?Z$',
                      re.ASCII)
GAIN_DECIMAL = re.compile(r'[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?', re.ASCII)


def parse_utc_ns(text):
    """Return ``YYYY-MM-DDTHH:MM:SS[.f{1,9}]Z`` as integer ns since the epoch, else None."""
    import calendar
    m = GAIN_UTC.match(text)
    if m is None:
        return None
    year, month, day, hour, minute, second = (int(g) for g in m.groups()[:6])
    if not (1970 <= year <= 2200 and 1 <= month <= 12 and hour <= 23 and minute <= 59
            and second <= 59):
        return None
    if not 1 <= day <= calendar.monthrange(year, month)[1]:
        return None
    frac = int((m.group(7) or '').ljust(9, '0') or 0)
    days = calendar.timegm((year, month, day, 0, 0, 0)) // 86400
    return (((days * 24 + hour) * 60 + minute) * 60 + second) * 1000000000 + frac


def _gain_offset_text(offset_db):
    """Return ``offset_db`` as ``%.6f``, negative zero as zero (the digest's spelling)."""
    text = '%.6f' % offset_db
    return '0.000000' if text == '-0.000000' else text


class GainSegment(collections.namedtuple(
        'GainSegment', 'id start_ns end_ns offset_db provenance note')):
    """One declared deviation from the reference gain state, ``[start_ns, end_ns)``."""

    __slots__ = ()


class GainDeclaration:
    """A loaded declaration: a reference state label and sorted, non-overlapping segments."""

    def __init__(self, path, reference_state, segments, sonar='', warnings=()):
        """Create it; ``segments`` must already be sorted and checked."""
        self.path = path
        self.reference_state = reference_state
        self.sonar = sonar
        self.segments = list(segments)
        self.warnings = list(warnings)

    def segment_at(self, stamp_ns_value):
        """Return the segment containing the stamp (half-open), or None: at the reference."""
        import bisect
        i = bisect.bisect_right([s.start_ns for s in self.segments], stamp_ns_value)
        if i == 0:
            return None
        seg = self.segments[i - 1]
        return seg if stamp_ns_value < seg.end_ns else None

    def offset_db_at(self, stamp_ns_value):
        """Return the dB to ADD to a ping reported at the stamp (0 outside every segment)."""
        seg = self.segment_at(stamp_ns_value)
        return seg.offset_db if seg is not None else 0.0

    def canonical_form(self):
        """
        Return the text the digest is taken of (GainDeclaration::canonicalForm in C++).

        The format version, the reference state and each segment's start_ns,
        end_ns and offset_db (``%.6f``) in sorted order. Ids, provenance, notes,
        comments, whitespace and field order are not in it.
        """
        lines = ['gain-state-declaration v1', 'reference_state=%s' % self.reference_state]
        lines += ['%d,%d,%s' % (s.start_ns, s.end_ns, _gain_offset_text(s.offset_db))
                  for s in self.segments]
        return '\n'.join(lines) + '\n'

    def sha256(self):
        """Return the SHA-256 (lowercase hex) of the canonical form."""
        import hashlib
        return hashlib.sha256(self.canonical_form().encode('utf-8')).hexdigest()


def load_gain_declaration(path):
    """
    Load a ``gain-state-declaration v1`` file; ValueError (``path:line: why``) on any problem.

    Whole or nothing, with the same rules as the C++ loader: the format line first,
    one ``# reference_state:`` label, UTC stamps with an explicit exclusive end,
    no overlap, ids unique, a finite offset within +-60 dB, a provenance of
    settings_file, measured or operator_reported and a non-empty note, no quoting
    and no comma inside a field. Zero segments is legal (a warning).
    """
    try:
        st = os.stat(path)
    except FileNotFoundError:
        raise ValueError('%s: cannot open (no such file)' % path)
    except OSError as e:
        raise ValueError('%s: cannot open (%s)' % (path, e))
    if not stat.S_ISREG(st.st_mode):
        raise ValueError('%s: not a regular file (a declaration is read from a file, never a '
                         'FIFO, device or directory)' % path)
    try:
        with open(path, 'rb') as f:
            raw = f.read(GAIN_MAX_BYTES + 1)
    except OSError as e:
        raise ValueError('%s: cannot open (%s)' % (path, e))
    if len(raw) > GAIN_MAX_BYTES:
        raise ValueError('%s: more than %d bytes, more than a declaration can be'
                         % (path, GAIN_MAX_BYTES))
    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError as e:
        raise ValueError('%s: not UTF-8 (%s)' % (path, e))
    if text.startswith('﻿'):
        text = text[1:]

    def fail(line_no, why):
        return ValueError('%s:%d: %s' % (path, line_no, why))

    lines = [(i, ln.rstrip('\r\n')) for i, ln in enumerate(text.split('\n'), 1)]
    lines = [(i, ln.rstrip('\r')) for i, ln in lines]
    first = next(((i, ln.strip(' \t')) for i, ln in lines if ln.strip(' \t')), None)
    if first is None:
        raise fail(len(lines), 'empty file, not a gain-state declaration')
    if first[1] != GAIN_FORMAT_LINE:
        if first[1].startswith(GAIN_FORMAT_PREFIX):
            raise fail(first[0], "unsupported declaration format '%s' (this loader reads '%s')"
                       % (first[1][:200], GAIN_FORMAT_LINE))
        raise fail(first[0], "not a gain-state declaration (first line must be '%s')"
                   % GAIN_FORMAT_LINE)

    reference = None
    sonar = ''
    rows = None
    last = first[0]
    for i, ln in lines:
        if i <= first[0]:
            continue
        last = i
        if rows is not None:
            if ln and not ln.startswith('#'):
                rows.append((i, ln))
            continue
        if not ln:
            continue
        if ln.startswith('#'):
            body = ln[1:].strip(' \t')
            if ':' not in body:
                continue
            key, _, value = body.partition(':')
            key, value = key.strip(' \t'), value.strip(' \t')
            if key == 'format':
                raise fail(i, "a second '# format:' line")
            if key == 'reference_state':
                if reference is not None:
                    raise fail(i, "'# reference_state:' given twice")
                if not GAIN_IDENTIFIER.match(value):
                    raise fail(i, "'# reference_state:' must be a non-empty label of "
                               "[A-Za-z0-9_.-], got '%s'" % value[:200])
                reference = value
            elif key == 'sonar':
                sonar = value
            continue
        if ln != GAIN_COLUMNS:
            raise fail(i, "expected the column line '%s'" % GAIN_COLUMNS)
        rows = []
    if reference is None:
        raise fail(last, "header lacks '# reference_state:'")
    if rows is None:
        raise fail(last, 'no column line')

    segments = []
    ids = set()
    for i, ln in rows:
        if '"' in ln:
            raise fail(i, 'a quote character: fields are never quoted')
        f = [c.strip(' \t') for c in ln.split(',')]
        if len(f) != 6:
            raise fail(i, 'expected 6 fields, got %d (a field may not contain a comma)'
                       % len(f))
        seg_id = f[0]
        if not GAIN_IDENTIFIER.match(seg_id):
            raise fail(i, "segment id '%s' must be non-empty [A-Za-z0-9_.-]" % seg_id[:200])
        if seg_id in ids:
            raise fail(i, "duplicate segment id '%s'" % seg_id)
        ids.add(seg_id)
        start = parse_utc_ns(f[1])
        if start is None:
            raise fail(i, "start_utc '%s' is not a UTC stamp YYYY-MM-DDTHH:MM:SS[.fff]Z"
                       % f[1][:200])
        end = parse_utc_ns(f[2])
        if end is None:
            raise fail(i, "end_utc '%s' is not a UTC stamp YYYY-MM-DDTHH:MM:SS[.fff]Z"
                       % f[2][:200])
        if not start < end:
            raise fail(i, 'start_utc must be before end_utc (the interval is [start, end))')
        offset = float('nan')
        if GAIN_DECIMAL.fullmatch(f[3]):
            offset = float(f[3])
            mantissa = re.split('[eE]', f[3])[0]
            if (offset == 0.0 and any(c in '123456789' for c in mantissa)) or (
                    0.0 < abs(offset) < sys.float_info.min):
                offset = float('nan')     # underflow or subnormal: refused, as the C++ loader does
        if not math.isfinite(offset):
            raise fail(i, "offset_db '%s' is not a finite number" % f[3][:200])
        if abs(offset) > GAIN_MAX_ABS_OFFSET_DB:
            raise fail(i, 'offset_db %s is beyond +-%d dB (a typo?)'
                       % (f[3], GAIN_MAX_ABS_OFFSET_DB))
        if f[4] not in GAIN_PROVENANCE:
            raise fail(i, "provenance '%s' must be settings_file, measured or "
                       'operator_reported' % f[4][:200])
        if not f[5]:
            raise fail(i, 'provenance_note is mandatory: say how the offset was obtained')
        segments.append(GainSegment(seg_id, start, end, offset, f[4], f[5]))

    segments.sort(key=lambda s: s.start_ns)
    for a, b in zip(segments, segments[1:]):
        if a.end_ns > b.start_ns:
            raise ValueError("%s: segments '%s' and '%s' overlap (touching is fine, "
                             'overlapping is not)' % (path, a.id, b.id))
    warnings = []
    if not segments:
        warnings.append("declares only the reference state '%s' and no segment: every ping "
                        'is taken to be at the reference' % reference)
    return GainDeclaration(path, reference, segments, sonar, warnings)


class GainTracker:
    """Apply a declaration ping by ping and count what it did (GainStateResolver in C++)."""

    def __init__(self, declaration):
        """Start counting against ``declaration``."""
        self.declaration = declaration
        self.pings = 0
        self.undeclared = 0
        self.by_segment = {s.id: 0 for s in declaration.segments}
        self.beams = {s.id: 0 for s in declaration.segments}

    def offset(self, ping_ns):
        """Return the dB to add to the ping stamped ``ping_ns`` and count the ping."""
        self.pings += 1
        seg = self.declaration.segment_at(ping_ns)
        if seg is None:
            self.undeclared += 1
            return 0.0
        self.by_segment[seg.id] += 1
        return seg.offset_db

    def note_beams(self, ping_ns, n_beams):
        """Count ``n_beams`` usable beams of a ping against its segment."""
        seg = self.declaration.segment_at(ping_ns)
        if seg is not None:
            self.beams[seg.id] += n_beams

    def state(self):
        """
        Return the counters as plain data (the accumulation cache payload).

        Per segment BY POSITION (the sorted order), not by id: the cache identity is the
        canonical digest, which excludes ids, so a cache may be refitted under a declaration
        whose segments were renamed or whose ids were swapped; the intervals are identical
        in the same order, and the counts belong to the interval.
        """
        return {'pings': self.pings, 'undeclared': self.undeclared,
                'segments': [[self.by_segment[s.id], self.beams[s.id]]
                             for s in self.declaration.segments]}

    def restore(self, state):
        """Set the counters from ``state`` (see ``state``), onto the current ids by position."""
        segments = self.declaration.segments
        if len(state['segments']) != len(segments):
            raise ValueError('the cached gain counters have %d segments, the declaration %d'
                             % (len(state['segments']), len(segments)))
        self.pings = state['pings']
        self.undeclared = state['undeclared']
        for seg, (pings, beams) in zip(segments, state['segments']):
            self.by_segment[seg.id] = pings
            self.beams[seg.id] = beams

    def summary(self):
        """Return what the table header and the run report print, as plain data."""
        d = self.declaration
        return {'reference_state': d.reference_state, 'sha256': d.sha256(), 'source': d.path,
                'pings': self.pings, 'undeclared_pings': self.undeclared,
                'segments': [{'id': s.id, 'start_ns': s.start_ns, 'end_ns': s.end_ns,
                              'offset_db': s.offset_db, 'provenance': s.provenance,
                              'pings': self.by_segment[s.id], 'beams': self.beams[s.id]}
                             for s in d.segments]}


def utc_text(ns):
    """Return integer ns since the epoch as ``YYYY-MM-DDTHH:MM:SS[.f]Z``."""
    import datetime
    t = datetime.datetime(1970, 1, 1) + datetime.timedelta(seconds=ns // 1000000000)
    text = t.strftime('%Y-%m-%dT%H:%M:%S')
    frac = ns % 1000000000
    if frac:
        text += ('.%09d' % frac).rstrip('0')
    return text + 'Z'


def gain_segment_lines(summary):
    """Return the ``# gain_segment:`` / ``# gain_undeclared:`` provenance lines of a summary."""
    lines = ['# gain_segment: %s [%s, %s) offset_db=%s provenance=%s pings=%d beams=%d'
             % (s['id'], utc_text(s['start_ns']), utc_text(s['end_ns']),
                _gain_offset_text(s['offset_db']), s['provenance'], s['pings'], s['beams'])
             for s in summary['segments']]
    lines.append('# gain_undeclared: %d pings at the reference state'
                 % summary['undeclared_pings'])
    return lines


def gain_header_lines(summary):
    """Return the header lines a derivation under a declaration writes (curve and table)."""
    return (['# gain_reference_state: %s' % summary['reference_state'],
             '# gain_declaration_sha256: %s' % summary['sha256'],
             '# gain_declaration_source: %s' % summary['source'],
             '# gain_sign: offset_db was ADDED to the reported level of each ping before '
             'fitting, so the result is in the reference state'] + gain_segment_lines(summary))


def gain_report_lines(summary):
    """Return the human run report of a summary (the same counts import_bag prints)."""
    out = ["gain-state declaration %s: reference state '%s', sha256 %s; %d ping(s) seen"
           % (summary['source'], summary['reference_state'], summary['sha256'],
              summary['pings'])]
    for s in summary['segments']:
        out.append('  %s [%s, %s) %+.3f dB (%s): %d pings, %d beams'
                   % (s['id'], utc_text(s['start_ns']), utc_text(s['end_ns']), s['offset_db'],
                      s['provenance'], s['pings'], s['beams']))
    out.append('  undeclared (at reference): %d pings' % summary['undeclared_pings'])
    return out


def gain_empty_segment_warnings(summary, box=False):
    """
    Return one warning per declared segment that no ping fell in (a mistyped stamp).

    The counts are of the pings this run used. With ``--box`` the pings outside the box never
    reach the counters, so a segment covering only transit is not evidence of a mistyped
    stamp: the warning says the counts are box-filtered instead.
    """
    note = (' (counts cover only pings inside --box; this may just be transit)' if box
            else ': check its stamps')
    return ["warning: gain segment '%s' is declared but no ping fell in it%s" % (s['id'], note)
            for s in summary['segments'] if s['pings'] == 0]


class PingRecord:
    """
    The fields of a SonarDetections ping that bin_ping reads, and nothing else.

    A deserialized message is several times larger than its serialized bytes
    (one Python object per beam flag, the header, the ping_info strings); a
    record keeps the three per-beam float32 arrays as they are, the flags as
    one byte each, and two floats -- about 13 bytes per beam.
    """

    __slots__ = ('intensities', 'rx_angles', 'two_way_travel_times',
                 'sound_speed', 'frequency', 'flags', 'stamp_ns')

    def __init__(self, msg):
        """Copy the binning fields of a SonarDetections ``msg``."""
        self.intensities = msg.intensities
        self.rx_angles = msg.rx_angles
        self.two_way_travel_times = msg.two_way_travel_times
        self.sound_speed = msg.ping_info.sound_speed
        self.frequency = msg.ping_info.frequency
        self.stamp_ns = stamp_ns(msg.header.stamp)   # the gain declaration's clock (#176)
        flags = getattr(msg, 'flags', None)
        self.flags = (None if flags is None
                      else bytes(getattr(f, 'flag', 0) for f in flags))


# --box drop reasons, in report order, with the words the run report uses.
BOX_DROP_REASONS = (
    ('outside', 'outside the box'),
    ('no_tf', 'with no earth transform for their frame'),
    ('extrapolation', 'outside the earth TF coverage at their stamp'),
    ('bad_stamp', 'with an unset (zero) header stamp or one more than %g s '
                  'ahead of the TF stream' % PING_FUTURE_LIMIT_SEC),
    ('bad_position', 'with an earth transform that is not a geocentric position'),
)


# Transforms PingLocator keeps out of the tf2 buffer, in report order.
TF_REJECT_REASONS = (
    ('invalid', 'invalid (NaN, zero quaternion, bad frame names or unset stamp)'),
    ('old', 'more than %g s older than the newest of their frame' % TF_CACHE_SEC),
    ('future', 'stamped more than %g s further ahead of the bag time than '
               'their frame, or more than %g s off it before their frame had '
               'a clock reference' % (TF_CACHE_SEC, PING_FUTURE_LIMIT_SEC)),
)


def tf_rejected_warning(tf_rejected):
    """Return the stderr warning for PingLocator.tf_rejected, or None."""
    total = sum(tf_rejected.values())
    if not total:
        return None
    return 'warning: --box ignored %d transform(s): %s' % (
        total, ', '.join('%d %s' % (tf_rejected[key], words)
                         for key, words in TF_REJECT_REASONS))


def new_box_stats():
    """
    Return zeroed --box counters.

    'kept', one per drop reason, 'max_pending', the most pings that waited
    for the TF stream at once, and 'tf_rejected', PingLocator's counter of
    transforms kept out of the buffer.
    """
    stats = {'kept': 0, 'max_pending': 0, 'tf_rejected': collections.Counter()}
    stats.update((key, 0) for key, _words in BOX_DROP_REASONS)
    return stats


def box_report(box, stats, n_pings):
    """Return the one-line --box summary printed after the pass."""
    return 'box %s: kept %d of %d pings; dropped %s; at most %d waited for TF' % (
        ' '.join('%g' % v for v in box), stats['kept'], n_pings,
        ', '.join('%d %s' % (stats[key], words) for key, words in BOX_DROP_REASONS),
        stats['max_pending'])


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
                   help='angular bin width in degrees, > 0 and <= 90 '
                        '(default 2.0)')
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
    p.add_argument('--gain-declaration', default=None, metavar='FILE',
                   help='gain-state declaration (first line "# format: gain-state-declaration '
                        'v1", cube_bathymetry#176): a reference gain state plus the half-open '
                        'UTC segments where the receiver gain deviated from it, each with an '
                        'additive dB offset (reported + offset = reference level). The '
                        'offset of each ping is added to its intensities BEFORE binning, so one '
                        'curve or table comes out in the reference state; the header records '
                        'the digest of the declaration, which import_bag and batch_regen compare '
                        'with the declaration they are given')
    p.add_argument('--box', type=float, nargs=4, default=None,
                   metavar=('LATMIN', 'LONMIN', 'LATMAX', 'LONMAX'),
                   help="keep only pings whose sensor position (from the bag's "
                        'TF tree, earth -> detections frame) lies inside this '
                        'WGS84 box, degrees; for deriving a per-site curve from '
                        'bags that mix survey and transit')
    m = p.add_argument_group(
        'per-mode table (--by-mode, cube_bathymetry#172)',
        'Attribute each ping to its pulse mode (the latest SonarInfo at or '
        'before it) and write an angular-response-table v1: per mode the '
        'angular intercepts referred to R0, a binned log-range term, per-bin '
        'variance, and an offset against a reference mode.')
    m.add_argument('--by-mode', action='store_true',
                   help='write the per-mode table instead of a single curve '
                        '(refused with --remove-tl: the range term replaces the '
                        'modelled TL add-back)')
    m.add_argument('--sonar-info-topic', default=None,
                   help='marine_interfaces/SonarInfo topic (default: the '
                        "detections topic's sibling 'sonar_info')")
    m.add_argument('--reference-mode', default=None,
                   help='mode key the offsets are measured against (default %s, '
                        'the Kongsberg M3 1 ms / 24 kHz FM-up mode; when absent the '
                        'most-sampled mode is used with a warning)'
                        % DEFAULT_REFERENCE_MODE)
    m.add_argument('--range-ref-m', type=float, default=None,
                   help='reference slant range R0, m (default: the reference '
                        "mode's median slant range)")
    m.add_argument('--adjacency-window-s', type=float, default=None,
                   help='pings within this many seconds either side of a mode '
                        'switch measure the offset (default %g)'
                        % DEFAULT_ADJACENCY_WINDOW_SEC)
    m.add_argument('--specular-exclusion-deg', type=float, default=None,
                   help='|angle| below which beams are left out of the range fit '
                        'and the offsets (default %g)' % DEFAULT_SPECULAR_EXCLUSION_DEG)
    m.add_argument('--misfit-threshold-db', type=float, default=None,
                   help='flag a mode whose binned post-fit residual exceeds this '
                        '(default %g dB)' % DEFAULT_MISFIT_THRESHOLD_DB)
    m.add_argument('--range-node-spacing-decades', type=float, default=None,
                   help='spacing of the range-term nodes in decades of log10(R / R0) '
                        '(default %g; the misfit bins are half of it; finer nodes '
                        'follow sub-node structure at the cost of support per node)'
                        % DEFAULT_RANGE_NODE_SPACING_DECADES)
    m.add_argument('--range-angle-edges', default=None,
                   help='|angle| edges, degrees, comma-separated, that split the '
                        'beams outside the specular band into the groups the range '
                        'term is fitted per (default %s, i.e. groups %g-%s, %s-%s and '
                        '%s-90 deg); "none" fits one term per mode'
                        % (','.join('%g' % e for e in DEFAULT_RANGE_ANGLE_EDGES_DEG),
                           DEFAULT_SPECULAR_EXCLUSION_DEG,
                           '%g' % DEFAULT_RANGE_ANGLE_EDGES_DEG[0],
                           '%g' % DEFAULT_RANGE_ANGLE_EDGES_DEG[0],
                           '%g' % DEFAULT_RANGE_ANGLE_EDGES_DEG[1],
                           '%g' % DEFAULT_RANGE_ANGLE_EDGES_DEG[1]))
    m.add_argument('--diagnostics-out', default=None,
                   help='also write every fit and offset diagnostic as JSON here')
    m.add_argument('--accumulation-cache', default=None,
                   help='pickle of the accumulated per-mode statistics: written '
                        'after the bag pass, and when it already exists the bag '
                        'pass is skipped and the table is refitted from it (same '
                        'bags, topic, box and bin width required; a local cache for '
                        'iterating on the fit, not an interchange format)')
    args = p.parse_args(argv)
    if not (math.isfinite(args.bin_width_deg) and 0.0 < args.bin_width_deg <= 90.0):
        p.error('--bin-width-deg must be > 0 and <= 90 degrees, got %r'
                % args.bin_width_deg)
    check_by_mode_args(p, args)
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


BY_MODE_ONLY_FLAGS = (('--sonar-info-topic', 'sonar_info_topic'),
                      ('--reference-mode', 'reference_mode'),
                      ('--range-ref-m', 'range_ref_m'),
                      ('--adjacency-window-s', 'adjacency_window_s'),
                      ('--specular-exclusion-deg', 'specular_exclusion_deg'),
                      ('--misfit-threshold-db', 'misfit_threshold_db'),
                      ('--range-angle-edges', 'range_angle_edges'),
                      ('--range-node-spacing-decades', 'range_node_spacing_decades'),
                      ('--accumulation-cache', 'accumulation_cache'),
                      ('--diagnostics-out', 'diagnostics_out'))


def check_by_mode_args(p, args):
    """Validate the --by-mode options and fill their defaults (argparse errors)."""
    if not args.by_mode:
        given = [flag for flag, name in BY_MODE_ONLY_FLAGS
                 if getattr(args, name) is not None]
        if given:
            p.error('%s only appl%s with --by-mode'
                    % (', '.join(given), 'ies' if len(given) == 1 else 'y'))
        return
    if args.remove_tl:
        p.error('--by-mode is refused with --remove-tl: a per-mode table carries '
                'its own measured range term, which replaces the modelled '
                '40*log10(R) + 2*alpha*R add-back (cube_bathymetry#172)')
    if args.sonar_info_topic is None:
        args.sonar_info_topic = default_sonar_info_topic(args.topic)
    if args.sonar_info_topic == args.topic:
        p.error('--sonar-info-topic %r is the detections topic itself; the SonarInfo '
                'stream must be a separate topic' % args.topic)
    if args.reference_mode is None:
        args.reference_mode = DEFAULT_REFERENCE_MODE
    if args.adjacency_window_s is None:
        args.adjacency_window_s = DEFAULT_ADJACENCY_WINDOW_SEC
    if args.specular_exclusion_deg is None:
        args.specular_exclusion_deg = DEFAULT_SPECULAR_EXCLUSION_DEG
    if args.misfit_threshold_db is None:
        args.misfit_threshold_db = DEFAULT_MISFIT_THRESHOLD_DB
    if args.range_node_spacing_decades is None:
        args.range_node_spacing_decades = DEFAULT_RANGE_NODE_SPACING_DECADES
    for flag, value, ok in (
            ('--range-node-spacing-decades', args.range_node_spacing_decades,
             math.isfinite(args.range_node_spacing_decades)
             and 2.0 * RANGE_CELL_DECADES <= args.range_node_spacing_decades <= 1.0),
            ('--range-ref-m', args.range_ref_m,
             args.range_ref_m is None or (math.isfinite(args.range_ref_m)
                                          and 0.0 < args.range_ref_m < 10000.0)),
            ('--adjacency-window-s', args.adjacency_window_s,
             math.isfinite(args.adjacency_window_s)
             and 0.0 < args.adjacency_window_s <= 600.0),
            ('--specular-exclusion-deg', args.specular_exclusion_deg,
             math.isfinite(args.specular_exclusion_deg)
             and 0.0 <= args.specular_exclusion_deg < 80.0),
            ('--misfit-threshold-db', args.misfit_threshold_db,
             math.isfinite(args.misfit_threshold_db) and args.misfit_threshold_db > 0.0)):
        if not ok:
            p.error('%s %r is out of range' % (flag, value))
    if not args.reference_mode or any(c in args.reference_mode for c in ', \t\r\n'):
        p.error('--reference-mode %r is not a mode key' % args.reference_mode)
    try:
        if args.range_angle_edges is None:
            args.range_angle_edges = DEFAULT_RANGE_ANGLE_EDGES_DEG
        else:
            args.range_angle_edges = parse_range_angle_edges(args.range_angle_edges)
        # The default edges can leave an empty group too (a wide bin width or a
        # large specular exclusion): validate whatever will be used.
        range_angle_groups(args.bin_width_deg, args.specular_exclusion_deg,
                           args.range_angle_edges)
    except ValueError as e:
        p.error(str(e))


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


def storage_id_for(bag_path):
    """Return the rosbag2 storage id for ``bag_path`` ('' = auto-detect)."""
    if bag_path.endswith('.mcap'):
        return 'mcap'
    if bag_path.endswith('.db3'):
        return 'sqlite3'
    return ''


def read_bag_metadata(bag_path):
    """
    Return ``({topic: type}, start_ns)`` from a bag's metadata.

    Nothing is held open afterwards, so every bag on the command line can be
    checked before any is read, however many split files there are.
    """
    import rosbag2_py

    meta = rosbag2_py.Info().read_metadata(bag_path, storage_id_for(bag_path))
    types = {t.topic_metadata.name: t.topic_metadata.type
             for t in meta.topics_with_message_count}
    return types, int(meta.starting_time.nanoseconds)


def open_reader(bag_path, topics=None):
    """
    Open a rosbag2 SequentialReader, auto-detecting the storage backend.

    With ``topics`` the reader yields only those topics.
    """
    import rosbag2_py

    storage_options = rosbag2_py.StorageOptions(
        uri=bag_path, storage_id=storage_id_for(bag_path))
    converter_options = rosbag2_py.ConverterOptions(
        input_serialization_format='cdr', output_serialization_format='cdr')
    reader = rosbag2_py.SequentialReader()
    reader.open(storage_options, converter_options)
    if topics:
        reader.set_filter(rosbag2_py.StorageFilter(topics=list(topics)))
    return reader


def merged_messages(sources):
    """
    Yield ``(source_index, topic, data, t_ns)`` from every source in time order.

    ``sources`` is a list of ``(start_ns, open_fn)``: the bag's first message
    time (from its metadata) and a function returning an opened reader. A
    k-way merge on each message's bag timestamp, so several bags -- split
    files of one recording, or bags recorded side by side -- are read as one
    chronological stream, as import_bag's BagReaders does. Ties go to the
    source given first.

    A bag is opened only once the stream reaches its start time and is closed
    as soon as it is exhausted, so a recording split into hundreds of files
    holds one or two open at a time, not one per file.
    """
    waiting = collections.deque(
        sorted(range(len(sources)), key=lambda i: (sources[i][0], i)))
    heap = []
    readers = {}

    def advance(i):
        reader = readers[i]
        if reader.has_next():
            name, data, t_ns = reader.read_next()
            heapq.heappush(heap, (t_ns, i, name, data))
        else:
            del readers[i]
            reader.close()

    try:
        while heap or waiting:
            while waiting and (not heap or sources[waiting[0]][0] <= heap[0][0]):
                i = waiting.popleft()
                readers[i] = sources[i][1]()
                advance(i)
            if not heap:
                continue
            # (t_ns, i) is unique: each reader has at most one message in the heap.
            t_ns, i, name, data = heapq.heappop(heap)
            yield i, name, data, t_ns
            advance(i)
    finally:
        for reader in readers.values():
            reader.close()


def deserialize_or_none(data, msg_type):
    """
    Deserialize one bag message, or return None when it is corrupt.

    rclpy raises (an RMWError, a RuntimeError) on bytes that are not a valid
    CDR ``msg_type``; one bad message must not abort a run over many bags.
    """
    from rclpy.serialization import deserialize_message

    try:
        return deserialize_message(data, msg_type)
    except (RuntimeError, ValueError, MemoryError):
        return None


def corrupt_report(corrupt):
    """Return the stderr warning for the corrupt-message counter, or None."""
    total = sum(corrupt.values())
    if not total:
        return None
    by_topic = ', '.join('%d on %s' % (n, name) for name, n in sorted(corrupt.items()))
    return ('warning: skipped %d corrupt message(s) that would not deserialize '
            '(%s)' % (total, by_topic))


def accumulate(bag_paths, topic, bin_width_deg, remove_tl, bins, freqs,
               box=None, box_stats=None, corrupt=None, gain=None):
    """
    Accumulate per-bin sums on `topic` over every bag into `bins`.

    Each bin slot is ``[sum_adjusted, sum_R, count]``:

    * tier-1 (``remove_tl`` False): ``sum_adjusted`` is the raw dB sum; ``sum_R``
      stays 0 (unused).
    * tier-2 (``remove_tl`` True): the range-INDEPENDENT-of-alpha part of the TL
      (the spreading term ``40*log10(R)``) is compensated (added back) inline, so
      ``sum_adjusted`` is ``sum(db + 40*log10(R))`` and ``sum_R`` is ``sum(R)``. The
      absorption term ``2*alpha*R`` is added in write_csv once alpha is known from the
      median frequency -- a single pass over the bags.

    `freqs` collects each ping's ``ping_info.frequency`` (Hz) for the median.

    `gain` (a GainTracker, cube_bathymetry#176) adds each ping's declared offset
    to its intensities BEFORE binning (and before the tier-2 TL add-back), so
    the curve comes out in the declaration's reference state.

    Every bag's topics are checked from its metadata before any bag is read.
    Without `box` the bags are opened and read one after another, each closed
    before the next is opened; the binned sums do not depend on the order.

    With `box` (latmin, lonmin, latmax, lonmax) each bag is filtered to the
    detections topic plus every /tf and /tf_static topic in it, and all bags
    are merged into ONE time-ordered pass (merged_messages) feeding ONE
    PingLocator, as import_bag does: a /tf_static recorded only in the first
    split file still positions the pings of the later splits, and a bag holding
    only TF positions the pings of a bag recorded beside it (so with `box` a
    bag without `topic` is allowed; only no bag at all having it is an error,
    whereas without `box` every bag must have it). Each ping is queued
    until each dynamic link of its earth chain is TF_GUARD_SEC past its stamp
    (see PingLocator), looked up exactly,
    and binned only if its sensor position lies inside the box. `box_stats` (see
    new_box_stats) counts kept and dropped pings; pings that are dropped
    contribute neither samples nor a frequency.

    A message that will not deserialize is skipped and counted per topic in
    `corrupt` (a collections.Counter), and the run goes on.
    Returns the number of messages read on `topic` (corrupt ones excluded).
    """
    from rosidl_runtime_py.utilities import get_message

    if corrupt is None:
        corrupt = collections.Counter()

    metas = [read_bag_metadata(path) for path in bag_paths]
    type_maps = [type_map for type_map, _start_ns in metas]
    for path, type_map in zip(bag_paths, type_maps):
        if topic not in type_map and box is None:
            raise SystemExit(
                "error: topic '%s' not in bag %s (have: %s)"
                % (topic, path, ', '.join(sorted(type_map)) or 'none'))
    if not any(topic in type_map for type_map in type_maps):
        # Only reachable with --box, where a bag without the detections topic
        # may still carry the TF that positions another bag's pings.
        raise SystemExit(
            "error: topic '%s' not in any bag (have: %s)"
            % (topic, ', '.join(sorted(set().union(*type_maps))) or 'none'))

    def add_ping(ping):
        offset_db = gain.offset(ping.stamp_ns) if gain is not None else 0.0
        used = bin_ping(ping, bin_width_deg, remove_tl, bins, freqs, offset_db)
        if gain is not None:
            gain.note_beams(ping.stamp_ns, used)

    n_msgs = 0
    if box is None:
        for path, type_map in zip(bag_paths, type_maps):
            msg_type = get_message(type_map[topic])
            reader = open_reader(path, [topic])
            try:
                while reader.has_next():
                    name, data, _stamp = reader.read_next()
                    if name != topic:
                        continue
                    msg = deserialize_or_none(data, msg_type)
                    if msg is None:
                        corrupt[name] += 1
                        continue
                    n_msgs += 1
                    add_ping(PingRecord(msg))
            finally:
                reader.close()
        return n_msgs

    sources = []      # (start_ns, open_fn) for merged_messages
    tf_topics = []    # per source: {topic name: 'tf' | 'tf_static'}
    msg_types = []    # per source: the detections message class, or None
    for path, (type_map, start_ns) in zip(bag_paths, metas):
        kinds = {name: kind for name, type_name in type_map.items()
                 for kind in (is_tf_topic(name),)
                 if kind is not None and type_name == 'tf2_msgs/msg/TFMessage'}
        wanted = ([topic] if topic in type_map else []) + sorted(kinds)
        if not wanted:
            continue   # neither detections nor TF: nothing to read in it
        sources.append((start_ns, functools.partial(open_reader, path, wanted)))
        tf_topics.append(kinds)
        msg_types.append(get_message(type_map[topic]) if topic in type_map else None)
    if not any(kind == 'tf' for kinds in tf_topics for kind in kinds.values()):
        # Without a dynamic TF stream the frontier never moves: every ping would
        # wait in memory until the end and then find no earth transform.
        raise SystemExit(
            'error: --box positions pings from the dynamic TF (/tf) in the bags, '
            'and no bag has a /tf topic; add the bag that recorded the '
            "vehicle's /tf, or drop --box")
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

    for i, name, data, t_ns in merged_messages(sources):
        kind = tf_topics[i].get(name)
        if kind is None and name != topic:
            continue
        msg = deserialize_or_none(data, tf_msg_type if kind else msg_types[i])
        if msg is None:
            corrupt[name] += 1
        elif kind is not None:
            locator.add_tf(msg, kind == 'tf_static', t_ns)
            take_ready(False)
        else:
            n_msgs += 1
            reason = locator.push(
                stamp_ns(msg.header.stamp), msg.header.frame_id, PingRecord(msg))
            if reason is not None:
                box_stats[reason] += 1
    take_ready(True)   # end of the stream: position whatever is still waiting
    box_stats['max_pending'] = locator.max_pending
    box_stats['tf_rejected'] = locator.tf_rejected
    return n_msgs


def bin_ping(ping, bin_width_deg, remove_tl, bins, freqs, offset_db=0.0):
    """
    Add one ping's beams (a PingRecord) to `bins` (see accumulate); return how many were used.

    `offset_db` (the ping's declared gain offset, cube_bathymetry#176) is added to
    each beam's intensity first.
    """
    intensities = ping.intensities
    rx_angles = ping.rx_angles
    twtt = ping.two_way_travel_times
    sound_speed = ping.sound_speed
    freq = ping.frequency
    if remove_tl and math.isfinite(freq) and freq > 0.0:
        freqs.append(freq)  # only needed for the tier-2 absorption alpha
    flags = ping.flags   # one flag value per beam, or None
    count = min(len(intensities), len(rx_angles))
    used = 0
    for i in range(count):
        db = intensities[i]
        if not math.isfinite(db):
            continue
        db += offset_db
        # Skip beams flagged bad (flag != 0) when flags are present and aligned.
        if flags is not None and i < len(flags) and flags[i] != 0:
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
        used += 1
    return used


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


def check_distinct_bags(bag_paths):
    """
    Refuse a bag given twice, which would count every ping in it twice.

    Paths are compared after resolving symlinks, ``..`` and trailing slashes,
    and a storage file is the same bag as a bag directory given that holds it.
    Bags that merely overlap in time are not detected: their shared pings are
    counted once per bag.
    """
    seen = {}
    for path in bag_paths:
        real = os.path.realpath(path)
        if real in seen:
            raise SystemExit('error: bag %r is the same as %r; give each bag once'
                             % (path, seen[real]))
        seen[real] = path
    for path in bag_paths:
        parent = os.path.dirname(os.path.realpath(path))
        if parent in seen and os.path.isdir(parent) and seen[parent] != path:
            raise SystemExit('error: bag %r is a file inside the bag directory %r; '
                             'give each bag once' % (path, seen[parent]))


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

    A symlinked ``path`` is written through: its final target is replaced and
    the link is kept. A path that is not (or would not be) a regular file -- a
    device or pipe such as ``/dev/stdout`` or ``/dev/null``, or anything under
    ``/dev`` or ``/proc`` -- is written directly, as open(path, 'w') would,
    since it cannot be replaced by a temp file.

    When no temp file can be created beside the target (a read-only
    directory holding a writable file), an existing writable file is written
    in place, as open(path, 'w') did, with a warning that a failure part-way
    would leave it truncated (and such a failure's error says it may now be);
    otherwise the run exits with an error naming the directory. Any other
    failure to write is an error naming the path, never a traceback.
    """
    if not _replaceable_regular_path(path):
        _write_in_place(path, text)
        return
    path = os.path.realpath(path)
    directory = os.path.dirname(path) or '.'
    try:
        mode = stat.S_IMODE(os.stat(path).st_mode)
    except FileNotFoundError:
        umask = os.umask(0)
        os.umask(umask)
        mode = 0o666 & ~umask
    try:
        fd, tmp_path = tempfile.mkstemp(
            dir=directory, prefix='.%s.' % os.path.basename(path), suffix='.tmp')
    except OSError as e:
        if (e.errno in (errno.EACCES, errno.EPERM) and os.path.isfile(path)
                and os.access(path, os.W_OK)):
            print('warning: cannot create a temp file in %s (%s); writing %s '
                  'in place instead, so a failure part-way would leave it '
                  'truncated' % (directory, e.strerror, path), file=sys.stderr)
            _write_in_place(path, text, '; it may now be truncated')
            return
        raise SystemExit('error: cannot write %s: cannot create a file in %s (%s)'
                         % (path, directory, e.strerror))
    try:
        with os.fdopen(fd, 'w') as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp_path, mode)
        os.replace(tmp_path, path)
    except BaseException as e:
        try:
            os.unlink(tmp_path)
        except FileNotFoundError:
            pass
        if isinstance(e, OSError):
            raise SystemExit('error: cannot write %s (%s); it is unchanged'
                             % (path, e.strerror or e))
        raise


def _write_in_place(path, text, note=''):
    """Write ``text`` to ``path`` as open(path, 'w') does; on failure exit with ``note`` added."""
    try:
        with open(path, 'w') as f:
            f.write(text)
    except OSError as e:
        raise SystemExit('error: cannot write %s (%s)%s' % (path, e.strerror, note))


def _replaceable_regular_path(path):
    """Return True when ``path`` names a regular file, or none yet, outside /dev and /proc."""
    special = ('/dev/', '/proc/')
    # The path as given first: /dev/stdout redirected to a file resolves to
    # that file, which must be written in place, not replaced.
    if os.path.abspath(path).startswith(special):
        return False
    if os.path.realpath(path).startswith(special):
        return False
    try:
        return stat.S_ISREG(os.stat(path).st_mode)
    except FileNotFoundError:
        return True


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


# ---------------------------------------------------------------------------
# --by-mode: per-pulse-mode table (cube_bathymetry#172)
# ---------------------------------------------------------------------------

MODE_TABLE_FORMAT = 'angular-response-table v1'
# The Kongsberg M3's 1 ms / 24 kHz FM-up mode at 500 kHz. A FIXED reference so
# tables from different campaigns share one; pass --reference-mode for other
# sonars.
DEFAULT_REFERENCE_MODE = 'FMU_1000us_24000Hz_500kHz'
DEFAULT_ADJACENCY_WINDOW_SEC = 2.0
DEFAULT_SPECULAR_EXCLUSION_DEG = 10.0
DEFAULT_MISFIT_THRESHOLD_DB = 1.5
# The range term's nodes: every quarter decade of log10(R / R0), node 0 at R0.
DEFAULT_RANGE_NODE_SPACING_DECADES = 0.25
# Samples are accumulated in fixed cells of log10(R) (absolute, metres) so the
# node grid can be placed relative to R0 after the pass, when R0 is known. A
# cell is a hundredth of a decade; the fit evaluates each cell at its centre, so
# the position error is at most 0.005 decade (0.1 dB at 20 dB/decade).
RANGE_CELL_DECADES = 0.01
RANGE_CELL_LOG10_MIN = -1.0          # 0.1 m
RANGE_CELL_COUNT = 500               # up to 10 km
# The misfit diagnostic bins residuals at half the node spacing, so structure
# between two nodes (a step inside a quarter decade) shows.
MISFIT_BINS_PER_NODE = 2
# The range term is identified only by range variation WITHIN an angle bin
# (depth change); below this pooled within-bin standard deviation of log10 R
# it is not fitted (a flat single-depth bottom spreads ~0.005 decade).
MIN_WITHIN_BIN_SPREAD_DECADES = 0.03
# Second-difference penalty on the range nodes, in sample-equivalents: keeps a
# thinly supported node smooth, negligible where a node has hundreds of
# samples. The tiny ridge fixes the constant shared by the angle intercepts and
# the range term (removed again by referring the term to R0).
RANGE_SMOOTHING_WEIGHT = 10.0
RANGE_RIDGE_WEIGHT = 1e-3
# A range node at either END of a group's grid is trimmed when it carries under
# this share of the group's beams (and under MIN_SAMPLES_PER_BIN): such a node
# describes the mode's rare excursions outside its operating range, typically
# junk detections (Appledore run 2: the 20 us mode's R0 node held 6e3 of 1e8
# beams, read 60 dB off, and anchored the whole term). Beams beyond the trimmed
# span are corrected with the clamped end value but do not shape the fit.
NODE_MIN_SHARE = 1e-3
# The misfit diagnostic reports a residual bin only when it holds at least this
# share of its group's fitted beams (and MIN_SAMPLES_PER_BIN), for the same
# reason: a 20 dB residual on 0.001 % of the beams is noise, not structure.
MISFIT_MIN_SHARE = 1e-3
# An angle bin's variance below this (it would print as 0.000 dB^2 with the
# table's three decimals) is written EMPTY: a loader weighting by 1/var_db2
# must never see a zero.
MIN_WRITTEN_VARIANCE_DB2 = 0.0005
# A mode pair needs this many switches before its offset is used.
MIN_SWITCHES_PER_PAIR = 3
# Up- vs down-switch offsets disagree when they differ by more than this AND by
# more than OFFSET_AGREEMENT_SE combined standard errors.
OFFSET_AGREEMENT_DB = 1.0
OFFSET_AGREEMENT_SE = 3.0
# Without --box, a ping is attributed to a SonarInfo once the bag clock is this
# far past the ping, so a SonarInfo recorded just after the first ping it
# describes (stamped before it) is still seen.
SONAR_INFO_GUARD_SEC = 1.0
# The range term is fitted per ANGLE GROUP of |theta| (deg): the first group
# starts at the specular exclusion, the last ends at 90. On the M3 the
# residual range dependence differs between the inner and the outer beams
# (Appledore, cube_bathymetry#172: 5-13 dB left in the 10-30 deg group by one
# term per mode), so one r per mode is not enough; a sonar whose range term
# IS separable gets the same r in every group, within noise. The edges snap
# to angle-bin boundaries and the snapped bounds are written in the table.
DEFAULT_RANGE_ANGLE_EDGES_DEG = (30.0, 50.0)

SIGNAL_TYPE_NAMES = {1: 'CW', 2: 'FMU', 3: 'FMD'}
TVG_MODEL_NAMES = {0: 'UNKNOWN', 1: 'NONE', 2: '20LOGR', 3: '40LOGR', 4: 'CUSTOM'}

TABLE_COLUMNS = ('mode', 'term', 'x', 'angle_lo_deg', 'angle_hi_deg', 'value_db', 'n',
                 'var_db2', 'db_relative_to_nadir', 'se_db', 'offset_db_vs_ref',
                 'offset_se_db', 'offset_anchored', 'declared_tvg_model',
                 'declared_tvg_absorption_db_per_km')


def key_number(value, scale=1.0):
    """
    Return one number of a mode key: 3 significant figures, plain decimal.

    Part of the mode-key spec (see canonical_mode_key). ``value`` is the raw
    message value (a float32 widened to double); NaN, infinite, non-positive or
    unconvertible values give ``?`` -- tested on the RAW value, before scaling
    and rounding. Otherwise ``value * scale`` (double) is formatted with C's
    ``%.2e`` (three significant figures, correctly rounded) and rewritten as a
    plain decimal: no exponent, no trailing fractional zeros, no trailing
    point (``1.00e+03`` -> ``1000``, ``2.40e+04`` -> ``24000``, ``2.00e+01`` ->
    ``20``, ``4.50e-01`` -> ``0.45``).
    """
    try:
        v = float(value)
    except (TypeError, ValueError):
        return '?'
    if not math.isfinite(v) or v <= 0.0:
        return '?'
    v *= scale
    if not math.isfinite(v) or v <= 0.0:
        return '?'
    mantissa, exponent = ('%.2e' % v).split('e')
    digits = mantissa.replace('.', '')      # three digits, the first non-zero
    shift = int(exponent) - 2               # value = digits * 10**shift
    if shift >= 0:
        return digits + '0' * shift
    padded = digits.rjust(1 - shift, '0')   # at least one integer digit
    return (padded[:shift] + '.' + padded[shift:]).rstrip('0').rstrip('.')


def sector_mode_key(pulse_lengths, bandwidths, tx_signal_types):
    """
    Return the transmit-sector part of a mode key (see canonical_mode_key).

    One ``<TYPE>_<pulse_us>us_<bw_Hz>Hz`` per sector, joined with ``+``.
    """
    pulses = [] if pulse_lengths is None else list(pulse_lengths)
    widths = [] if bandwidths is None else list(bandwidths)
    types = [] if tx_signal_types is None else list(tx_signal_types)
    n_sectors = max(len(pulses), len(widths), len(types), 1)
    parts = []
    for i in range(n_sectors):
        kind = SIGNAL_TYPE_NAMES.get(int(types[i]), 'UNK') if i < len(types) else 'UNK'
        pulse = key_number(pulses[i], 1e6) if i < len(pulses) else '?'
        width = key_number(widths[i]) if i < len(widths) else '?'
        parts.append('%s_%sus_%sHz' % (kind, pulse, width))
    return '+'.join(parts)


def canonical_mode_key(pulse_lengths, bandwidths, tx_signal_types, frequency_hz):
    """
    Return the canonical pulse-mode key of a ping: THE SPEC (cube_bathymetry#172).

    The C++ resolver (PR 2) must produce the same string for the same inputs;
    ``test/data/mode_keys.json`` holds the shared vectors.

    Inputs are the latest SonarInfo at or before the ping (``pulse_lengths`` in
    seconds, ``bandwidths`` in Hz, ``tx_signal_types``, one element per
    transmit sector in sector order) and the ping's own
    ``PingInfo.frequency`` (Hz). Floats are the message's float32 values
    widened to double.

    Key: for each sector ``<TYPE>_<pulse_us>us_<bw_Hz>Hz``, sectors joined with
    ``+``, then ``_<freq_kHz>kHz`` once, e.g. ``FMU_1000us_24000Hz_500kHz``.

    * TYPE: ``tx_signal_types`` 1 -> ``CW``, 2 -> ``FMU``, 3 -> ``FMD``; 0, any
      other value, or a missing element -> ``UNK``. An empty array means UNK
      for every sector (SonarInfo.msg: never assume CW).
    * Numbers: pulse * 1e6 (us), bandwidth (Hz), frequency * 1e-3 (kHz), each
      through key_number: 3 significant figures, plain decimal, and ``?`` for a
      NaN, infinite or non-positive raw value (tested before rounding) or a
      missing element.
    * The sector count is the longest of the three arrays; a SonarInfo with all
      three empty gives one ``UNK_?us_?Hz`` sector.

    The key always starts with a letter (the TYPE), never contains a comma or
    whitespace, and ``*`` is reserved for "every mode". No TYPE begins with
    ``inf``/``nan`` or a digit, so the single-curve loaders' ``std::stof``
    column-0 parse rejects every table row (cube_bathymetry#172 must-fix 1).
    """
    return '%s_%skHz' % (sector_mode_key(pulse_lengths, bandwidths, tx_signal_types),
                         key_number(frequency_hz, 1e-3))


def declared_tvg_state(tvg_model, tvg_absorption_db_per_km):
    """
    Return ``(model_name, alpha_db_per_km)`` for a SonarInfo's TVG fields.

    ``alpha_db_per_km`` is None when it is NaN (unknown) or not a number, so
    equal states compare equal.
    """
    try:
        name = TVG_MODEL_NAMES.get(int(tvg_model), 'UNKNOWN')
    except (TypeError, ValueError):
        name = 'UNKNOWN'
    try:
        alpha = float(tvg_absorption_db_per_km)
    except (TypeError, ValueError):
        return name, None
    return name, (alpha if math.isfinite(alpha) else None)


def default_sonar_info_topic(detections_topic):
    """Return the ``sonar_info`` sibling of a detections topic."""
    parent = detections_topic.rsplit('/', 1)[0] if '/' in detections_topic else ''
    return parent + '/sonar_info'


class OffsetSummary:
    """
    Pooled non-specular beams of one or more pings, for the offset levels.

    ``n``, ``s``: beam count and dB sum; ``angle_counts`` per ``|theta|`` bin and
    ``cell_counts`` per (angle group, range cell), flat with the group as the
    slow index, so the level corrected by any mode's curve and per-group
    range term is ``(s - angle_counts . g - cell_counts . r) / n``, computed
    once the fit is known.
    """

    __slots__ = ('n', 's', 'angle_counts', 'cell_counts', 'pings')

    def __init__(self, n_abs=0, n_cells=0, n_groups=1):
        """Create an empty summary: ``n_abs`` angle bins, ``n_groups`` x ``n_cells`` cells."""
        import numpy as np
        self.n = 0
        self.s = 0.0
        self.angle_counts = np.zeros(n_abs, dtype=np.int64)
        self.cell_counts = np.zeros(n_groups * n_cells, dtype=np.int64)
        self.pings = 0

    def add(self, other):
        """Pool ``other`` into this summary."""
        self.n += other.n
        self.s += other.s
        self.angle_counts += other.angle_counts
        self.cell_counts += other.cell_counts
        self.pings += other.pings

    def compact(self):
        """
        Keep only the non-zero counts, as ``(indices, counts)`` pairs.

        A closed switch event keeps two summaries until the fit; a sonar that
        switches every few pings makes many events, and the per-cell counts
        are mostly zero. A compacted summary can no longer be added to.
        """
        import numpy as np
        for name in ('angle_counts', 'cell_counts'):
            counts = getattr(self, name)
            if not isinstance(counts, tuple):
                nz = np.flatnonzero(counts)
                setattr(self, name, (nz.astype(np.int32), counts[nz].astype(np.int32)))

    def level(self, rel_db, range_db):
        """
        Mean corrected dB given a mode's per-bin curve and per-(group, cell) range term.

        ``range_db`` is (groups, cells) or already flat in the same order.
        """
        import numpy as np
        if self.n == 0:
            return float('nan')
        total = self.s
        for counts, values in ((self.angle_counts, np.asarray(rel_db).ravel()),
                               (self.cell_counts, np.asarray(range_db).ravel())):
            if isinstance(counts, tuple):
                total -= float(np.dot(counts[1], values[counts[0]]))
            else:
                total -= float(np.dot(counts, values))
        return float(total / self.n)


class SwitchEvent:
    """One mode switch: the old mode's pings before it, the new mode's after."""

    __slots__ = ('from_key', 'to_key', 't_ns', 'before', 'after')

    def __init__(self, from_key, to_key, t_ns, before, after):
        """Record a switch from ``from_key`` to ``to_key`` at ping stamp ``t_ns``."""
        self.from_key = from_key
        self.to_key = to_key
        self.t_ns = t_ns
        self.before = before
        self.after = after


class SwitchTracker:
    """
    Collect mode-switch events from a stamp-ordered ping stream.

    At a switch (a ping whose key differs from the previous ping's), the
    previous mode's contiguous run within ``window_ns`` before the switching
    ping is the event's "before" side, and the new mode's pings up to
    ``window_ns`` after it (until the next switch) its "after" side. A gap
    wider than the window leaves the before side empty and discards the
    switch; a ping without a key, or one stamped before its predecessor,
    breaks the run.
    """

    def __init__(self, window_ns, n_abs, n_cells, n_groups=1):
        """Track switches with an adjacency window of ``window_ns``."""
        self.window_ns = window_ns
        self.n_abs = n_abs
        self.n_cells = n_cells
        self.n_groups = n_groups
        self.recent = collections.deque()    # (t_ns, key, summary)
        self.open = []
        self.events = []
        self.discarded = collections.Counter()
        self.last_t = None

    def _close(self, event):
        if event.before.n == 0 or event.after.n == 0:
            self.discarded['no_beams'] += 1
        else:
            event.before.compact()
            event.after.compact()
            self.events.append(event)

    def break_run(self, reason=None):
        """End every open event and forget the run (a ping with no key)."""
        for event in self.open:
            self._close(event)
        self.open = []
        self.recent.clear()
        if reason:
            self.discarded[reason] += 1

    def add(self, t_ns, key, summary):
        """Add one ping (stamp, mode key, OffsetSummary) in stamp order."""
        if self.last_t is not None and t_ns < self.last_t:
            self.break_run('out_of_order')
        self.last_t = t_ns
        still_open = []
        for event in self.open:
            if key == event.to_key and t_ns - event.t_ns <= self.window_ns:
                event.after.add(summary)
                still_open.append(event)
            else:
                self._close(event)
        self.open = still_open
        if self.recent and self.recent[-1][1] != key:
            prev_key = self.recent[-1][1]
            before = OffsetSummary(self.n_abs, self.n_cells, self.n_groups)
            for t_prev, k_prev, s_prev in reversed(self.recent):
                if k_prev != prev_key or t_ns - t_prev > self.window_ns:
                    break
                before.add(s_prev)
            if before.pings == 0:
                self.discarded['gap'] += 1
            else:
                after = OffsetSummary(self.n_abs, self.n_cells, self.n_groups)
                after.add(summary)
                self.open.append(SwitchEvent(prev_key, key, t_ns, before, after))
        self.recent.append((t_ns, key, summary))
        while self.recent and t_ns - self.recent[0][0] > self.window_ns:
            self.recent.popleft()

    def finish(self):
        """Close every open event (end of the stream)."""
        self.break_run()


class ModeAccumulator:
    """
    Sufficient statistics of one mode: n, sum and sum of squares of dB per cell.

    A cell is (signed angle bin, range cell): the signed bin is the ``|theta|``
    bin ``j = floor(|theta| / bin_width)`` on the port side (``theta < 0``) or
    ``n_half + j`` on the starboard side, so folding to ``|theta|`` bins is
    exact; the range cell is ``floor((log10 R + 1) / 0.01)``. Beams are
    buffered and added in batches.
    """

    FLUSH_BEAMS = 1 << 21

    def __init__(self, n_half):
        """Create empty statistics for ``n_half`` angle bins per side."""
        import numpy as np
        self.n_half = n_half
        self.n_cells = 2 * n_half * RANGE_CELL_COUNT
        self.n = np.zeros(self.n_cells)
        self.s = np.zeros(self.n_cells)
        self.ss = np.zeros(self.n_cells)
        self._idx = []
        self._y = []
        self._buffered = 0
        self.pings = 0
        self.beams = 0
        self.declared = collections.Counter()   # (model, alpha) -> pings

    def add(self, flat_idx, y):
        """Buffer beams (flat cell indices, dB values)."""
        self._idx.append(flat_idx)
        self._y.append(y)
        self._buffered += len(y)
        self.beams += len(y)
        if self._buffered >= self.FLUSH_BEAMS:
            self.flush()

    def flush(self):
        """Add the buffered beams to the statistics."""
        import numpy as np
        if not self._idx:
            return
        idx = np.concatenate(self._idx)
        y = np.concatenate(self._y)
        self.n += np.bincount(idx, minlength=self.n_cells)
        self.s += np.bincount(idx, weights=y, minlength=self.n_cells)
        self.ss += np.bincount(idx, weights=y * y, minlength=self.n_cells)
        self._idx, self._y, self._buffered = [], [], 0

    def cells(self):
        """Return ``(n, s, ss)``, each shaped (2 * n_half, RANGE_CELL_COUNT)."""
        self.flush()
        shape = (2 * self.n_half, RANGE_CELL_COUNT)
        return self.n.reshape(shape), self.s.reshape(shape), self.ss.reshape(shape)


def ping_beams(ping, bin_width_deg, n_half, offset_db=0.0):
    """
    Return ``(abs_bin, side, cell, y)`` arrays of a ping's usable beams.

    ``offset_db`` (the ping's declared gain offset, cube_bathymetry#176) is added
    to ``y``: the fit sees reference-state levels.

    Usable: finite intensity and angle, positive finite slant range
    ``R = twtt * c / 2``, not flagged bad, ``|theta| < 90`` deg and
    ``0.1 m <= R < 10 km``. None when the ping has none.
    """
    import numpy as np
    count = min(len(ping.intensities), len(ping.rx_angles),
                len(ping.two_way_travel_times))
    if count == 0:
        return None
    y = np.asarray(ping.intensities, dtype=np.float64)[:count]
    ang = np.asarray(ping.rx_angles, dtype=np.float64)[:count]
    twtt = np.asarray(ping.two_way_travel_times, dtype=np.float64)[:count]
    with np.errstate(invalid='ignore', over='ignore'):
        rng = twtt * float(ping.sound_speed) / 2.0
        ok = np.isfinite(y) & np.isfinite(ang) & np.isfinite(rng) & (rng > 0.0)
    if ping.flags is not None:
        flags = np.frombuffer(ping.flags, dtype=np.uint8)[:count]
        ok[:len(flags)] &= flags == 0
    if not ok.any():
        return None
    y, ang, rng = y[ok] + offset_db, ang[ok], rng[ok]
    deg = np.degrees(ang)
    # |theta| < 90 explicitly, BEFORE the bin cast: the last bin reaches past
    # 90 when the bin width does not divide 90, a beam at or beyond 90 deg is
    # not a seabed return, and a huge finite angle would overflow the cast.
    in_range = np.abs(deg) < 90.0
    if not in_range.all():
        if not in_range.any():
            return None
        y, rng, deg = y[in_range], rng[in_range], deg[in_range]
    abs_bin = np.floor(np.abs(deg) / bin_width_deg).astype(np.int64)
    cell = np.floor((np.log10(rng) - RANGE_CELL_LOG10_MIN)
                    / RANGE_CELL_DECADES).astype(np.int64)
    keep = (abs_bin >= 0) & (abs_bin < n_half) & (cell >= 0) & (cell < RANGE_CELL_COUNT)
    if not keep.all():
        if not keep.any():
            return None
        abs_bin, cell, y, deg = abs_bin[keep], cell[keep], y[keep], deg[keep]
    side = (deg >= 0.0).astype(np.int64)
    return abs_bin, side, cell, y


class SonarInfoHistory:
    """
    SonarInfo by header stamp: the ping's mode comes from the latest at or before it.

    Keyed by stamp and deduplicated (heartbeats repeat the change stamp; a
    repeated stamp keeps the latest message read).
    """

    def __init__(self):
        """Create an empty history."""
        self.stamps = []
        self.entries = []    # (sector key, declared state)

    def add(self, stamp_ns, sector_key, declared):
        """Insert a SonarInfo stamped ``stamp_ns``."""
        import bisect
        i = bisect.bisect_left(self.stamps, stamp_ns)
        if i < len(self.stamps) and self.stamps[i] == stamp_ns:
            self.entries[i] = (sector_key, declared)
            return
        self.stamps.insert(i, stamp_ns)
        self.entries.insert(i, (sector_key, declared))

    def at(self, stamp_ns):
        """Return the entry at or before ``stamp_ns``, or None."""
        import bisect
        i = bisect.bisect_right(self.stamps, stamp_ns) - 1
        return self.entries[i] if i >= 0 else None

    def __len__(self):
        """Return the number of distinct stamps."""
        return len(self.stamps)


class ModeTableBuilder:
    """
    Accumulate pings per pulse mode and derive the per-mode table.

    Feed SonarInfo with add_sonar_info and pings with add_ping (pings in stamp
    order, after any SonarInfo that could describe them), then call finish.
    """

    def __init__(self, bin_width_deg=2.0,
                 specular_exclusion_deg=DEFAULT_SPECULAR_EXCLUSION_DEG,
                 window_sec=DEFAULT_ADJACENCY_WINDOW_SEC,
                 range_angle_edges_deg=DEFAULT_RANGE_ANGLE_EDGES_DEG, gain=None):
        """
        Create an empty builder (ValueError on an edge list that leaves a group empty).

        ``gain`` is a GainDeclaration (cube_bathymetry#176): each ping's offset is
        added to its intensities before binning, so the table is in the
        declaration's reference state.
        """
        self.gain = GainTracker(gain) if gain is not None else None
        self.bin_width_deg = bin_width_deg
        self.n_half = int(math.ceil(90.0 / bin_width_deg - 1e-9))
        self.specular_exclusion_deg = specular_exclusion_deg
        # First |theta| bin whose lower edge is outside the specular band.
        self.first_nonspecular_bin = int(math.ceil(
            specular_exclusion_deg / bin_width_deg - 1e-9))
        self.range_angle_edges_deg = tuple(range_angle_edges_deg)
        self.groups = range_angle_groups(bin_width_deg, specular_exclusion_deg,
                                         self.range_angle_edges_deg)
        self.bin_group = bin_groups(self.groups, self.n_half)
        self.window_sec = window_sec
        self.history = SonarInfoHistory()
        self.modes = {}
        self.pings_without_info = 0
        self.pings_without_beams = 0
        self.tracker = SwitchTracker(int(round(window_sec * 1e9)), self.n_half,
                                     RANGE_CELL_COUNT, len(self.groups))
        self.sonar_models = collections.Counter()

    def add_sonar_info(self, stamp_ns, pulse_lengths, bandwidths, tx_signal_types,
                       tvg_model=0, tvg_absorption_db_per_km=float('nan'),
                       sonar_model=''):
        """Record one SonarInfo (its acquisition settings and declared TVG)."""
        self.history.add(stamp_ns, sector_mode_key(pulse_lengths, bandwidths,
                                                   tx_signal_types),
                         declared_tvg_state(tvg_model, tvg_absorption_db_per_km))
        if sonar_model:
            self.sonar_models[sonar_model] += 1

    def add_sonar_info_msg(self, msg):
        """Record a marine_interfaces/SonarInfo message."""
        self.add_sonar_info(stamp_ns(msg.header.stamp), msg.pulse_lengths,
                            msg.bandwidths, msg.tx_signal_types, msg.tvg_model,
                            msg.tvg_absorption_db_per_km, msg.sonar_model)

    def add_ping(self, ping_ns, ping):
        """Attribute a ping (a PingRecord) to its mode and accumulate it."""
        import numpy as np
        offset_db = self.gain.offset(ping_ns) if self.gain is not None else 0.0
        entry = self.history.at(ping_ns)
        if entry is None:
            self.pings_without_info += 1
            self.tracker.break_run()
            return
        sector_key, declared = entry
        key = '%s_%skHz' % (sector_key, key_number(ping.frequency, 1e-3))
        acc = self.modes.get(key)
        if acc is None:
            acc = self.modes[key] = ModeAccumulator(self.n_half)
        acc.pings += 1
        acc.declared[declared] += 1
        summary = OffsetSummary(self.n_half, RANGE_CELL_COUNT, len(self.groups))
        summary.pings = 1
        beams = ping_beams(ping, self.bin_width_deg, self.n_half, offset_db)
        if beams is None:
            self.pings_without_beams += 1
        else:
            abs_bin, side, cell, y = beams
            if self.gain is not None:
                self.gain.note_beams(ping_ns, len(y))
            signed = abs_bin + side * self.n_half
            acc.add(signed * RANGE_CELL_COUNT + cell, y)
            ns = abs_bin >= self.first_nonspecular_bin
            if ns.any():
                summary.n = int(ns.sum())
                summary.s = float(y[ns].sum())
                summary.angle_counts = np.bincount(abs_bin[ns], minlength=self.n_half)
                summary.cell_counts = np.bincount(
                    self.bin_group[abs_bin[ns]] * RANGE_CELL_COUNT + cell[ns],
                    minlength=len(self.groups) * RANGE_CELL_COUNT)
        self.tracker.add(ping_ns, key, summary)

    def finish(self, reference_mode=DEFAULT_REFERENCE_MODE, range_ref_m=None,
               misfit_threshold_db=DEFAULT_MISFIT_THRESHOLD_DB,
               node_spacing_decades=DEFAULT_RANGE_NODE_SPACING_DECADES):
        """Fit every mode and the offsets; return the result dict (see derive_mode_table)."""
        self.tracker.finish()
        return derive_mode_table(self, reference_mode, range_ref_m, misfit_threshold_db,
                                 node_spacing_decades)

    def to_state(self):
        """
        Return the accumulated statistics as plain Python/numpy data (the cache payload).

        Closes the switch tracker and flushes every mode first. Only dicts,
        lists, tuples, strings, numbers and numpy arrays, so the pickle does
        not name this module's classes and loads however the script was
        imported.
        """
        self.tracker.finish()
        modes = {}
        for key, acc in self.modes.items():
            acc.flush()
            modes[key] = {'n': acc.n, 's': acc.s, 'ss': acc.ss, 'pings': acc.pings,
                          'beams': acc.beams, 'declared': list(acc.declared.items())}

        def summary_state(sm):
            sm.compact()
            return {'n': sm.n, 's': sm.s, 'pings': sm.pings,
                    'angle_counts': sm.angle_counts, 'cell_counts': sm.cell_counts}
        events = [{'from_key': ev.from_key, 'to_key': ev.to_key, 't_ns': ev.t_ns,
                   'before': summary_state(ev.before), 'after': summary_state(ev.after)}
                  for ev in self.tracker.events]
        return {'bin_width_deg': self.bin_width_deg,
                'specular_exclusion_deg': self.specular_exclusion_deg,
                'window_sec': self.window_sec,
                'range_angle_edges_deg': list(self.range_angle_edges_deg),
                'history': {'stamps': list(self.history.stamps),
                            'entries': list(self.history.entries)},
                'modes': modes, 'events': events,
                'discarded': dict(self.tracker.discarded),
                'pings_without_info': self.pings_without_info,
                'pings_without_beams': self.pings_without_beams,
                'sonar_models': dict(self.sonar_models),
                'gain': self.gain.state() if self.gain is not None else None}

    @classmethod
    def from_state(cls, state, gain=None):
        """
        Rebuild a builder from to_state output (ready for finish, not for more pings).

        ``gain`` is the declaration the state was accumulated under (the cache
        identity has already matched it).
        """
        b = cls(state['bin_width_deg'], state['specular_exclusion_deg'], state['window_sec'],
                state['range_angle_edges_deg'], gain=gain)
        if b.gain is not None:
            b.gain.restore(state['gain'])
        b.history.stamps = list(state['history']['stamps'])
        b.history.entries = [tuple(e) for e in state['history']['entries']]
        for key, m in state['modes'].items():
            acc = b.modes[key] = ModeAccumulator(b.n_half)
            acc.n, acc.s, acc.ss = m['n'], m['s'], m['ss']
            acc.pings, acc.beams = m['pings'], m['beams']
            acc.declared = collections.Counter({tuple(k): v for k, v in m['declared']})

        def summary(st):
            sm = OffsetSummary(b.n_half, RANGE_CELL_COUNT, len(b.groups))
            sm.n, sm.s, sm.pings = st['n'], st['s'], st['pings']
            sm.angle_counts, sm.cell_counts = st['angle_counts'], st['cell_counts']
            return sm
        b.tracker.events = [SwitchEvent(ev['from_key'], ev['to_key'], ev['t_ns'],
                                        summary(ev['before']), summary(ev['after']))
                            for ev in state['events']]
        b.tracker.discarded = collections.Counter(state['discarded'])
        b.pings_without_info = state['pings_without_info']
        b.pings_without_beams = state['pings_without_beams']
        b.sonar_models = collections.Counter(state['sonar_models'])
        return b


def cell_log10_range():
    """Return the absolute log10 R (m) at each range cell's centre."""
    import numpy as np
    return RANGE_CELL_LOG10_MIN + (np.arange(RANGE_CELL_COUNT) + 0.5) * RANGE_CELL_DECADES


def median_log10_range(n_cells):
    """Median log10 R of a mode from its per-range-cell counts (interpolated in the cell)."""
    import numpy as np
    counts = n_cells.sum(axis=0) if n_cells.ndim == 2 else n_cells
    total = counts.sum()
    if total <= 0:
        return None
    cum = np.cumsum(counts)
    c = int(np.searchsorted(cum, total / 2.0))
    before = cum[c - 1] if c > 0 else 0.0
    frac = (total / 2.0 - before) / counts[c] if counts[c] > 0 else 0.5
    return RANGE_CELL_LOG10_MIN + (c + frac) * RANGE_CELL_DECADES


def tent_weights(x, nodes):
    """
    Return the (len(x), len(nodes)) linear-interpolation weights of ``x`` on ``nodes``.

    Linear between neighbouring nodes, clamped to the end nodes beyond them,
    the way the estimator applies the range term.
    """
    import numpy as np
    x = np.asarray(x, dtype=np.float64)
    w = np.zeros((len(x), len(nodes)))
    if len(nodes) == 1:
        w[:, 0] = 1.0
        return w
    xc = np.clip(x, nodes[0], nodes[-1])
    k = np.clip(np.searchsorted(nodes, xc, side='right') - 1, 0, len(nodes) - 2)
    t = (xc - nodes[k]) / (nodes[k + 1] - nodes[k])
    rows = np.arange(len(x))
    w[rows, k] = 1.0 - t
    w[rows, k + 1] += t
    return w


def _solve_additive(nb, sb, ssb, basis, penalty):
    """
    Least squares for ``y = a_bin + basis(x) . r`` on cell statistics.

    ``nb``, ``sb``, ``ssb``: (bins, cells) count, sum, sum of squares; ``basis``:
    (cells, K); ``penalty``: (K, K) added to the r block of the normal matrix.
    Returns ``(a, r, cov_rr, sigma2)``.
    """
    import numpy as np
    n_bins, k = nb.shape[0], basis.shape[1]
    nb_tot = nb.sum(axis=1)
    cross = nb @ basis
    xtx = np.zeros((n_bins + k, n_bins + k))
    xtx[:n_bins, :n_bins] = np.diag(nb_tot)
    xtx[:n_bins, n_bins:] = cross
    xtx[n_bins:, :n_bins] = cross.T
    xtx[n_bins:, n_bins:] = basis.T @ (nb.sum(axis=0)[:, None] * basis)
    xty = np.concatenate([sb.sum(axis=1), basis.T @ sb.sum(axis=0)])
    lhs = xtx.copy()
    lhs[n_bins:, n_bins:] += penalty
    inv = np.linalg.pinv(lhs)
    beta = inv @ xty
    rss = float(ssb.sum() - 2.0 * beta @ xty + beta @ xtx @ beta)
    dof = max(float(nb_tot.sum()) - (n_bins + k - 1), 1.0)
    sigma2 = max(rss, 0.0) / dof
    return beta[:n_bins], beta[n_bins:], sigma2 * inv[n_bins:, n_bins:], sigma2


def _binned_residuals(nb, sb, a, r_bin, x_rel, groups, min_n, min_share=0.0,
                      bin_decades=DEFAULT_RANGE_NODE_SPACING_DECADES / MISFIT_BINS_PER_NODE):
    """
    Mean post-fit residual per eighth-decade bin of log10(R/R0), pooled and per angle group.

    ``r_bin`` is each fit bin's range term per cell, (bins, cells); ``groups``
    maps a group name to a boolean mask over the fit bins. Returns ``{name:
    [(bin_centre, n, mean_db), ...]}`` with only bins of at least ``min_n``
    samples and at least ``min_share`` of the group's samples; ``'all'`` is
    the pooled one.
    """
    import numpy as np
    resid = sb - nb * (a[:, None] + r_bin)
    q = np.round(x_rel / bin_decades).astype(np.int64)
    out = {}
    for name, mask in (('all', np.ones(nb.shape[0], dtype=bool)),) + tuple(groups):
        rows = []
        if mask.any():
            n_c = nb[mask].sum(axis=0)
            e_c = resid[mask].sum(axis=0)
            floor = max(min_n, min_share * float(n_c.sum()))
            for qi in np.unique(q[n_c > 0]):
                sel = q == qi
                n = float(n_c[sel].sum())
                if n >= floor:
                    rows.append((float(qi * bin_decades), int(round(n)),
                                 float(e_c[sel].sum() / n)))
        out[name] = rows
    return out


def _misfit_stats(binned):
    """Return ``(max |mean|, n-weighted rms)`` over every group's binned residuals."""
    values = [(n, m) for rows in binned.values() for _x, n, m in rows]
    if not values:
        return float('nan'), float('nan')
    total = sum(n for n, _m in values)
    return (max(abs(m) for _n, m in values),
            math.sqrt(sum(n * m * m for n, m in values) / total))


def range_angle_groups(bin_width_deg, specular_exclusion_deg, edges_deg):
    """
    Return the range-term angle groups as ``[(lo_bin, hi_bin), ...]`` over ``|theta|`` bins.

    The first group starts at the first bin outside the specular band, the
    last ends at the last bin below 90 deg; each edge in ``edges_deg`` snaps
    up to the first bin whose lower edge is at or above it. Raises ValueError
    when an edge is not finite, not strictly increasing, or snaps onto the
    previous bound or past the last bin (an empty group).
    """
    n_half = int(math.ceil(90.0 / bin_width_deg - 1e-9))
    bounds = [int(math.ceil(specular_exclusion_deg / bin_width_deg - 1e-9))]
    if bounds[0] >= n_half:
        raise ValueError('no angle bin lies outside the specular band (bins of %g deg, '
                         'specular exclusion %g deg): nothing to fit a range term on'
                         % (bin_width_deg, specular_exclusion_deg))
    for edge in edges_deg:
        if not (isinstance(edge, (int, float)) and math.isfinite(edge)):
            raise ValueError('range angle edge %r is not a finite number' % (edge,))
        b = int(math.ceil(edge / bin_width_deg - 1e-9))
        if b <= bounds[-1] or b >= n_half:
            raise ValueError('range angle edge %g deg gives an empty group (bins of %g '
                             'deg, specular exclusion %g deg, previous bound %g deg)'
                             % (edge, bin_width_deg, specular_exclusion_deg,
                                bounds[-1] * bin_width_deg))
        bounds.append(b)
    bounds.append(n_half)
    return list(zip(bounds[:-1], bounds[1:]))


def group_bounds_deg(group, bin_width_deg):
    """Return ``(lo_deg, hi_deg)`` of a ``(lo_bin, hi_bin)`` group (hi clamped to 90)."""
    return group[0] * bin_width_deg, min(group[1] * bin_width_deg, 90.0)


def bin_groups(groups, n_half):
    """
    Return the group index of every ``|theta|`` bin, length ``n_half``.

    Specular bins (below the first group) take the first group, the rule the
    estimator applies to a beam below the first ``angle_lo_deg`` of a mode.
    """
    import numpy as np
    out = np.zeros(n_half, dtype=np.int64)
    for g, (lo, hi) in enumerate(groups):
        out[lo:hi] = g
    return out


def parse_range_angle_edges(text):
    """Parse ``--range-angle-edges``: ``none`` -> (), else comma-separated degrees."""
    if text.strip().lower() == 'none':
        return ()
    try:
        edges = tuple(float(v) for v in text.split(','))
    except ValueError:
        raise ValueError('--range-angle-edges %r is not a comma-separated list of '
                         'degrees or "none"' % text)
    for a, b in zip(edges, edges[1:]):
        if not b > a:
            raise ValueError('--range-angle-edges %r must increase strictly' % text)
    return edges


def _trim_span(nb, x_rel, spacing=DEFAULT_RANGE_NODE_SPACING_DECADES):
    """
    Return ``(nodes, cell_mask, trimmed)`` for one group: its node grid after trimming the ends.

    The candidate grid covers every populated cell at ``spacing`` decades;
    an END node carrying under ``max(MIN_SAMPLES_PER_BIN, NODE_MIN_SHARE *
    total)`` beams is dropped, repeatedly, down to two nodes. ``cell_mask``
    keeps the cells within the kept span (so the fit, the intercepts and the
    misfit ignore the beams beyond it; they are corrected with the clamped end
    value); ``trimmed`` lists the dropped node positions. With no populated
    cell the grid is ``[0.0]`` and the mask is empty.
    """
    import numpy as np
    counts = nb.sum(axis=0) if nb.shape[0] else np.zeros(nb.shape[1])
    populated = np.nonzero(counts > 0)[0]
    if not len(populated):
        return np.zeros(1), np.zeros(nb.shape[1], dtype=bool), []
    lo = math.floor(x_rel[populated[0]] / spacing)
    hi = math.ceil(x_rel[populated[-1]] / spacing)
    nodes = np.arange(lo, hi + 1) * spacing
    floor = max(float(MIN_SAMPLES_PER_BIN), NODE_MIN_SHARE * float(counts.sum()))
    trimmed = []
    while len(nodes) > 2:
        # Only the cells inside the current span count: tent_weights clamps a
        # cell beyond the ends onto the end node, which would credit the beams
        # of a node just trimmed to its neighbour (cross-model review, #172).
        inside = (x_rel >= nodes[0] - 1e-9) & (x_rel <= nodes[-1] + 1e-9)
        support = tent_weights(x_rel, nodes).T @ (counts * inside)
        # The weaker end goes first: with three nodes and both ends under the
        # floor only one can go, and it must be the one with less support.
        if support[0] < floor and support[0] <= support[-1]:
            trimmed.append(float(nodes[0]))
            nodes = nodes[1:]
        elif support[-1] < floor:
            trimmed.append(float(nodes[-1]))
            nodes = nodes[:-1]
        else:
            break
    cell_mask = (x_rel >= nodes[0] - 1e-9) & (x_rel <= nodes[-1] + 1e-9)
    return nodes, cell_mask, sorted(trimmed)


def _fit_group(nb, sb, ssb, x_abs, x_rel, range_model,
               spacing=DEFAULT_RANGE_NODE_SPACING_DECADES):
    """
    Fit ``y = a_bin + r(log10(R / R0))`` on one angle group's cell statistics.

    ``nb``, ``sb``, ``ssb``: (fit bins of the group, cells). The node grid is
    trimmed first (_trim_span) and only the cells within the kept span are
    fitted, for both range models. Returns ``(a, r_cell, cell_mask, info)``:
    the per-bin intercepts, the range term per range cell (zeros when not
    fitted; clamped beyond the span), the in-span cell mask, and a dict with
    ``range_status`` (``'fitted'`` or ``'zero: <reason>'``),
    ``nonspecular_samples`` (in span), ``out_of_span_samples``,
    ``trimmed_nodes``, ``span`` (first and last node),
    ``within_bin_spread_decades``, ``sigma_db`` and, for ``'nodes'``,
    ``nodes``/``r``/``r_se``/``r_support`` (0 at R0 when R0 is inside the
    span, else 0 at the nearest end node), for ``'scalar'``
    ``slope_db_per_decade``/``slope_se``.
    """
    import numpy as np
    nodes, cell_mask, trimmed = _trim_span(nb, x_rel, spacing)
    all_total = float(nb.sum())
    n_rows = nb.shape[0]
    nb = np.where(cell_mask[None, :], nb, 0.0)
    sb = np.where(cell_mask[None, :], sb, 0.0)
    ssb = np.where(cell_mask[None, :], ssb, 0.0)
    # A fit bin whose every beam lay beyond the span has no intercept to fit:
    # drop it here (its intercept is reported as 0 and never used), or its
    # zero count makes the within-bin spread NaN and skips the identifiability
    # check (cross-model review, #172).
    keep_rows = nb.sum(axis=1) > 0
    nb, sb, ssb = nb[keep_rows], sb[keep_rows], ssb[keep_rows]
    total = float(nb.sum())
    info = {'range_model': range_model, 'nonspecular_samples': int(total),
            'out_of_span_samples': int(round(all_total - total)),
            'trimmed_nodes': trimmed, 'span': [float(nodes[0]), float(nodes[-1])],
            'nodes': [], 'r': [], 'r_se': [], 'r_support': []}

    # Within-angle-bin spread of log10 R: what identifies the range term.
    if total > 0:
        nb_tot = nb.sum(axis=1)
        mean_x = (nb @ x_abs) / nb_tot
        spread = math.sqrt(float((nb * (x_abs[None, :] - mean_x[:, None]) ** 2).sum())
                           / total)
    else:
        spread = 0.0
    info['within_bin_spread_decades'] = spread

    r_cell = np.zeros(RANGE_CELL_COUNT)
    a = np.zeros(nb.shape[0])
    sigma2 = float('nan')
    status = 'fitted'
    if total < MIN_SAMPLES_PER_BIN:
        status = ('zero: %d non-specular samples, fewer than %d'
                  % (int(total), MIN_SAMPLES_PER_BIN))
    elif not spread >= MIN_WITHIN_BIN_SPREAD_DECADES:   # a NaN spread is "not identified"
        status = ('zero: not identified, the within-angle-bin spread of log10 R '
                  'is %.3f decade (< %g)' % (spread, MIN_WITHIN_BIN_SPREAD_DECADES))
    if range_model == 'scalar':
        basis = np.clip(x_rel, nodes[0], nodes[-1])[:, None]
        penalty = np.zeros((1, 1))
    else:
        if status == 'fitted' and len(nodes) < 2:
            status = 'zero: the ranges span less than one node spacing'
        basis = tent_weights(x_rel, nodes)
        k = len(nodes)
        penalty = RANGE_RIDGE_WEIGHT * np.eye(k)
        if k >= 3:
            d2 = np.zeros((k - 2, k))
            for i in range(k - 2):
                d2[i, i:i + 3] = (1.0, -2.0, 1.0)
            penalty += RANGE_SMOOTHING_WEIGHT * d2.T @ d2
    if status == 'fitted':
        a, r, cov, sigma2 = _solve_additive(nb, sb, ssb, basis, penalty)
        if range_model == 'scalar':
            info['slope_db_per_decade'] = float(r[0])
            info['slope_se'] = float(math.sqrt(max(cov[0, 0], 0.0)))
            r_cell = basis @ r
        else:
            # Refer the term to R0: r(0) = 0, the constant moves to the intercepts.
            c0 = tent_weights([0.0], nodes)[0]
            r0 = float(c0 @ r)
            r = r - r0
            a = a + r0
            t = np.eye(len(nodes)) - np.outer(np.ones(len(nodes)), c0)
            cov = t @ cov @ t.T
            r_cell = basis @ r
            info['nodes'] = [float(v) for v in nodes]
            info['r'] = [float(v) for v in r]
            info['r_se'] = [float(math.sqrt(max(v, 0.0))) for v in np.diag(cov)]
            info['r_support'] = [float(v) for v in basis.T @ nb.sum(axis=0)]
    elif range_model == 'scalar':
        info['slope_db_per_decade'] = 0.0
        info['slope_se'] = float('nan')
    if status != 'fitted' and nb.shape[0]:
        # Intercepts only: per-bin means.
        tot = nb.sum(axis=1)
        a = np.where(tot > 0, sb.sum(axis=1) / np.maximum(tot, 1), 0.0)
    info['range_status'] = status
    info['sigma_db'] = math.sqrt(sigma2) if math.isfinite(sigma2) else float('nan')
    a_full = np.zeros(n_rows)
    a_full[keep_rows] = a
    return a_full, r_cell, cell_mask, info


def range_status_summary(groups):
    """One word for a mode's range terms: ``fitted``, ``zero`` or ``partial (k of G fitted)``."""
    fitted = sum(1 for g in groups if g['range_status'] == 'fitted')
    if fitted == len(groups):
        return 'fitted'
    if fitted == 0:
        return 'zero'
    return 'partial (%d of %d groups fitted)' % (fitted, len(groups))


def fit_mode(n, s, ss, bin_width_deg, groups, log_r0, fold=False, range_model='nodes',
             spacing=DEFAULT_RANGE_NODE_SPACING_DECADES):
    """
    Fit one mode's model ``L = A(theta) + r_g(log10(R / R0))``, ``g`` the angle group of ``theta``.

    ``n``, ``s``, ``ss``: (2 * n_half, RANGE_CELL_COUNT) cell statistics (see
    ModeAccumulator). ``groups`` is the ``[(lo_bin, hi_bin), ...]`` list from
    range_angle_groups: the range term is fitted per group, jointly with one
    intercept per SIGNED angle bin of the group (``fold=True`` folds to
    ``|theta|`` bins, for comparison only); the bins below the first group
    (the specular band) are left out of every fit. Every angle bin belongs to
    exactly one group and no parameter is shared, so the joint least squares
    separates exactly into one fit per group. ``range_model`` is ``'nodes'``
    (quarter-decade nodes of log10(R/R0), linear between them, clamped beyond
    the ends: the table's term) or ``'scalar'`` (one slope per decade, the
    diagnostic comparison).

    Returns a dict: ``groups`` (one dict per group, in order: ``lo_deg``,
    ``hi_deg``, ``lo_bin``, ``hi_bin`` and the _fit_group info —
    ``range_status``, ``nodes``/``r``/``r_se``/``r_support``,
    ``within_bin_spread_decades``, ``nonspecular_samples``, ``sigma_db``, for
    ``'scalar'`` the slope), ``range_status`` (the summary word),
    ``nonspecular_samples`` (all groups), ``angle`` rows ``(centre, n, A, var,
    rel)`` per populated ``|theta|`` bin (A = mean of ``L - r_g``, the level at
    R0; rel = A minus the nadir-most bin's A), ``binned_residuals``,
    ``misfit_max_db``, ``misfit_rms_db``, ``range_db_per_cell`` ((groups,
    cells)) and ``rel_db_per_bin`` (for the offsets).
    """
    import numpy as np
    n_half = n.shape[0] // 2
    x_abs = cell_log10_range()
    x_rel = x_abs - log_r0
    abs_n = n[:n_half] + n[n_half:]
    abs_s = s[:n_half] + s[n_half:]
    abs_ss = ss[:n_half] + ss[n_half:]
    if fold:
        fit_n, fit_s, fit_ss = abs_n, abs_s, abs_ss
        bin_abs = np.arange(n_half)
    else:
        fit_n, fit_s, fit_ss = n, s, ss
        bin_abs = np.concatenate([np.arange(n_half), np.arange(n_half)])
    bin_group = bin_groups(groups, n_half)
    populated_bins = fit_n.sum(axis=1) > 0
    result = {'range_model': range_model, 'groups': [], 'nonspecular_samples': 0,
              'node_spacing_decades': spacing}
    r_cell = np.zeros((len(groups), RANGE_CELL_COUNT))
    cell_masks = np.zeros((len(groups), RANGE_CELL_COUNT), dtype=bool)
    a_all = np.zeros(fit_n.shape[0])
    use_all = np.zeros(fit_n.shape[0], dtype=bool)
    for g, (lo_bin, hi_bin) in enumerate(groups):
        use = (bin_abs >= lo_bin) & (bin_abs < hi_bin) & populated_bins
        a, r_g, cell_mask, info = _fit_group(fit_n[use], fit_s[use], fit_ss[use], x_abs,
                                             x_rel, range_model, spacing)
        info['lo_deg'], info['hi_deg'] = group_bounds_deg((lo_bin, hi_bin), bin_width_deg)
        info['lo_bin'], info['hi_bin'] = lo_bin, hi_bin
        r_cell[g] = r_g
        cell_masks[g] = cell_mask
        a_all[use] = a
        use_all |= use
        result['groups'].append(info)
        result['nonspecular_samples'] += info['nonspecular_samples']
    result['range_status'] = range_status_summary(result['groups'])
    result['range_db_per_cell'] = r_cell

    # Angular intercepts per |theta| bin (specular bins included), referred to
    # R0 with the bin's own group's range term, over EVERY cell of the bin: a
    # beam beyond the group's span is corrected with the clamped end value at
    # apply time, so it enters A the same way (the trimming only decides what
    # shapes r_g). Specular beams and modes with no range fit keep all their
    # beams (cross-model review, #172).
    rows = []
    rel_per_bin = np.zeros(n_half)
    nadir = None
    for j in range(n_half):
        cnt = float(abs_n[j].sum())
        if cnt <= 0:
            continue
        r_j = r_cell[bin_group[j]]
        level = float((abs_s[j].sum() - abs_n[j] @ r_j) / cnt)
        if cnt >= 2:
            sse = float((abs_ss[j] - 2.0 * r_j * abs_s[j] + abs_n[j] * r_j ** 2).sum())
            var = max(sse - cnt * level * level, 0.0) / (cnt - 1.0)
        else:
            var = float('nan')
        if nadir is None:
            nadir = level
        rel_per_bin[j] = level - nadir
        rows.append(((j + 0.5) * bin_width_deg, int(round(cnt)), level, var, level - nadir))
    result['angle'] = rows
    result['rel_db_per_bin'] = rel_per_bin

    # Misfit: binned residuals after the fit, pooled, per fit group and per
    # HALF group. The per-bin intercepts make the pooled mean residual of a
    # group near zero even when the range dependence still differs between
    # its inner and outer bins, so each group is also split in two.
    fit_groups = bin_group[bin_abs[use_all]]
    in_span = cell_masks[fit_groups]
    nb, sb = fit_n[use_all] * in_span, fit_s[use_all] * in_span
    r_bin = r_cell[fit_groups]
    lower_bin = bin_abs[use_all]
    diag_groups = []
    for lo_bin, hi_bin in groups:
        parts = [(lo_bin, hi_bin, '')]
        if hi_bin - lo_bin >= 2:
            mid = lo_bin + (hi_bin - lo_bin) // 2
            parts += [(lo_bin, mid, ' (lower half)'), (mid, hi_bin, ' (upper half)')]
        for p_lo, p_hi, tag in parts:
            lo_deg, hi_deg = group_bounds_deg((p_lo, p_hi), bin_width_deg)
            diag_groups.append(('%g-%g deg%s' % (lo_deg, hi_deg, tag),
                                (lower_bin >= p_lo) & (lower_bin < p_hi)))
    binned = _binned_residuals(nb, sb, a_all[use_all], r_bin, x_rel, diag_groups,
                               MIN_SAMPLES_PER_BIN, MISFIT_MIN_SHARE,
                               spacing / MISFIT_BINS_PER_NODE)
    result['binned_residuals'] = binned
    result['misfit_max_db'], result['misfit_rms_db'] = _misfit_stats(binned)
    return result


def solve_offsets(events, fits, reference):
    """
    Return ``(offsets, pairs)`` from the switch events, chained to ``reference``.

    Each event gives ``level_after - level_before``, an observation of
    ``o_to - o_from``, with each side's non-specular beams corrected by its own
    mode's curve and range term. ``pairs`` lists per unordered mode pair the
    switch count, mean and SE, and the same per switch direction with their
    agreement. ``offsets`` maps every mode to ``{offset_db, se_db, anchored}``:
    weighted least squares over the pairs with at least MIN_SWITCHES_PER_PAIR
    switches, ``o_reference = 0``; a mode with no path to the reference is
    unanchored (offset 0).
    """
    import numpy as np

    def stats(values):
        m = len(values)
        if m == 0:
            return {'count': 0, 'mean_db': float('nan'), 'se_db': float('nan'),
                    'median_db': float('nan'), 'sd_db': float('nan')}
        arr = np.asarray(values)
        sd = float(arr.std(ddof=1)) if m >= 2 else float('nan')
        return {'count': m, 'mean_db': float(arr.mean()),
                'median_db': float(np.median(arr)), 'sd_db': sd,
                'se_db': sd / math.sqrt(m) if m >= 2 else float('nan')}

    by_pair = {}
    for ev in events:
        if ev.from_key not in fits or ev.to_key not in fits:
            continue
        lv = {}
        for side, key in (('before', ev.from_key), ('after', ev.to_key)):
            fit = fits[key]
            lv[side] = getattr(ev, side).level(fit['rel_db_per_bin'],
                                               fit['range_db_per_cell'])
        d = lv['after'] - lv['before']
        if not math.isfinite(d):
            continue
        m, k = sorted((ev.from_key, ev.to_key))
        value = d if (ev.from_key, ev.to_key) == (m, k) else -d
        entry = by_pair.setdefault((m, k), {'%s->%s' % (m, k): [], '%s->%s' % (k, m): []})
        entry['%s->%s' % (ev.from_key, ev.to_key)].append(value)

    pairs = []
    for (m, k), dirs in sorted(by_pair.items()):
        values = [v for vs in dirs.values() for v in vs]
        info = {'modes': [m, k], 'meaning': 'offset(%s) - offset(%s)' % (k, m)}
        info.update(stats(values))
        info['directions'] = {name: stats(vs) for name, vs in dirs.items()}
        d1, d2 = [info['directions'][name] for name in sorted(dirs)]
        if d1['count'] >= 2 and d2['count'] >= 2:
            diff = d1['mean_db'] - d2['mean_db']
            se = math.sqrt(d1['se_db'] ** 2 + d2['se_db'] ** 2)
            info['direction_difference_db'] = diff
            info['direction_difference_se_db'] = se
            info['directions_agree'] = not (abs(diff) > OFFSET_AGREEMENT_DB
                                            and abs(diff) > OFFSET_AGREEMENT_SE * se)
        else:
            info['direction_difference_db'] = float('nan')
            info['direction_difference_se_db'] = float('nan')
            info['directions_agree'] = None
        info['used'] = info['count'] >= MIN_SWITCHES_PER_PAIR and math.isfinite(info['se_db'])
        pairs.append(info)

    # Component of the reference over the usable pairs.
    edges = [(p['modes'][0], p['modes'][1], p['mean_db'], max(p['se_db'], 0.01))
             for p in pairs if p['used']]
    adjacency = collections.defaultdict(set)
    for m, k, _v, _se in edges:
        adjacency[m].add(k)
        adjacency[k].add(m)
    component = {reference}
    stack = [reference]
    while stack:
        for nxt in adjacency[stack.pop()]:
            if nxt not in component:
                component.add(nxt)
                stack.append(nxt)
    unknown = sorted(component - {reference})
    # An unanchored mode has NO offset: NaN (null in the JSON), never 0.0.
    offsets = {key: {'offset_db': float('nan'), 'se_db': float('nan'), 'anchored': False}
               for key in fits}
    if reference in offsets:
        offsets[reference] = {'offset_db': 0.0, 'se_db': 0.0, 'anchored': True}
    chi2 = float('nan')
    dof = 0
    if unknown:
        col = {key: i for i, key in enumerate(unknown)}
        rows, rhs, weights = [], [], []
        for m, k, v, se in edges:
            if m not in component:
                continue
            row = np.zeros(len(unknown))
            if k in col:
                row[col[k]] += 1.0
            if m in col:
                row[col[m]] -= 1.0
            rows.append(row)
            rhs.append(v)
            weights.append(1.0 / (se * se))
        a = np.asarray(rows)
        w = np.asarray(weights)
        b = np.asarray(rhs)
        normal = a.T @ (w[:, None] * a)
        cov = np.linalg.inv(normal)
        sol = cov @ (a.T @ (w * b))
        resid = a @ sol - b
        chi2 = float((w * resid * resid).sum())
        dof = len(b) - len(unknown)
        for key, i in col.items():
            offsets[key] = {'offset_db': float(sol[i]),
                            'se_db': float(math.sqrt(max(cov[i, i], 0.0))),
                            'anchored': True}
    return offsets, pairs, {'chi2': chi2, 'dof': dof}


def derive_mode_table(builder, reference_mode, range_ref_m, misfit_threshold_db,
                      node_spacing_decades=DEFAULT_RANGE_NODE_SPACING_DECADES):
    """
    Fit every mode and the offsets; return the table as a dict.

    Keys: ``reference_mode``, ``reference_source`` (``'requested'`` or the
    fallback text), ``log_r0``/``range_ref_m``/``range_ref_source``, ``modes``
    (key -> fit dict plus pings, beams, declared state, scalar comparison,
    misfit flag), ``offsets``, ``pairs``, ``chain``, ``switches`` (counts),
    ``warnings``.
    """
    warnings = []
    cells = {key: acc.cells() for key, acc in builder.modes.items()}
    sampled = {key: float(c[0].sum()) for key, c in cells.items()}
    sampled = {key: v for key, v in sampled.items() if v > 0}
    if not sampled:
        raise SystemExit('error: --by-mode found no usable beams in any mode')
    if reference_mode in sampled:
        reference, ref_source = reference_mode, 'requested'
    else:
        reference = max(sorted(sampled), key=lambda k: sampled[k])
        ref_source = ('most-sampled fallback (requested %s is absent): NOT '
                      'comparable across campaigns' % reference_mode)
        warnings.append('warning: reference mode %s is not in the data; using the '
                        'most-sampled mode %s instead. Offsets are relative to it '
                        'and NOT comparable with tables from other campaigns; pass '
                        '--reference-mode' % (reference_mode, reference))
    if range_ref_m is not None:
        log_r0 = math.log10(range_ref_m)
        r0_source = '--range-ref-m'
    else:
        log_r0 = median_log10_range(cells[reference][0])
        r0_source = 'median slant range of the reference mode'

    fits = {}
    for key in sorted(sampled):
        n, s, ss = cells[key]
        acc = builder.modes[key]
        fit = fit_mode(n, s, ss, builder.bin_width_deg, builder.groups, log_r0,
                       spacing=node_spacing_decades)
        scalar = fit_mode(n, s, ss, builder.bin_width_deg, builder.groups, log_r0,
                          range_model='scalar', spacing=node_spacing_decades)
        fit['scalar_comparison'] = {
            'misfit_max_db': scalar['misfit_max_db'],
            'misfit_rms_db': scalar['misfit_rms_db'],
            'range_status': scalar['range_status'],
            'groups': [{'lo_deg': g['lo_deg'], 'hi_deg': g['hi_deg'],
                        'slope_db_per_decade': g.get('slope_db_per_decade'),
                        'slope_se': g.get('slope_se'),
                        'range_status': g['range_status']}
                       for g in scalar['groups']]}
        fit['pings'] = acc.pings
        fit['beams'] = acc.beams
        fit['median_range_m'] = 10.0 ** median_log10_range(n)
        states = sorted(acc.declared.items(),
                        key=lambda kv: (-kv[1], kv[0][0], kv[0][1] or 0.0))
        fit['declared_states'] = [{'model': m, 'absorption_db_per_km': a, 'pings': c}
                                  for (m, a), c in states]
        if len(states) == 1:
            fit['declared_tvg_model'], fit['declared_tvg_absorption_db_per_km'] = states[0][0]
        else:
            fit['declared_tvg_model'] = 'MIXED'
            fit['declared_tvg_absorption_db_per_km'] = float('nan')
            warnings.append('warning: mode %s was recorded under %d declared TVG states '
                            '(%s); written as MIXED'
                            % (key, len(states), ', '.join(
                                '%s/%s dB/km: %d pings' % (m, _fmt(a, '%g'), c)
                                for (m, a), c in states)))
        fit['misfit_exceeded'] = bool(math.isfinite(fit['misfit_max_db'])
                                      and fit['misfit_max_db'] > misfit_threshold_db)
        if fit['misfit_exceeded']:
            warnings.append('warning: mode %s misfit %.2f dB exceeds the %.2f dB '
                            'threshold: the separable binned model leaves structure '
                            '(see the binned residuals)'
                            % (key, fit['misfit_max_db'], misfit_threshold_db))
        fits[key] = fit
    for key, acc in builder.modes.items():
        if key not in fits:
            warnings.append('warning: mode %s had %d pings and no usable beams; left out'
                            % (key, acc.pings))

    offsets, pairs, chain = solve_offsets(builder.tracker.events, fits, reference)
    unanchored = sorted(k for k, v in offsets.items() if not v['anchored'])
    if unanchored:
        warnings.append('warning: no switch path to the reference mode for %s; they have '
                        'no offset (offset_anchored=false, empty offset columns) and the '
                        'table must not be applied to them' % ', '.join(unanchored))
    for p in pairs:
        if p['directions_agree'] is False:
            warnings.append('warning: offset %s disagrees between switch directions by '
                            '%.2f +/- %.2f dB (seabed or range-term bias)'
                            % (p['meaning'], p['direction_difference_db'],
                               p['direction_difference_se_db']))
    return {'reference_mode': reference, 'reference_source': ref_source,
            'requested_reference_mode': reference_mode,
            'log_r0': log_r0, 'range_ref_m': 10.0 ** log_r0, 'range_ref_source': r0_source,
            'bin_width_deg': builder.bin_width_deg,
            'specular_exclusion_deg': builder.specular_exclusion_deg,
            'range_angle_groups_deg': [group_bounds_deg(g, builder.bin_width_deg)
                                       for g in builder.groups],
            'range_angle_edges_deg': list(builder.range_angle_edges_deg),
            'window_sec': builder.window_sec, 'misfit_threshold_db': misfit_threshold_db,
            'node_spacing_decades': node_spacing_decades,
            'modes': fits, 'offsets': offsets, 'pairs': pairs, 'chain': chain,
            'switches': {'events': len(builder.tracker.events),
                         'discarded': dict(builder.tracker.discarded)},
            'pings_without_info': builder.pings_without_info,
            'pings_without_beams': builder.pings_without_beams,
            'sonar_info_count': len(builder.history),
            'sonar_models': dict(builder.sonar_models),
            'gain': builder.gain.summary() if builder.gain is not None else None,
            'warnings': warnings}


def _fmt(value, spec):
    """Format a float with ``spec``, or ``nan``."""
    if value is None or not math.isfinite(value):
        return 'nan'
    return spec % value


def mode_table_lines(table, source_bags, box=None):
    """Return the lines (without newlines) of the per-mode table CSV."""
    lines = ['# format: %s' % MODE_TABLE_FORMAT,
             '# Per-mode empirical backscatter table (derive_angular_response.py '
             '--by-mode, cube_bathymetry#172)',
             '# key_spec: canonical_mode_key in derive_angular_response.py; '
             'vectors in test/data/mode_keys.json',
             '# reference_mode: %s' % table['reference_mode'],
             '# reference_mode_source: %s' % table['reference_source'],
             '# range_ref_m: %.6g' % table['range_ref_m'],
             '# range_ref_source: %s' % table['range_ref_source'],
             '# range_node_spacing_decades: %g' % table['node_spacing_decades'],
             '# range_angle_groups_deg: %s' % ','.join(
                 '%g-%g' % (lo, hi) for lo, hi in table['range_angle_groups_deg']),
             '# range_term_lookup: a beam uses the range rows whose angle_lo_deg <= '
             '|angle| < angle_hi_deg; below the first angle_lo_deg (the specular band) '
             'it uses the first group; a mode or group with no range rows has a zero term',
             '# angle_bin_width_deg: %g' % table['bin_width_deg'],
             '# specular_exclusion_deg: %g' % table['specular_exclusion_deg'],
             '# adjacency_window_s: %g' % table['window_sec'],
             '# misfit_threshold_db: %g' % table['misfit_threshold_db'],
             '# absorption: not modelled separately; the per-mode range term carries it',
             '# offset_meaning: offset_db_vs_ref is the level a mode reads above the '
             'reference mode after db_relative_to_nadir and its range term are removed '
             'from both, measured at mode switches; the corrected beam is '
             'raw - db_relative_to_nadir - r - offset_db_vs_ref, on the reference '
             "mode's nadir level at R0; a mode with offset_anchored=false has NO "
             'offset (the columns are empty) and must not be corrected by this table',
             '# empty_fields: var_db2 is empty where it cannot weight a beam (n < 2, or a '
             'variance under %g dB^2 that would print as 0.000): treat such a beam as '
             'uncorrectable; se_db is empty wherever it is not finite; the offset columns '
             'are empty for an unanchored mode; declared_tvg_absorption_db_per_km is nan '
             'for MIXED or an undeclared absorption; columns that do not apply to a row '
             '(var_db2 and db_relative_to_nadir on range rows, the angle bounds on angle '
             'rows) are empty; x is written with 10 significant digits'
             % MIN_WRITTEN_VARIANCE_DB2]
    if table.get('gain') is not None:
        # Written only under a declaration (cube_bathymetry#176): import_bag compares the
        # digest and the reference state with the declaration it is given.
        lines += gain_header_lines(table['gain'])
    if box is not None:
        lines.append('# box: %s' % ' '.join('%.9g' % v for v in box))
    lines.append('# source_bags: %s' % ','.join(source_bags))
    for key, fit in sorted(table['modes'].items()):
        off = table['offsets'][key]
        lines.append('# mode_summary: %s pings=%d beams=%d declared_tvg=%s '
                     'offset_db=%s offset_se_db=%s anchored=%s misfit_max_db=%s '
                     'misfit_exceeded=%s'
                     % (key, fit['pings'], fit['beams'], fit['declared_tvg_model'],
                        _fmt(off['offset_db'], '%.3f') if off['anchored'] else 'none',
                        _fmt(off['se_db'], '%.3f') if off['anchored'] else 'none',
                        'true' if off['anchored'] else 'false',
                        _fmt(fit['misfit_max_db'], '%.3f'),
                        'yes' if fit['misfit_exceeded'] else 'no'))
        for g in fit['groups']:
            lines.append('# range_term: %s %g-%g %s' % (
                key, g['lo_deg'], g['hi_deg'],
                ('fitted nodes=%d span=%g..%g trimmed=%s out_of_span=%d'
                 % (len(g['nodes']), g['span'][0], g['span'][1],
                    ','.join('%g' % t for t in g['trimmed_nodes']) or 'none',
                    g['out_of_span_samples']))
                if g['range_status'] == 'fitted' else g['range_status']))
    lines.append(','.join(TABLE_COLUMNS))
    for key, fit in sorted(table['modes'].items()):
        off = table['offsets'][key]
        # An unanchored mode has NO measured offset: the columns are left empty
        # (never 0.000, which a loader would apply) and offset_anchored=false.
        tail = ','.join((_fmt(off['offset_db'], '%.3f') if off['anchored'] else '',
                         _fmt(off['se_db'], '%.3f') if off['anchored'] else '',
                         'true' if off['anchored'] else 'false',
                         fit['declared_tvg_model'],
                         _fmt(fit['declared_tvg_absorption_db_per_km'], '%.6g')))
        for centre, cnt, level, var, rel in fit['angle']:
            # A variance that is not finite, or would print as 0.000 (identical
            # beams), cannot weight anything: the field is left EMPTY, so a
            # loader never divides by zero (review round 2).
            weighable = math.isfinite(var) and var >= MIN_WRITTEN_VARIANCE_DB2
            se = math.sqrt(var / cnt) if weighable else float('nan')
            # x is written with %.10g so any bin width or node spacing round-trips.
            lines.append('%s,angle,%.10g,,,%.3f,%d,%s,%+.3f,%s,%s'
                         % (key, centre, level, cnt, _fmt(var, '%.3f') if weighable else '',
                            rel, _fmt(se, '%.3f') if math.isfinite(se) else '', tail))
        for g in fit['groups']:
            if g['range_status'] != 'fitted':
                continue
            for x, r, se, sup in zip(g['nodes'], g['r'], g['r_se'], g['r_support']):
                lines.append('%s,range,%.10g,%g,%g,%.3f,%d,,,%s,%s'
                             % (key, x, g['lo_deg'], g['hi_deg'], r, int(round(sup)),
                                _fmt(se, '%.3f') if math.isfinite(se) else '', tail))
    return lines


def mode_report_lines(table):
    """Return the human run summary of a per-mode table (stdout)."""
    out = ['reference mode %s (%s); R0 = %.2f m (%s)'
           % (table['reference_mode'], table['reference_source'], table['range_ref_m'],
              table['range_ref_source'])]
    if table.get('gain') is not None:
        out += gain_report_lines(table['gain'])
    out.append('%-34s %9s %11s %8s %15s %8s %9s  %s'
               % ('mode', 'pings', 'beams', 'med_R_m', 'offset_dB(se)', 'misfit',
                  'scalar_mf', 'range term'))
    for key, fit in sorted(table['modes'].items()):
        off = table['offsets'][key]
        offset = ('%+.2f(%.2f)' % (off['offset_db'], off['se_db'])
                  if off['anchored'] else 'unanchored')
        out.append('%-34s %9d %11d %8.2f %15s %8s %9s  %s'
                   % (key, fit['pings'], fit['beams'], fit['median_range_m'], offset,
                      _fmt(fit['misfit_max_db'], '%.2f'),
                      _fmt(fit['scalar_comparison']['misfit_max_db'], '%.2f'),
                      fit['range_status']))
        for g in fit['groups']:
            if g['range_status'] == 'fitted':
                out.append('    r(log10 R/R0) %g-%g deg: ' % (g['lo_deg'], g['hi_deg'])
                           + ' '.join('%+.2f:%+.1f' % (x, r)
                                      for x, r in zip(g['nodes'], g['r']))
                           + ('; trimmed %s, %d beams beyond the span'
                              % (','.join('%g' % t for t in g['trimmed_nodes']),
                                 g['out_of_span_samples'])
                              if g['trimmed_nodes'] else ''))
            else:
                out.append('    r %g-%g deg: %s' % (g['lo_deg'], g['hi_deg'],
                                                    g['range_status']))
    sw = table['switches']
    out.append('switches: %d used; discarded %s'
               % (sw['events'], ', '.join('%d %s' % (v, k)
                                          for k, v in sorted(sw['discarded'].items()))
                  or 'none'))
    for p in table['pairs']:
        dirs = ', '.join('%s n=%d %s' % (name, d['count'], _fmt(d['mean_db'], '%+.2f'))
                         for name, d in sorted(p['directions'].items()))
        out.append('  %s: n=%d %s +/- %s dB; %s; directions differ %s +/- %s dB%s'
                   % (p['meaning'], p['count'], _fmt(p['mean_db'], '%+.2f'),
                      _fmt(p['se_db'], '%.2f'), dirs,
                      _fmt(p['direction_difference_db'], '%+.2f'),
                      _fmt(p['direction_difference_se_db'], '%.2f'),
                      '' if p['used'] else ' (not used: too few switches)'))
    if table['pings_without_info']:
        out.append('%d pings before the first SonarInfo were left out'
                   % table['pings_without_info'])
    return out


def _jsonable(value):
    """Convert a table dict to JSON-safe values (NaN/inf -> None, arrays -> lists)."""
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()
                if k not in ('range_db_per_cell', 'rel_db_per_bin')}
    if isinstance(value, (list, tuple)):
        return [_jsonable(v) for v in value]
    if isinstance(value, float):
        return value if math.isfinite(value) else None
    if hasattr(value, 'tolist'):
        return _jsonable(value.tolist())
    return value


def accumulate_by_mode(bag_paths, topic, info_topic, builder, box=None,
                       box_stats=None, corrupt=None):
    """
    Read detections + SonarInfo (+ TF with ``box``) in ONE time-ordered pass into ``builder``.

    Every bag is merged on the bag timestamp (merged_messages), so a ping is
    attributed to the latest SonarInfo at or before its stamp however the
    split files fall. Without ``box`` a ping is attributed once the bag clock
    is SONAR_INFO_GUARD_SEC past the time it was recorded; with ``box`` it is
    attributed when PingLocator releases it (after the TF guard) and only if it
    lies inside the box. Returns the number of detections messages read.
    """
    from rosidl_runtime_py.utilities import get_message

    if corrupt is None:
        corrupt = collections.Counter()
    metas = [read_bag_metadata(path) for path in bag_paths]
    type_maps = [type_map for type_map, _start in metas]
    if not any(topic in tm for tm in type_maps):
        raise SystemExit("error: topic '%s' not in any bag (have: %s)"
                         % (topic, ', '.join(sorted(set().union(*type_maps))) or 'none'))
    if not any(info_topic in tm for tm in type_maps):
        raise SystemExit("error: --by-mode needs the SonarInfo topic '%s' and no bag has "
                         'it; pass --sonar-info-topic (have: %s)'
                         % (info_topic, ', '.join(sorted(set().union(*type_maps)))))
    sources, tf_topics, msg_types = [], [], []
    for path, (type_map, start_ns) in zip(bag_paths, metas):
        kinds = {}
        if box is not None:
            kinds = {name: kind for name, type_name in type_map.items()
                     for kind in (is_tf_topic(name),)
                     if kind is not None and type_name == 'tf2_msgs/msg/TFMessage'}
        wanted = [t for t in (topic, info_topic) if t in type_map] + sorted(kinds)
        if not wanted:
            # A bag with none of the wanted topics is not part of this survey,
            # with or without --box: refusing beats listing it in source_bags.
            raise SystemExit("error: bag %s has neither '%s' nor '%s'%s"
                             % (path, topic, info_topic,
                                ' nor a TF topic' if box is not None else ''))
        sources.append((start_ns, functools.partial(open_reader, path, wanted)))
        tf_topics.append(kinds)
        msg_types.append({t: get_message(type_map[t]) for t in (topic, info_topic)
                          if t in type_map})
    locator = None
    if box is not None:
        if not any(kind == 'tf' for kinds in tf_topics for kind in kinds.values()):
            raise SystemExit(
                'error: --box positions pings from the dynamic TF (/tf) in the bags, '
                'and no bag has a /tf topic; add the bag that recorded the '
                "vehicle's /tf, or drop --box")
        locator = PingLocator()
    tf_msg_type = get_message('tf2_msgs/msg/TFMessage')
    guard_ns = int(round(SONAR_INFO_GUARD_SEC * 1e9))
    pending = collections.deque()    # (bag ns, stamp ns, PingRecord), no --box

    def take_ready(flush):
        for item, position, reason in locator.drain(flush):
            if position is None:
                box_stats[reason] += 1
            elif not in_box(position[0], position[1], box):
                box_stats['outside'] += 1
            else:
                box_stats['kept'] += 1
                builder.add_ping(item[0], item[1])

    n_msgs = 0
    for i, name, data, t_ns in merged_messages(sources):
        kind = tf_topics[i].get(name)
        if kind is not None:
            msg = deserialize_or_none(data, tf_msg_type)
            if msg is None:
                corrupt[name] += 1
                continue
            locator.add_tf(msg, kind == 'tf_static', t_ns)
            take_ready(False)
            continue
        msg_type = msg_types[i].get(name)
        if msg_type is None:
            continue
        msg = deserialize_or_none(data, msg_type)
        if msg is None:
            corrupt[name] += 1
        elif name == info_topic:
            builder.add_sonar_info_msg(msg)
        else:
            n_msgs += 1
            ping_ns = stamp_ns(msg.header.stamp)
            record = PingRecord(msg)
            if locator is not None:
                reason = locator.push(ping_ns, msg.header.frame_id, (ping_ns, record))
                if reason is not None:
                    box_stats[reason] += 1
            else:
                pending.append((t_ns, ping_ns, record))
        while pending and pending[0][0] + guard_ns <= t_ns:
            _bag_ns, ping_ns, record = pending.popleft()
            builder.add_ping(ping_ns, record)
    while pending:
        _bag_ns, ping_ns, record = pending.popleft()
        builder.add_ping(ping_ns, record)
    if locator is not None:
        take_ready(True)
        box_stats['max_pending'] = locator.max_pending
        box_stats['tf_rejected'] = locator.tf_rejected
    return n_msgs


ACCUMULATION_CACHE_FORMAT = 'derive-accumulation-cache v1'
# Bump when the accumulation changes (ping_beams filters, the cell grid, the
# mode key, OffsetSummary, ModeAccumulator): a cache made by older code must
# not be refitted as if it were current.
ACCUMULATION_VERSION = 2   # 2: the gain declaration's counters (#176)


def bag_identity(bag_path):
    """
    Return what identifies a bag's CONTENT for the accumulation cache.

    The real path plus, for a bag directory, every file's name, size and
    mtime (for a single file, its own): a bag replaced under the same name,
    or the same basename in another campaign's directory, no longer matches.
    """
    real = os.path.realpath(bag_path)
    files = []
    try:
        if os.path.isdir(real):
            for name in sorted(os.listdir(real)):
                st = os.stat(os.path.join(real, name))
                files.append([name, st.st_size, st.st_mtime_ns])
        else:
            st = os.stat(real)
            files.append([os.path.basename(real), st.st_size, st.st_mtime_ns])
    except OSError:
        files.append(['?', -1, -1])
    return [real, files]


def accumulation_cache_identity(args, source_bags, gain=None):
    """
    Return the settings an accumulation cache must have been made with to be reused.

    ``gain`` (a GainDeclaration or None) is part of it: a cache accumulated without a
    declaration is never refitted with one (or the other way round), because the
    statistics already hold the shifted, or unshifted, levels (cube_bathymetry#176).
    """
    return {'format': ACCUMULATION_CACHE_FORMAT, 'version': ACCUMULATION_VERSION,
            'cell_grid': [RANGE_CELL_LOG10_MIN, RANGE_CELL_DECADES, RANGE_CELL_COUNT],
            'source_bags': list(source_bags),
            'bags': [bag_identity(b) for b in args.bag],
            'topic': args.topic, 'sonar_info_topic': args.sonar_info_topic,
            'box': list(args.box) if args.box is not None else None,
            'bin_width_deg': args.bin_width_deg,
            'specular_exclusion_deg': args.specular_exclusion_deg,
            'adjacency_window_s': args.adjacency_window_s,
            'range_angle_edges': list(args.range_angle_edges),
            'gain_declaration': (None if gain is None
                                 else [gain.reference_state, gain.sha256()])}


def check_cache_trust(path, fd):
    """
    Refuse a cache file this user does not own or that others can write (SystemExit).

    The cache is a pickle, which runs code when loaded; the documented rule
    is to load only a file this script wrote for this user, and this is the
    check that enforces the part a stat can see. ``fd`` is the already-open
    descriptor the bytes will be read from, so what is checked is what is
    loaded (no window between a stat by name and the open).
    """
    try:
        st = os.fstat(fd)
    except OSError as e:
        raise SystemExit('error: --accumulation-cache %s cannot be read: %s' % (path, e))
    if not stat.S_ISREG(st.st_mode):
        raise SystemExit('error: --accumulation-cache %s is not a regular file' % path)
    if hasattr(os, 'getuid') and st.st_uid != os.getuid():
        raise SystemExit('error: --accumulation-cache %s is not owned by this user; a '
                         'cache is a pickle and is loaded only when this user wrote it'
                         % path)
    if st.st_mode & (stat.S_IWGRP | stat.S_IWOTH):
        raise SystemExit('error: --accumulation-cache %s is group- or world-writable; a '
                         'cache is a pickle and is loaded only when nobody else could '
                         'have written it' % path)


def load_accumulation_cache(path, identity, gain=None):
    """
    Return ``(builder, n_msgs, box_stats, corrupt)`` from write_accumulation_cache's file.

    SystemExit when the file fails check_cache_trust, is not such a cache, or
    was made with other settings, code or bag contents (the mismatching keys
    are named).
    """
    import pickle
    try:
        # O_NONBLOCK so a FIFO at the path cannot stall the open; the regular-file
        # check then rejects it before anything is read.
        fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NONBLOCK', 0))
    except OSError as e:
        raise SystemExit('error: --accumulation-cache %s cannot be read: %s' % (path, e))
    try:
        check_cache_trust(path, fd)
        with os.fdopen(fd, 'rb') as f:
            fd = None
            data = pickle.load(f)
    except SystemExit:
        raise
    except Exception as e:   # any unpickling failure is "not a cache"
        raise SystemExit('error: --accumulation-cache %s cannot be read: %s' % (path, e))
    finally:
        if fd is not None:
            os.close(fd)
    found = data.get('identity') if isinstance(data, dict) else None
    if not isinstance(found, dict) or found.get('format') != ACCUMULATION_CACHE_FORMAT:
        raise SystemExit('error: --accumulation-cache %s is not a %s file'
                         % (path, ACCUMULATION_CACHE_FORMAT))
    mismatch = sorted(k for k in identity if found.get(k) != identity[k])
    if mismatch:
        raise SystemExit('error: --accumulation-cache %s was made with different %s; '
                         'delete it to re-accumulate' % (path, ', '.join(mismatch)))
    try:
        builder = ModeTableBuilder.from_state(data['state'], gain=gain)
        n_msgs = int(data['n_msgs'])
        box_stats = data.get('box_stats')
        corrupt = collections.Counter(data.get('corrupt') or {})
    except Exception as e:   # a half-written or foreign payload
        raise SystemExit('error: --accumulation-cache %s is not a complete %s file: %s'
                         % (path, ACCUMULATION_CACHE_FORMAT, e))
    return builder, n_msgs, box_stats, corrupt


def write_accumulation_cache(path, builder, n_msgs, identity, box_stats, corrupt):
    """
    Write the accumulated statistics (builder.to_state) as a pickle, atomically.

    Returns None on success, else the reason as text: a cache that cannot be
    written is a warning, never the end of a run whose bag pass is done.
    """
    import pickle
    directory = os.path.dirname(os.path.abspath(path)) or '.'
    tmp = None
    try:
        fd, tmp = tempfile.mkstemp(prefix='.accumulation-cache-', dir=directory)
        with os.fdopen(fd, 'wb') as f:
            pickle.dump({'identity': identity, 'state': builder.to_state(), 'n_msgs': n_msgs,
                         'box_stats': box_stats, 'corrupt': dict(corrupt or {})},
                        f, protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, path)
        tmp = None
        return None
    except Exception as e:   # reported to the caller as text
        return str(e)
    finally:
        if tmp is not None:
            try:
                os.unlink(tmp)
            except OSError:
                pass


def main_by_mode(args, source_bags, gain=None):
    """Run --by-mode: read the bags (or the cache), fit, write the table and diagnostics."""
    import json

    identity = accumulation_cache_identity(args, source_bags, gain)
    cache = args.accumulation_cache
    if cache is not None and os.path.exists(cache):
        builder, n_msgs, box_stats, corrupt = load_accumulation_cache(cache, identity, gain)
        print('loaded the accumulated statistics from %s (bag pass skipped)' % cache)
    else:
        builder = ModeTableBuilder(args.bin_width_deg, args.specular_exclusion_deg,
                                   args.adjacency_window_s, args.range_angle_edges, gain=gain)
        box_stats = new_box_stats() if args.box is not None else None
        corrupt = collections.Counter()
        n_msgs = accumulate_by_mode(args.bag, args.topic, args.sonar_info_topic, builder,
                                    box=args.box, box_stats=box_stats, corrupt=corrupt)
        usable = box_stats is None or box_stats['kept'] > 0
        if usable and len(builder.history) == 0:
            # The corrupt-message report is often the reason: print it first.
            warning = corrupt_report(corrupt)
            if warning is not None:
                print(warning, file=sys.stderr)
            raise SystemExit("error: no SonarInfo message on '%s' could be read; --by-mode "
                             'needs it' % args.sonar_info_topic)
        if cache is not None and usable:
            failure = write_accumulation_cache(cache, builder, n_msgs, identity, box_stats,
                                               corrupt)
            if failure is None:
                print('wrote the accumulated statistics to %s' % cache)
            else:
                print('warning: could not write --accumulation-cache %s (%s); the table '
                      'is still derived from this run' % (cache, failure), file=sys.stderr)
    # The bag-pass reports are printed on a cache hit too: the table is built
    # from the same data, so the same dropped pings and corrupt messages apply.
    warning = corrupt_report(corrupt)
    if warning is not None:
        print(warning, file=sys.stderr)
    if box_stats is not None:
        print(box_report(args.box, box_stats, n_msgs))
        warning = tf_rejected_warning(box_stats['tf_rejected'])
        if warning is not None:
            print(warning, file=sys.stderr)
        if box_stats['kept'] == 0:
            raise SystemExit('error: no pings inside --box; nothing to derive')
    table = builder.finish(args.reference_mode, args.range_ref_m, args.misfit_threshold_db,
                           args.range_node_spacing_decades)
    for warning in table['warnings']:
        print(warning, file=sys.stderr)
    for warning in (gain_empty_segment_warnings(table['gain'], args.box is not None)
                    if table['gain'] else []):
        print(warning, file=sys.stderr)
    write_text_atomically(args.output, ''.join(
        line + '\n' for line in mode_table_lines(table, source_bags, args.box)))
    if args.diagnostics_out:
        diag = dict(table)
        diag['source_bags'] = source_bags
        diag['box'] = args.box
        diag['detections_read'] = n_msgs
        write_text_atomically(args.diagnostics_out,
                              json.dumps(_jsonable(diag), indent=1, sort_keys=True) + '\n')
    for line in mode_report_lines(table):
        print(line)
    print('read %d pings across %d bag(s) on %s; wrote a %d-mode table to %s'
          % (n_msgs, len(args.bag), args.topic, len(table['modes']), args.output))
    return 0


def main(argv=None):
    """Read the bag(s), bin by |rx_angles| and write the curve CSV (one line)."""
    args = parse_args(sys.argv[1:] if argv is None else argv)
    source_bags = bag_basenames(args.bag)  # refuse a bad name before reading
    check_distinct_bags(args.bag)
    gain = None
    if args.gain_declaration is not None:
        try:
            gain = load_gain_declaration(args.gain_declaration)
        except ValueError as e:
            raise SystemExit('error: --gain-declaration could not be loaded: %s' % e)
        print("gain-state declaration %s: reference state '%s', %d segment(s), sha256 %s"
              % (gain.path, gain.reference_state, len(gain.segments), gain.sha256()))
        for warning in gain.warnings:
            print('warning: --gain-declaration %s: %s' % (gain.path, warning), file=sys.stderr)
    if args.by_mode:
        return main_by_mode(args, source_bags, gain)
    tracker = GainTracker(gain) if gain is not None else None

    bins = {}      # bin_index -> [sum_adjusted, sum_R, count], merged across bags
    freqs = []     # per-ping ping_info.frequency (Hz)
    box_stats = new_box_stats() if args.box is not None else None
    corrupt = collections.Counter()   # topic -> messages that would not deserialize
    n_msgs = accumulate(
        args.bag, args.topic, args.bin_width_deg, args.remove_tl, bins, freqs,
        box=args.box, box_stats=box_stats, corrupt=corrupt, gain=tracker)
    warning = corrupt_report(corrupt)
    if warning is not None:
        print(warning, file=sys.stderr)
    if box_stats is not None:
        print(box_report(args.box, box_stats, n_msgs))
        warning = tf_rejected_warning(box_stats['tf_rejected'])
        if warning is not None:
            print(warning, file=sys.stderr)
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

    if tracker is not None:
        summary = tracker.summary()
        header += gain_header_lines(summary)
        for line in gain_report_lines(summary):
            print(line)
        for warning in gain_empty_segment_warnings(summary, args.box is not None):
            print(warning, file=sys.stderr)
    n_rows = write_csv(
        bins, args.bin_width_deg, args.output, args.remove_tl, alpha, header)
    print('read %d pings across %d bag(s) on %s; wrote %d-bin curve to %s'
          % (n_msgs, len(args.bag), args.topic, n_rows, args.output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
