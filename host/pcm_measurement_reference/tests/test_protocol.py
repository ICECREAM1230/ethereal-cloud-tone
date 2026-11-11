import struct
import json
import io
from pathlib import Path
import tempfile
import unittest
from contextlib import redirect_stdout

from host.pcm_measurement_reference.pcm_capture.protocol import (
    CRC16_VERSION,
    FLAG_CRC16_CCITT,
    LEGACY_VERSION,
    FrameParser,
    crc16_ccitt,
    pack_frame,
)
from host.pcm_measurement_reference.pcm_capture.capture import run_capture
from host.pcm_measurement_reference.verify_capture import main as verify_capture_main


class BytesReader:
    source_name = "test_bytes"

    def __init__(self, payload: bytes):
        self.payload = payload

    def read(self, max_bytes: int) -> bytes:
        chunk, self.payload = self.payload[:max_bytes], self.payload[max_bytes:]
        return chunk

    def close(self) -> None:
        pass


class FrameProtocolTests(unittest.TestCase):
    def test_crc16_ccitt_matches_standard_check_value(self):
        self.assertEqual(crc16_ccitt(b"123456789"), 0x29B1)

    def test_v2_crc_frame_is_verified(self):
        wire = pack_frame(b"\x34\x12\x78\x56", seq=7, sample_rate=48000)

        parser = FrameParser()
        frames = parser.feed(wire)

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].version, CRC16_VERSION)
        self.assertTrue(frames[0].flags & FLAG_CRC16_CCITT)
        self.assertTrue(frames[0].crc_verified)
        self.assertEqual(parser.stats.frames_crc_valid, 1)
        self.assertEqual(parser.stats.frames_unverified, 0)

    def test_crc_error_is_rejected_and_not_returned_as_pcm(self):
        wire = bytearray(pack_frame(b"\x01\x00\x02\x00", seq=8, sample_rate=48000))
        wire[-1] ^= 0x80

        parser = FrameParser()
        frames = parser.feed(bytes(wire))

        self.assertEqual(frames, [])
        self.assertEqual(parser.stats.crc_errors, 1)
        self.assertEqual(parser.stats.frames_rejected, 1)
        self.assertEqual(parser.stats.frames_valid, 0)

    def test_v1_zero_crc_remains_compatible_but_is_unverified(self):
        wire = pack_frame(
            b"\x01\x00",
            seq=9,
            sample_rate=48000,
            version=LEGACY_VERSION,
        )

        parser = FrameParser()
        frames = parser.feed(wire)

        self.assertEqual(len(frames), 1)
        self.assertFalse(frames[0].crc_verified)
        self.assertEqual(parser.stats.frames_unverified, 1)
        self.assertEqual(parser.stats.frames_crc_valid, 0)

    def test_v2_without_crc_flag_is_rejected(self):
        payload = b"\x01\x00"
        wire = struct.pack("<HBBHIHH", 0xA55A, CRC16_VERSION, 0, 1, 48000, 2, 0) + payload

        parser = FrameParser()

        self.assertEqual(parser.feed(wire), [])
        self.assertEqual(parser.stats.frames_rejected, 1)

    def test_sequence_gap_reports_missing_frame_count(self):
        parser = FrameParser()
        parser.feed(pack_frame(b"\x00\x00", seq=10, sample_rate=48000))
        parser.feed(pack_frame(b"\x00\x00", seq=13, sample_rate=48000))

        self.assertEqual(parser.stats.seq_jumps, 1)
        self.assertEqual(parser.stats.seq_frames_missing, 2)

    def test_crc_bad_header_sequence_does_not_poison_next_gap(self):
        parser = FrameParser()
        parser.feed(pack_frame(b"\x00\x00", seq=10, sample_rate=48000))
        corrupt = bytearray(pack_frame(b"\x00\x00", seq=11, sample_rate=48000))
        corrupt[4:6] = struct.pack("<H", 5000)
        parser.feed(bytes(corrupt))
        parser.feed(pack_frame(b"\x00\x00", seq=12, sample_rate=48000))

        self.assertEqual(parser.stats.crc_errors, 1)
        self.assertEqual(parser.stats.seq_jumps, 1)
        self.assertEqual(parser.stats.seq_frames_missing, 1)

    def test_legacy_frame_does_not_change_trusted_sequence_state(self):
        parser = FrameParser()
        parser.feed(pack_frame(b"\x00\x00", seq=10, sample_rate=48000))
        parser.feed(pack_frame(b"\x00\x00", seq=5000, sample_rate=48000, version=LEGACY_VERSION))
        parser.feed(pack_frame(b"\x00\x00", seq=11, sample_rate=48000))

        self.assertEqual(parser.stats.seq_jumps, 0)
        self.assertEqual(parser.stats.frames_unverified, 1)

    def test_partial_magic_and_byte_fragments_recover_one_frame(self):
        wire = pack_frame(b"\x34\x12", seq=4, sample_rate=48000)
        parser = FrameParser()
        frames = parser.feed(b"noise" + wire[:1])
        for byte in wire[1:]:
            frames.extend(parser.feed(bytes([byte])))

        self.assertEqual([frame.seq for frame in frames], [4])
        self.assertGreater(parser.stats.bytes_discarded, 0)

    def test_sequence_wrap_is_contiguous(self):
        parser = FrameParser()
        parser.feed(pack_frame(b"\x00\x00", seq=0xFFFF, sample_rate=48000))
        parser.feed(pack_frame(b"\x00\x00", seq=0, sample_rate=48000))

        self.assertEqual(parser.stats.seq_jumps, 0)
        self.assertEqual(parser.stats.seq_frames_missing, 0)

    def test_repeat_or_backward_sequence_is_not_counted_as_missing(self):
        parser = FrameParser()
        parser.feed(pack_frame(b"\x00\x00", seq=20, sample_rate=48000))
        parser.feed(pack_frame(b"\x00\x00", seq=20, sample_rate=48000))

        self.assertEqual(parser.stats.seq_jumps, 1)
        self.assertEqual(parser.stats.seq_frames_missing, 0)
        self.assertEqual(parser.stats.seq_repeats_or_reorders, 1)

    def test_capture_writes_only_verified_or_explicit_legacy_payloads(self):
        legacy = pack_frame(b"\x01\x00", seq=1, sample_rate=8000, version=LEGACY_VERSION)
        corrupt = bytearray(pack_frame(b"\x02\x00", seq=2, sample_rate=8000))
        corrupt[-1] ^= 1
        verified = pack_frame(b"\x03\x00", seq=3, sample_rate=8000)

        with tempfile.TemporaryDirectory() as tmp:
            capture_dir = run_capture(
                reader=BytesReader(legacy + bytes(corrupt) + verified),
                duration=0.005,
                output_root=Path(tmp),
                label=None,
                debug_errors=False,
                chunk_size=4096,
            )
            payload = (capture_dir / "payload.pcm").read_bytes()
            stats = json.loads((capture_dir / "stats.json").read_text())

        self.assertEqual(payload, b"\x01\x00\x03\x00")
        self.assertEqual(stats["frames_crc_valid"], 1)
        self.assertEqual(stats["frames_unverified"], 1)
        self.assertEqual(stats["crc_errors"], 1)
        self.assertEqual(stats["frames_rejected"], 1)
        self.assertEqual(stats["dropped_frames"], 1)

    def test_capture_rejects_format_change_and_fails_stream_integrity(self):
        mono_a = pack_frame(b"\x01\x00", seq=1, sample_rate=8000)
        stereo = pack_frame(b"\x02\x00\x03\x00", seq=2, sample_rate=8000, flags=1)
        mono_b = pack_frame(b"\x04\x00", seq=3, sample_rate=8000)

        payload, stats = self._capture(mono_a + stereo + mono_b)

        self.assertEqual(payload, b"\x01\x00\x04\x00")
        self.assertEqual(stats["protocol_valid_frames"], 3)
        self.assertEqual(stats["frames_written"], 2)
        self.assertEqual(stats["session_format_rejected"], 1)
        self.assertTrue(stats["wav_container_verified"])
        self.assertFalse(stats["stream_integrity_verified"])
        self.assertFalse(stats["wav_verified"])

    def test_capture_rejects_sample_rate_change_instead_of_mixing_wav(self):
        first = pack_frame(b"\x01\x00", seq=1, sample_rate=8000)
        wrong_rate = pack_frame(b"\x02\x00", seq=2, sample_rate=16000)
        last = pack_frame(b"\x03\x00", seq=3, sample_rate=8000)

        payload, stats = self._capture(first + wrong_rate + last)

        self.assertEqual(payload, b"\x01\x00\x03\x00")
        self.assertEqual(stats["sample_rate"], 8000)
        self.assertEqual(stats["frames_written"], 2)
        self.assertEqual(stats["session_sample_rate_rejected"], 1)
        self.assertTrue(stats["wav_container_verified"])
        self.assertFalse(stats["stream_integrity_verified"])
        self.assertFalse(stats["wav_verified"])

    def test_dropped_frames_is_lower_bound_without_crc_gap_double_count(self):
        good_a = pack_frame(b"\x01\x00", seq=1, sample_rate=8000)
        corrupt = bytearray(pack_frame(b"\x02\x00", seq=2, sample_rate=8000))
        corrupt[-1] ^= 1
        good_b = pack_frame(b"\x03\x00", seq=3, sample_rate=8000)

        _, stats = self._capture(good_a + bytes(corrupt) + good_b)

        self.assertEqual(stats["protocol_frames_rejected"], 1)
        self.assertEqual(stats["seq_frames_missing"], 1)
        self.assertEqual(stats["dropped_frames"], 1)
        self.assertEqual(stats["dropped_frames_semantics"], "conservative_lower_bound")

    def test_verify_report_distinguishes_container_from_stream_integrity(self):
        first = pack_frame(b"\x01\x00", seq=1, sample_rate=8000)
        wrong_rate = pack_frame(b"\x02\x00", seq=2, sample_rate=16000)
        with tempfile.TemporaryDirectory() as tmp:
            capture_dir = run_capture(
                reader=BytesReader(first + wrong_rate),
                duration=0.005,
                output_root=Path(tmp),
                label=None,
                debug_errors=False,
            )
            output = io.StringIO()
            with redirect_stdout(output):
                result = verify_capture_main([str(capture_dir), "--expect-rate", "8000"])

        self.assertEqual(result, 1)
        self.assertIn("wav_container=True", output.getvalue())
        self.assertIn("stream_integrity=False", output.getvalue())
        self.assertIn("source stream integrity failed", output.getvalue())

    def _capture(self, wire: bytes):
        with tempfile.TemporaryDirectory() as tmp:
            capture_dir = run_capture(
                reader=BytesReader(wire),
                duration=0.005,
                output_root=Path(tmp),
                label=None,
                debug_errors=False,
                chunk_size=4096,
            )
            payload = (capture_dir / "payload.pcm").read_bytes()
            stats = json.loads((capture_dir / "stats.json").read_text())
        return payload, stats


if __name__ == "__main__":
    unittest.main()
