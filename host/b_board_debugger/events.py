"""Bounded event history with sequence deduplication and optional streaming CSV."""
from __future__ import annotations

import csv
from collections import deque
from datetime import datetime, timezone

CSV_FIELDS = ('session', 'mode', 'seq', 'time_ms', 'channel', 'duration_ms',
              'velocity', 'note', 'synthetic', 'revision', 'received_at')


class EventLog:
    def __init__(self, max_rows=20000):
        self.rows = deque(maxlen=max_rows)
        self.total = 0
        self.invalid = 0
        self.evicted = 0
        self.duplicates = 0
        self.capture_path = None
        self._capture = None
        self._writer = None
        self._epoch = 0
        self._boot = 0
        self._demo = False
        self._seen = set()
        self._recent = deque()
        self._highest = None
        self._last_uptime = None

    def new_connection(self, epoch, demo=False):
        self._epoch, self._demo = epoch, demo
        self._boot = 0
        self._reset_sequences()

    def _reset_sequences(self):
        self._seen.clear()
        self._recent.clear()
        self._highest = None
        self._last_uptime = None

    def new_device_boot(self):
        self._boot += 1
        self._reset_sequences()

    def add(self, event, uptime_ms=None):
        if not isinstance(event, list) or len(event) != 8:
            self.invalid += 1
            return False
        seq, timestamp, channel, duration, velocity, note, synthetic, revision = event
        fields = ((seq, 0, 0xffffffff), (timestamp, 0, 0xffffffff),
                  (channel, 1, 12), (duration, 0, 0xffffffff), (velocity, 1, 127),
                  (note, -1, 127), (revision, 0, 0xffffffff))
        if (any(type(value) is not int or not low <= value <= high for value, low, high in fields)
                or type(synthetic) not in (bool, int) or synthetic not in (False, True, 0, 1)):
            self.invalid += 1
            return False
        if type(uptime_ms) is int and uptime_ms >= 0:
            # millis() wrap is forward progress; a small reverse jump is a reboot.
            if self._last_uptime is not None and uptime_ms < self._last_uptime:
                delta = (uptime_ms - self._last_uptime) & 0xffffffff
                if delta >= 0x80000000:
                    self.new_device_boot()
            self._last_uptime = uptime_ms
        if seq in self._seen:
            self.duplicates += 1
            return False
        if self._highest is not None:
            delta = (seq - self._highest) & 0xffffffff
            if delta == 0 or (delta >= 0x80000000 and ((self._highest - seq) & 0xffffffff) > 8192):
                self.duplicates += 1
                return False
            if delta < 0x80000000:
                self._highest = seq
        else:
            self._highest = seq
        self._seen.add(seq)
        self._recent.append(seq)
        if len(self._recent) > 8192:
            self._seen.discard(self._recent.popleft())
        row = dict(zip(CSV_FIELDS, (f'{self._epoch}.{self._boot}', 'demo' if self._demo else 'serial',
                       seq, timestamp, channel, duration, velocity, note, int(synthetic), revision,
                       datetime.now(timezone.utc).isoformat(timespec='milliseconds'))))
        if len(self.rows) == self.rows.maxlen:
            self.evicted += 1
        self.rows.append(row)
        self.total += 1
        if self._writer:
            self._writer.writerow(row)
            self._capture.flush()
        return True

    def write_csv(self, target):
        writer = csv.DictWriter(target, CSV_FIELDS)
        writer.writeheader()
        writer.writerows(self.rows)

    def start_capture(self, path):
        self.stop_capture()
        # UTF-8 BOM helps spreadsheet applications display Chinese filesystem exports.
        self._capture = open(path, 'w', newline='', encoding='utf-8-sig')
        self._writer = csv.DictWriter(self._capture, CSV_FIELDS)
        self._writer.writeheader()
        self._capture.flush()
        self.capture_path = str(path)

    def stop_capture(self):
        if self._capture:
            self._capture.close()
        self._capture = self._writer = None
        self.capture_path = None
