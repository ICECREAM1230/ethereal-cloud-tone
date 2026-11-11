"""Loopback web monitor with one UART owner and automatic read-only reconnect.

Run from the repository root: python3 -m host.b_board_debugger.web_monitor --help
"""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import signal
import threading
import time

from .config import ConfigError, validate_config
from .events import EventLog
from .protocol import FrameParser, encode_request
from .transport import open_serial


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec='milliseconds')


class SerialMonitor:
    """Only the worker opens/closes UART; HTTP threads read snapshots/control it."""
    def __init__(self, port, output, *, retry_seconds=1, stale_seconds=2, query_seconds=1):
        self.port, self.output = port, Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.retry_seconds, self.stale_seconds, self.query_seconds = retry_seconds, stale_seconds, query_seconds
        self._lock = threading.RLock()
        self._control = threading.Lock()
        self._stop = threading.Event()
        self._thread = None
        self._running = self._port_open = False
        self._last_seen = self._last_seen_at = self._last_error = None
        self._state = self._config = self._config_revision = self._last_uptime = None
        self._session = self._connections = self._states = self._bad_frames = 0
        self._request_id = 0
        self._pending = {}
        self._events = EventLog(max_rows=4096)
        self._started = time.monotonic()
        self._log = logging.Logger(f'harp-web-{id(self)}')
        self._handler = RotatingFileHandler(self.output/'frames.jsonl', maxBytes=4*1024*1024,
                                            backupCount=3, encoding='utf-8')
        self._log.addHandler(self._handler)

    def _record(self, kind, **fields):
        self._log.info(json.dumps(dict(at=utc_now(), kind=kind, session=self._session, **fields),
                                  ensure_ascii=False))

    def snapshot(self):
        with self._lock:
            age = None if self._last_seen is None else time.monotonic()-self._last_seen
            connected = self._connected()
            status = ('stopped' if not self._running else 'online' if connected
                      else 'waiting_for_data' if self._port_open else 'reconnecting')
            return dict(pid=os.getpid(), running=self._running, connected=connected,
                        connection_state=status, port=self.port, session_id=self._session,
                        connection_count=self._connections, updated_at=utc_now(),
                        last_seen_at=self._last_seen_at, state_age_seconds=age,
                        last_error=self._last_error, elapsed_seconds=round(time.monotonic()-self._started, 2),
                        states=self._states, parser_bad_frames=self._bad_frames,
                        config=copy.deepcopy(self._config), last_state=copy.deepcopy(self._state),
                        events=list(self._events.rows)[-100:], event_total=self._events.total,
                        event_evicted=self._events.evicted, event_invalid=self._events.invalid,
                        configuration_changed=False, software_strike_sent=False)

    def _connected(self):
        return bool(self._running and self._port_open and self._config is not None
                    and self._last_seen is not None and time.monotonic()-self._last_seen < self.stale_seconds)

    def _save(self):
        tmp = self.output/'live.tmp'
        tmp.write_text(json.dumps(self.snapshot(), ensure_ascii=False)+'\n', encoding='utf-8')
        tmp.replace(self.output/'live.json')

    def start(self):
        with self._control:
            if self._thread and self._thread.is_alive():
                return self._running
            self._stop = threading.Event()
            with self._lock:
                self._running = True
            self._thread = threading.Thread(target=self._run, name='harp-web-uart', daemon=True)
            self._thread.start()
            return True

    def stop(self):
        with self._control:
            self._stop.set()
            with self._lock:
                self._running = False
            if self._thread:
                self._thread.join(3)
            return not (self._thread and self._thread.is_alive())

    def close(self):
        if not self.stop():
            raise RuntimeError('UART worker has not stopped')
        self._log.removeHandler(self._handler)
        self._handler.close()

    def _new_session(self):
        # Called for both a new UART handle and an observed MCU uptime rollback.
        self._session += 1
        self._state = self._config = self._config_revision = self._last_uptime = None
        self._last_seen = self._last_seen_at = None
        self._pending.clear()
        self._events.new_connection(self._session)

    def _consume(self, frame):
        reboot = False
        with self._lock:
            state = frame if frame['type'] == 'state' else frame.get('state')
            if isinstance(state, dict) and state.get('timing', {}).get('last_gap_us'):
                uptime = state.get('uptime_ms')
                if type(uptime) is not int or not 0 <= uptime <= 0xffffffff:
                    self._bad_frames += 1
                    return False
                previous = self._last_uptime
                if previous is not None and uptime < previous and ((uptime-previous) & 0xffffffff) >= 0x80000000:
                    self._new_session()
                    self._record('mcu_restart')
                    reboot = True
                self._last_uptime = uptime
                self._state, self._last_seen, self._last_seen_at = state, time.monotonic(), utc_now()
                self._states += 1
                if self._config_revision != state.get('revision'):
                    self._config = None  # Refresh configuration after device-side edits too.
                for event in state.get('events', [])[:8]:
                    self._events.add(event)
            if frame['type'] == 'reply':
                verb = self._pending.pop(frame.get('id'), None)
                if verb in ('get', 'ping') and frame.get('ok') is True and 'config' in frame:
                    try:
                        self._config = validate_config(frame['config'])
                        self._config_revision = frame.get('revision')
                    except ConfigError as exc:
                        self._last_error = str(exc)
                        self._bad_frames += 1
            if self._config is not None and self._last_seen is not None:
                self._last_error = None
        self._record('frame', frame=frame)
        return reboot

    def _query(self, device):
        # New IDs reject late replies from an earlier connection/boot/handshake.
        self._pending.clear()
        for verb, args in (('ping', ''), ('get', ''), ('status', ''), ('stream', '200')):
            if self._stop.is_set():
                return
            self._request_id = self._request_id % 0xffffffff + 1
            self._pending[self._request_id] = verb
            device.write(encode_request(self._request_id, -1, verb, args))

    def _read_connection(self, device):
        parser, next_query, next_save = FrameParser(), 0, 0
        while not self._stop.is_set():
            now = time.monotonic()
            with self._lock:
                needs_query = not self._connected()
            if now >= next_query and needs_query:
                self._query(device)
                next_query = now + self.query_seconds
            previous_bad = parser.bad_frames
            for frame in parser.feed(device.read(4096)):
                if self._consume(frame):
                    next_query = 0
            with self._lock:
                self._bad_frames += parser.bad_frames-previous_bad
            if now >= next_save:
                self._save()
                next_save = now + 0.4

    def _run(self):
        import serial
        try:
            while not self._stop.is_set():
                device = None
                try:
                    device = open_serial(self.port)
                    with self._lock:
                        self._port_open = True
                        self._connections += 1
                        self._new_session()
                    self._record('uart_open')
                    self._read_connection(device)
                except (OSError, serial.SerialException) as exc:
                    with self._lock:
                        self._last_error = str(exc)
                    self._record('uart_error', error=str(exc))
                finally:
                    if device is not None:
                        try:
                            device.close()
                        except OSError as exc:
                            self._record('uart_close_error', error=str(exc))
                    with self._lock:
                        self._port_open = False
                    self._save()
                self._stop.wait(self.retry_seconds)
        except Exception as exc:
            # Unexpected errors stay visible in HTTP; /start can retry after repair.
            with self._lock:
                self._last_error = f'{type(exc).__name__}: {exc}'
            self._record('worker_error', error=self._last_error)
        finally:
            with self._lock:
                self._running = self._port_open = False
            self._save()


