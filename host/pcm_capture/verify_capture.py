"""Check the integrity of one CH569 PCM capture using its saved files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import wave


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_dir", type=Path)
    parser.add_argument("--expect-rate", type=int, default=None)
    parser.add_argument("--require-audio", action="store_true")
    args = parser.parse_args()

    capture_dir = args.capture_dir
    stats = json.loads((capture_dir / "stats.json").read_text(encoding="utf-8"))
    failures: list[str] = []
    channels=stats.get("channels",1);sample_width=stats.get("sample_width",2)
    expected_rate=args.expect_rate or stats.get("sample_rate")
    samples = stats.get("audio", {}).get("sample_count", 0)

    for field in ("valid_frames", "payload_bytes", "sample_rate"):
        if field not in stats:
            failures.append(f"missing {field} in stats.json")
    if stats.get("valid_frames", 0) <= 0:
        failures.append("no valid frames")
    if stats.get("sample_rate") != expected_rate:
        failures.append(f"sample rate is {stats.get('sample_rate')}, expected {expected_rate}")
    for field in ("invalid_frames", "seq_jumps", "resync_count", "sample_rate_changes"):
        if stats.get(field, 0) != 0:
            failures.append(f"{field}={stats[field]}")
    if not stats.get("wav_verified"):
        failures.append(f"capture WAV verification failed: {stats.get('wav_error')}")
    if samples <= 0:
        failures.append("no PCM samples")

    payload = capture_dir / "payload.pcm"
    if not payload.is_file():
        failures.append("payload.pcm is missing")
    elif payload.stat().st_size != stats.get("payload_bytes") or payload.stat().st_size != channels * sample_width * samples:
        failures.append("payload size disagrees with stats.json")

    wav_path = capture_dir / "audio.wav"
    if not wav_path.is_file():
        failures.append("audio.wav is missing")
    else:
        try:
            with wave.open(str(wav_path), "rb") as wav:
                if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) != (
                    channels, sample_width, expected_rate, samples
                ):
                    failures.append("WAV format or frame count disagrees with stats.json")
                if payload.is_file():
                    with payload.open('rb') as pcm:
                        mismatch=False
                        while True:
                            chunk=wav.readframes(8192)
                            if not chunk:break
                            if pcm.read(len(chunk))!=chunk:
                                failures.append('WAV samples differ from payload.pcm');mismatch=True;break
                        if not mismatch and pcm.read(1):failures.append('payload.pcm has bytes beyond WAV samples')
        except (OSError, EOFError, wave.Error) as exc:
            failures.append(f"cannot read audio.wav: {exc}")

    if args.require_audio and stats.get("audio", {}).get("rms", 0) <= 0:
        failures.append("capture is silent")

    print(
        f"frames={stats.get('valid_frames', 0)} samples={samples} "
        f"rate={stats.get('sample_rate')} seq_jumps={stats.get('seq_jumps', 0)} "
        f"rms={stats.get('audio', {}).get('rms', 0):.2f}"
    )
    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print("PASS: capture structure and continuity")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
