"""Frame format and streaming parser for FPGA PCM monitor data."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import List

MAGIC = 0xA55A
MAGIC_BYTES = struct.pack("<H", MAGIC)
VERSION = 1
HEADER_FORMAT = "<HBBHIHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
FLAG_STEREO = 0x01
FLAG_S32 = 0x02


@dataclass
class Frame:
    version: int
    flags: int
    seq: int
    sample_rate: int
    payload_len: int
    crc16: int
    payload: bytes

    @property
    def channels(self) -> int:
        return 2 if self.flags & FLAG_STEREO else 1

    @property
    def sample_width(self) -> int:
        return 4 if self.flags & FLAG_S32 else 2


@dataclass
class ParserStats:
    bytes_discarded: int = 0
    frames_valid: int = 0
    frames_invalid: int = 0
    resync_count: int = 0
    seq_jumps: int = 0
    sample_rate_changes: int = 0
    crc_nonzero: int = 0
    padding_bytes: int = 0


class FrameParser:
    """Incrementally parses little-endian framed PCM byte streams."""

    def __init__(self, max_payload_len: int = 65535) -> None:
        self._buffer = bytearray()
        self._expected_seq: int | None = None
        self._sample_rate: int | None = None
        self._max_payload_len = max_payload_len
        self.stats = ParserStats()
        self._after_legacy_frame = False

    @property
    def pending_bytes(self) -> int:
        return len(self._buffer)

    def feed(self, chunk: bytes) -> List[Frame]:
        self._buffer.extend(chunk)
        frames: List[Frame] = []

        while True:
            # The verified legacy transport inserts414 zeros after seven
            # 526-byte PCM frames. USB reads may split this padding anywhere.
            # It is transport padding, not a damaged PCM frame. No bytes in a
            # declared payload are ever examined or removed here.
            if self._after_legacy_frame and self._buffer[:1] == b'\0':
                zeros = len(self._buffer)-len(self._buffer.lstrip(b'\0'))
                if zeros < 414 and zeros == len(self._buffer):
                    return frames
                if zeros >= 414:
                    del self._buffer[:414]
                    self.stats.padding_bytes += 414
                    self._after_legacy_frame = False
                    continue
            if len(self._buffer) < HEADER_SIZE:
                return frames

            if self._buffer[0:2] != MAGIC_BYTES:
                self._after_legacy_frame = False
                next_magic = self._buffer.find(MAGIC_BYTES, 1)
                if next_magic < 0:
                    keep = 1 if self._buffer[-1:] == MAGIC_BYTES[:1] else 0
                    discarded = len(self._buffer) - keep
                    if discarded:
                        self.stats.bytes_discarded += discarded
                        self.stats.resync_count += 1
                    del self._buffer[:discarded]
                    return frames

                self.stats.bytes_discarded += next_magic
                self.stats.resync_count += 1
                del self._buffer[:next_magic]
                if len(self._buffer) < HEADER_SIZE:
                    return frames

            header = bytes(self._buffer[:HEADER_SIZE])
            magic, version, flags, seq, sample_rate, payload_len, crc16 = struct.unpack(
                HEADER_FORMAT, header
            )
            if magic != MAGIC:
                self.stats.frames_invalid += 1
                del self._buffer[0]
                continue

            if (not payload_len or payload_len > self._max_payload_len or payload_len % 2 != 0
                    or version != VERSION or flags & ~(FLAG_STEREO | FLAG_S32) or not sample_rate
                    or payload_len % ((2 if flags & FLAG_STEREO else 1) * (4 if flags & FLAG_S32 else 2)) != 0):
                self.stats.frames_invalid += 1
                self.stats.bytes_discarded += 1
                del self._buffer[0]
                continue

            total_len = HEADER_SIZE + payload_len
            if len(self._buffer) < total_len:
                return frames

            payload = bytes(self._buffer[HEADER_SIZE:total_len])
            del self._buffer[:total_len]

            if version != VERSION:
                self.stats.frames_invalid += 1
                continue

            if crc16:
                self.stats.crc_nonzero += 1

            if self._expected_seq is not None and seq != self._expected_seq:
                self.stats.seq_jumps += 1
            self._expected_seq = (seq + 1) & 0xFFFF

            if self._sample_rate is None:
                self._sample_rate = sample_rate
            elif sample_rate != self._sample_rate:
                self.stats.sample_rate_changes += 1

            frame = Frame(version, flags, seq, sample_rate, payload_len, crc16, payload)
            self.stats.frames_valid += 1
            self._after_legacy_frame = payload_len == 512
            frames.append(frame)


def pack_frame(
    payload: bytes,
    *,
    seq: int,
    sample_rate: int,
    flags: int = 0,
    version: int = VERSION,
    crc16: int = 0,
) -> bytes:
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        version,
        flags,
        seq & 0xFFFF,
        sample_rate,
        len(payload),
        crc16,
    )
    return header + payload
