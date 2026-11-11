#!/usr/bin/env python3
"""Read frozen FIRST-20 Note On measurements over the existing UART TX.

N20 records contain raw FPGA sys-clock observations and full PCM24 L/R endpoint
snapshots. UART arrival time is not part of the measured interval. Offline mode
uses only the standard library; --port explicitly opens a receive-only serial
port with DTR/RTS disabled and sends no bytes or reset/arm commands.
N202/N203 mean first nonzero full PCM24; historical N201 means abs(PCM16)>1.
N203 adds passive command/DSP/output stage observations. Record versions and
endpoint policies are never accepted as a single mixed capture batch.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from pathlib import Path
import re
from statistics import fmean
import struct
import sys
import time

FLAGS = {
    0: "allocated", 1: "frozen", 2: "timeout", 3: "invalid",
    4: "overlap", 5: "not_quiet", 6: "route_flush_or_fault",
    7: "observation_loss", 8: "left_over_pcm16_threshold",
    9: "right_over_pcm16_threshold", 18: "order_ambiguous",
}
ALLOWED_FLAGS = sum(1 << bit for bit in FLAGS)
ERROR_FLAGS = sum(1 << bit for bit in (2, 3, 4, 5, 6, 7, 18))
USED_WORDS = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 16, 17, 20, 21, 22, 23, 30, 31}
RECORD_VERSIONS = {0x4E323031: "N201", 0x4E323032: "N202", 0x4E323033: "N203"}
# Presence flags, rather than a nonzero timestamp, distinguish missing events.
STAGE_FIELDS = (("begin", 10, 8), ("commit_queued", 11, 18),
                ("ack_success_return", 12, 12), ("dsp_first_nonzero_pcm24", 13, 14),
                ("wire_end", 14, 24))
STAGE_FLAGS = {bit: name + "_valid" for name, bit, _ in STAGE_FIELDS}
STAGE_FLAG_MASK = sum(1 << bit for bit in STAGE_FLAGS)
N203_USED_WORDS = USED_WORDS | {8, 9, 12, 13, 14, 15, 18, 19, 24, 25, 26, 27, 28}
STAGE_PHASES = (("input_assembly", "first_input", "complete_note_on"),
                ("ae350_response", "complete_note_on", "begin"),
                ("command_group_submit", "begin", "commit_queued"),
                ("commit_dispatch_and_return", "commit_queued", "ack_success_return"),
                ("dsp_after_ack", "ack_success_return", "dsp_first_nonzero_pcm24"),
                ("audio_output_wait", "dsp_first_nonzero_pcm24", "pair"),
                ("serial_transmission", "pair", "wire_end"),
                # DSP may beat the return ACK, but cannot precede COMMIT enqueue.
                ("commit_queued_to_dsp", "commit_queued", "dsp_first_nonzero_pcm24"))
AE350_RESPONSE_SCOPE = ("M1 to BEGIN system response, including queue, polling, parsing, voice allocation "
                        "and compilation; not pure CPU execution time or WCET")
MASK64 = (1 << 64) - 1


def u64(words: tuple[int, ...], low: int) -> int:
    return words[low] | (words[low + 1] << 32)


def signed32(value: int) -> int:
    return value if value < 0x80000000 else value - 0x100000000


def interval(begin: int, end: int, hz: int) -> dict:
    cycles = (end - begin) & MASK64
    return {"cycles": cycles, "microseconds": cycles * 1_000_000 / hz}


def decode_line(line: str) -> dict | None:
    """Ignore other diagnostics; reject every malformed N20 candidate line."""
    if "N20" not in line:
        return None
    match = re.fullmatch(r"N20 ([0-9a-fA-F]{256}) ([0-9a-fA-F]{4})\r\n", line)
    if not match:
        raise ValueError("N20 requires exactly 256 record hex digits, space, four CRC hex digits and CRLF")
    words = tuple(int(match[1][i:i + 8], 16) for i in range(0, 256, 8))
    binary = struct.pack("<32I", *words)
    crc = binascii.crc_hqx(binary, 0xFFFF)
    if crc != int(match[2], 16):
        raise ValueError(f"N20 CRC16 mismatch: expected {crc:04x}, received {match[2]}")
    row_id, flags, metadata = words[2], words[1], words[3]
    version = RECORD_VERSIONS.get(words[0])
    if version is None or not 1 <= row_id <= 20:
        raise ValueError("N20 unsupported signature or row ID outside 1..20")
    stages = version == "N203"
    nonzero_pcm24 = version != "N201"
    policy = "first_nonzero_pcm24" if nonzero_pcm24 else "legacy_abs_pcm16_gt1"
    allowed_flags = ALLOWED_FLAGS | STAGE_FLAG_MASK if stages else ALLOWED_FLAGS
    if flags & ~allowed_flags or flags & 3 != 3:
        raise ValueError("N20 unknown flags or row not allocated and frozen")
    if metadata >> 22 or metadata & 3 > 2 or (metadata >> 2) & 3 > 2 or (metadata >> 15) & 127 == 0:
        raise ValueError("N20 invalid metadata source/engine/velocity or reserved bits")
    used_words = N203_USED_WORDS if stages else USED_WORDS
    if any(words[i] for i in range(32) if i not in used_words):
        raise ValueError("N20 nonzero reserved word")
    if stages:
        if words[28] >> 5:
            raise ValueError("N203 nonzero reserved command identity bits")
        for name, bit, low in STAGE_FIELDS:
            if not flags & (1 << bit) and u64(words, low):
                raise ValueError(f"N203 absent {name} stage has a nonzero timestamp")
        if not flags & (1 << 10) and any(words[index] for index in (26, 27, 28)):
            raise ValueError("N203 nonzero command identity without BEGIN presence")
        if flags & STAGE_FLAG_MASK != STAGE_FLAG_MASK and not flags & ERROR_FLAGS:
            raise ValueError("N203 missing required stage without an error flag")
    hz = words[30]
    if not 0 < hz <= 1_000_000_000 or not 0 < words[31] < 0x80000000:
        raise ValueError("N20 invalid counter frequency or timeout")
    if (words[22] | words[23]) & 0xFF:
        raise ValueError("N20 PCM24 left-aligned s32 container low eight bits must be zero")
    endpoint = bool(flags & ((1 << 8) | (1 << 9)))
    left32, right32 = signed32(words[22]), signed32(words[23])
    left24, right24 = left32 >> 8, right32 >> 8
    if endpoint:
        left_on = left24 != 0 if nonzero_pcm24 else abs(left24 >> 8) > 1
        right_on = right24 != 0 if nonzero_pcm24 else abs(right24 >> 8) > 1
        criterion = "nonzero PCM24" if nonzero_pcm24 else "PCM16 threshold>1"
        if bool(flags & (1 << 8)) != left_on:
            raise ValueError(f"N20 left onset flag disagrees with {criterion} snapshot")
        if bool(flags & (1 << 9)) != right_on:
            raise ValueError(f"N20 right onset flag disagrees with {criterion} snapshot")
    elif any(words[i] for i in (10, 11, 16, 17, 20, 21, 22, 23)):
        raise ValueError("N20 absent endpoint has nonzero pair snapshot fields")
    first, complete = u64(words, 4), u64(words, 6)
    pair = u64(words, 10) if endpoint else None
    timestamps = {"first_input": first, "complete_note_on": complete, "pair": pair}
    raw_deltas = {"input_assembly": (complete - first) & MASK64}
    if endpoint:
        raw_deltas.update(first_input_to_pair=(pair - first) & MASK64,
                          complete_note_on_to_pair=(pair - complete) & MASK64)
    if stages:
        raw_timestamps = {"first_input": first, "complete_note_on": complete, "pair": u64(words, 10)}
        for name, bit, low in STAGE_FIELDS:
            raw_timestamps[name] = u64(words, low)
            timestamps[name] = raw_timestamps[name] if flags & (1 << bit) else None
        for name, begin, end in STAGE_PHASES:
            if timestamps[begin] is not None and timestamps[end] is not None:
                raw_deltas[name] = (timestamps[end] - timestamps[begin]) & MASK64
    unordered = [name for name, delta in raw_deltas.items() if delta >= 1 << 63]
    # ACK is a return observation in another domain. A native DSP onset may
    # precede it; that diagnostic segment is unordered without breaking the
    # COMMIT -> DSP -> PCM causal chain or fabricating a huge elapsed time.
    causal_violations = [name for name in unordered if not stages or name != "dsp_after_ack"]
    qualified = endpoint and not flags & ERROR_FLAGS and not causal_violations
    # Failed/overlapping trials remain raw evidence, not presented as usable
    # causal microsecond values. Zero ticks are valid when endpoint flags say so.
    intervals = {
        "first_input_to_pair": interval(first, pair, hz) if qualified else None,
        "complete_note_on_to_pair": interval(complete, pair, hz) if qualified else None,
    }
    if stages:
        for name, begin, end in STAGE_PHASES:
            intervals[name] = (interval(timestamps[begin], timestamps[end], hz)
                               if qualified and name in raw_deltas and name not in unordered else None)
    flag_names = dict(FLAGS)
    if nonzero_pcm24:
        flag_names.update({8: "left_nonzero_pcm24", 9: "right_nonzero_pcm24"})
    if stages:
        flag_names.update(STAGE_FLAGS)
    result = {
        "format": "N20", "id": row_id, "successful": bool(qualified),
        "record_version": version,
        "measurement_policy": policy,
        "status": "SUCCESS" if qualified else "TIMEOUT" if flags & 4 else "INVALID_OR_INCOMPLETE",
        "flags_raw": flags, "flags": [name for bit, name in flag_names.items() if flags & (1 << bit)],
        "source": metadata & 3, "source_name": ("USB", "RAW1", "RAW2")[metadata & 3],
        "engine": (metadata >> 2) & 3, "engine_name": ("SUB", "KS", "FM")[(metadata >> 2) & 3],
        "channel": (metadata >> 4) & 15, "note": (metadata >> 8) & 127,
        "velocity": (metadata >> 15) & 127,
        "clock_hz": hz, "timeout_cycles": words[31],
        "threshold_pcm16_lsb": None if nonzero_pcm24 else 1,
        "threshold_pcm24_lsb": 0 if nonzero_pcm24 else None,
        "timestamps_cycles": timestamps,
        "raw_deltas_cycles": raw_deltas, "unordered_intervals": unordered,
        "pair_sample_index": u64(words, 16) if endpoint else None,
        "pair_tag": u64(words, 20) if endpoint else None,
        "pcm24_left": left24 if endpoint else None, "pcm24_right": right24 if endpoint else None,
        "s32le_left": left32 if endpoint else None, "s32le_right": right32 if endpoint else None,
        "intervals": intervals, "crc16": f"{crc:04x}",
        "binary_sha256": hashlib.sha256(binary).hexdigest(),
        "raw_words": [f"{word:08x}" for word in words],
        "boundary": ("Logical MIDI acceptance to first nonzero full PCM24 stereo-pair observation; UART/host arrival excluded"
                     if nonzero_pcm24 else
                     "Logical MIDI acceptance to first abs(PCM16)>1 stereo-pair observation; UART/host arrival excluded"),
    }
    if stages:
        result.update(raw_timestamps_cycles=raw_timestamps,
                      command_identity={"revision": words[26], "generation": words[27], "voice": words[28]},
                      signed_deltas_cycles={name: delta if delta < 1 << 63 else delta - (1 << 64)
                                            for name, delta in raw_deltas.items()},
                      interval_ordered={name: name not in unordered if name in raw_deltas else None
                                        for name in intervals},
                      causal_order_violations=causal_violations,
                      ae350_response_scope=AE350_RESPONSE_SCOPE)
    return result


def interval_statistics(rows: list[dict]) -> dict:
    """Summarize only qualified segments, with explicit empty-stage counts."""
    names = sorted({name for row in rows for name in row["intervals"]})
    result = {}
    for name in names:
        values = [row["intervals"][name] for row in rows if row["intervals"].get(name) is not None]
        result[name] = {"count": len(values)}
        for unit in ("cycles", "microseconds"):
            numbers = [value[unit] for value in values]
            result[name][unit] = {"min": min(numbers) if numbers else None,
                                  "max": max(numbers) if numbers else None,
                                  "mean": fmean(numbers) if numbers else None}
    return result


class Note20UartDecoder:
    """Keep all twenty IDs, identical repeats, malformed lines and conflicts."""
    def __init__(self) -> None:
        self.pending = bytearray()
        self.records: dict[int, dict] = {}
        self.errors: list[dict] = []
        self.conflicting_records: list[dict] = []
        self.lines = self.other_lines = self.repeats = self.raw_bytes = 0
        self.raw_hash = hashlib.sha256()
        self.finished = False
        self.measurement_policy = None
        self.record_versions: set[str] = set()

    def feed(self, data: bytes) -> None:
        if self.finished:
            raise RuntimeError("cannot feed a finished decoder")
        self.raw_bytes += len(data)
        self.raw_hash.update(data)
        self.pending.extend(data)
        while b"\n" in self.pending:
            index = self.pending.index(10) + 1
            raw = bytes(self.pending[:index]); del self.pending[:index]
            self.lines += 1
            try:
                row = decode_line(raw.decode("ascii"))
            except (ValueError, UnicodeDecodeError) as exc:
                self.errors.append({"line": self.lines, "error": str(exc), "raw_hex": raw.hex()})
                continue
            if row is None:
                self.other_lines += 1
                continue
            if self.measurement_policy is None:
                self.measurement_policy = row["measurement_policy"]
            elif row["measurement_policy"] != self.measurement_policy:
                self.errors.append({"line": self.lines, "id": row["id"],
                                    "error": "mixed measurement policies in one capture session"})
            if self.record_versions and row["record_version"] not in self.record_versions:
                self.errors.append({"line": self.lines, "id": row["id"],
                                    "error": "mixed record versions in one capture session"})
            self.record_versions.add(row["record_version"])
            previous = self.records.get(row["id"])
            if previous is None:
                self.records[row["id"]] = row
            elif row["binary_sha256"] == previous["binary_sha256"]:
                self.repeats += 1
            else:
                self.errors.append({"line": self.lines, "id": row["id"],
                                    "error": "same ID changed without a proven new capture session"})
                self.conflicting_records.append(row)
        if len(self.pending) > 4096:
            self.errors.append({"line": self.lines + 1, "error": "unterminated UART data beyond 4096 bytes",
                                "raw_hex": self.pending.hex()})
            self.pending.clear()

    def finish(self) -> dict:
        if not self.finished:
            if self.pending and (b"N20" in self.pending or b"N20 ".startswith(bytes(self.pending))):
                self.errors.append({"line": self.lines + 1, "error": "truncated N20 line",
                                    "raw_hex": self.pending.hex()})
            self.finished = True
        missing = [i for i in range(1, 21) if i not in self.records]
        rows = [self.records[i] for i in sorted(self.records)]
        qualified = sum(row["successful"] for row in rows)
        status = "FAIL" if self.errors else "INCOMPLETE" if missing else \
                 "PASS" if qualified == 20 else "CAPTURED_WITH_INVALID_MEASUREMENTS"
        return {
            "status": status, "stream_integrity": "FAIL" if self.errors else "PASS",
            "measurement_policy": self.measurement_policy,
            "record_versions": sorted(self.record_versions),
            "raw_bytes": self.raw_bytes, "raw_sha256": self.raw_hash.hexdigest(),
            "lines": self.lines, "ignored_other_lines": self.other_lines,
            "records": rows, "received_ids": sorted(self.records), "missing_ids": missing,
            "successful_records": qualified, "invalid_records": len(rows) - qualified,
            "identical_repeats": self.repeats, "errors": self.errors,
            "conflicting_records": self.conflicting_records,
            "interval_statistics": interval_statistics(rows),
            "engine_interval_statistics": {
                engine: interval_statistics([row for row in rows if row["engine_name"] == engine])
                for engine in ("SUB", "KS", "FM") if any(row["engine_name"] == engine for row in rows)
            },
            "scope": "Frozen first20 valid Note On test records, including invalid/timeout slots. "
                     "Per-record FPGA observations only; no UART/host transport time, pure CPU WCET or board precision claim.",
            "session": "One receive session only. Restart capture after FPGA reset; changed ID content is never silently accepted.",
        }


class SerialReader:
    def __init__(self, port: str, baud: int, *, serial_module=None) -> None:
        if serial_module is None:
            try:
                import serial as serial_module
            except ImportError as exc:
                raise RuntimeError("--port requires pyserial; --input needs only Python standard library") from exc
        self.serial = serial_module.Serial(port=None, baudrate=baud, timeout=0.1, exclusive=True)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()

    def read(self, count: int = 4096, *, timeout_seconds: float | None = None) -> bytes:
        if timeout_seconds is None:
            return bytes(self.serial.read(count))
        # Receive-only local timeout change bounds the final read to the grace
        # deadline. It neither changes control lines nor emits a serial byte.
        previous = self.serial.timeout
        self.serial.timeout = min(previous, max(0.0, timeout_seconds)) if previous is not None else max(0.0, timeout_seconds)
        try:
            return bytes(self.serial.read(count))
        finally:
            self.serial.timeout = previous

    def close(self) -> None:
        self.serial.close()


def finish_pending_serial_line(reader: SerialReader, decoder: Note20UartDecoder, raw,
                               *, grace_seconds: float = 0.3, monotonic=None) -> dict:
    """After capture deadline, finish only the already-started UART line.

    No new line is started when the pending buffer is empty. A valid N20 line
    is at most267 bytes, so the total pending length plus grace bytes is capped
    at267. Missing LF, invalid CRC or prior errors remain failures in finish().
    Offline files do not use this helper and remain strict about truncation.
    """
    clock = monotonic or time.monotonic
    initial = len(decoder.pending)
    result = {"attempted": bool(initial), "initial_pending_bytes": initial,
              "read_bytes": 0, "completed_line": not initial,
              "grace_seconds": grace_seconds, "line_byte_cap": 267}
    if not initial:
        return result
    deadline = clock() + max(0.0, grace_seconds)
    budget = max(0, 267 - initial)
    while result["read_bytes"] < budget:
        remaining = deadline - clock()
        if remaining <= 0:
            break
        chunk = reader.read(1, timeout_seconds=remaining)
        if len(chunk) > 1:
            raise RuntimeError("serial read(1) unexpectedly returned more than one byte")
        raw.write(chunk); raw.flush(); decoder.feed(chunk)
        result["read_bytes"] += len(chunk)
        if chunk == b"\n":
            result["completed_line"] = True
            break
    if not result["completed_line"]:
        decoder.errors.append({"line": decoder.lines + 1,
                               "error": "serial grace expired or byte cap reached before pending line LF",
                               "pending_bytes": len(decoder.pending)})
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=Path, help="saved raw UART bytes; no device access")
    source.add_argument("--port", help="explicit receive-only FPGA UART port")
    parser.add_argument("--baud", type=int, default=200000)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--raw-output", type=Path, help="new exact capture file, required with --port")
    parser.add_argument("--output", type=Path, required=True, help="new JSON result file")
    args = parser.parse_args(argv)
    if args.baud <= 0 or args.seconds <= 0:
        parser.error("positive --baud and --seconds required")
    if bool(args.port) != bool(args.raw_output):
        parser.error("--raw-output is required only with --port")
    decoder = Note20UartDecoder()
    reader = None
    failure = None
    serial_tail = None
    with args.output.open("x", encoding="utf-8") as output:
        try:
            if args.input:
                with args.input.open("rb") as stream:
                    while chunk := stream.read(4096):
                        decoder.feed(chunk)
            else:
                with args.raw_output.open("xb") as raw:
                    reader = SerialReader(args.port, args.baud)
                    until = time.monotonic() + args.seconds
                    while time.monotonic() < until:
                        chunk = reader.read()
                        raw.write(chunk); raw.flush(); decoder.feed(chunk)
                    serial_tail = finish_pending_serial_line(reader, decoder, raw)
        except Exception as exc:
            failure = f"{type(exc).__name__}: {exc}"
        finally:
            if reader:
                reader.close()
        result = decoder.finish()
        result.update(input=str(args.input.resolve()) if args.input else None,
                      raw_capture=str(args.raw_output.resolve()) if args.raw_output else None,
                      baud=args.baud if args.port else None,
                      serial_tail=serial_tail,
                      decoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        if failure:
            result.update(status="FAIL", capture_error=failure)
        json.dump(result, output, ensure_ascii=False, indent=2); output.write("\n")
    print(f"N20 {result['status']}: {len(result['records'])}/20 IDs, "
          f"{result['successful_records']} qualified, {result['identical_repeats']} repeats")
    for row in result["records"]:
        measured = row["intervals"]["first_input_to_pair"]
        detail = f"{measured['cycles']} cycles / {measured['microseconds']:.3f} us" if measured else ",".join(row["flags"])
        print(f"#{row['id']:02d} {row['status']} {row['source_name']} note={row['note']} "
              f"L24={row['pcm24_left']} R24={row['pcm24_right']}: {detail}")
    return 0 if result["status"] == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
