"""Exercise USB-UART loss and MCU restarts with real pyserial on PTYs."""
import copy
import json
import os
from pathlib import Path
import select
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from host.b_board_debugger.config import DEFAULT_CONFIG
from host.b_board_debugger.web_monitor import SerialMonitor, make_server


def wait_for(predicate, timeout=4):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.01)
    raise AssertionError('monitor did not recover before deadline')


class PtyBoard:
    """No physical device: repeat full v1 telemetry and answer read-only RPCs."""
    def __init__(self):
        self.master, self.slave = os.openpty()
        self.path = os.ttyname(self.slave)
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.commands = []
        self.config = copy.deepcopy(DEFAULT_CONFIG)
        self.silent = False
        self.offset = 10000
        self.started = time.monotonic()
        self.events = [[1, 10000, 1, 100, 109, 60, False, 0]]
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def snapshot(self):
        return dict(v=1, type='state', revision=0,
                    uptime_ms=(self.offset + int((time.monotonic()-self.started)*1000)) & 0xffffffff,
                    lifecycle='running', raw=1, shade=1, playing=0,
                    duration=[100]*12, velocity=[109]*12, notes=list(range(60, 72)), hits=[1]*12,
                    controls=dict(transpose=0, volume=100, fine=0, cutoff=127, resonance=0),
                    links=dict(usb=True, ble=False, selected=1, usb_sent=0, ble_sent=0,
                               usb_retries=0, ble_retries=0, ble_errors=0),
                    timing=dict(last_gap_us=1000, max_gap_us=1001, max_work_us=30, missed_periods=0),
                    drops=dict(commands=0, replies=0, events=0), illegal_edges=0,
                    encoder_enabled=15, ab=[3]*4, pressed=0, heap_free=180000, events=self.events)

    def emit(self, frame):
        os.write(self.master, b'harp> @B ' + json.dumps(frame).encode() + b'\r\n')

    def run(self):
        buffer = b''
        try:
            while not self.stop_event.is_set():
                readable, _, _ = select.select([self.master], [], [], 0.03)
                if readable:
                    buffer += os.read(self.master, 4096)
                with self.lock:
                    while b'\n' in buffer:
                        line, buffer = buffer.split(b'\n', 1)
                        parts = line.decode(errors='ignore').strip().split()
                        if len(parts) < 4 or parts[0] != 'rpc':
                            continue
                        self.commands.append(parts)
                        if self.silent:
                            continue
                        reply = dict(v=1, type='reply', id=int(parts[1]), ok=True, code='ok', revision=0)
                        if parts[3] in ('get', 'ping'):
                            reply['config'] = self.config
                        if parts[3] == 'status':
                            reply['state'] = self.snapshot()
                        self.emit(reply)
                    if not self.silent:
                        self.emit(self.snapshot())
        except OSError:
            if not self.stop_event.is_set():
                raise

    def close(self):
        self.stop_event.set()
        self.thread.join(1)
        os.close(self.master)
        os.close(self.slave)


