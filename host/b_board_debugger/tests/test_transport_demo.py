import copy
import json
import os
import select
import threading
import time
import unittest
from unittest.mock import patch

from host.b_board_debugger.config import DEFAULT_CONFIG
from host.b_board_debugger.controller import Controller
from host.b_board_debugger.transport import Inbox, SerialWorker, open_serial


def pump(controller, predicate, timeout=3):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        controller.poll()
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError(f'timeout: {controller.message}')


class DemoTests(unittest.TestCase):
    def test_demo_uses_real_worker_protocol_ack_save_and_twelve_channels(self):
        controller = Controller()
        try:
            # A demo cannot accidentally enumerate/open a physical port.
            with patch('host.b_board_debugger.transport.open_serial', side_effect=AssertionError('real port')):
                controller.connect(demo=True)
                pump(controller, lambda: controller.ready)
                controller.edit({'volume': 87, 'gains': [150] * 12})
                controller.apply()
                self.assertEqual(controller.config['volume'], 100)
                pump(controller, lambda: not controller.writing)
                self.assertEqual(controller.config['volume'], 87)
                controller.strike(2, 500)
                pump(controller, lambda: any(r['synthetic'] and r['channel'] == 2
                                             for r in controller.events.rows))
                row = next(r for r in controller.events.rows if r['synthetic'])
                self.assertEqual((row['velocity'], row['note'], row['revision']), (24, 61, 1))
                controller.save()
                self.assertTrue(controller.writing)
                pump(controller, lambda: not controller.writing)
                self.assertIn('保存完成', controller.message)
                pump(controller, lambda: len({r['channel'] for r in controller.events.rows}) == 12, timeout=4)
                self.assertEqual(controller.state['drops']['events'], 0)
                self.assertEqual(controller.state['timing']['missed_periods'], 0)
        finally:
            worker = controller.worker
            controller.disconnect()
            if worker:
                worker._thread.join(1)

    def test_open_sets_modem_lines_before_open_and_timeouts_are_bounded(self):
        observations = []
        class Port:
            def __init__(self, **kwargs):
                self.kwargs = kwargs
                self.dtr = self.rts = True
            def open(self):
                observations.append((self.dtr, self.rts, self.kwargs))
            def close(self):
                pass
        with patch('serial.Serial', Port):
            port = open_serial('test-only')
        self.assertEqual(observations[0][:2], (False, False))
        self.assertIsNone(port.kwargs['port'])
        self.assertGreater(port.kwargs['timeout'], 0)
        self.assertGreater(port.kwargs['write_timeout'], 0)
        if os.name == 'posix':
            self.assertTrue(port.kwargs['exclusive'])


@unittest.skipUnless(os.name == 'posix', 'PTY integration requires POSIX')
class PtyTests(unittest.TestCase):
    def test_real_pyserial_handshake_split_replies_and_hup_disconnect(self):
        master, slave = os.openpty()
        path = os.ttyname(slave)
        stop = threading.Event()
        commands = []
        errors = []
        def device():
            buffer = b''
            try:
                while not stop.is_set():
                    readable, _, _ = select.select([master], [], [], 0.05)
                    if not readable:
                        continue
                    buffer += os.read(master, 4096)
                    while b'\n' in buffer:
                        line, buffer = buffer.split(b'\n', 1)
                        parts = line.decode('ascii').split()
                        if not parts or parts[0] != 'rpc':
                            continue
                        commands.append(parts)
                        reply = dict(v=1, type='reply', id=int(parts[1]), ok=True, code='ok', revision=3)
                        if parts[3] in ('ping', 'get'):
                            reply['config'] = copy.deepcopy(DEFAULT_CONFIG)
                        if parts[3] == 'status':
                            reply['state'] = dict(v=1, type='state', uptime_ms=200, revision=3, events=[])
                        frame = b'echo\r\nharp> \x1b[0m@B ' + json.dumps(reply).encode() + b'\r\n'
                        os.write(master, frame[:29])
                        time.sleep(0.002)
                        os.write(master, frame[29:])
            except OSError as exc:
                if not stop.is_set():
                    errors.append(exc)
        thread = threading.Thread(target=device, daemon=True)
        controller = Controller()
        try:
            thread.start()
            controller.connect(path)
            pump(controller, lambda: controller.ready)
            self.assertEqual([p[3] for p in commands], ['ping', 'get', 'status', 'stream'])
            self.assertEqual(controller.revision, 3)
            self.assertFalse(errors)
            stop.set()
            thread.join(1)
            os.close(master)
            master = -1
            pump(controller, lambda: not controller.connected)
            self.assertIn('连接中断', controller.message)
        finally:
            stop.set()
            worker = controller.worker
            controller.disconnect()
            thread.join(1)
            if worker:
                worker._thread.join(1)
            if master >= 0:
                os.close(master)
            os.close(slave)


if __name__ == '__main__':
    unittest.main()
