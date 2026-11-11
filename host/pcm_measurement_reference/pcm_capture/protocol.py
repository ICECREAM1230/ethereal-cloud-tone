"""Frame format and streaming parser for FPGA PCM monitor data."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import List

MAGIC = 0xA55A
MAGIC_BYTES = struct.pack("<H", MAGIC)
LEGACY_VERSION = 1
CRC16_VERSION = 2
VERSION = CRC16_VERSION
HEADER_FORMAT = "<HBBHIHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
FLAG_STEREO = 0x01
FLAG_S32 = 0x02
FLAG_CRC16_CCITT = 0x80


@dataclass
class Frame:
    version: int
    flags: int
    seq: int
    sample_rate: int
    payload_len: int
    crc16: int
    payload: bytes
    crc_verified: bool


@dataclass
class ParserStats:
    bytes_discarded: int = 0
    frames_valid: int = 0
    frames_invalid: int = 0
    resync_count: int = 0
    seq_jumps: int = 0
    sample_rate_changes: int = 0
    frames_crc_valid: int = 0
    crc_errors: int = 0
    frames_unverified: int = 0
    frames_rejected: int = 0
    seq_frames_missing: int = 0
    seq_repeats_or_reorders: int = 0


class FrameParser:
    """Incrementally parses little-endian framed PCM byte streams."""

    def __init__(self, max_payload_len: int = 65535) -> None:
        self._buffer = bytearray()
        self._expected_seq: int | None = None
        self._sample_rate: int | None = None
        self._max_payload_len = max_payload_len
        self.stats = ParserStats()

    @property
    def pending_bytes(self) -> int:
        return len(self._buffer)

    def feed(self, chunk: bytes) -> List[Frame]:
        self._buffer.extend(chunk)
        frames: List[Frame] = []

        while True:
            if len(self._buffer) < HEADER_SIZE:
                return frames

            if self._buffer[0:2] != MAGIC_BYTES:
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

            if payload_len > self._max_payload_len or payload_len % 2 != 0:
                self.stats.frames_invalid += 1
                self.stats.bytes_discarded += 1
                del self._buffer[0]
                continue

            total_len = HEADER_SIZE + payload_len
            if len(self._buffer) < total_len:
                return frames

            payload = bytes(self._buffer[HEADER_SIZE:total_len])
            del self._buffer[:total_len]

            if version not in (LEGACY_VERSION, CRC16_VERSION):
                self.stats.frames_invalid += 1
                self.stats.frames_rejected += 1
                continue

            crc_verified = False
            if version == CRC16_VERSION:
                if not flags & FLAG_CRC16_CCITT:
                    self.stats.frames_invalid += 1
                    self.stats.frames_rejected += 1
                    continue
                expected_crc = crc16_ccitt(_header_with_zero_crc(header) + payload)
                if crc16 != expected_crc:
                    self.stats.frames_invalid += 1
                    self.stats.frames_rejected += 1
                    self.stats.crc_errors += 1
                    continue
                crc_verified = True
                self.stats.frames_crc_valid += 1
                self._observe_trusted_sequence(seq)
            else:
                self.stats.frames_unverified += 1

            if self._sample_rate is None:
                self._sample_rate = sample_rate
            elif sample_rate != self._sample_rate:
                self.stats.sample_rate_changes += 1

            frame = Frame(
                version, flags, seq, sample_rate, payload_len, crc16, payload, crc_verified
            )
            self.stats.frames_valid += 1
            frames.append(frame)

    def _observe_trusted_sequence(self, seq: int) -> None:
        if self._expected_seq is not None and seq != self._expected_seq:
            self.stats.seq_jumps += 1
            delta = (seq - self._expected_seq) & 0xFFFF
            if 0 < delta < 0x8000:
                self.stats.seq_frames_missing += delta
            else:
                self.stats.seq_repeats_or_reorders += 1
        self._expected_seq = (seq + 1) & 0xFFFF


def pack_frame(
    payload: bytes,
    *,
    seq: int,
    sample_rate: int,
    flags: int = 0,
    version: int = VERSION,
    crc16: int | None = None,
) -> bytes:
    if version == CRC16_VERSION:
        flags |= FLAG_CRC16_CCITT
    elif version != LEGACY_VERSION:
        raise ValueError(f"unsupported frame version: {version}")

    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        version,
        flags,
        seq & 0xFFFF,
        sample_rate,
        len(payload),
        0 if crc16 is None else crc16,
    )
    if version == CRC16_VERSION and crc16 is None:
        crc16 = crc16_ccitt(header + payload)
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


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly=0x1021, init=0xffff, xorout=0."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _header_with_zero_crc(header: bytes) -> bytes:
    return header[:-2] + b"\x00\x00"
