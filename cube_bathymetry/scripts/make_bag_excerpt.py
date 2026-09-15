#!/usr/bin/env python3
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
r"""
Write a ping-bounded excerpt of a SonarDetections bag (cube_bathymetry#143).

`test_real_bag_smoke` runs the real `import_bag` / `batch_regen_bag` binaries
over a short excerpt of a survey bag. The excerpt is survey data, not a
committed fixture: it lives beside its source bag in the archive and is
referenced by path. This rebuilds it on any host that can read the source.

The excerpt starts at the source bag's first message, so the latched
`/tf_static` is included; it carries exactly ``--pings`` messages on the ping
topic, plus ``--tail`` seconds of the other topics after the last one, so the
projector's pending pings still find their transforms.

The 3,000-ping excerpt the smoke test expects was made with::

    make_bag_excerpt.py \\
        <archive>/bag_2026-06-09T14.51.50_m3_detections \\
        <archive>/bag_2026-06-09T14.51.50_m3_detections_3000ping_excerpt \\
        --pings 3000 --drop /bizzy/sensors/m3/soundings

`/bizzy/sensors/m3/soundings` is dropped because it is the same detections
re-published as a PointCloud2, which the import does not read: keeping it
would triple the excerpt for nothing.
"""

import argparse
import sys

import rosbag2_py


def open_reader(uri, storage_id):
    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=uri, storage_id=storage_id),
        rosbag2_py.ConverterOptions('', ''))
    return reader


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', help='bag directory to read')
    parser.add_argument('destination', help='bag directory to write (must not exist)')
    parser.add_argument('--pings', type=int, default=3000,
                        help='messages to keep on --topic (default 3000)')
    parser.add_argument('--topic', default='/bizzy/sensors/m3/detections',
                        help='the ping topic the count applies to')
    parser.add_argument('--tail', type=float, default=5.0,
                        help='seconds of the other topics to keep after the last ping')
    parser.add_argument('--drop', action='append', default=[],
                        help='topic to leave out entirely (repeatable)')
    parser.add_argument('--storage-id', default='mcap')
    args = parser.parse_args()

    drop = set(args.drop)

    # Pass one: the timestamp of the Nth ping.
    reader = open_reader(args.source, args.storage_id)
    seen = 0
    cutoff = None
    while reader.has_next():
        topic, _data, stamp = reader.read_next()
        if topic == args.topic:
            seen += 1
            if seen == args.pings:
                cutoff = stamp
                break
    if cutoff is None:
        sys.exit(f'{args.source} has only {seen} message(s) on {args.topic}; '
                 f'wanted {args.pings}')
    del reader

    # Pass two: copy from the start (so /tf_static comes with it) to the
    # cutoff plus the tail, capping the ping topic at exactly --pings.
    reader = open_reader(args.source, args.storage_id)
    writer = rosbag2_py.SequentialWriter()
    writer.open(
        rosbag2_py.StorageOptions(uri=args.destination, storage_id=args.storage_id),
        rosbag2_py.ConverterOptions('', ''))
    for meta in reader.get_all_topics_and_types():
        if meta.name not in drop:
            writer.create_topic(meta)

    end = cutoff + int(args.tail * 1e9)
    written = {}
    pings = 0
    while reader.has_next():
        topic, data, stamp = reader.read_next()
        if stamp > end:
            break
        if topic in drop:
            continue
        if topic == args.topic:
            if pings >= args.pings:
                continue
            pings += 1
        writer.write(topic, data, stamp)
        written[topic] = written.get(topic, 0) + 1
    del writer

    print(f'{args.destination}: {pings} ping(s) on {args.topic}')
    for name in sorted(written):
        print(f'  {name}: {written[name]}')


if __name__ == '__main__':
    main()
