"""Capture loop, output file writing, and run statistics."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime
import json
from pathlib import Path
import re
import time
import wave

from .analysis import analyze_s16le_mono
from .protocol import FLAG_S32, FLAG_STEREO, FrameParser
from .readers import Reader


@dataclass
class CaptureStats:
    source: str
    started_at: str
    ended_at: str | None = None
    duration_requested_s: float = 0.0
    duration_actual_s: float = 0.0
    raw_bytes: int = 0
    payload_bytes: int = 0
    valid_frames: int = 0
    protocol_valid_frames: int = 0
    frames_written: int = 0
    invalid_frames: int = 0
    resync_count: int = 0
    bytes_discarded: int = 0
    seq_jumps: int = 0
    seq_frames_missing: int = 0
    seq_repeats_or_reorders: int = 0
    sample_rate_changes: int = 0
    frames_crc_valid: int = 0
    crc_errors: int = 0
    frames_unverified: int = 0
    protocol_frames_rejected: int = 0
    session_format_rejected: int = 0
    session_sample_rate_rejected: int = 0
    frames_rejected: int = 0
    dropped_frames: int = 0
    dropped_frames_semantics: str = "conservative_lower_bound"
    sample_rate: int | None = None
    flags_seen: list[int] = field(default_factory=list)
    wav_container_verified: bool = False
    stream_integrity_verified: bool = False
    stream_integrity_error: str | None = None
    wav_verified: bool = False
    wav_error: str | None = None
    audio: dict = field(default_factory=dict)


def run_capture(
    *,
    reader: Reader,
    duration: float,
    output_root: Path,
    label: str | None,
    debug_errors: bool,
    chunk_size: int = 4096,
) -> Path:
    capture_dir = make_capture_dir(output_root, reader.source_name, label)
    raw_path = capture_dir / "raw.bin"
    payload_path = capture_dir / "payload.pcm"
    wav_path = capture_dir / "audio.wav"
    stats_path = capture_dir / "stats.json"
    errors_path = capture_dir / "errors.bin"

    parser = FrameParser()
    stats = CaptureStats(
        source=reader.source_name,
        started_at=datetime.now().isoformat(timespec="seconds"),
        duration_requested_s=duration,
    )
    start = time.monotonic()
    next_progress = start + 1.0
    payload = bytearray()
    flags_seen: set[int] = set()
    frames_written = 0
    session_format_rejected = 0
    session_sample_rate_rejected = 0

    try:
        with raw_path.open("wb") as raw_file, payload_path.open("wb") as payload_file:
            errors_file = errors_path.open("wb") if debug_errors else None
            try:
                while duration <= 0 or time.monotonic() - start < duration:
                    chunk = reader.read(chunk_size)
                    if not chunk:
                        continue

                    raw_file.write(chunk)
                    stats.raw_bytes += len(chunk)
                    before_errors = parser.stats.frames_invalid + parser.stats.resync_count
                    frames = parser.feed(chunk)

                    for frame in frames:
                        flags_seen.add(frame.flags)
                        if frame.flags & (FLAG_STEREO | FLAG_S32):
                            session_format_rejected += 1
                            continue

                        if stats.sample_rate is None:
                            stats.sample_rate = frame.sample_rate
                        elif frame.sample_rate != stats.sample_rate:
                            session_sample_rate_rejected += 1
                            continue

                        payload_file.write(frame.payload)
                        payload.extend(frame.payload)
                        stats.payload_bytes += len(frame.payload)
                        frames_written += 1

                    after_errors = parser.stats.frames_invalid + parser.stats.resync_count
                    if errors_file and after_errors > before_errors:
                        errors_file.write(chunk)

                    now = time.monotonic()
                    if now >= next_progress:
                        _print_progress(stats.raw_bytes, stats.payload_bytes, parser, start)
                        next_progress = now + 1.0
            finally:
                if errors_file:
                    errors_file.close()
    finally:
        reader.close()

    elapsed = time.monotonic() - start
    stats.ended_at = datetime.now().isoformat(timespec="seconds")
    stats.duration_actual_s = elapsed
    stats.valid_frames = frames_written
    stats.protocol_valid_frames = parser.stats.frames_valid
    stats.frames_written = frames_written
    stats.invalid_frames = (
        parser.stats.frames_invalid + session_format_rejected + session_sample_rate_rejected
    )
    stats.resync_count = parser.stats.resync_count
    stats.bytes_discarded = parser.stats.bytes_discarded
    stats.seq_jumps = parser.stats.seq_jumps
    stats.seq_frames_missing = parser.stats.seq_frames_missing
    stats.seq_repeats_or_reorders = parser.stats.seq_repeats_or_reorders
    stats.sample_rate_changes = parser.stats.sample_rate_changes
    stats.frames_crc_valid = parser.stats.frames_crc_valid
    stats.crc_errors = parser.stats.crc_errors
    stats.frames_unverified = parser.stats.frames_unverified
    stats.protocol_frames_rejected = parser.stats.frames_rejected
    stats.session_format_rejected = session_format_rejected
    stats.session_sample_rate_rejected = session_sample_rate_rejected
    session_rejected = session_format_rejected + session_sample_rate_rejected
    stats.frames_rejected = parser.stats.frames_rejected + session_rejected
    stats.dropped_frames = (
        max(parser.stats.frames_rejected, parser.stats.seq_frames_missing) + session_rejected
    )
    stats.flags_seen = sorted(flags_seen)
    stats.stream_integrity_verified = not any(
        (
            parser.stats.frames_invalid,
            parser.stats.resync_count,
            parser.stats.seq_jumps,
            parser.stats.sample_rate_changes,
            session_rejected,
        )
    )
    if not stats.stream_integrity_verified:
        reasons = []
        if parser.stats.frames_invalid:
            reasons.append(f"protocol_invalid={parser.stats.frames_invalid}")
        if parser.stats.resync_count:
            reasons.append(f"resync={parser.stats.resync_count}")
        if parser.stats.seq_jumps:
            reasons.append(f"seq_jumps={parser.stats.seq_jumps}")
        if parser.stats.sample_rate_changes:
            reasons.append(f"sample_rate_changes={parser.stats.sample_rate_changes}")
        if session_format_rejected:
            reasons.append(f"format_rejected={session_format_rejected}")
        if session_sample_rate_rejected:
            reasons.append(f"sample_rate_rejected={session_sample_rate_rejected}")
        stats.stream_integrity_error = ", ".join(reasons)

    if stats.sample_rate:
        write_wav_s16le_mono(wav_path, bytes(payload), stats.sample_rate)
        stats.wav_container_verified, stats.wav_error = verify_wav(
            wav_path,
            expected_sample_rate=stats.sample_rate,
            expected_samples=len(payload) // 2,
        )
        stats.wav_verified = stats.wav_container_verified and stats.stream_integrity_verified
        stats.audio = analyze_s16le_mono(bytes(payload), stats.sample_rate).to_dict()
    else:
        stats.wav_error = "No valid frame with a sample_rate was captured"

    stats_path.write_text(
        json.dumps(asdict(stats), indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    _print_final(capture_dir, stats)
    return capture_dir


def make_capture_dir(output_root: Path, source: str, label: str | None) -> Path:
    timestamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    pieces = [timestamp, safe_name(source)]
    if label:
        pieces.append(safe_name(label))
    capture_dir = output_root / "_".join(piece for piece in pieces if piece)
    capture_dir.mkdir(parents=True, exist_ok=False)
    return capture_dir


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")


def write_wav_s16le_mono(path: Path, pcm: bytes, sample_rate: int) -> None:
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm)


def verify_wav(
    path: Path,
    *,
    expected_sample_rate: int,
    expected_samples: int,
) -> tuple[bool, str | None]:
    try:
        with wave.open(str(path), "rb") as wav:
            if wav.getnchannels() != 1:
                return False, "WAV channel count is not mono"
            if wav.getsampwidth() != 2:
                return False, "WAV sample width is not 16-bit"
            if wav.getframerate() != expected_sample_rate:
                return False, "WAV sample rate does not match captured frames"
            if wav.getnframes() != expected_samples:
                return False, "WAV sample count does not match parsed payload"
    except wave.Error as exc:
        return False, str(exc)
    return True, None


def _print_progress(
    raw_bytes: int,
    payload_bytes: int,
    parser: FrameParser,
    start: float,
) -> None:
    elapsed = max(time.monotonic() - start, 1e-6)
    print(
        "progress "
        f"raw={raw_bytes}B payload={payload_bytes}B "
        f"frames={parser.stats.frames_valid} "
        f"errors={parser.stats.frames_invalid} "
        f"resync={parser.stats.resync_count} "
        f"rate={raw_bytes / elapsed:.0f}B/s"
    )


def _print_final(capture_dir: Path, stats: CaptureStats) -> None:
    freq = stats.audio.get("estimated_frequency_hz")
    freq_text = f"{freq:.2f} Hz" if isinstance(freq, float) else "n/a"
    print(f"capture_dir={capture_dir}")
    print(
        "summary "
        f"written={stats.frames_written}/{stats.protocol_valid_frames} crc_ok={stats.frames_crc_valid} "
        f"unverified={stats.frames_unverified} crc_errors={stats.crc_errors} "
        f"dropped={stats.dropped_frames} seq_jumps={stats.seq_jumps} "
        f"resync={stats.resync_count} samples={stats.audio.get('sample_count', 0)} "
        f"freq={freq_text} wav_container={stats.wav_container_verified} "
        f"stream_integrity={stats.stream_integrity_verified}"
    )
