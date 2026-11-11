"""Lightweight PCM sanity checks using only the Python standard library."""

from __future__ import annotations

from dataclasses import dataclass, asdict
import math
import struct


@dataclass
class AudioStats:
    sample_count: int = 0
    min_sample: int | None = None
    max_sample: int | None = None
    mean: float = 0.0
    rms: float = 0.0
    dc_offset: float = 0.0
    clipping_count: int = 0
    estimated_frequency_hz: float | None = None

    def to_dict(self) -> dict:
        return asdict(self)


def analyze_s16le_mono(pcm: bytes, sample_rate: int | None) -> AudioStats:
    count = len(pcm) // 2
    if count == 0:
        return AudioStats()

    samples = struct.unpack("<" + "h" * count, pcm[: count * 2])
    total = 0
    total_sq = 0
    clipping = 0
    crossings = 0
    previous = samples[0]

    for sample in samples:
        total += sample
        total_sq += sample * sample
        if sample in (-32768, 32767):
            clipping += 1
        if previous < 0 <= sample or previous > 0 >= sample:
            crossings += 1
        previous = sample

    mean = total / count
    rms = math.sqrt(total_sq / count)
    frequency = None
    if sample_rate and count > 1 and crossings >= 2:
        duration = count / sample_rate
        frequency = (crossings / 2.0) / duration

    return AudioStats(
        sample_count=count,
        min_sample=min(samples),
        max_sample=max(samples),
        mean=mean,
        rms=rms,
        dc_offset=mean,
        clipping_count=clipping,
        estimated_frequency_hz=frequency,
    )
