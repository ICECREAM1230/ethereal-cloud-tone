"""UART is exclusively owned by a bounded worker; never calls Tk."""
from __future__ import annotations

import copy
import os
import queue
import threading
from collections import deque
from dataclasses import dataclass

from .protocol import FrameParser


@dataclass
class Batch:
    controls: list
    state: tuple | None
    events: list
    fatal: tuple | None


class Inbox:
    """Replies have their own queue; snapshots coalesce, event rows do not."""

    def __init__(self, event_limit=4096, control_limit=128):
        self._lock = threading.Lock()
        self._controls = deque()
        self._events = deque()
        self._state = None
        self._fatal = None
        self._active_epoch = None
        self._event_limit, self._control_limit = event_limit, control_limit
        self._stats = dict(event_drops=0, state_coalesced=0, control_drops=0)

    @property
    def stats(self):
        with self._lock:
            return dict(self._stats)

    def activate(self, epoch):
        """Retire queued and future callbacks from a previous connection."""
        with self._lock:
            self._active_epoch = epoch
            self._controls.clear()
            self._events.clear()
            self._state = self._fatal = None

    def put(self, epoch, kind, payload):
        with self._lock:
            if self._active_epoch is not None and epoch != self._active_epoch:
                return False
            if kind == 'frame':
                payload = copy.deepcopy(payload)
                state = payload if payload.get('type') == 'state' else payload.get('state')
                if isinstance(state, dict):
                    events = state.pop('events', [])
                    if isinstance(events, list):
                        # Reject an invalid overlong event batch instead of unbounded work.
                        if len(events) > 8:
                            self._stats['event_drops'] += len(events)
                        else:
                            for event in events:
                                if len(self._events) >= self._event_limit:
                                    self._stats['event_drops'] += 1
                                else:
                                    self._events.append((epoch, event, state.get('uptime_ms')))
                if payload.get('type') == 'state':
                    if self._state is not None:
                        self._stats['state_coalesced'] += 1
                    self._state = (epoch, payload)
                    return True
            if len(self._controls) >= self._control_limit:
                self._stats['control_drops'] += 1
                self._fatal = (epoch, '主机回复队列已满；连接已中止，请回读设备状态')
                return False
            self._controls.append((epoch, kind, payload))
            return True

    def drain(self):
        with self._lock:
            result = Batch(list(self._controls), self._state, list(self._events), self._fatal)
            self._controls.clear()
            self._events.clear()
            self._state = self._fatal = None
            return result


def open_serial(port):
    """This function is called only inside SerialWorker after explicit selection."""
    import serial
    device = serial.Serial(port=None, baudrate=115200, timeout=0.05, write_timeout=0.5,
                           rtscts=False, dsrdtr=False, exclusive=True if os.name == 'posix' else None)
    device.dtr = False
    device.rts = False
    device.port = port
    try:
        device.open()
    except BaseException:
        device.close()
        raise
    return device


def list_ports():
    from serial.tools import list_ports as serial_ports
    return [(p.device, p.description) for p in serial_ports.comports()]


class SerialWorker:
    def __init__(self, epoch, inbox, port, *, demo=False, port_factory=None):
        self.epoch, self.inbox, self.port = epoch, inbox, port
        self.demo = demo
        self._factory = port_factory
        self._outgoing = queue.Queue(maxsize=32)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name=f'b-uart-{epoch}', daemon=True)
        self._parser = FrameParser()

    @property
    def bad_frames(self):
        return self._parser.bad_frames

    @property
    def alive(self):
        return self._thread.is_alive()

    def start(self):
        self._thread.start()

    def send(self, data):
        if self._stop.is_set():
            return False
        try:
            self._outgoing.put_nowait(data)
            return True
        except queue.Full:
            return False

    def stop(self):
        # Only the worker closes its serial handle. No GUI join/serial call can hang Tk.
        self._stop.set()

    def _run(self):
        device = None
        try:
            if self._factory:
                device = self._factory(self.port)
            elif self.demo:
                from .demo import DemoPort
                device = DemoPort()
            else:
                device = open_serial(self.port)
            if self._stop.is_set():
                return
            if not self.inbox.put(self.epoch, 'opened', {}):
                return
            while not self._stop.is_set():
                # A bounded burst gives incoming data a chance even during command bursts.
                for _ in range(4):
                    try:
                        data = self._outgoing.get_nowait()
                    except queue.Empty:
                        break
                    if self._stop.is_set():
                        break
                    written = device.write(data)
                    if written != len(data):
                        raise OSError('串口写入不完整；不自动重发')
                data = device.read(4096)
                if data:
                    for frame in self._parser.feed(data):
                        if not self.inbox.put(self.epoch, 'frame', frame):
                            return
        except Exception as exc:
            if not self._stop.is_set():
                self.inbox.put(self.epoch, 'error', {'message': str(exc)})
        finally:
            if device is not None:
                try:
                    device.close()
                except Exception:
                    pass
            self.inbox.put(self.epoch, 'closed', {})
