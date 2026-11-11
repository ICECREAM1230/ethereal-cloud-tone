"""Bounded, data-only line framing tolerant of the esp_console REPL."""
from __future__ import annotations

import json
import re

from .config import reject_constant, unique_object

ANSI = re.compile(rb'\x1b\[[0-?]*[ -/]*[@-~]')
VERBS = frozenset(('ping', 'get', 'status', 'apply', 'panic', 'save', 'strike', 'stream'))


def encode_request(request_id: int, expected_revision: int, verb: str, arguments: str = '') -> bytes:
    if type(request_id) is not int or not 1 <= request_id <= 0xffffffff:
        raise ValueError('RPC id out of range')
    if type(expected_revision) is not int or not -1 <= expected_revision <= 0xffffffff:
        raise ValueError('RPC revision out of range')
    if verb not in VERBS or any(ord(c) < 32 or ord(c) > 126 for c in arguments):
        raise ValueError('RPC verb or arguments invalid')
    line = f'rpc {request_id} {expected_revision} {verb}'
    if arguments:
        line += ' ' + arguments
    data = line.encode('ascii')
    if len(data) > 1024:
        raise ValueError('RPC exceeds 1024 bytes')
    return data + b'\n'


class FrameParser:
    """Only the suffix starting at @B is JSON. Noise is never executed."""

    def __init__(self, max_frame: int = 16384):
        self.max_frame = max_frame
        self._buffer = b''
        self.bad_frames = 0
        self.discarded_bytes = 0

    @property
    def buffered_bytes(self):
        return len(self._buffer)

    def feed(self, data: bytes) -> list[dict]:
        combined = self._buffer + data
        lines = combined.split(b'\n')
        self._buffer = lines.pop()
        if len(self._buffer) > self.max_frame:
            self.discarded_bytes += len(self._buffer) - self.max_frame
            self._buffer = self._buffer[-self.max_frame:]
        frames = []
        for line in lines:
            line = ANSI.sub(b'', line).rstrip(b'\r')
            marker = line.find(b'@B ')
            if marker < 0:
                continue
            payload = line[marker + 3:]
            if len(payload) > self.max_frame:
                self.bad_frames += 1
                continue
            try:
                obj = json.loads(payload.decode('utf-8'), object_pairs_hook=unique_object,
                                 parse_constant=reject_constant)
                if (not isinstance(obj, dict) or type(obj.get('v')) is not int
                        or obj['v'] != 1 or obj.get('type') not in ('reply', 'state')):
                    raise ValueError('unsupported frame')
                frames.append(obj)
            except (ValueError, UnicodeError, RecursionError):
                self.bad_frames += 1
        return frames
