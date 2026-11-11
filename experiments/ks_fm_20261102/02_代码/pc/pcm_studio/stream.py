"""Incremental, strict framing. No resync that could hide lost PCM samples."""
from .protocol import BLOCK_BYTES, decode_block, validate_frames


class StreamDecoder:
    def __init__(self, require_pattern=False):
        self.require_pattern = require_pattern
        self._pending = bytearray()
        self._previous = None

    @property
    def pending_bytes(self):
        return len(self._pending)

    def feed(self, data):
        self._pending.extend(data)
        while len(self._pending) >= BLOCK_BYTES:
            block = bytes(self._pending[:BLOCK_BYTES])
            del self._pending[:BLOCK_BYTES]
            frame = decode_block(block)
            if self._previous is not None:
                keys=('protocol_version','sample_rate','channels','sample_width')
                if any(frame[k]!=self._previous[k] for k in keys):
                    raise ValueError('PCM protocol/format/sample rate changed inside capture')
            if frame['protocol_version']==1:
                if self.require_pattern:
                    raise ValueError('v1 PCM has no pattern/build metadata')
                if self._previous is not None and frame['seq']!=(self._previous['last_seq']+1)&65535:
                    raise ValueError('v1 block sequence gap/repeat/reorder')
            else:
                validate_frames(([self._previous] if self._previous else []) + [frame],
                                require_pattern=self.require_pattern)
            self._previous = frame
            yield frame
