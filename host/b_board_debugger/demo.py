"""Software B board speaking the exact byte protocol through SerialWorker.

This simulates protocol behavior, not electrical inputs, USB/BLE, NVS or timing.
"""
from __future__ import annotations

import copy
import json
import time
from collections import deque

from .config import DEFAULT_CONFIG, ConfigError, effective_notes, parse_apply_arguments, velocity_for


class DemoPort:
    def __init__(self):
        self.config = copy.deepcopy(DEFAULT_CONFIG)
        self.revision = 0
        self.lifecycle = 'running'
        self._start = time.monotonic()
        self._next_strike = self._start + 0.18
        self._next_stream = self._start
        self._period = 0
        self._channel = 0
        self._seq = 0
        self._out = bytearray()
        self._events = deque(maxlen=64)
        self._duration = [0] * 12
        self._velocity = [0] * 12
        self._hits = [0] * 12
        self._off = [0.0] * 12
        self._transaction = None
        self._closed = False

    def _ms(self):
        return int((time.monotonic() - self._start) * 1000) & 0xffffffff

    def _emit(self, frame):
        self._out.extend(b'\x1b[32mharp> \x1b[0m@B ' + json.dumps(frame, separators=(',', ':')).encode('ascii') + b'\r\n')

    def _reply(self, request_id, ok=True, code='ok', **extra):
        self._emit(dict(v=1, type='reply', id=request_id, ok=ok, code=code,
                        revision=self.revision, config=copy.deepcopy(self.config), **extra))

    def _strike(self, index, duration, synthetic):
        self._seq = (self._seq + 1) & 0xffffffff
        velocity = velocity_for(self.config, duration, index)
        self._duration[index], self._velocity[index] = duration, velocity
        self._hits[index] += 1
        note = effective_notes(self.config)[index]
        self._off[index] = time.monotonic() + self.config['gate'] / 1000 if note >= 0 else 0
        self._events.append([self._seq, self._ms(), index + 1, duration, velocity,
                             note, synthetic, self.revision])

    def _snapshot(self):
        now = time.monotonic()
        playing = sum(1 << i for i, deadline in enumerate(self._off) if deadline > now)
        shade = 1 << self._channel if self.lifecycle == 'running' else 0
        raw = shade if self.config['polarity'] else (shade ^ 0xfff)
        events = [self._events.popleft() for _ in range(min(8, len(self._events)))]
        return dict(v=1, type='state', uptime_ms=self._ms(), revision=self.revision,
                    lifecycle=self.lifecycle, raw=raw, shade=shade, playing=playing,
                    duration=list(self._duration), velocity=list(self._velocity),
                    notes=effective_notes(self.config), hits=list(self._hits),
                    controls={key: self.config[key] for key in ('transpose', 'volume', 'fine', 'cutoff', 'resonance')},
                    links=dict(usb=False, ble=False, selected=0, usb_sent=0, ble_sent=0,
                               usb_retries=0, ble_retries=0, ble_errors=0),
                    timing=dict(last_gap_us=1000, max_gap_us=1000, max_work_us=35, missed_periods=0),
                    drops=dict(commands=0, replies=0, events=0), illegal_edges=0,
                    encoder_enabled=sum(1 << i for i, row in enumerate(self.config['pins']) if row[0] >= 0),
                    ab=[3] * 4, pressed=0, heap_free=180000, events=events)

    def write(self, data):
        if self._closed:
            raise OSError('demo closed')
        parts = data.decode('ascii').strip().split(maxsplit=4)
        if len(parts) < 4 or parts[0] != 'rpc':
            return len(data)
        request_id, expected, verb = int(parts[1]), int(parts[2]), parts[3]
        args = parts[4] if len(parts) > 4 else ''
        if expected != -1 and expected != self.revision:
            self._reply(request_id, False, 'revision_conflict')
            return len(data)
        if self.lifecycle != 'running' and verb in ('apply', 'save', 'strike'):
            self._reply(request_id, False, 'busy')
            return len(data)
        try:
            if verb in ('ping', 'get'):
                self._reply(request_id)
            elif verb == 'status':
                self._reply(request_id, state=self._snapshot())
            elif verb == 'stream':
                period = int(args)
                if period != 0 and not 200 <= period <= 2000:
                    raise ValueError('stream range')
                self._period = period / 1000
                self._next_stream = time.monotonic()
                self._reply(request_id)
            elif verb == 'apply':
                proposed = parse_apply_arguments(args, self.config)
                hardware_change = any(proposed[key] != self.config[key] for key in ('channel', 'polarity', 'pins'))
                if hardware_change:
                    self._off = [0] * 12
                    self.lifecycle = 'quiescing'
                    self._transaction = (time.monotonic() + 0.16, request_id, proposed)
                else:
                    self.config = proposed
                    self.revision = (self.revision + 1) & 0xffffffff
                    self._reply(request_id)
            elif verb == 'save':
                self.lifecycle = 'saving'
                self._off = [0] * 12
                self._transaction = (time.monotonic() + 0.25, request_id, None)
            elif verb == 'panic':
                self._off = [0] * 12
                self._reply(request_id)
            elif verb == 'strike':
                channel, duration = [int(part) for part in args.split()]
                if not 1 <= channel <= 12 or not 1 <= duration <= 60000:
                    raise ValueError('strike range')
                self._strike(channel - 1, duration, True)
                self._reply(request_id)
            else:
                self._reply(request_id, False, 'unsupported')
        except (ValueError, ConfigError):
            self._reply(request_id, False, 'invalid_config' if verb == 'apply' else 'bad_request')
        return len(data)

    def _advance(self):
        now = time.monotonic()
        if self._transaction and now >= self._transaction[0]:
            _, request_id, proposed = self._transaction
            if proposed is not None:
                self.config = proposed
                self.revision = (self.revision + 1) & 0xffffffff
            self.lifecycle = 'running'
            self._transaction = None
            self._next_strike = now + 0.18
            self._reply(request_id)
        if self.lifecycle == 'running' and now >= self._next_strike:
            self._strike(self._channel, 20 + self._channel * 45, False)
            self._channel = (self._channel + 1) % 12
            self._next_strike = now + 0.18
        if self._period and now >= self._next_stream:
            self._emit(self._snapshot())
            self._next_stream = now + self._period

    def read(self, size):
        if self._closed:
            raise OSError('demo closed')
        deadline = time.monotonic() + 0.05
        while time.monotonic() < deadline:
            self._advance()
            if self._out:
                # Fragment frames to exercise the production framing path in demo too.
                count = min(size, 197, len(self._out))
                data = bytes(self._out[:count])
                del self._out[:count]
                return data
            time.sleep(0.005)
        return b''

    def close(self):
        self._closed = True
