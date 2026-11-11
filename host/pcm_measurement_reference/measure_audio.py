"""Measure one PCM/WAV file and optionally compare a parameter-transition take."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from .pcm_capture.measurement import analyze_audio, compare_audio, load_audio


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="candidate WAV or interleaved s16le PCM")
    parser.add_argument("--reference", type=Path, help="reference file for A/B comparison")
    parser.add_argument("--sample-rate", type=int, help="required for raw PCM")
    parser.add_argument("--channels", type=int, help="required for raw PCM")
    parser.add_argument("--silence-dbfs", type=float, default=-70.0)
    parser.add_argument("--transition-index", type=int, help="sample index at the parameter change")
    parser.add_argument("--window-ms", type=float, default=20.0)
    parser.add_argument("--json", type=Path, help="save the complete report as JSON")
    args = parser.parse_args(argv)

    candidate = load_audio(args.input, sample_rate=args.sample_rate, channels=args.channels)
    report = {
        "schema": "ch569-pcm-measurement-v1",
        "input": str(args.input),
        "measurement": analyze_audio(candidate, silence_dbfs=args.silence_dbfs),
    }
    if args.reference:
        reference = load_audio(
            args.reference,
            sample_rate=args.sample_rate,
            channels=args.channels,
        )
        report["reference"] = str(args.reference)
        report["comparison"] = compare_audio(
            reference,
            candidate,
            transition_index=args.transition_index,
            window_ms=args.window_ms,
            silence_dbfs=args.silence_dbfs,
        )
    elif args.transition_index is not None:
        parser.error("--transition-index requires --reference")

    safe_report = _json_safe(report)
    rendered = json.dumps(safe_report, indent=2, ensure_ascii=False, allow_nan=False)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


def _json_safe(value):
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {key: _json_safe(item) for key, item in value.items()}
    if isinstance(value, list):
        return [_json_safe(item) for item in value]
    return value


if __name__ == "__main__":
    raise SystemExit(main())
