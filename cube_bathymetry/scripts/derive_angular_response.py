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
transmission loss ``TL = 40*log10(R) + 2*alpha*R`` (R = ``twtt*c/2`` per beam,
alpha = freshwater Francois-Garrison absorption from ``--water-temp-c`` and the
per-ping frequency) is subtracted from each beam BEFORE binning, so the curve
becomes a TL-removed residual that is depth/range transferable. The CSV columns
are unchanged; the header records ``tl_removed``/``absorption_db_per_m`` so the
C++ estimator applies the identical TL (and never recomputes alpha).

Usage:
    derive_angular_response.py <bag> [<bag> ...] -t <topic> -o <out.csv>
    derive_angular_response.py <bag> ... -t <topic> -o <out.csv> \\
        --remove-tl --water-temp-c 24.0
"""

import argparse
import math
import sys


def parse_args(argv):
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
                   help='water temperature, degC (REQUIRED with --remove-tl); '
                        'feeds the freshwater absorption alpha')
    p.add_argument('--salinity', type=float, default=0.0,
                   help='salinity, psu (default 0.0 = fresh water). Non-zero '
                        'seawater absorption is out of scope for cube#87.')
    p.add_argument('--depth-m', type=float, default=0.0,
                   help='nominal depth for the absorption pressure term D '
                        '(default 0.0 -> P3=1 at lake depth)')
    args = p.parse_args(argv)
    if args.remove_tl and args.water_temp_c is None:
        p.error('--water-temp-c is required when --remove-tl is set')
    if args.remove_tl and args.salinity > 0.0:
        p.error('--salinity > 0 (seawater) is out of scope for cube#87 '
                '(freshwater pure-water absorption only); omit --salinity')
    return args


def freshwater_absorption_db_per_m(freq_khz, temp_c, depth_m=0.0):
    """
    Francois-Garrison pure-water (freshwater) absorption alpha, dB/m.

    Only the pure-water term is needed in fresh water (the boric-acid and
    MgSO4 seawater terms vanish at salinity 0):

        alpha_w(dB/km) = A3 * P3 * f_kHz**2

    with the temperature-split A3 and the pressure term P3 (D in metres).
    Returns alpha in dB/m (= alpha_w / 1000). Sanity: f=500 kHz, T=24 C,
    D=0 -> ~0.049 dB/m.
    """
    t = temp_c
    if t > 20.0:
        a3 = (3.964e-4 - 1.146e-5 * t + 1.45e-7 * t ** 2 - 6.5e-10 * t ** 3)
    else:
        a3 = (4.937e-4 - 2.590e-5 * t + 9.110e-7 * t ** 2 - 1.500e-8 * t ** 3)
    p3 = 1.0 - 3.83e-5 * depth_m + 4.9e-10 * depth_m ** 2
    alpha_db_per_km = a3 * p3 * freq_khz ** 2
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


def accumulate(reader, topic, bin_width_deg, remove_tl, bins, freqs):
    """
    Accumulate per-bin sums on `topic` into `bins` (merged across bags).

    Each bin slot is ``[sum_adjusted, sum_R, count]``:

    * tier-1 (``remove_tl`` False): ``sum_adjusted`` is the raw dB sum; ``sum_R``
      stays 0 (unused).
    * tier-2 (``remove_tl`` True): the range-INDEPENDENT-of-alpha part of the TL
      (the spreading term ``40*log10(R)``) is removed inline, so ``sum_adjusted``
      is ``sum(db - 40*log10(R))`` and ``sum_R`` is ``sum(R)``. The absorption
      term ``2*alpha*R`` is removed in write_csv once alpha is known from the
      median frequency -- a single pass over the bag.

    `freqs` collects each ping's ``ping_info.frequency`` (Hz) for the median.
    Returns the number of messages read on `topic`.
    """
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py.utilities import get_message

    type_map = {t.name: t.type for t in reader.get_all_topics_and_types()}
    if topic not in type_map:
        raise SystemExit(
            "error: topic '%s' not in bag (have: %s)"
            % (topic, ', '.join(sorted(type_map)) or 'none'))
    msg_type = get_message(type_map[topic])

    n_msgs = 0
    while reader.has_next():
        name, data, _stamp = reader.read_next()
        if name != topic:
            continue
        msg = deserialize_message(data, msg_type)
        n_msgs += 1
        intensities = msg.intensities
        rx_angles = msg.rx_angles
        twtt = msg.two_way_travel_times
        sound_speed = msg.ping_info.sound_speed
        freq = msg.ping_info.frequency
        if math.isfinite(freq) and freq > 0.0:
            freqs.append(freq)
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
                # (log10 undefined) so they never poison the residual.
                if i >= len(twtt):
                    continue
                range_m = twtt[i] * sound_speed / 2.0
                if not math.isfinite(range_m) or range_m <= 0.0:
                    continue
                adjusted = db - 40.0 * math.log10(range_m)
            else:
                adjusted = db

            abs_deg = abs(math.degrees(ang))
            idx = int(abs_deg // bin_width_deg)
            slot = bins.setdefault(idx, [0.0, 0.0, 0])
            slot[0] += adjusted
            slot[1] += range_m
            slot[2] += 1
    return n_msgs


def write_csv(bins, bin_width_deg, out_path, remove_tl, alpha, water_temp_c):
    """
    Write the binned means and db_relative_to_nadir to a curve CSV.

    For tier-2 (`remove_tl`), the absorption term ``2*alpha*R`` is removed here
    using the per-bin mean range, completing the TL removal started in
    accumulate. The header records the TL provenance so the C++ estimator applies
    the identical model.
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
            # Remove the absorption part of the TL: 2*alpha*mean_R.
            mean -= 2.0 * alpha * (s_range / n)
        rows.append([center, mean, n])

    nadir_mean = rows[0][1]  # lowest-angle (nadir-most) bin
    with open(out_path, 'w') as f:
        f.write('# Empirical angular response (derive_angular_response.py, cube#81)\n')
        if remove_tl:
            # Self-describing tier-2 header (cube#87): the estimator reads
            # tl_removed + absorption_db_per_m and applies the identical TL.
            f.write('# tl_removed: true\n')
            f.write('# absorption_db_per_m: %.6g\n' % alpha)
            f.write('# water_temp_c: %.3g\n' % water_temp_c)
            f.write('# tl_model: 40*log10(R) + 2*alpha*R   (R = twtt*c/2, metres)\n')
        else:
            f.write('# tl_removed: false\n')
        f.write('abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n')
        for center, mean_db, n in rows:
            db_rel = mean_db - nadir_mean
            f.write('%.1f,%.3f,%d,%+.3f\n' % (center, mean_db, n, db_rel))
    return len(rows)


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

    bins = {}      # bin_index -> [sum_adjusted, sum_R, count], merged across bags
    freqs = []     # per-ping ping_info.frequency (Hz)
    n_msgs = 0
    for bag_path in args.bag:
        reader = open_reader(bag_path)
        n_msgs += accumulate(
            reader, args.topic, args.bin_width_deg, args.remove_tl, bins, freqs)

    alpha = 0.0
    if args.remove_tl:
        if not freqs:
            raise SystemExit(
                'error: --remove-tl needs a finite ping_info.frequency; none found')
        freq_khz = median(freqs) / 1000.0
        alpha = freshwater_absorption_db_per_m(
            freq_khz, args.water_temp_c, args.depth_m)
        print('tier-2: median frequency %.1f kHz, T=%.1f C, D=%.1f m -> '
              'alpha=%.6g dB/m' % (freq_khz, args.water_temp_c, args.depth_m, alpha))

    n_rows = write_csv(
        bins, args.bin_width_deg, args.output,
        args.remove_tl, alpha, args.water_temp_c if args.remove_tl else 0.0)
    print('read %d pings across %d bag(s) on %s; wrote %d-bin curve to %s'
          % (n_msgs, len(args.bag), args.topic, n_rows, args.output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
