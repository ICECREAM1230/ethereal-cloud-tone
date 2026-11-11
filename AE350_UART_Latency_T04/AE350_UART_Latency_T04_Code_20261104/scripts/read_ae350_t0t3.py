#!/usr/bin/env python3
"""Decode AE350 AT04 raw T0/T1/T2/T3 counters and CPU subtraction results.

All counters use control48 at 48 MHz. T0 is accepted complete cable-zero USB
NoteOn; T1 is first NEW BEGIN queue submission; T2 matching COMMIT successful
return; T3 first nonzero full PCM24 playback load. UART arrival time and AE350
instruction WCET are not measured. Retain exact input bytes, including errors.
"""
from __future__ import annotations
import argparse
import binascii
from fractions import Fraction
import json
from pathlib import Path
import re
import struct

SIGNATURE = 0x4C543034
CLOCK_HZ = 48_000_000
MASK64 = (1 << 64) - 1
ENGINES = ("SUB", "KS", "FM")
PHASES = ("receive_queue_and_ae350", "command_transfer_and_commit",
          "synthesis_and_output_wait", "usb_note_on_to_pcm")
TIME_NAMES = ("T0", "T1", "T2", "T3")
PAIRS = ((0, 1), (1, 2), (2, 3), (0, 3))


def u64(words, index):
    return words[index] | (words[index + 1] << 32)


def signed64(value):
    return value - (1 << 64) if value & (1 << 63) else value


def pcm24(word):
    value = word >> 8
    return value - (1 << 24) if value & (1 << 23) else value


def _line_bytes(line):
    try:
        raw = line.encode("ascii") if isinstance(line, str) else bytes(line)
        raw.decode("ascii")
    except (UnicodeError, ValueError) as exc:
        raise ValueError("non-ASCII serial bytes") from exc
    if not raw.endswith(b"\r\n"):
        raise ValueError("missing exact CRLF terminator")
    return raw


