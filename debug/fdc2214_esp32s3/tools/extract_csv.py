#!/usr/bin/env python3
"""Extract complete application records from an idf.py monitor/terminal log."""
import argparse
import csv
from pathlib import Path
import re
import sys

SAMPLE_HEADER = ["type", "epoch", "sequence", "time_us", "valid_mask", "status", "status_errors",
                 "error_channels", "ratio_errors", "output_skipped", "ch0", "flags0", "ch1", "flags1", "ch2", "flags2", "ch3", "flags3"]
RANGE_HEADER = ["type", "epoch", "channel", "valid_count", "error_count", "has_range", "min", "max", "span"]
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")

def parse_record(line):
    line = ANSI.sub("", line)
    match = re.search(r"(?:sample|range),", line)
    if not match:
        return None
    fields = next(csv.reader([line[match.start():].strip()]))
    try:
        if fields[0] == "sample":
            if len(fields) != len(SAMPLE_HEADER):
                return None
            values = [int(x, 0) for x in fields[1:]]
            if min(values) < 0 or values[3] > 15 or values[6] > 15 or values[7] > 15:
                return None
            if any(values[i] > 0x0FFFFFFF or values[i+1] > 3 for i in (9, 11, 13, 15)):
                return None
        else:
            if len(fields) != len(RANGE_HEADER):
                return None
            epoch, channel, valid, errors, present = map(int, fields[1:6])
            if min(epoch, channel, valid, errors) < 0 or channel > 3 or present not in (0, 1):
                return None
            if present:
                low, high, span = map(int, fields[6:9])
                if valid == 0 or not 0 <= low <= high <= 0x0FFFFFFF or span != high-low:
                    return None
            elif any(fields[6:9]):
                return None
    except (ValueError, IndexError):
        return None
    return fields

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="Existing log file, or '-' for stdin")
    parser.add_argument("--samples", type=Path, required=True)
    parser.add_argument("--ranges", type=Path, required=True)
    args = parser.parse_args()
    counts = {"sample": 0, "range": 0}
    source = sys.stdin if str(args.log) == "-" else args.log.open(errors="replace")
    try:
        with args.samples.open("w", newline="") as samples, args.ranges.open("w", newline="") as ranges:
            writers = {"sample": csv.writer(samples), "range": csv.writer(ranges)}
            writers["sample"].writerow(SAMPLE_HEADER)
            writers["range"].writerow(RANGE_HEADER)
            for line in source:
                record = parse_record(line)
                if record:
                    writers[record[0]].writerow(record)
                    counts[record[0]] += 1
    finally:
        if source is not sys.stdin:
            source.close()
    print(f"Extracted complete records: {counts}; incomplete/interleaved lines ignored", file=sys.stderr)

if __name__ == "__main__":
    main()