@unittest.skipUnless(os.name == 'posix', 'PTY integration requires POSIX')
class WebMonitorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='harp-web-test-')
        self.root = Path(self.temp.name)
        self.port = self.root/'stable-uart'
        self.boards = []
        self.monitor = SerialMonitor(str(self.port), self.root/'capture', retry_seconds=0.08,
                                     stale_seconds=0.3, query_seconds=0.15)

    def tearDown(self):
        self.monitor.stop()
        self.monitor.close()
        for board in self.boards:
            if not board.stop_event.is_set():
                board.close()
        self.temp.cleanup()

    def attach(self):
        board = PtyBoard()
        self.boards.append(board)
        self.port.symlink_to(board.path)
        return board

    def online(self):
        return wait_for(lambda: (s if (s := self.monitor.snapshot())['connected'] else None))

    def test_usb_unplug_and_replug_recovers_same_path_and_new_event_sequence(self):
        first = self.attach()
        self.monitor.start()
        before = self.online()
        wait_for(lambda: self.monitor.snapshot()['events'])
        self.port.unlink()
        first.close()  # Real HUP: pyserial.read raises SerialException.
        wait_for(lambda: not self.monitor.snapshot()['connected'])
        self.assertTrue(self.monitor.snapshot()['running'])
        self.assertIsNotNone(self.monitor.snapshot()['last_error'])
        self.attach()
        after = self.online()
        self.assertGreater(after['session_id'], before['session_id'])
        rows = wait_for(lambda: (rows if len(rows := self.monitor.snapshot()['events']) == 2 else None))
        self.assertEqual([r['seq'] for r in rows], [1, 1])
        self.assertNotEqual(rows[0]['session'], rows[1]['session'])
        self.assertEqual(after['config']['channel'], 2)

    def test_mcu_reboot_without_uart_disconnect_refreshes_config_and_sequences(self):
        board = self.attach()
        self.monitor.start()
        before = self.online()
        wait_for(lambda: self.monitor.snapshot()['events'])
        with board.lock:
            board.offset = 0
            board.started = time.monotonic()
            board.config['polarity'] = 0
            board.events = [[1, 0, 2, 260, 72, 61, False, 0]]
        wait_for(lambda: self.monitor.snapshot()['session_id'] > before['session_id'])
        after = self.online()
        self.assertEqual(after['config']['polarity'], 0)
        self.assertEqual(after['connection_count'], 1)
        self.assertEqual([r['channel'] for r in after['events']], [1, 2])

    def test_silent_uart_resyncs_without_reopening_and_manual_stop_stays_stopped(self):
        board = self.attach()
        self.monitor.start()
        self.online()
        with board.lock:
            board.silent = True
        wait_for(lambda: not self.monitor.snapshot()['connected'])
        with board.lock:
            board.silent = False
        self.assertEqual(self.online()['connection_count'], 1)
        self.assertTrue(self.monitor.stop())
        self.assertFalse(self.monitor.snapshot()['running'])
        count = self.monitor.snapshot()['connection_count']
        time.sleep(0.25)
        self.assertEqual(self.monitor.snapshot()['connection_count'], count)
        self.monitor.start()
        self.assertEqual(self.online()['connection_count'], count + 1)
        # The monitor may never send apply, save, strike or panic.
        self.assertLessEqual({c[3] for c in board.commands}, {'get', 'ping', 'status', 'stream'})

    def test_uptime_counter_wrap_keeps_current_session(self):
        board = self.attach()
        with board.lock:
            board.offset = 0xfffff000
        self.monitor.start()
        before = self.online()
        with board.lock:
            board.offset = 100
            board.started = time.monotonic()
        wait_for(lambda: self.monitor.snapshot()['last_state']['uptime_ms'] < 10000)
        self.assertEqual(self.monitor.snapshot()['session_id'], before['session_id'])

    def test_http_remains_usable_without_device_and_supports_stop_resume(self):
        server = make_server(self.monitor, port=0)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f'http://127.0.0.1:{server.server_port}'
        def request(path, method='GET', origin=None):
            req = Request(base+path, method=method, headers={'Origin': origin} if origin else {})
            with urlopen(req, timeout=2) as response:
                return response.read()
        try:
            self.monitor.start()
            self.assertIn('实时输入监控', request('/').decode())
            self.assertFalse(json.loads(request('/live.json'))['connected'])
            self.assertIn(b'session', request('/events.csv'))
            with self.assertRaises(HTTPError) as exc:
                request('/stop', 'POST', 'https://unrelated.example')
            self.assertEqual(exc.exception.code, 403)
            self.assertTrue(json.loads(request('/stop', 'POST'))['ok'])
            self.assertFalse(json.loads(request('/live.json'))['running'])
            self.attach()
            self.assertTrue(json.loads(request('/start', 'POST'))['ok'])
            self.online()
            self.assertTrue(json.loads(request('/live.json'))['connected'])
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)


if __name__ == '__main__':
    unittest.main()
