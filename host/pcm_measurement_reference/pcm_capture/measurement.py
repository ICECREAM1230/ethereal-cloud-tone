"""Comparable PCM/WAV level, spectrum, and parameter-transition measurements."""

from __future__ import annotations

from dataclasses import dataclass
import cmath
import math
from pathlib import Path
import struct
import wave


FULL_SCALE = 32768.0
MAX_SPECTRUM_SAMPLES = 4096


@dataclass(frozen=True)
class AudioData:
    sample_rate: int
    channel_count: int
    channels_data: tuple[tuple[int, ...], ...]


def load_audio(
    path: Path,
    *,
    sample_rate: int | None = None,
    channels: int | None = None,
) -> AudioData:
    if path.suffix.lower() == ".wav":
        with wave.open(str(path), "rb") as wav:
            if wav.getsampwidth() != 2:
                raise ValueError("only signed 16-bit WAV is supported")
            rate = wav.getframerate()
            count = wav.getnchannels()
            payload = wav.readframes(wav.getnframes())
        if sample_rate is not None and sample_rate != rate:
            raise ValueError("WAV sample rate does not match --sample-rate")
        if channels is not None and channels != count:
            raise ValueError("WAV channel count does not match --channels")
    else:
        if sample_rate is None:
            raise ValueError("raw PCM requires a sample rate")
        if channels is None:
            raise ValueError("raw PCM requires a channel count")
        rate, count = sample_rate, channels
        payload = path.read_bytes()

    if count <= 0 or len(payload) % (2 * count):
        raise ValueError("audio payload is not complete interleaved s16le frames")
    values = struct.unpack("<" + "h" * (len(payload) // 2), payload)
    separated = tuple(tuple(values[offset::count]) for offset in range(count))
    return AudioData(rate, count, separated)


def analyze_audio(data: AudioData, *, silence_dbfs: float = -70.0) -> dict:
    flat = tuple(sample for channel in data.channels_data for sample in channel)
    if not flat:
        return _empty_metrics(data, silence_dbfs)

    peak = max(abs(sample) for sample in flat) / FULL_SCALE
    mean = sum(flat) / len(flat) / FULL_SCALE
    rms = math.sqrt(sum(sample * sample for sample in flat) / len(flat)) / FULL_SCALE
    rms_dbfs = _db(rms)
    silent = rms_dbfs < silence_dbfs
    spectrum = _average_spectrum(data)
    band_metrics = _band_metrics(spectrum)
    power_sum = sum(power for _, power in spectrum)
    centroid = None
    if not silent and power_sum > 0:
        centroid = sum(freq * power for freq, power in spectrum) / power_sum

    return {
        "sample_rate": data.sample_rate,
        "channels": data.channel_count,
        "samples_per_channel": len(data.channels_data[0]),
        "peak": peak,
        "peak_dbfs": _db(peak),
        "rms": rms,
        "rms_dbfs": rms_dbfs,
        "crest_factor_db": _db(peak / rms) if rms else None,
        "clipping_count": sum(sample in (-32768, 32767) for sample in flat),
        "dc_offset": mean,
        "dc_offset_dbfs": _db(abs(mean)),
        "silent": silent,
        "silence_threshold_dbfs": silence_dbfs,
        "spectral_trusted": not silent and power_sum > 0,
        "spectral_centroid_hz": centroid,
        "bands": band_metrics,
    }


def compare_audio(
    reference: AudioData,
    candidate: AudioData,
    *,
    transition_index: int | None = None,
    window_ms: float = 20.0,
    silence_dbfs: float = -70.0,
) -> dict:
    if reference.sample_rate != candidate.sample_rate:
        raise ValueError("sample rate mismatch")
    if reference.channel_count != candidate.channel_count:
        raise ValueError("channel count mismatch")
    ref_metrics = analyze_audio(reference, silence_dbfs=silence_dbfs)
    cand_metrics = analyze_audio(candidate, silence_dbfs=silence_dbfs)
    report = {
        "comparable": True,
        "silent_input": ref_metrics["silent"] or cand_metrics["silent"],
        "reference": ref_metrics,
        "candidate": cand_metrics,
        "delta": {
            "peak_db": _difference(cand_metrics["peak_dbfs"], ref_metrics["peak_dbfs"]),
            "rms_db": _difference(cand_metrics["rms_dbfs"], ref_metrics["rms_dbfs"]),
            "crest_db": _difference(cand_metrics["crest_factor_db"], ref_metrics["crest_factor_db"]),
            "dc_offset": cand_metrics["dc_offset"] - ref_metrics["dc_offset"],
            "clipping_count": cand_metrics["clipping_count"] - ref_metrics["clipping_count"],
            "centroid_hz": _difference(
                cand_metrics["spectral_centroid_hz"], ref_metrics["spectral_centroid_hz"]
            ),
        },
    }
    if transition_index is not None:
        radius = max(2, round(reference.sample_rate * window_ms / 1000.0 / 2.0))
        ref_high = _transition_high_rms(reference, transition_index, radius)
        cand_high = _transition_high_rms(candidate, transition_index, radius)
        report["transition"] = {
            "index": transition_index,
            "window_ms": window_ms,
            "reference_high_rms": ref_high,
            "candidate_high_rms": cand_high,
            "high_band_delta_db": _db(cand_high / ref_high) if ref_high else None,
        }
    return report


def _average_spectrum(data: AudioData) -> list[tuple[float, float]]:
    sample_count = min(len(data.channels_data[0]), MAX_SPECTRUM_SAMPLES)
    if sample_count < 2:
        return []
    start = (len(data.channels_data[0]) - sample_count) // 2
    channels = [channel[start : start + sample_count] for channel in data.channels_data]
    window = [0.5 - 0.5 * math.cos(2 * math.pi * n / (sample_count - 1)) for n in range(sample_count)]
    spectrum = []
    for index in range(sample_count // 2 + 1):
        power = 0.0
        for samples in channels:
            value = sum(
                samples[n] / FULL_SCALE * window[n] * cmath.exp(-2j * math.pi * index * n / sample_count)
                for n in range(sample_count)
            )
            power += abs(value) ** 2
        frequency = index * data.sample_rate / sample_count
        spectrum.append((frequency, power / data.channel_count))
    return spectrum


def _band_metrics(spectrum: list[tuple[float, float]]) -> dict:
    limits = {"low": (20.0, 250.0), "mid": (250.0, 2000.0), "high": (2000.0, math.inf)}
    total_power = sum(power for _, power in spectrum)
    result = {}
    for name, (low, high) in limits.items():
        power = sum(value for freq, value in spectrum if low <= freq < high)
        rms = math.sqrt(power / total_power) if total_power else 0.0
        result[name] = {"rms": rms, "relative_db": _db(rms)}
    return result


def _transition_high_rms(data: AudioData, center: int, radius: int) -> float:
    values = []
    for channel in data.channels_data:
        start = max(1, center - radius)
        stop = min(len(channel), center + radius)
        values.extend((channel[index] - channel[index - 1]) / (2 * FULL_SCALE) for index in range(start, stop))
    return math.sqrt(sum(value * value for value in values) / len(values)) if values else 0.0


def _db(value: float) -> float:
    return 20.0 * math.log10(value) if value > 0 else float("-inf")


def _difference(left, right):
    return left - right if left is not None and right is not None else None


def _empty_metrics(data: AudioData, silence_dbfs: float) -> dict:
    return {
        "sample_rate": data.sample_rate,
        "channels": data.channel_count,
        "samples_per_channel": 0,
        "peak": 0.0,
        "peak_dbfs": float("-inf"),
        "rms": 0.0,
        "rms_dbfs": float("-inf"),
        "crest_factor_db": None,
        "clipping_count": 0,
        "dc_offset": 0.0,
        "dc_offset_dbfs": float("-inf"),
        "silent": True,
        "silence_threshold_dbfs": silence_dbfs,
        "spectral_trusted": False,
        "spectral_centroid_hz": None,
        "bands": {name: {"rms": 0.0, "relative_db": float("-inf")} for name in ("low", "mid", "high")},
    }
