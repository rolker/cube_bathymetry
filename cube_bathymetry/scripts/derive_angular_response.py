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

"""
Derive an empirical backscatter angular-response curve from a bag.

Reads a marine_acoustic_msgs/SonarDetections topic, bins each beam's
``intensities`` (dB) by ``|rx_angles|`` (converted to degrees) into fixed-width
bins, and writes a CSV with the columns consumed by the CUBE estimator's
empirical ARA correction (cube_bathymetry#81):

    abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir

``db_relative_to_nadir`` is ``mean_bs_db - nadir_bin_mean_bs_db`` so the nadir
bin is ~0 and off-nadir bins are <= 0; the estimator subtracts this column from
each beam's raw dB. NaN/inf intensities and beams flagged bad are skipped.

Usage:
    derive_angular_response.py <bag> -t <detections_topic> -o <out.csv>
"""

import argparse
import math
import sys


def parse_args(argv):
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('bag', help='rosbag2 directory (or .mcap/.db3 file)')
    p.add_argument('-t', '--topic', required=True,
                   help='marine_acoustic_msgs/SonarDetections topic')
    p.add_argument('-o', '--output', required=True, help='output CSV path')
    p.add_argument('--bin-width-deg', type=float, default=2.0,
                   help='angular bin width in degrees (default 2.0)')
    return p.parse_args(argv)


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


def accumulate(reader, topic, bin_width_deg):
    """Return {bin_index: [sum_db, n]} for finite intensities on `topic`."""
    from rclpy.serialization import deserialize_message
    from rosidl_runtime_py.utilities import get_message

    type_map = {t.name: t.type for t in reader.get_all_topics_and_types()}
    if topic not in type_map:
        raise SystemExit(
            "error: topic '%s' not in bag (have: %s)"
            % (topic, ', '.join(sorted(type_map)) or 'none'))
    msg_type = get_message(type_map[topic])

    bins = {}  # bin_index -> [sum_db, count]
    n_msgs = 0
    while reader.has_next():
        name, data, _stamp = reader.read_next()
        if name != topic:
            continue
        msg = deserialize_message(data, msg_type)
        n_msgs += 1
        intensities = msg.intensities
        rx_angles = msg.rx_angles
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
            abs_deg = abs(math.degrees(ang))
            idx = int(abs_deg // bin_width_deg)
            slot = bins.setdefault(idx, [0.0, 0])
            slot[0] += db
            slot[1] += 1
    return bins, n_msgs


def write_csv(bins, bin_width_deg, out_path):
    """Write the binned means and db_relative_to_nadir to a curve CSV."""
    if not bins:
        raise SystemExit('error: no finite intensity/angle samples found')

    # Per-bin mean dB at the bin centre.
    rows = []
    for idx in sorted(bins):
        s, n = bins[idx]
        center = (idx + 0.5) * bin_width_deg
        rows.append([center, s / n, n])

    nadir_mean = rows[0][1]  # lowest-angle (nadir-most) bin
    with open(out_path, 'w') as f:
        f.write('# Empirical angular response (derive_angular_response.py, cube#81)\n')
        f.write('abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir\n')
        for center, mean_db, n in rows:
            db_rel = mean_db - nadir_mean
            f.write('%.1f,%.3f,%d,%+.3f\n' % (center, mean_db, n, db_rel))
    return len(rows)


def main(argv=None):
    """Read the bag, bin by |rx_angles|, and write the curve CSV."""
    args = parse_args(sys.argv[1:] if argv is None else argv)
    reader = open_reader(args.bag)
    bins, n_msgs = accumulate(reader, args.topic, args.bin_width_deg)
    n_rows = write_csv(bins, args.bin_width_deg, args.output)
    print('read %d pings on %s; wrote %d-bin curve to %s'
          % (n_msgs, args.topic, n_rows, args.output))
    return 0


if __name__ == '__main__':
    sys.exit(main())
