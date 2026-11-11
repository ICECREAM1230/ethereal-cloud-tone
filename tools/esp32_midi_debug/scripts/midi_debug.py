#!/usr/bin/env python3
"""Linux local serial debugger. stdout is JSON; no writes without --execute."""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import sys
import time
import uuid

VERSION = "0.1.0"
KINDS = {"note-off": (0x80, 2), "note-on": (0x90, 2),
         "poly-pressure": (0xA0, 2), "cc": (0xB0, 2),
         "program": (0xC0, 1), "channel-pressure": (0xD0, 1), "bend": (0xE0, 1)}
ENDINGS = {"lf": b"\n", "crlf": b"\r\n", "cr": b"\r", "none": b""}


class DebugError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def require(condition, message, code="INVALID_ARGUMENT"):
    if not condition:
        raise DebugError(code, message)


def integer(value, low, high, name):
    require(type(value) is int and low <= value <= high,
            f"{name} must be an integer in {low}..{high}")
    return value


def duration(value, low, high, name):
    require(math.isfinite(value) and low <= value <= high,
            f"{name} must be finite and in {low}..{high} seconds")


def hex_text(data):
    return data.hex(" ").upper()


def encode_midi(kind, channel, data):
    require(kind in KINDS, "Unsupported MIDI message kind")
    integer(channel, 1, 16, "channel")
    status, length = KINDS[kind]
    require(len(data) == length, f"{kind} needs {length} data value(s)")
    if kind == "bend":
        value = integer(data[0], 0, 16383, "bend")
        values = [value & 0x7F, value >> 7]
    else:
        values = [integer(value, 0, 127, "MIDI data") for value in data]
    return bytes([status | (channel - 1), *values])


def parse_hex(value):
    words = value.split()
    require(1 <= len(words) <= 256 and all(re.fullmatch(r"[0-9a-fA-F]{2}", x) for x in words),
            "Hex input needs 1..256 space-separated two-digit bytes, e.g. '90 3C 64'")
    return bytes(int(word, 16) for word in words)


def command_bytes(command, ending):
    require(isinstance(command, str) and bool(command.strip()), "Command must not be empty")
    require(not any(c in command for c in ("\r", "\n", "\x00")),
            "One command per call: embedded CR, LF and NUL are not allowed")
    payload = command.encode("utf-8")
    require(len(payload) <= 2048, "Command exceeds 2048 UTF-8 bytes")
    return payload + ENDINGS[ending]


def load_profile(path):
    try:
        require(Path(path).stat().st_size <= 65536, "Profile exceeds 64 KiB", "PROFILE_INVALID")
        profile = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise DebugError("PROFILE_INVALID", f"Cannot read profile: {exc}") from exc
    require(isinstance(profile, dict), "Profile must be a JSON object", "PROFILE_INVALID")
    require(type(profile.get("schema_version")) is int and profile["schema_version"] == 1,
            "Unsupported profile schema_version", "PROFILE_INVALID")
    require(type(profile.get("confirmed")) is bool, "confirmed must be a boolean", "PROFILE_INVALID")
    template = profile.get("command_template")
    # Use replacement instead of str.format: attribute lookups and expressions are never evaluated.
    require(isinstance(template, str) and template.count("{hex}") == 1,
            "command_template needs exactly one {hex}", "PROFILE_INVALID")
    remainder = template.replace("{hex}", "")
    require(not any(c in remainder for c in "{}\r\n\x00") and len(template) <= 1024,
            "Only a single-line template with a literal {hex} placeholder is supported", "PROFILE_INVALID")
    require(isinstance(profile.get("line_ending"), str) and profile["line_ending"] in ENDINGS,
            "Invalid line_ending", "PROFILE_INVALID")
    require(isinstance(profile.get("name"), str) and bool(profile["name"].strip()),
            "Profile name is required", "PROFILE_INVALID")
    return profile


def render(profile, midi):
    command = profile["command_template"].replace("{hex}", hex_text(midi))
    return command_bytes(command, profile["line_ending"])


def error_info(exc):
    if isinstance(exc, KeyboardInterrupt):
        return {"code": "INTERRUPTED", "message": "Interrupted; inspect logs for completed writes"}
    return {"code": getattr(exc, "code", "IO_ERROR"), "message": str(exc)}


