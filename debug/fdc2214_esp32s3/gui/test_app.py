"""Backend unit tests only: fake serial, no sockets, no physical device access."""
import csv
import importlib.util
import io
import json
from pathlib import Path
import queue
import sys
import threading
import time
import unittest

SPEC = importlib.util.spec_from_file_location("fdc_gui_backend", Path(__file__).with_name("app.py"))
app = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = app
SPEC.loader.exec_module(app)


def sample(sequence=1, *, epoch=1, device_us=None, raw=(100, 200, 300, 400),
           flags=(0, 0, 0, 0), mask=15, status_errors=0, skipped=0):
    if device_us is None:
        device_us = sequence * 10000
    values = ["sample", epoch, sequence, device_us, mask, "0x0040", hex(status_errors), 0, 0, skipped]
    for value, flag in zip(raw, flags):
        values += [value, flag]
    return (",".join(map(str, values)) + "\r\n").encode()


def ranges(channel=0, *, epoch=1, minimum=120, maximum=150, count=8, errors=2):
    return f"range,{epoch},{channel},{count},{errors},1,{minimum},{maximum},{maximum-minimum}\r\n".encode()


class Clock:
    def __init__(self):
        self.value = 0.0

    def __call__(self):
        return self.value

    def advance(self, seconds):
        self.value += seconds


