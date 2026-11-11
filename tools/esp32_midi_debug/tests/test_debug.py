"""Behavior checks on virtual terminals only; no real boards are opened."""
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "midi_debug.py"
spec = importlib.util.spec_from_file_location("midi_debug", SCRIPT)
debug = importlib.util.module_from_spec(spec)
spec.loader.exec_module(debug)


class MidiTests(unittest.TestCase):
    def test_channel_message_encoding(self):
        cases = [
            ("note-on", 1, [60, 100], "903c64"),
            ("note-off", 16, [60, 0], "8f3c00"),
            ("poly-pressure", 2, [60, 90], "a13c5a"),
            ("cc", 1, [64, 127], "b0407f"),
            ("program", 3, [9], "c209"),
            ("channel-pressure", 4, [20], "d314"),
            ("bend", 1, [8192], "e00040"),
            ("bend", 16, [16383], "ef7f7f"),
        ]
        for kind, channel, data, expected in cases:
            with self.subTest(kind=kind, channel=channel, data=data):
                self.assertEqual(debug.encode_midi(kind, channel, data).hex(), expected)

    def test_invalid_channel_data_and_lengths(self):
        for kind, channel, data in [
            ("note-on", 0, [60, 100]), ("note-on", 17, [60, 100]),
            ("cc", 1, [64, 128]), ("program", 1, [1, 2]),
            ("bend", 1, [-1]), ("bend", 1, [16384]),
            ("note-on", True, [60, 100]), ("program", 1, [1.0]),
        ]:
            with self.subTest(kind=kind, channel=channel, data=data):
                with self.assertRaises(debug.DebugError):
                    debug.encode_midi(kind, channel, data)

    def test_raw_hex_strict_byte_boundaries(self):
        self.assertEqual(debug.parse_hex("90 3c 64"), b"\x90\x3c\x64")
        for bad in ["", "903c64", "0x90 3c", "9 3c", "GG"]:
            with self.subTest(bad=bad), self.assertRaises(debug.DebugError):
                debug.parse_hex(bad)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.directory = Path(self.tmp.name)
        self.env = dict(os.environ, ESP32_MIDI_DEBUG_LOCK_DIR=str(self.directory / "locks"))
        self.lock_environment = mock.patch.dict(os.environ, {
            "ESP32_MIDI_DEBUG_LOCK_DIR": self.env["ESP32_MIDI_DEBUG_LOCK_DIR"]})
        self.lock_environment.start()

    def tearDown(self):
        self.lock_environment.stop()
        self.tmp.cleanup()

    def invoke(self, *args):
        proc = subprocess.run([sys.executable, str(SCRIPT), *args], text=True,
                              capture_output=True, timeout=8, env=self.env)
        self.assertEqual(proc.stderr, "", proc.stderr)
        return proc.returncode, json.loads(proc.stdout)

    def profile(self, **overrides):
        profile = {"schema_version": 1, "name": "test-only", "confirmed": True,
                   "command_template": "midi {hex}", "line_ending": "crlf"}
        profile.update(overrides)
        path = self.directory / "profile.json"
        path.write_text(json.dumps(profile))
        return path

    def test_dry_run_without_port_never_creates_logs_or_locks(self):
        code, result = self.invoke("send", "--command", "status",
                                   "--log-root", str(self.directory / "logs"))
        self.assertEqual(code, 0)
        self.assertTrue(result["preview"])
        self.assertEqual(result["commands"][0]["wire_hex"], "73 74 61 74 75 73 0A")
        self.assertEqual(list(self.directory.iterdir()), [])

    def test_unconfirmed_profile_blocks_before_port_open(self):
        path = self.profile(confirmed=False)
        args = ["midi", "--profile", str(path), "--kind", "note-on", "--data", "60", "100"]
        code, result = self.invoke(*args)
        self.assertEqual(code, 0)
        self.assertFalse(result["profile_confirmed"])
        code, result = self.invoke(*args, "--execute", "--port", "/does/not/exist", "--baud", "115200")
        self.assertEqual(code, 2)
        self.assertEqual(result["error"]["code"], "PROFILE_UNCONFIRMED")
        self.assertFalse((self.directory / "locks").exists())

    def test_invalid_cli_and_nonfinite_wait_are_json_errors(self):
        for args in [("send", "--command", "x", "--wait", "nan"),
                     ("send", "--command", "x\ny"),
                     ("midi", "--profile", str(self.profile()), "--kind", "cc", "--data", "1", "128"),
                     ("encode", "--kind", "cc"),
                     ("no-such-command",)]:
            with self.subTest(args=args):
                code, result = self.invoke(*args)
                self.assertEqual(code, 2)
                self.assertFalse(result["ok"])

    def test_profile_cannot_inject_additional_lines_or_format_expressions(self):
        for template in ["midi {hex}\nreset", "{hex.__class__}", "{hex!r}", "{hex:>8}", "status"]:
            with self.subTest(template=template):
                path = self.profile(command_template=template)
                code, result = self.invoke("midi", "--profile", str(path), "--hex", "90 3C 64")
                self.assertEqual(code, 2)
                self.assertEqual(result["error"]["code"], "PROFILE_INVALID")

    @contextlib.contextmanager
    def peer(self, response=b"", expect_commands=1):
        master, slave = pty.openpty()
        port = os.ttyname(slave)
        received = []
        stop = threading.Event()
        failure = []

        def serve():
            pending = b""
            try:
                while not stop.is_set() and len(received) < expect_commands:
                    if select.select([master], [], [], 0.02)[0]:
                        pending += os.read(master, 4096)
                        while b"\n" in pending:
                            line, pending = pending.split(b"\n", 1)
                            received.append(line + b"\n")
                            if response:
                                os.write(master, response[:2])
                                time.sleep(0.01)
                                os.write(master, response[2:])
            except OSError as exc:
                if not stop.is_set():
                    failure.append(exc)

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        try:
            yield port, received, master
        finally:
            stop.set()
            worker.join(1)
            os.close(master)
            os.close(slave)
            self.assertFalse(worker.is_alive())
            self.assertEqual(failure, [])

    def common(self, port):
        return ["--port", port, "--baud", "115200", "--log-root", str(self.directory / "logs")]

    def test_real_pty_exchange_preserves_binary_and_split_response(self):
        with self.peer(b"\xffOK\r\n") as (port, received, _):
            code, result = self.invoke("send", "--command", "status", "--ending", "crlf",
                                       "--execute", "--expect", "OK", "--wait", "0.2", *self.common(port))
        self.assertEqual(code, 0)
        self.assertTrue(result["expected_response_found"])
        self.assertEqual(received, [b"status\r\n"])
        logs = Path(result["log_dir"])
        self.assertEqual((logs / "rx.bin").read_bytes(), b"\xffOK\r\n")
        self.assertEqual((logs / "tx.bin").read_bytes(), b"status\r\n")
        events = [json.loads(line) for line in (logs / "events.jsonl").read_text().splitlines()]
        self.assertTrue(any(e["event"] == "rx" and e["phase"] == "response" for e in events))
        self.assertEqual(json.loads((logs / "result.json").read_text())["ok"], True)

    def test_silence_is_timeout_when_response_required(self):
        with self.peer() as (port, _, _):
            code, result = self.invoke("send", "--command", "status", "--execute",
                                       "--expect", "OK", "--wait", "0.1", *self.common(port))
        self.assertEqual(code, 3)
        self.assertEqual(result["error"]["code"], "RESPONSE_TIMEOUT")
        self.assertFalse(result["expected_response_found"])
        self.assertTrue(Path(result["log_dir"], "result.json").exists())

    def test_preexisting_data_does_not_count_as_response(self):
        master, slave = pty.openpty()
        try:
            with debug.Session(os.ttyname(slave), 115200, self.directory / "logs", 4096) as session:
                os.write(master, b"OLD_OK\n")
                session.capture(0.08, "before_send")
                session.write(b"status\n", "command")
                session.capture(0.06, "response")
                self.assertFalse(session.has_response("OK"))
                self.assertEqual(session.before_bytes, 7)
        finally:
            os.close(master)
            os.close(slave)

    def test_listen_preserves_bytes_without_transmitting(self):
        master, slave = pty.openpty()
        port = os.ttyname(slave)
        stop = threading.Event()
        def produce():
            # Repeat until the listener is open; output before open may be flushed by the OS.
            while not stop.wait(0.03):
                os.write(master, b"\x00\xff\n")
        worker = threading.Thread(target=produce, daemon=True)
        worker.start()
        try:
            code, result = self.invoke("listen", "--seconds", "0.2", *self.common(port))
        finally:
            stop.set()
            worker.join(1)
            os.close(master)
            os.close(slave)
        self.assertEqual(code, 0)
        self.assertGreater(result["rx_bytes"], 0)
        self.assertEqual(Path(result["log_dir"], "tx.bin").read_bytes(), b"")
        self.assertIn(b"\x00\xff\n", Path(result["log_dir"], "rx.bin").read_bytes())

    def test_note_releases_in_same_connection(self):
        with self.peer(b"ok\n", expect_commands=2) as (port, received, _):
            code, result = self.invoke("note", "--profile", str(self.profile()), "--note", "60",
                                       "--velocity", "100", "--hold", "0.04", "--wait", "0.1",
                                       "--execute", *self.common(port))
        self.assertEqual(code, 0)
        self.assertTrue(result["note_off_written"])
        self.assertEqual(received, [b"midi 90 3C 64\r\n", b"midi 80 3C 00\r\n"])

    def test_note_still_releases_after_capture_overflow(self):
        with self.peer(b"x" * 8192, expect_commands=2) as (port, received, _):
            code, result = self.invoke("note", "--profile", str(self.profile()), "--hold", "0.1",
                                       "--max-rx", "4096", "--execute", *self.common(port))
            deadline = time.monotonic() + 1
            while len(received) < 2 and time.monotonic() < deadline:
                time.sleep(0.01)
        self.assertEqual(code, 2)
        self.assertEqual(result["error"]["code"], "RX_LIMIT")
        self.assertTrue(result["note_off_written"])
        self.assertEqual(received[-1], b"midi 80 3C 00\r\n")

    def test_note_releases_when_process_is_terminated(self):
        with self.peer(expect_commands=2) as (port, received, _):
            proc = subprocess.Popen([sys.executable, str(SCRIPT), "note", "--profile", str(self.profile()),
                                     "--hold", "5", "--execute", *self.common(port)],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=self.env)
            try:
                deadline = time.monotonic() + 3
                while not received and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertTrue(received, "Note On was not received")
                proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=3)
                deadline = time.monotonic() + 1
                while len(received) < 2 and time.monotonic() < deadline:
                    time.sleep(0.01)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.communicate()
        self.assertEqual(stderr, "")
        result = json.loads(stdout)
        self.assertEqual(proc.returncode, 130)
        self.assertEqual(result["error"]["code"], "INTERRUPTED")
        self.assertTrue(result["note_off_written"])
        self.assertEqual(received[-1], b"midi 80 3C 00\r\n")

    def test_result_log_failure_is_not_reported_as_success(self):
        # Inject only filesystem failure; serial behavior remains a real PTY.
        master, slave = pty.openpty()
        try:
            args = debug.parser().parse_args(["listen", "--seconds", "0.01", *self.common(os.ttyname(slave))])
            with mock.patch.object(Path, "write_text", side_effect=OSError("test disk full")):
                code, result = debug.run(args)
            self.assertEqual(code, 2)
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"]["code"], "LOG_ERROR")
        finally:
            os.close(master)
            os.close(slave)

    def test_large_capture_has_bounded_stdout_preview(self):
        with self.peer(b"z" * 10000) as (port, _, _):
            code, result = self.invoke("send", "--command", "status", "--execute", "--wait", "0.15",
                                       *self.common(port))
        self.assertEqual(code, 0)
        self.assertTrue(result["response_preview_truncated"])
        self.assertLessEqual(len(result["response_text"]), 4096)
        self.assertEqual(Path(result["log_dir"], "rx.bin").stat().st_size, 10000)

    def test_alias_cannot_bypass_lock_and_lock_is_released(self):
        master, slave = pty.openpty()
        port = os.ttyname(slave)
        alias = self.directory / "serial-alias"
        alias.symlink_to(port)
        old = os.environ.get("ESP32_MIDI_DEBUG_LOCK_DIR")
        os.environ["ESP32_MIDI_DEBUG_LOCK_DIR"] = self.env["ESP32_MIDI_DEBUG_LOCK_DIR"]
        try:
            with debug.Session(port, 115200, self.directory / "logs", 4096):
                code, result = self.invoke("listen", "--seconds", "0.05", *self.common(str(alias)))
                self.assertEqual(code, 2)
                self.assertEqual(result["error"]["code"], "PORT_BUSY")
            code, result = self.invoke("listen", "--seconds", "0.02", *self.common(str(alias)))
            self.assertEqual(code, 0)
        finally:
            if old is None:
                os.environ.pop("ESP32_MIDI_DEBUG_LOCK_DIR", None)
            else:
                os.environ["ESP32_MIDI_DEBUG_LOCK_DIR"] = old
            os.close(master)
            os.close(slave)

    def test_receive_limit_reports_incomplete_capture(self):
        with self.peer(b"x" * 8192) as (port, _, _):
            code, result = self.invoke("send", "--command", "status", "--execute", "--wait", "0.2",
                                       "--max-rx", "4096", *self.common(port))
        self.assertEqual(code, 2)
        self.assertEqual(result["error"]["code"], "RX_LIMIT")
        self.assertEqual(Path(result["log_dir"], "rx.bin").stat().st_size, 4096)


if __name__ == "__main__":
    unittest.main()
