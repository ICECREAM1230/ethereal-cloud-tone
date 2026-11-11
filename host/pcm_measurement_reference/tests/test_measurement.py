import math
from pathlib import Path
import struct
import tempfile
import unittest
import wave

from host.pcm_measurement_reference.pcm_capture.measurement import (
    AudioData,
    analyze_audio,
    compare_audio,
    load_audio,
)
from host.pcm_measurement_reference.measure_audio import main as measure_main


def sine(rate: int, frequency: float, amplitude: int, seconds: float = 0.1) -> tuple[int, ...]:
    return tuple(
        round(amplitude * math.sin(2.0 * math.pi * frequency * n / rate))
        for n in range(round(rate * seconds))
    )


class MeasurementTests(unittest.TestCase):
    def test_metrics_cover_level_dc_clipping_and_spectrum(self):
        rate = 8000
        samples = list(sine(rate, 1000.0, 12000))
        samples[0] = 32767
        samples[1] = -32768
        data = AudioData(rate, 1, (tuple(samples),))

        metrics = analyze_audio(data)

        self.assertEqual(metrics["sample_rate"], rate)
        self.assertEqual(metrics["channels"], 1)
        self.assertEqual(metrics["clipping_count"], 2)
        self.assertGreater(metrics["peak"], 0.99)
        self.assertGreater(metrics["rms"], 0.20)
        self.assertGreater(metrics["crest_factor_db"], 1.0)
        self.assertLess(abs(metrics["dc_offset"]), 0.01)
        self.assertAlmostEqual(metrics["spectral_centroid_hz"], 1000.0, delta=180.0)
        self.assertGreater(metrics["bands"]["mid"]["rms"], metrics["bands"]["low"]["rms"] * 4)

    def test_silence_gate_marks_spectral_results_untrusted(self):
        metrics = analyze_audio(AudioData(48000, 1, ((0,) * 4096,)), silence_dbfs=-70.0)

        self.assertTrue(metrics["silent"])
        self.assertIsNone(metrics["spectral_centroid_hz"])
        self.assertFalse(metrics["spectral_trusted"])

    def test_comparison_requires_equal_rate_and_channels(self):
        left = AudioData(48000, 1, ((0,) * 100,))
        wrong_rate = AudioData(44100, 1, ((0,) * 100,))
        wrong_channels = AudioData(48000, 2, ((0,) * 100, (0,) * 100))

        with self.assertRaisesRegex(ValueError, "sample rate"):
            compare_audio(left, wrong_rate)
        with self.assertRaisesRegex(ValueError, "channel count"):
            compare_audio(left, wrong_channels)

    def test_transition_comparison_reports_added_high_band_energy(self):
        rate = 8000
        reference = AudioData(rate, 1, (sine(rate, 200.0, 8000),))
        candidate_samples = list(reference.channels_data[0])
        midpoint = len(candidate_samples) // 2
        candidate_samples[midpoint] = 28000
        candidate = AudioData(rate, 1, (tuple(candidate_samples),))

        report = compare_audio(reference, candidate, transition_index=midpoint, window_ms=20.0)

        self.assertFalse(report["silent_input"])
        self.assertGreater(report["transition"]["candidate_high_rms"], report["transition"]["reference_high_rms"])
        self.assertGreater(report["transition"]["high_band_delta_db"], 0.0)

    def test_loads_wav_and_requires_raw_pcm_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            wav_path = root / "tone.wav"
            pcm_path = root / "tone.pcm"
            payload = struct.pack("<hhhh", 1, -2, 3, -4)
            pcm_path.write_bytes(payload)
            with wave.open(str(wav_path), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(8000)
                wav.writeframes(payload)

            wav_data = load_audio(wav_path)
            pcm_data = load_audio(pcm_path, sample_rate=8000, channels=1)

        self.assertEqual(wav_data, pcm_data)
        with self.assertRaisesRegex(ValueError, "sample rate"):
            load_audio(pcm_path)

    def test_cli_saves_json_for_measurement_and_transition_comparison(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            reference = root / "reference.wav"
            candidate = root / "candidate.wav"
            output = root / "report.json"
            for path, amplitude in ((reference, 4000), (candidate, 8000)):
                payload = struct.pack("<" + "h" * 800, *sine(8000, 1000, amplitude))
                with wave.open(str(path), "wb") as wav:
                    wav.setnchannels(1)
                    wav.setsampwidth(2)
                    wav.setframerate(8000)
                    wav.writeframes(payload)

            exit_code = measure_main(
                [str(candidate), "--reference", str(reference), "--transition-index", "400", "--json", str(output)]
            )
            report = __import__("json").loads(output.read_text())

        self.assertEqual(exit_code, 0)
        self.assertTrue(report["comparison"]["comparable"])
        self.assertEqual(report["comparison"]["transition"]["index"], 400)


if __name__ == "__main__":
    unittest.main()