def decode_line(line):
    raw = _line_bytes(line)
    if len(raw) != 220:
        raise ValueError("AT04 record must contain exactly 220 raw bytes")
    match = re.fullmatch(rb"AT04 ([0-9a-fA-F]{208}) ([0-9a-fA-F]{4})\r\n", raw)
    if not match:
        raise ValueError("AT04 framing/prefix/hex error (legacy/mixed formats rejected)")
    text = match.group(1).decode("ascii")
    words = [int(text[index:index + 8], 16) for index in range(0, 208, 8)]
    calculated = binascii.crc_hqx(struct.pack("<26I", *words), 0xFFFF)
    if calculated != int(match.group(2), 16):
        raise ValueError("CRC16 mismatch")
    if words[0] != SIGNATURE:
        raise ValueError("unsupported AT04 binary signature")
    if not 1 <= words[1] <= 20:
        raise ValueError("sample id outside first20")
    meta, flags = words[2:4]
    engine = meta & 3
    if meta >> 20 or engine > 2 or (meta >> 13) & 127 == 0:
        raise ValueError("invalid USB NoteOn metadata or reserved bits")
    if flags >> 13 or flags & 0x11 != 0x11:
        raise ValueError("reserved flags or missing T0/frozen presence")
    if flags & 15 != 15 and not flags & 0x40:
        raise ValueError("missing timestamp without invalid flag")
    if words[14] >> 5:
        raise ValueError("reserved voice bits")
    if words[15] != CLOCK_HZ:
        raise ValueError("counter frequency must be control48=48000000")
    raw_ticks = {name: u64(words, 4 + 2 * i) for i, name in enumerate(TIME_NAMES)}
    ticks = {name: raw_ticks[name] if flags & (1 << i) else None
             for i, name in enumerate(TIME_NAMES)}
    for i, name in enumerate(TIME_NAMES):
        if ticks[name] is None and raw_ticks[name]:
            raise ValueError("absent timestamp must contain zero raw ticks: " + name)
    if not flags & 2 and any(words[index] for index in (12, 13, 14)):
        raise ValueError("identity without T1")
    if flags & 2 and words[14] >= (32, 16, 8)[engine]:
        raise ValueError("voice outside selected engine capacity")
    if words[16] & 255 or words[17] & 255:
        raise ValueError("PCM24 alignment/reserved bits")
    left, right = pcm24(words[16]), pcm24(words[17])
    if bool(flags & 8) != bool(left or right):
        raise ValueError("T3 presence must match first nonzero full PCM24 pair")
    raw_deltas, deltas, ordered, intervals = {}, {}, {}, {}
    for index, (name, (start, end)) in enumerate(zip(PHASES, PAIRS)):
        present = bool(flags & (1 << start) and flags & (1 << end))
        expected = (raw_ticks[TIME_NAMES[end]] - raw_ticks[TIME_NAMES[start]]) & MASK64 if present else 0
        value = u64(words, 18 + 2 * index)
        if value != expected:
            raise ValueError("AE350 computed delta mismatch: " + name)
        raw_deltas[name] = value
        delta = signed64(value) if present else None
        deltas[name] = delta
        ordered[name] = delta >= 0 if present else None
        intervals[name] = ({"cycles": delta, "microseconds": float(Fraction(delta, 48))}
                           if present else None)
    # T3 may precede matching COMMIT return: report that signed interval rather
    # than clamping it or silently substituting COMMIT submission time.
    begin_to_pcm = (signed64((raw_ticks["T3"] - raw_ticks["T1"]) & MASK64)
                    if flags & 10 == 10 else None)
    causal = (all(deltas[name] is not None and 0 <= deltas[name] <= CLOCK_HZ
                  for name in (PHASES[0], PHASES[1], PHASES[3])) and
              begin_to_pcm is not None and 0 <= begin_to_pcm <= CLOCK_HZ)
    if flags == 0x1F and not causal:
        raise ValueError("successful record violates T0/T1/T2, T1/T3 or T0/T3 causal order/timeout")
    return {
        "record_version": "AT04", "id": words[1], "engine": engine,
        "engine_name": ENGINES[engine], "source": "USB cable0",
        "channel": (meta >> 2) & 15, "note": (meta >> 6) & 127,
        "velocity": (meta >> 13) & 127, "flags": flags,
        "successful": flags == 0x1F and causal,
        "status": "SUCCESS" if flags == 0x1F and causal else "TIMEOUT" if flags & 32 else "INVALID",
        "clock_hz": CLOCK_HZ, "raw_timestamps_cycles": raw_ticks,
        "timestamps_cycles": ticks, "cpu_raw_deltas_cycles": raw_deltas,
        "signed_deltas_cycles": deltas, "interval_ordered": ordered,
        "intervals": intervals,
        "command_identity": {"revision": words[12], "generation": words[13], "voice": words[14]},
        "pcm24_left": left, "pcm24_right": right,
        "raw_words": words, "raw_line_hex": raw.hex(),
        "boundary": "USB NoteOn logical acceptance to first full PCM24 nonzero playback load; UART/host and analog DAC excluded",
        "ae350_response_scope": "Receive queue plus AE350 parsing/allocation/calculation; not pure CPU instruction WCET",
        "observation_scope": "T0 direct control48 edge; T1/T2/T3 include event CDC observation delay; no board precision or STA claim",
    }


def decode_error(line):
    raw = _line_bytes(line)
    match = re.fullmatch(rb"AT4E ([0-9a-fA-F]{8}) ([0-9a-fA-F]{4})\r\n", raw)
    if not match:
        raise ValueError("AT4E diagnostic framing error")
    value = int(match.group(1), 16)
    if value == 0 or value & ~15:
        raise ValueError("AT4E invalid fault mask")
    if binascii.crc_hqx(struct.pack("<I", value), 0xFFFF) != int(match.group(2), 16):
        raise ValueError("AT4E diagnostic CRC16 mismatch")
    return value