def make_server(monitor, *, port=8765):
    class Handler(BaseHTTPRequestHandler):
        def send(self, data, content_type, status=200):
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass  # A browser closing does not affect the UART worker.

        def do_GET(self):
            path = self.path.split('?', 1)[0]
            if path in ('/', '/index.html'):
                self.send(Path(__file__).with_name('web_monitor.html').read_bytes(), 'text/html; charset=utf-8')
            elif path == '/live.json':
                self.send(json.dumps(monitor.snapshot(), ensure_ascii=False).encode(), 'application/json; charset=utf-8')
            elif path == '/events.csv':
                buf = io.StringIO(newline='')
                with monitor._lock:
                    monitor._events.write_csv(buf)
                self.send(buf.getvalue().encode('utf-8-sig'), 'text/csv; charset=utf-8')
            elif path == '/favicon.ico':
                self.send(b'', 'image/x-icon', 204)
            else:
                self.send(b'Not found', 'text/plain', 404)

        def do_POST(self):
            if self.path not in ('/start', '/stop'):
                self.send(b'Not found', 'text/plain', 404)
                return
            allowed = (None, f'http://127.0.0.1:{self.server.server_port}',
                       f'http://localhost:{self.server.server_port}')
            if self.headers.get('Origin') not in allowed:
                self.send(b'Forbidden', 'text/plain', 403)
                return
            ok = monitor.start() if self.path == '/start' else monitor.stop()
            self.send(json.dumps(dict(ok=ok)).encode(), 'application/json', 200 if ok else 503)

        def log_message(self, *_):
            pass

    return ThreadingHTTPServer(('127.0.0.1', port), Handler)


def main():
    parser = argparse.ArgumentParser(description='激光竖琴只读网页监控 · 断线自动重连')
    parser.add_argument('--port', required=True, help='明确选择UART串口；Linux建议使用/dev/serial/by-id/...')
    parser.add_argument('--http-port', type=int, default=8765)
    parser.add_argument('--output', type=Path, default=Path('.ai/evidence')/datetime.now().strftime('web-monitor-%Y%m%d-%H%M%S'))
    args = parser.parse_args()
    monitor = SerialMonitor(args.port, args.output)
    server = make_server(monitor, port=args.http_port)
    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, lambda *_: threading.Thread(target=server.shutdown, daemon=True).start())
    url = f'http://127.0.0.1:{server.server_port}/'
    (args.output/'process.json').write_text(json.dumps(dict(server_pid=os.getpid(), uart_owner_pid=os.getpid(),
        url=url, serial_port=args.port, automatic_reconnect=True, capture_limit_seconds=None), indent=2)+'\n')
    print(f'MONITOR {url} UART_OWNER={os.getpid()} OUTPUT={args.output}', flush=True)
    try:
        monitor.start()
        server.serve_forever()
    finally:
        monitor.close()
        server.server_close()


if __name__ == '__main__':
    main()
