"""Lightweight PCM sanity checks using only the Python standard library."""

from __future__ import annotations

from dataclasses import dataclass, asdict
import math
import struct
from array import array
from collections import deque
import sys


class StreamingAudioStats:
    """Same mono16 statistics without retaining an unbounded PCM recording."""
    def __init__(self, preview_samples: int = 4096, sample_bits: int = 16) -> None:
        self.sample_bits = sample_bits
        self.count = self.total = self.total_sq = self.clipping = self.crossings = 0
        self.minimum = self.maximum = self.previous = None
        self.preview = deque(maxlen=preview_samples)

    def feed(self, pcm: bytes) -> None:
        samples = array('h' if self.sample_bits == 16 else 'i')
        samples.frombytes(pcm)
        if sys.byteorder != 'little':
            samples.byteswap()
        self.feed_values(samples)

    def feed_values(self, samples) -> None:
        for sample in samples:
            self.count += 1
            self.total += sample
            self.total_sq += sample * sample
            self.minimum = sample if self.minimum is None else min(self.minimum, sample)
            self.maximum = sample if self.maximum is None else max(self.maximum, sample)
            self.clipping += sample in (-(1 << (self.sample_bits-1)), (1 << (self.sample_bits-1))-1)
            if self.previous is not None:
                self.crossings += (self.previous < 0 <= sample or self.previous > 0 >= sample)
            self.previous = sample
        self.preview.extend(samples)

    def to_dict(self, sample_rate: int | None) -> dict:
        if not self.count:
            return AudioStats().to_dict()
        frequency = (self.crossings * sample_rate / (2 * self.count)
                     if sample_rate and self.crossings >= 2 else None)
        return AudioStats(self.count, self.minimum, self.maximum, self.total/self.count,
                          math.sqrt(self.total_sq/self.count), self.total/self.count,
                          self.clipping, frequency).to_dict()


class StreamingPCMStats:
    """Bounded per-channel statistics, signed16/32 little-endian interleaved."""
    def __init__(self, channels=1, sample_width=2):
        self.channels, self.sample_width = channels, sample_width
        self.channel = [StreamingAudioStats(sample_bits=sample_width*8) for _ in range(channels)]

    @property
    def count(self):
        return self.channel[0].count

    @property
    def preview(self):
        return self.channel[0].preview

    @property
    def preview_right(self):
        return self.channel[1].preview if self.channels==2 else ()

    def feed(self, pcm):
        values=array('h' if self.sample_width==2 else 'i');values.frombytes(pcm)
        if sys.byteorder!='little':values.byteswap()
        if self.channels==1:self.channel[0].feed_values(values)
        else:
            self.channel[0].feed_values(values[0::2]);self.channel[1].feed_values(values[1::2])

    def to_dict(self, sample_rate):
        channel_stats=[c.to_dict(sample_rate) for c in self.channel]
        result=dict(channel_stats[0])
        if self.channels==2:
            result.update(rms=math.sqrt(sum(c['rms']**2 for c in channel_stats)/2),
                mean=sum(c['mean'] for c in channel_stats)/2,
                dc_offset=sum(c['dc_offset'] for c in channel_stats)/2,
                min_sample=min((c['min_sample'] for c in channel_stats if c['min_sample'] is not None),default=None),
                max_sample=max((c['max_sample'] for c in channel_stats if c['max_sample'] is not None),default=None),
                clipping_count=sum(c['clipping_count'] for c in channel_stats),estimated_frequency_hz=None)
        result['channel_stats']=channel_stats
        return result


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