class FakeSerial:
    def __init__(self, **kwargs):
        self.kwargs = kwargs
        self.dtr = self.rts = True
        self.port = kwargs.get("port")
        self.opened = self.closed = False
        self.chunks = queue.Queue()
        self.open_lines = None
        self.write_calls = []

    def open(self):
        self.open_lines = (self.dtr, self.rts, self.port)
        self.opened = True

    def close(self):
        self.closed = True

    def read(self, size):
        if self.closed:
            raise OSError("closed")
        try:
            chunk = self.chunks.get(timeout=0.02)
        except queue.Empty:
            return b""
        if isinstance(chunk, Exception):
            raise chunk
        return chunk[:size]

    def write(self, payload):
        self.write_calls.append(payload)
        raise AssertionError("backend must never send serial commands")


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.model = app.ViewerState(clock=self.clock)
        self.handles = []

        def factory(**kwargs):
            handle = FakeSerial(**kwargs)
            self.handles.append(handle)
            return handle

        self.port = "/dev/serial/by-id/usb-TEST-if00"
        self.reader = app.SerialReader(self.model, serial_factory=factory,
                                       port_provider=lambda: [{"path": self.port, "label": "Mock USB"}])
        self.api = app.Application(self.model, self.reader, port=8765)
        self.headers = {"Host": "127.0.0.1:8765", "Content-Type": "application/json"}

    def tearDown(self):
        self.reader.disconnect()
        self.assertTrue(all(not handle.write_calls for handle in self.handles))

    def request(self, method, path, data=None, headers=None):
        body = json.dumps(data).encode() if data is not None else b""
        result = self.api.handle(method, path, body, self.headers if headers is None else headers)
        return result, json.loads(result.body) if result.content_type.startswith("application/json") else result.body

    def connected(self):
        # Model-only connection for deterministic parsing/rate tests.
        self.model.connected_to(self.port)

    def wait_for(self, condition):
        limit = time.monotonic() + 1.0
        while time.monotonic() < limit:
            if condition():
                return
            time.sleep(0.005)
        self.fail("mock reader condition timed out")

    def test_default_and_get_do_not_open_serial(self):
        response, state = self.request("GET", "/api/state")
        self.assertEqual(response.status, 200)
        self.assertFalse(state["connected"])
        self.assertEqual(len(state["channels"]), 4)
        self.assertIsNone(state["channels"][0]["raw"])
        self.assertIsNone(state["sample_age_ms"])
        _, ports = self.request("GET", "/api/ports")
        self.assertEqual(ports["ports"][0]["path"], self.port)
        self.assertEqual(self.handles, [])

    def test_all_four_raw_values_survive_warning(self):
        self.connected()
        self.model.feed(sample(raw=(120, 240, 360, 480), flags=(1, 1, 1, 1), mask=0))
        state = self.model.state()
        self.assertEqual([c["raw"] for c in state["channels"]], [120, 240, 360, 480])
        self.assertEqual([c["flags"] for c in state["channels"]], [1] * 4)
        self.assertEqual([c["valid"] for c in state["channels"]], [False] * 4)
        self.assertEqual([c["observed_min"] for c in state["channels"]], [120, 240, 360, 480])
        self.assertTrue(all(c["valid_count"] is None for c in state["channels"]))

    def test_inconsistent_valid_mask_does_not_hide_warning(self):
        self.connected()
        self.model.feed(sample(flags=(0, 0, 1, 2), mask=15))
        state = self.model.state()
        self.assertEqual([c["valid"] for c in state["channels"]], [True, True, False, False])
        self.assertEqual(state["history"][0]["valid_mask"], 3)

    def test_status_error_masks_all_channels_without_hiding_raw(self):
        self.connected()
        self.model.feed(sample(status_errors=0x0800))
        state = self.model.state()
        self.assertEqual([c["raw"] for c in state["channels"]], [100, 200, 300, 400])
        self.assertTrue(all(not c["valid"] for c in state["channels"]))

    def test_observed_and_device_ranges_are_distinct(self):
        self.connected()
        self.model.feed(sample(raw=(100, 200, 300, 400)))
        self.model.feed(sample(2, raw=(1000, 20, 333, 380), flags=(1, 1, 1, 1), mask=0))
        self.model.feed(ranges(minimum=120, maximum=150, count=8, errors=99))
        first = self.model.state()["channels"][0]
        self.assertEqual((first["observed_min"], first["observed_max"], first["observed_span"]), (100, 1000, 900))
        self.assertEqual((first["device_min"], first["device_max"], first["device_span"]), (120, 150, 30))
        self.assertEqual((first["valid_count"], first["error_count"]), (8, 99))

    def test_empty_device_range_is_not_zero(self):
        self.connected()
        self.model.feed(b"range,1,3,0,15,0,,,\n")
        ch = self.model.state()["channels"][3]
        self.assertEqual((ch["valid_count"], ch["error_count"]), (0, 15))
        self.assertIsNone(ch["device_min"])

    def test_partial_lines_and_ansi(self):
        self.connected()
        payload = b"\x1b[0mfdc> " + sample(raw=(11, 22, 33, 44))
        self.model.feed(payload[:20])
        self.assertEqual(self.model.state()["sample_count"], 0)
        for offset in range(20, len(payload), 7):
            self.model.feed(payload[offset:offset + 7])
        self.assertEqual(self.model.state()["sample_count"], 1)
        self.assertEqual(self.model.state()["channels"][3]["raw"], 44)

    def test_bad_lines_and_oversize_line_are_bounded(self):
        self.connected()
        self.model.feed(b"ESP harmless boot message\nsample,1,broken\nrange,1,0,0\n")
        self.assertEqual(self.model.state()["malformed"], 2)
        self.model.feed(b"sample," + b"9" * (app.LINE_LIMIT + 50))
        self.assertLessEqual(len(self.model.partial), app.LINE_LIMIT)
        self.model.feed(b"more of bad line\n" + sample())
        self.assertEqual(self.model.state()["malformed"], 3)
        self.assertEqual(self.model.state()["sample_count"], 1)

    def test_unreasonably_large_integer_does_not_break_reader(self):
        self.connected()
        self.model.feed(sample(sequence=10 ** 100))
        self.assertEqual(self.model.state()["sample_count"], 0)
        self.assertEqual(self.model.state()["malformed"], 1)
        self.model.feed(sample())
        self.assertEqual(self.model.state()["sample_count"], 1)

    def test_epoch_change_clears_history_and_both_ranges(self):
        self.connected()
        self.model.feed(sample())
        self.model.feed(ranges())
        before = self.model.state()["session"]
        self.model.feed(sample(3, epoch=2, raw=(5, 6, 7, 8)))
        state = self.model.state()
        self.assertGreater(state["session"], before)
        self.assertEqual(state["epoch"], 2)
        self.assertEqual(len(state["history"]), 1)
        self.assertEqual(state["channels"][0]["observed_min"], 5)
        self.assertIsNone(state["channels"][0]["device_min"])

    def test_epoch_event_clears_before_first_new_sample(self):
        self.connected()
        self.model.feed(sample())
        self.model.feed(b"event,12345,epoch,2\n")
        state = self.model.state()
        self.assertEqual(state["epoch"], 2)
        self.assertEqual(state["history"], [])
        self.assertIsNone(state["channels"][0]["raw"])

    def test_sequence_rollback_starts_new_session(self):
        self.connected()
        self.model.feed(sample(100))
        self.model.feed(ranges())
        before = self.model.state()["session"]
        self.model.feed(sample(2, raw=(1, 2, 3, 4)))
        state = self.model.state()
        self.assertGreater(state["session"], before)
        self.assertEqual(state["sample_count"], 1)
        self.assertIsNone(state["channels"][0]["device_min"])

    def test_boot_clears_previous_epoch(self):
        self.connected()
        self.model.feed(sample())
        self.model.feed(b"ESP-ROM:esp32s3-20210327\n")
        state = self.model.state()
        self.assertIsNone(state["epoch"])
        self.assertEqual(state["history"], [])

    def test_history_is_bounded_observed_range_is_not_sliding(self):
        self.connected()
        for sequence in range(1, 650):
            self.model.feed(sample(sequence, raw=(sequence, 200, 300, 400)))
        state = self.model.state()
        self.assertEqual(len(state["history"]), 600)
        self.assertEqual(state["sample_count"], 649)
        self.assertEqual(state["channels"][0]["observed_min"], 1)
        self.assertEqual(state["channels"][0]["observed_max"], 649)
        self.assertLessEqual(len(self.model.rates), 100)

    def test_gui_clear_keeps_device_ranges_and_sends_no_command(self):
        self.connected()
        self.model.feed(sample(100))
        self.model.feed(ranges())
        before = self.model.state()["session"]
        response, state = self.request("POST", "/api/clear", {})
        self.assertEqual(response.status, 200)
        self.assertGreater(state["session"], before)
        self.assertEqual(state["history"], [])
        self.assertIsNone(state["channels"][0]["observed_min"])
        self.assertEqual(state["channels"][0]["device_min"], 120)
        self.assertEqual(state["channels"][0]["valid_count"], 8)
        self.assertEqual(self.handles, [])
        # A local clear must not suppress detection of a later MCU rollback.
        self.model.feed(sample(2))
        self.assertIsNone(self.model.state()["channels"][0]["device_min"])

    def test_visible_rate_and_device_rate_are_not_conflated(self):
        self.connected()
        self.model.feed(sample(10, device_us=1000000, skipped=13))
        self.clock.advance(0.1)
        self.model.feed(sample(24, device_us=1100000, skipped=13))
        state = self.model.state()
        self.assertAlmostEqual(state["visible_rate_hz"], 10)
        self.assertAlmostEqual(state["device_rate_hz"], 140)
        self.assertEqual(state["output_skipped"], 26)

    def test_disconnect_and_staleness_keep_raw_but_invalidate_current_data(self):
        self.connected()
        self.model.feed(sample())
        self.assertTrue(self.model.state()["channels"][0]["valid"])
        self.clock.advance(2.1)
        state = self.model.state()
        self.assertTrue(state["connected"])
        self.assertTrue(state["stale"])
        self.assertFalse(state["channels"][0]["valid"])
        self.assertGreater(state["sample_age_ms"], 2000)
        self.model.disconnected("unplugged")
        state = self.model.state()
        self.assertFalse(state["connected"])
        self.assertEqual(state["channels"][0]["raw"], 100)
        self.assertEqual(state["error"], "unplugged")

    def test_csv_export_has_all_four_raw_flags_and_mask(self):
        self.connected()
        self.model.feed(sample(flags=(0, 1, 2, 3), mask=1))
        response, body = self.request("GET", "/api/export.csv")
        self.assertEqual(response.status, 200)
        rows = list(csv.DictReader(io.StringIO(body.decode())))
        self.assertEqual(len(rows), 1)
        self.assertEqual([rows[0][f"ch{i}"] for i in range(4)], ["100", "200", "300", "400"])
        self.assertEqual([rows[0][f"flags{i}"] for i in range(4)], ["0", "1", "2", "3"])
        self.assertEqual(rows[0]["valid_mask"], "1")

    def test_mock_connect_sets_lines_before_open_and_reads_only(self):
        response, state = self.request("POST", "/api/connect", {"port": self.port})
        self.assertEqual(response.status, 200)
        handle = self.handles[0]
        self.assertEqual(handle.open_lines, (False, False, self.port))
        self.assertEqual(handle.kwargs["timeout"], 0.1)
        handle.chunks.put(sample(raw=(12, 34, 56, 78)))
        self.wait_for(lambda: self.model.state()["sample_count"] == 1)
        response, state = self.request("POST", "/api/disconnect", {})
        self.assertEqual(response.status, 200)
        self.assertFalse(state["connected"])
        self.assertTrue(handle.closed)
        self.assertIsNone(self.reader.thread)

    def test_read_error_marks_disconnect_and_reconnect_resets_session(self):
        self.reader.connect(self.port)
        first = self.handles[0]
        first.chunks.put(sample())
        self.wait_for(lambda: self.model.state()["sample_count"] == 1)
        first.chunks.put(OSError("mock unplug"))
        self.wait_for(lambda: not self.model.state()["connected"])
        old_session = self.model.state()["session"]
        self.assertEqual(self.model.state()["channels"][0]["raw"], 100)
        self.reader.connect(self.port)
        self.assertGreater(self.model.state()["session"], old_session)
        self.assertEqual(self.model.state()["history"], [])
        self.assertTrue(first.closed)

    def test_invalid_ports_and_paths_are_never_opened(self):
        for port in ("/etc/passwd", "/dev/ttyS0", "../../secret", "http://remote", ""):
            response, body = self.request("POST", "/api/connect", {"port": port})
            self.assertEqual(response.status, 400)
            self.assertIn("error", body)
        self.assertEqual(self.handles, [])
        for path in ("/../sdkconfig", "/%2e%2e/sdkconfig", "/static/../../etc/passwd", "/app.py"):
            response, _ = self.request("GET", path)
            self.assertEqual(response.status, 404)

    def test_json_limits_content_type_and_cross_origin(self):
        response = self.api.handle("POST", "/api/clear", b"x" * (app.REQUEST_LIMIT + 1), self.headers)
        self.assertEqual(response.status, 413)
        response = self.api.handle("POST", "/api/clear", b"{", self.headers)
        self.assertEqual(response.status, 400)
        response = self.api.handle("POST", "/api/clear", b"[]", self.headers)
        self.assertEqual(response.status, 400)
        response = self.api.handle("POST", "/api/clear", b"{}", {"Host": "127.0.0.1:8765", "Content-Type": "text/plain"})
        self.assertEqual(response.status, 415)
        bad = {**self.headers, "Origin": "http://example.org"}
        response, _ = self.request("POST", "/api/clear", {}, bad)
        self.assertEqual(response.status, 403)
        response, _ = self.request("GET", "/api/state", headers={**self.headers, "Host": "evil.example:8765"})
        self.assertEqual(response.status, 403)
        response, _ = self.request("OPTIONS", "/api/connect")
        self.assertEqual(response.status, 405)

    def test_only_twelve_events_are_retained(self):
        for number in range(25):
            self.model.feed(f"event,{number},RUNNING,example\n".encode())
        self.assertEqual(len(self.model.state()["events"]), 12)

    def test_latest_successful_log_replay(self):
        log = app.ROOT / "verification/ground_retest_20261029T222633/serial.log"
        if not log.is_file():
            self.skipTest("optional real log fixture not present")
        self.connected()
        expected = []
        for line in log.read_text(errors="replace").splitlines():
            record = app.parse_record(line)
            if record and record[0] == "sample":
                expected.append(record)
            self.clock.advance(0.01)
            self.model.feed((line + "\n").encode())
        state = self.model.state()
        self.assertGreaterEqual(len(expected), 100)
        self.assertEqual(state["sample_count"], len(expected))
        self.assertEqual([c["raw"] for c in state["channels"]], [int(expected[-1][i]) for i in (10, 12, 14, 16)])
        self.assertEqual([c["flags"] for c in state["channels"]], [int(expected[-1][i]) for i in (11, 13, 15, 17)])
        self.assertGreater(state["device_rate_hz"], 120)
        self.assertLess(state["device_rate_hz"], 160)


if __name__ == "__main__":
    unittest.main(verbosity=2)
