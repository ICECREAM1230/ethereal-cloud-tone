"""Check the integrity of one CH569 PCM capture using its saved files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import wave


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_dir", type=Path)
    parser.add_argument("--expect-rate", type=int, default=48000)
    parser.add_argument("--require-audio", action="store_true")
    parser.add_argument(
        "--require-crc",
        action="store_true",
        help="reject legacy v1 frames that have no integrity check",
    )
    args = parser.parse_args(argv)

    capture_dir = args.capture_dir
    stats = json.loads((capture_dir / "stats.json").read_text(encoding="utf-8"))
    failures: list[str] = []
    samples = stats.get("audio", {}).get("sample_count", 0)

    for field in ("valid_frames", "payload_bytes", "sample_rate"):
        if field not in stats:
            failures.append(f"missing {field} in stats.json")
    if stats.get("valid_frames", 0) <= 0:
        failures.append("no valid frames")
    if stats.get("sample_rate") != args.expect_rate:
        failures.append(f"sample rate is {stats.get('sample_rate')}, expected {args.expect_rate}")
    for field in (
        "invalid_frames",
        "seq_jumps",
        "seq_frames_missing",
        "seq_repeats_or_reorders",
        "resync_count",
        "sample_rate_changes",
        "crc_errors",
        "frames_rejected",
        "dropped_frames",
    ):
        if stats.get(field, 0) != 0:
            failures.append(f"{field}={stats[field]}")
    unverified = stats.get("frames_unverified", 0)
    if args.require_crc and unverified:
        failures.append(f"frames_unverified={unverified}")
    if not stats.get("wav_container_verified", stats.get("wav_verified", False)):
        failures.append(f"WAV container verification failed: {stats.get('wav_error')}")
    if not stats.get("stream_integrity_verified", stats.get("wav_verified", False)):
        failures.append(
            "source stream integrity failed: "
            f"{stats.get('stream_integrity_error', 'details unavailable')}"
        )
    if samples <= 0:
        failures.append("no PCM samples")

    payload = capture_dir / "payload.pcm"
    if not payload.is_file():
        failures.append("payload.pcm is missing")
    elif payload.stat().st_size != stats.get("payload_bytes") or payload.stat().st_size != 2 * samples:
        failures.append("payload size disagrees with stats.json")

    wav_path = capture_dir / "audio.wav"
    if not wav_path.is_file():
        failures.append("audio.wav is missing")
    else:
        try:
            with wave.open(str(wav_path), "rb") as wav:
                if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) != (
                    1, 2, args.expect_rate, samples
                ):
                    failures.append("WAV format or frame count disagrees with stats.json")
        except (OSError, EOFError, wave.Error) as exc:
            failures.append(f"cannot read audio.wav: {exc}")

    if args.require_audio and stats.get("audio", {}).get("rms", 0) <= 0:
        failures.append("capture is silent")

    print(
        f"frames={stats.get('valid_frames', 0)} samples={samples} "
        f"rate={stats.get('sample_rate')} seq_jumps={stats.get('seq_jumps', 0)} "
        f"crc_ok={stats.get('frames_crc_valid', 0)} unverified={unverified} "
        f"crc_errors={stats.get('crc_errors', 0)} dropped={stats.get('dropped_frames', 0)} "
        f"wav_container={stats.get('wav_container_verified', stats.get('wav_verified', False))} "
        f"stream_integrity={stats.get('stream_integrity_verified', stats.get('wav_verified', False))} "
        f"rms={stats.get('audio', {}).get('rms', 0):.2f}"
    )
    if unverified:
        print("WARN: legacy v1 frames have no CRC and are not bit-perfect evidence")
    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: capture structure and continuity")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
