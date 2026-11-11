"""Command-line interface for PCM capture."""

from __future__ import annotations

import argparse
from pathlib import Path

from .capture import run_capture
from .readers import FakeReader, RateLimitedReader, SerialReader, UsbBulkReader


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        reader = make_reader(args)
        run_capture(
            reader=reader,
            duration=args.duration,
            output_root=Path(args.out),
            label=args.label,
            debug_errors=args.debug_errors,
        )
    except KeyboardInterrupt:
        print("capture interrupted")
        return 130
    except Exception as exc:
        print(f"error: {exc}")
        return 1

    return 0


def build_parser() -> argparse.ArgumentParser:
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        "--out",
        default="captures",
        help="output root for timestamped capture directories",
    )
    common.add_argument("--label", help="optional label added to the capture directory name")
    common.add_argument(
        "--duration",
        type=float,
        default=10.0,
        help="seconds to capture; use 0 to run until Ctrl+C",
    )
    common.add_argument(
        "--debug-errors",
        action="store_true",
        help="also write chunks near parser errors to errors.bin",
    )

    parser = argparse.ArgumentParser(
        prog="pcm_capture",
        description="Capture framed s16le mono PCM from FPGA/CH569 streams.",
        parents=[common],
    )
    subparsers = parser.add_subparsers(dest="source", required=True)

    fake = subparsers.add_parser(
        "fake",
        help="generate valid test frames locally",
        parents=[common],
    )
    fake.add_argument("--pattern", choices=("counter", "sine"), default="counter")
    fake.add_argument("--sample-rate", type=int, default=48000)
    fake.add_argument("--samples-per-frame", type=int, default=256)
    fake.add_argument("--frequency", type=float, default=1000.0)
    fake.add_argument(
        "--unlimited-rate",
        action="store_true",
        help="do not pace fake data to the selected sample rate",
    )

    serial = subparsers.add_parser(
        "serial",
        help="read bytes from a serial/CDC port",
        parents=[common],
    )
    serial.add_argument("--port", required=True)
    serial.add_argument("--baud", type=int, required=True)

    usb = subparsers.add_parser(
        "usb",
        help="read bytes from a USB Bulk IN endpoint",
        parents=[common],
    )
    usb.add_argument("--vid", required=True, type=parse_int)
    usb.add_argument("--pid", required=True, type=parse_int)
    usb.add_argument("--ep-in", type=parse_int)
    usb.add_argument("--timeout-ms", type=int, default=200)

    return parser


def make_reader(args):
    if args.source == "fake":
        reader = FakeReader(
            pattern=args.pattern,
            sample_rate=args.sample_rate,
            samples_per_frame=args.samples_per_frame,
            frequency=args.frequency,
        )
        if args.unlimited_rate:
            return reader

        frame_bytes = 14 + args.samples_per_frame * 2
        frames_per_second = args.sample_rate / args.samples_per_frame
        return RateLimitedReader(reader, frame_bytes * frames_per_second)

    if args.source == "serial":
        return SerialReader(port=args.port, baud=args.baud)

    if args.source == "usb":
        return UsbBulkReader(
            vid=args.vid,
            pid=args.pid,
            ep_in=args.ep_in,
            timeout_ms=args.timeout_ms,
        )

    raise ValueError(f"unknown source: {args.source}")


def parse_int(value: str) -> int:
    return int(value, 0)
