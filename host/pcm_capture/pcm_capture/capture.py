"""Capture loop, output file writing, and run statistics."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime
import json
from pathlib import Path
import re
import time
import wave
from typing import Callable

from .analysis import StreamingPCMStats
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
    invalid_frames: int = 0
    resync_count: int = 0
    bytes_discarded: int = 0
    seq_jumps: int = 0
    sample_rate_changes: int = 0
    crc_nonzero: int = 0
    sample_rate: int | None = None
    channels: int = 1
    sample_width: int = 2
    flags_seen: list[int] = field(default_factory=list)
    wav_verified: bool = False
    wav_error: str | None = None
    audio: dict = field(default_factory=dict)
    padding_bytes: int = 0
    pending_bytes: int = 0
    received_sample_rate_hz: float = 0.0
    usb_info: dict = field(default_factory=dict)
    capture_error: str | None = None
    stopped_by_user: bool = False


class CaptureFailure(RuntimeError):
    def __init__(self, message: str, capture_dir: Path):
        super().__init__(f"{message}\nPartial capture retained: {capture_dir}")
        self.capture_dir = capture_dir


def run_capture(
    *, reader: Reader, duration: float, output_root: Path, label: str | None,
    debug_errors: bool, chunk_size: int = 4096,
    stop_requested: Callable[[], bool] | None = None,
    on_progress: Callable[[dict], None] | None = None,
) -> Path:
    if duration < 0 or chunk_size <= 0:
        reader.close()
        raise ValueError("duration must be >=0 and chunk_size positive")
    try:
        capture_dir = make_capture_dir(output_root, reader.source_name, label)
    except Exception:
        reader.close()
        raise
    parser, analyzer = FrameParser(), StreamingPCMStats()
    stats = CaptureStats(source=reader.source_name,
                         started_at=datetime.now().astimezone().isoformat(),
                         duration_requested_s=duration,
                         usb_info=dict(getattr(reader, 'link_info', {})))
    start = time.monotonic()
    next_progress, next_log = start, start+1.0
    wav_file = None
    error = None
    flags_seen: set[int] = set()

    def refresh() -> dict:
        stats.duration_actual_s = time.monotonic()-start
        stats.invalid_frames = parser.stats.frames_invalid
        stats.resync_count = parser.stats.resync_count
        stats.bytes_discarded = parser.stats.bytes_discarded
        stats.seq_jumps = parser.stats.seq_jumps
        stats.sample_rate_changes = parser.stats.sample_rate_changes
        stats.crc_nonzero = parser.stats.crc_nonzero
        stats.padding_bytes = parser.stats.padding_bytes
        stats.pending_bytes = parser.pending_bytes
        stats.flags_seen = sorted(flags_seen)
        stats.audio = analyzer.to_dict(stats.sample_rate)
        stats.received_sample_rate_hz = analyzer.count/max(stats.duration_actual_s, 1e-6)
        return {**asdict(stats), 'waveform': list(analyzer.preview), 'waveform_right': list(analyzer.preview_right),
                'sample_bits':stats.sample_width*8,
                'capture_dir': str(capture_dir),
                'raw_bytes_per_second': stats.raw_bytes/max(stats.duration_actual_s, 1e-6)}

    try:
        with (capture_dir/'raw.bin').open('wb') as raw_file, (capture_dir/'payload.pcm').open('wb') as payload_file:
            errors_file = (capture_dir/'errors.bin').open('wb') if debug_errors else None
            try:
                while not stop_requested or not stop_requested():
                    now = time.monotonic()
                    if duration > 0 and now-start >= duration:
                        break
                    chunk = reader.read(chunk_size)
                    if chunk:
                        raw_file.write(chunk)
                        stats.raw_bytes += len(chunk)
                        before_errors = parser.stats.frames_invalid+parser.stats.resync_count
                        for frame in parser.feed(chunk):
                            flags_seen.add(frame.flags)
                            if stats.sample_rate is None:
                                stats.sample_rate = frame.sample_rate
                                wav_file = wave.open(str(capture_dir/'audio.wav'), 'wb')
                                stats.channels,stats.sample_width=frame.channels,frame.sample_width
                                analyzer=StreamingPCMStats(frame.channels,frame.sample_width)
                                wav_file.setnchannels(stats.channels)
                                wav_file.setsampwidth(stats.sample_width)
                                wav_file.setframerate(frame.sample_rate)
                            if frame.sample_rate != stats.sample_rate:
                                raise RuntimeError('PCM sample rate changed; start a new capture file')
                            if (frame.channels,frame.sample_width)!=(stats.channels,stats.sample_width):
                                raise RuntimeError('PCM channel count or sample width changed; start a new capture file')
                            payload_file.write(frame.payload)
                            wav_file.writeframesraw(frame.payload)
                            analyzer.feed(frame.payload)
                            stats.payload_bytes += len(frame.payload)
                            stats.valid_frames += 1
                        if errors_file and parser.stats.frames_invalid+parser.stats.resync_count > before_errors:
                            errors_file.write(chunk)
                    now = time.monotonic()
                    if now >= next_progress:
                        if on_progress:
                            on_progress(refresh())
                        raw_file.flush()
                        payload_file.flush()
                        next_progress = now+0.1
                    if now >= next_log:
                        _print_progress(stats.raw_bytes, stats.payload_bytes, parser, start)
                        next_log = now+1.0
                stats.stopped_by_user = bool(stop_requested and stop_requested())
            finally:
                if errors_file:
                    errors_file.close()
    except (Exception, KeyboardInterrupt) as exc:
        error = exc
        stats.capture_error = str(exc) or 'capture interrupted'
    finally:
        try:
            if wav_file:
                wav_file.close()
        except Exception as exc:
            if error is None:
                error = exc
                stats.capture_error = str(exc)
        try:
            reader.close()
        except Exception as exc:
            if error is None:
                error = exc
                stats.capture_error = str(exc)

    refresh()
    stats.ended_at = datetime.now().astimezone().isoformat()
    if stats.sample_rate:
        stats.wav_verified, stats.wav_error = verify_wav(
            capture_dir/'audio.wav', expected_sample_rate=stats.sample_rate,
            expected_samples=analyzer.count,expected_channels=stats.channels,expected_sample_width=stats.sample_width)
    else:
        stats.wav_error = 'No valid PCM frames captured'
        if error is None:
            error = RuntimeError(stats.wav_error)
            stats.capture_error = stats.wav_error
    (capture_dir/'stats.json').write_text(json.dumps(asdict(stats), indent=2, ensure_ascii=False), encoding='utf-8')
    if on_progress:
        on_progress(refresh())
    _print_final(capture_dir, stats)
    if error:
        raise CaptureFailure(stats.capture_error, capture_dir) from error
    return capture_dir


def make_capture_dir(output_root: Path, source: str, label: str | None) -> Path:
    timestamp = datetime.now().strftime("%Y-%m-%d_%H%M%S_%f")
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
    expected_channels: int = 1,
    expected_sample_width: int = 2,
) -> tuple[bool, str | None]:
    try:
        with wave.open(str(path), "rb") as wav:
            if wav.getnchannels() != expected_channels:
                return False, "WAV channel count does not match captured frames"
            if wav.getsampwidth() != expected_sample_width:
                return False, "WAV sample width does not match captured frames"
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
        f"frames={stats.valid_frames} seq_jumps={stats.seq_jumps} "
        f"resync={stats.resync_count} samples={stats.audio.get('sample_count', 0)} "
        f"freq={freq_text} wav_verified={stats.wav_verified}"
    )