def error_exit(info):
    return {"RESPONSE_TIMEOUT": 3, "INTERRUPTED": 130}.get(info["code"], 2)


class Session:
    """One serial owner; logs are retained even when capture or matching fails."""
    def __init__(self, port, baud, log_root, max_rx):
        self.port = os.path.realpath(port)
        self.baud = baud
        self.log_root = Path(log_root)
        self.max_rx = max_rx
        self.serial = self.lock = self.events = self.rx_file = self.tx_file = None
        self.log_dir = None
        self.rx_bytes = self.tx_bytes = self.before_bytes = 0
        self.response = bytearray()
        self.details = {}
        self.result = None
        self.exit_code = 0
        self.start = time.monotonic()

    def event(self, name, **fields):
        if self.events:
            row = {"utc": datetime.now(timezone.utc).isoformat(),
                   "elapsed_ms": round((time.monotonic() - self.start) * 1000, 3),
                   "event": name, **fields}
            self.events.write(json.dumps(row, ensure_ascii=False) + "\n")
            self.events.flush()

    def __enter__(self):
        try:
            try:
                import serial
            except ImportError as exc:
                raise DebugError("DEPENDENCY_MISSING", "Install pyserial==3.5 for serial I/O") from exc
            cache = Path(os.environ.get("XDG_CACHE_HOME", str(Path.home() / ".cache")))
            lock_dir = Path(os.environ.get("ESP32_MIDI_DEBUG_LOCK_DIR", str(cache / "esp32-midi-debug/locks")))
            lock_dir.mkdir(parents=True, exist_ok=True)
            self.lock = (lock_dir / (hashlib.sha256(self.port.encode()).hexdigest() + ".lock")).open("a+")
            try:
                fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as exc:
                # Closing an unacquired descriptor must not unlock another session.
                self.lock.close()
                self.lock = None
                raise DebugError("PORT_BUSY", f"Port is already owned by this tool: {self.port}") from exc
            self.log_root.mkdir(parents=True, exist_ok=True)
            name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ") + "-" + uuid.uuid4().hex[:8]
            self.log_dir = (self.log_root / name).resolve()
            self.log_dir.mkdir(mode=0o700)
            self.events = (self.log_dir / "events.jsonl").open("x", encoding="utf-8")
            self.rx_file = (self.log_dir / "rx.bin").open("xb", buffering=0)
            self.tx_file = (self.log_dir / "tx.bin").open("xb", buffering=0)
            self.event("opening", port=self.port, baud=self.baud, dtr=False, rts=False)
            self.serial = serial.Serial(port=None, baudrate=self.baud, timeout=0.02,
                                        write_timeout=1, exclusive=True,
                                        xonxoff=False, rtscts=False, dsrdtr=False)
            self.serial.dtr = False
            self.serial.rts = False
            self.serial.port = self.port
            self.serial.open()
            self.event("opened")
            return self
        except (Exception, KeyboardInterrupt) as exc:
            self.__exit__(type(exc), exc, exc.__traceback__)
            raise

    def write(self, payload, label):
        self.event("tx_attempt", label=label, hex=hex_text(payload), bytes=len(payload))
        # A write exception may already have sent part of the payload; never retry automatically.
        count = self.serial.write(payload)
        self.tx_file.write(payload[:count])
        self.tx_bytes += count
        self.event("tx", label=label, hex=hex_text(payload[:count]), bytes=count)
        require(count == len(payload), "Partial serial write; delivery is uncertain, no retry performed", "SHORT_WRITE")

    def capture(self, seconds, phase):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            room = self.max_rx - self.rx_bytes
            waiting = self.serial.in_waiting
            if room == 0:
                require(waiting == 0, "RX limit reached; capture is incomplete. Raise --max-rx or shorten capture", "RX_LIMIT")
                time.sleep(min(0.01, max(0, deadline - time.monotonic())))
                continue
            self.serial.timeout = min(0.02, max(0, deadline - time.monotonic()))
            block = self.serial.read(min(max(waiting, 1), room, 4096))
            if block:
                self.rx_file.write(block)
                self.rx_bytes += len(block)
                if phase == "before_send":
                    self.before_bytes += len(block)
                else:
                    self.response.extend(block)
                self.event("rx", phase=phase, bytes=len(block), hex=hex_text(block),
                           text=block.decode("utf-8", errors="replace"))

    def has_response(self, literal):
        return literal.encode("utf-8") in self.response

    def __exit__(self, exc_type, exc, tb):
        # Release the serial owner even if a log write fails (disk full, unplug, etc.).
        self.result = {"ok": exc is None, "tool": "esp32-midi-debug", "version": VERSION,
                       "preview": False, "port": self.port, "baud": self.baud,
                       "tx_bytes": self.tx_bytes, "rx_bytes": self.rx_bytes,
                       "before_send_rx_bytes": self.before_bytes,
                       "response_text": self.response[:4096].decode("utf-8", errors="replace"),
                       "response_preview_truncated": len(self.response) > 4096,
                       "log_dir": str(self.log_dir) if self.log_dir else None, **self.details}
        if exc:
            self.result["error"] = error_info(exc)
            self.exit_code = error_exit(self.result["error"])

        def record_failure(failure, field, code):
            info = {"code": code, "message": str(failure)}
            self.result[field] = info
            if self.result["ok"]:
                self.result["ok"] = False
                self.result["error"] = info
                self.exit_code = error_exit(info)

        if self.serial:
            try:
                self.serial.close()
            except (Exception, KeyboardInterrupt) as failure:
                record_failure(failure, "close_error", "IO_ERROR")
        try:
            self.event("finished", ok=self.result["ok"])
        except (Exception, KeyboardInterrupt) as failure:
            record_failure(failure, "log_error", "LOG_ERROR")
        for handle in (self.rx_file, self.tx_file, self.events):
            if handle:
                try:
                    handle.close()
                except (Exception, KeyboardInterrupt) as failure:
                    record_failure(failure, "log_error", "LOG_ERROR")
        if self.lock:
            try:
                fcntl.flock(self.lock, fcntl.LOCK_UN)
            except (Exception, KeyboardInterrupt) as failure:
                record_failure(failure, "lock_error", "IO_ERROR")
            finally:
                self.lock.close()
                self.lock = None
        if self.log_dir:
            try:
                (self.log_dir / "result.json").write_text(
                    json.dumps(self.result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            except (Exception, KeyboardInterrupt) as failure:
                record_failure(failure, "log_error", "LOG_ERROR")
        return False


class JsonParser(argparse.ArgumentParser):
    def error(self, message):
        raise DebugError("INVALID_ARGUMENT", message)


def parser():
    cli = JsonParser(description=__doc__)
    cli.add_argument("--version", action="version", version=VERSION)
    commands = cli.add_subparsers(dest="action", required=True, parser_class=JsonParser)
    commands.add_parser("ports", help="List serial metadata without opening devices")
    for action in ("encode", "send", "midi", "note", "listen"):
        sub = commands.add_parser(action)
        if action in ("encode", "midi"):
            source = sub.add_mutually_exclusive_group(required=True)
            source.add_argument("--kind", choices=list(KINDS))
            source.add_argument("--hex", dest="raw_hex")
            sub.add_argument("--data", type=int, nargs="+", default=[])
            sub.add_argument("--channel", type=int, default=1)
        if action in ("midi", "note"):
            sub.add_argument("--profile", required=True)
        if action == "note":
            sub.add_argument("--note", type=int, default=60)
            sub.add_argument("--velocity", type=int, default=100)
            sub.add_argument("--channel", type=int, default=1)
            sub.add_argument("--hold", type=float, default=0.2)
        if action == "send":
            sub.add_argument("--command", required=True)
            sub.add_argument("--ending", choices=list(ENDINGS), default="lf")
        if action != "encode":
            sub.add_argument("--port")
            sub.add_argument("--baud", type=int)
            sub.add_argument("--log-root", default=str(Path.cwd() / "output/esp32-midi-debug-runs"))
            sub.add_argument("--max-rx", type=int, default=1048576)
        if action in ("send", "midi", "note"):
            sub.add_argument("--execute", action="store_true")
            sub.add_argument("--wait", type=float, default=1.0)
            sub.add_argument("--expect", help="Literal UTF-8 substring, checked only in post-write RX")
        if action == "listen":
            sub.add_argument("--seconds", type=float, default=5.0)
    return cli


def midi_from_args(args):
    if args.raw_hex is not None:
        require(not args.data and args.channel == 1, "--data/--channel cannot accompany --hex")
        return parse_hex(args.raw_hex)
    return encode_midi(args.kind, args.channel, args.data)


def run(args):
    if args.action == "ports":
        try:
            from serial.tools import list_ports
        except ImportError as exc:
            raise DebugError("DEPENDENCY_MISSING", "Install pyserial==3.5 for port enumeration") from exc
        ports = [{key: getattr(p, key, None) for key in
                  ("device", "description", "hwid", "vid", "pid", "serial_number", "location",
                   "manufacturer", "product", "interface")}
                 for p in sorted(list_ports.comports(), key=lambda p: p.device)]
        return 0, {"ok": True, "ports": ports, "devices_opened": 0}
    payloads = []
    profile = None
    if args.action in ("encode", "midi"):
        midi = midi_from_args(args)
        if args.action == "encode":
            return 0, {"ok": True, "midi_hex": hex_text(midi), "bytes": list(midi), "devices_opened": 0}
        profile = load_profile(args.profile)
        payloads = [render(profile, midi)]
    elif args.action == "send":
        payloads = [command_bytes(args.command, args.ending)]
    elif args.action == "note":
        duration(args.hold, 0.01, 10, "hold")
        integer(args.velocity, 1, 127, "note-on velocity")
        profile = load_profile(args.profile)
        payloads = [render(profile, encode_midi("note-on", args.channel, [args.note, args.velocity])),
                    render(profile, encode_midi("note-off", args.channel, [args.note, 0]))]
    integer(args.max_rx, 4096, 4194304, "max-rx")
    if args.action == "listen":
        duration(args.seconds, 0.01, 60, "seconds")
    else:
        duration(args.wait, 0, 30, "wait")
        if args.expect is not None:
            require(0 < len(args.expect.encode("utf-8")) <= 4096 and args.wait > 0,
                    "--expect needs 1..4096 UTF-8 bytes and --wait > 0")
        if not args.execute:
            preview = {"ok": True, "preview": True, "devices_opened": 0,
                       "action": args.action, "port": args.port, "baud": args.baud,
                       "commands": [{"wire_hex": hex_text(p), "text": p.decode("utf-8")} for p in payloads]}
            if profile:
                preview["profile_confirmed"] = profile["confirmed"]
            if args.action == "note":
                preview["hold_seconds"] = args.hold
            return 0, preview
        if profile:
            require(profile["confirmed"],
                    "Profile is an example. Verify firmware command format before setting confirmed=true",
                    "PROFILE_UNCONFIRMED")
    require(isinstance(args.port, str) and bool(args.port.strip()), "An explicit --port is required")
    require(args.baud is not None, "An explicit --baud is required")
    integer(args.baud, 1, 4000000, "baud")
    session = Session(args.port, args.baud, args.log_root, args.max_rx)
    session.details["action"] = args.action
    try:
        with session:
            if args.action == "listen":
                session.capture(args.seconds, "listen")
            else:
                session.capture(0.1, "before_send")
                if args.action == "note":
                    session.details["note_off_written"] = False
                    try:
                        session.write(payloads[0], "note_on")
                        session.capture(args.hold, "hold")
                    finally:
                        pending = sys.exc_info()[0] is not None
                        try:
                            session.write(payloads[1], "note_off")
                            session.details["note_off_written"] = True
                        except (Exception, KeyboardInterrupt) as cleanup:
                            session.details["note_off_error"] = error_info(cleanup)
                            if not pending:
                                raise
                else:
                    session.write(payloads[0], "command")
                session.capture(args.wait, "response")
                if args.expect is not None:
                    matched = session.has_response(args.expect)
                    session.details["expected_response_found"] = matched
                    require(matched, "Expected literal response not seen within capture window", "RESPONSE_TIMEOUT")
                else:
                    session.details["expected_response_found"] = None
                session.details["delivery_scope"] = "Host serial writes only; FPGA decoding and audio are unverified"
    except (Exception, KeyboardInterrupt):
        if session.result is None:
            raise
        return session.exit_code, session.result
    return session.exit_code, session.result


def main(argv=None):
    try:
        args = parser().parse_args(argv)
        code, result = run(args)
    except (Exception, KeyboardInterrupt) as exc:
        info = error_info(exc)
        code, result = error_exit(info), {"ok": False, "error": info}
    print(json.dumps(result, ensure_ascii=False))
    return code


def terminated(signum, frame):
    raise KeyboardInterrupt()


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, terminated)
    sys.exit(main())
