#!/usr/bin/env python3
"""No device/network access: actual client with a deterministic HTTP boundary."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch, Mock

spec = importlib.util.spec_from_file_location("midi_debug", Path(__file__).resolve().parents[2]/"tools/midi_debug.py")
work = Path(__file__).resolve().parents[2] / "build/host-tests/work"
work.mkdir(parents=True, exist_ok=True)
debug = importlib.util.module_from_spec(spec)
spec.loader.exec_module(debug)


class Clock:
    def __init__(self):
        self.now = 100.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FirmwareBoundary:
    def __init__(self):
        self.context, self.sent, self.dropped, self.owner = 7, 0, 0, 0
        self.sequence, self.reset = 0, 2
        self.mode, self.raw_calls, self.renew_calls = "ok", 0, 0
        self.calls = []

    def exchange(self, method, path, body, headers):
        self.calls.append((method, path, body, headers))
        assert headers["X-FPGA-Control"] == "1"
        if path == "/api/debug/midi" and method == "GET":
            return 200, {"version": 1, "max_bytes": 256, "ack_scope": "fpga_raw_byte",
                         "ready": True, "context_epoch": self.context, "reset_count": self.reset,
                         "web": {"session": self.owner, "sent_bytes": self.sent,
                                 "dropped_bytes": self.dropped, "queued_bytes": 0, "fault": 0}}
        if path == "/api/v2/session":
            assert body == b""
            return 201, {"api_major": 2, "token": 77, "client": 0,
                         "context_epoch": self.context, "lease_ms": 1200}
        if path == "/api/v2/state":
            return 200, {"context_epoch": self.context, "mode": {"applied": 0},
                         "clients": [{"token": 77, "result_sequence": self.sequence,
                                      "result_code": 0, "result_context_epoch": self.context}]}
        if path == "/api/v2/command":
            parts = body.decode().split()
            assert parts[0] == "77" and parts[2:] == ["7", "3", "0", "100", "0"]
            self.sequence = int(parts[1])
            self.renew_calls += 1
            return 202, {"accepted": self.sequence}
        if path == "/api/debug/midi" and method == "POST":
            data = json.loads(body)
            self.raw_calls += 1
            assert data["token"] == 77 and data["context_epoch"] == 7
            assert data["sequence"] > self.sequence
            self.sequence = data["sequence"]
            self.owner = 77
            if self.mode == "busy":
                return 409, {"code": "raw_source_busy"}
            if self.mode == "io_error":
                raise TimeoutError("ambiguous response")
            if self.mode != "timeout":
                self.sent = (self.sent + len(data["bytes"])) & 0xffffffff
            if self.mode == "context":
                self.context += 1
            if self.mode == "drop":
                self.dropped += 1
            if self.mode == "reset":
                self.reset += 1
            if self.mode == "foreign":
                self.owner = 99
            return 202, {"accepted": True, "queued_bytes": len(data["bytes"])}
        raise AssertionError((method, path))


class Tests(unittest.TestCase):
    def setUp(self):
        self.clock, self.fw = Clock(), FirmwareBoundary()
        self.client = debug.MidiDebugClient("http://192.168.4.1", exchange=self.fw.exchange,
                                           clock=self.clock.time, sleep=self.clock.sleep)

    def test_status_does_not_open_session(self):
        self.client.status()
        self.assertEqual(len(self.fw.calls), 1)
        self.assertEqual(self.fw.calls[0][:2], ("GET", "/api/debug/midi"))

    def test_real_protocol_and_256_byte_limit(self):
        self.client.open()
        self.client.send(list(range(256)))
        self.assertEqual(self.fw.sent, 256)
        self.assertEqual(self.client.report["events"][0]["stage"], "fpga_byte_ack")
        self.assertFalse(self.client.report["pcm_capture"])
        self.assertEqual(self.fw.raw_calls, 1)

    def test_replay_wait_renews_and_preserves_order(self):
        self.client.open()
        self.client.replay([{"at_ms": 0, "bytes": [0x90, 60, 80]},
                            {"at_ms": 1600, "bytes": [0x80, 60, 0]}])
        self.assertGreaterEqual(self.fw.renew_calls, 5)
        events = self.client.report["events"]
        self.assertEqual([x["bytes"][0] for x in events], [0x90, 0x80])
        self.assertGreaterEqual(events[1]["dispatch_ms"]-events[0]["dispatch_ms"], 1600-1e-6)

    def test_counter_wrap(self):
        self.fw.sent = 0xfffffffe
        self.client.open()
        self.client.send([0x90, 60, 80])
        self.assertEqual(self.fw.sent, 1)

    def test_failure_modes_stop_without_replay(self):
        for mode in ("busy", "io_error", "context", "drop", "reset", "foreign", "timeout"):
            with self.subTest(mode=mode):
                self.setUp()
                self.client.open()
                self.fw.mode = mode
                with self.assertRaises(debug.DebugError):
                    self.client.send([0x90, 60, 80])
                self.assertEqual(self.fw.raw_calls, 1)
                self.assertNotEqual(self.client.report["events"][0]["stage"], "fpga_byte_ack")

    def test_invalid_data_never_sent(self):
        for data in ([], [256], [-1], [True], [1.0], [0]*257, "90 3c 50"):
            with self.subTest(data=str(data)[:30]):
                with self.assertRaises(ValueError):
                    self.client.send(data)
        self.assertFalse(self.fw.calls)

    def test_foreign_owner_not_claimed(self):
        self.fw.owner = 88
        with self.assertRaises(debug.DebugError):
            self.client.open()
        self.assertEqual(len(self.fw.calls), 1)

    def test_sequence_exhausted(self):
        self.client.open()
        self.client.sequence = 0xffffffff
        with self.assertRaises(debug.DebugError):
            self.client.send([0])
        self.assertEqual(self.fw.raw_calls, 0)

    def test_url_validation(self):
        for url in ("https://host", "http://host/path", "http://user:pass@host", "file:///tmp/x",
                    "http://host/?x=1", "http://host/#fragment"):
            with self.assertRaises(ValueError):
                debug.MidiDebugClient(url)

    def test_load_entire_replay_before_sending(self):
        with tempfile.TemporaryDirectory(dir=work) as folder:
            path = Path(folder)/"events.json"
            good = [{"at_ms": 0, "bytes": [144, 60, 80]}, {"at_ms": 500, "bytes": [128, 60, 0]}]
            path.write_text(json.dumps(good))
            events, digest = debug.load_events(path)
            self.assertEqual(events, good)
            self.assertEqual(len(digest), 64)
            for bad in ([], [{"at_ms": True, "bytes": [0]}], [{"at_ms": 0, "bytes": [True]}],
                        [{"at_ms": 10, "bytes": [0]}, {"at_ms": 0, "bytes": [0]}],
                        [{"at_ms": 600001, "bytes": [0]}], [{"at_ms": 0, "bytes": [0], "extra": 1}]):
                path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):
                    debug.load_events(path)

    def test_http_boundary_headers_no_proxy_no_retry_and_close(self):
        connection = Mock()
        response = Mock(status=503)
        response.read.return_value = b'{"code":"busy"}'
        connection.getresponse.return_value = response
        with patch.object(debug.http.client, "HTTPConnection", return_value=connection) as ctor:
            client = debug.MidiDebugClient("http://127.0.0.1:8000")
            with self.assertRaises(debug.DebugError):
                client.status()
            ctor.assert_called_once_with("127.0.0.1", 8000, timeout=0.4)
            connection.request.assert_called_once()
            connection.close.assert_called_once()

    def test_cli_note_writes_trace_and_note_off(self):
        with tempfile.TemporaryDirectory(dir=work) as folder:
            path = Path(folder)/"trace.json"
            with patch.object(debug, "MidiDebugClient", return_value=self.client):
                self.assertEqual(debug.main(["--report", str(path), "note", "60", "--duration-ms", "1500"]), 0)
            trace = json.loads(path.read_text())
            self.assertEqual([x["bytes"] for x in trace["events"]], [[144, 60, 80], [128, 60, 0]])
            self.assertEqual(trace["status"], "PASS")
            self.assertIn("lease", trace["release"])
            with patch.object(debug, "MidiDebugClient", return_value=self.client):
                with self.assertRaises(FileExistsError):
                    debug.main(["--report", str(path), "status"])

    def test_cli_failure_keeps_trace_and_attempts_note_off(self):
        with tempfile.TemporaryDirectory(dir=work) as folder:
            path = Path(folder)/"failure.json"
            self.fw.mode = "io_error"
            with patch.object(debug, "MidiDebugClient", return_value=self.client):
                self.assertEqual(debug.main(["--report", str(path), "note", "60"]), 1)
            trace = json.loads(path.read_text())
            self.assertEqual(trace["status"], "FAIL")
            self.assertEqual(len(trace["events"]), 2)
            self.assertIn("note_off_error", trace)


if __name__ == "__main__":
    unittest.main()