class Capture:
    def __init__(self):
        self.buffer = bytearray()
        self.records = {}
        self.errors = []
        self.duplicates = 0
        self.lines = 0

    def error(self, why, raw):
        self.errors.append({"line": self.lines, "error": str(why),
                            "raw_hex": raw.hex(), "raw_text": raw.decode("ascii", "backslashreplace")})

    def feed(self, data):
        self.buffer.extend(data)
        while b"\n" in self.buffer:
            end = self.buffer.index(10) + 1
            raw = bytes(self.buffer[:end]); del self.buffer[:end]
            self.lines += 1
            try:
                if raw.startswith(b"AT4E "):
                    fault = decode_error(raw)
                    self.error(f"AE350 sticky fault mask 0x{fault:08x}", raw)
                    continue
                row = decode_line(raw)
                old = self.records.get(row["id"])
                if old is not None:
                    if old["raw_words"] != row["raw_words"]:
                        self.error("conflicting repeated sample id", raw)
                    else:
                        self.duplicates += 1
                else:
                    self.records[row["id"]] = row
            except ValueError as exc:
                self.error(exc, raw)
        if len(self.buffer) > 4096:
            self.lines += 1
            self.error("unterminated serial data beyond 4096 bytes", bytes(self.buffer))
            self.buffer.clear()

    def finish(self):
        if self.buffer:
            self.lines += 1
            self.error("truncated unterminated serial line", bytes(self.buffer))
            self.buffer.clear()

    def summary(self, expected_engine=None):
        rows = [self.records[key] for key in sorted(self.records)]
        successful = [row for row in rows if row["successful"]]
        groups = {}
        for engine, name in enumerate(ENGINES):
            selected = [row for row in successful if row["engine"] == engine]
            phases = {}
            for phase in PHASES:
                values = [row["signed_deltas_cycles"][phase] for row in selected
                          if row["signed_deltas_cycles"][phase] is not None]
                average = Fraction(sum(values), len(values)) if values else None
                phases[phase] = {"count": len(values), "negative_count": sum(v < 0 for v in values),
                                 "min_cycles": min(values) if values else None,
                                 "max_cycles": max(values) if values else None,
                                 "mean_cycles": float(average) if average is not None else None,
                                 "mean_cycles_fraction": str(average) if average is not None else None,
                                 "mean_microseconds": float(average / 48) if average is not None else None}
            groups[name] = {"successful_count": len(selected), "intervals": phases}
        engines = {row["engine_name"] for row in rows}
        missing = [index for index in range(1, 21) if index not in self.records]
        policy_errors = []
        if len(engines) > 1:
            policy_errors.append("mixed engines in one first20 batch")
        if expected_engine is not None and engines - {expected_engine}:
            policy_errors.append("record engine differs from requested batch")
        if self.errors or policy_errors:
            status = "FAIL"
        elif missing:
            status = "INCOMPLETE"
        elif len(successful) != 20:
            status = "FAIL"
        else:
            status = "PASS"
        return {"status": status, "stream_status": "FAIL" if self.errors else "PASS",
                "record_version": "AT04", "clock_hz": CLOCK_HZ, "unique_records": len(rows),
                "successful_records": len(successful), "missing_ids": missing,
                "identical_repeats": self.duplicates, "errors": self.errors,
                "batch_errors": policy_errors, "engines": groups, "records": rows,
                "raw_policy": "Exact original input bytes retained; invalid/mixed/CRC/conflict/diagnostic lines are not filtered out",
                "measurement_scope": __doc__}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Exact saved raw UART bytes; offline only")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--engine", choices=ENGINES)
    args = parser.parse_args()
    capture = Capture(); capture.feed(args.input.read_bytes()); capture.finish()
    result = capture.summary(args.engine)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("status", "unique_records", "successful_records", "missing_ids")}))
    return 0 if result["status"] == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
