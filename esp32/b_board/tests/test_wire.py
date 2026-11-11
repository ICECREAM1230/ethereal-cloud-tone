"""Production PC encoder/parser against the actual portable B firmware."""
import copy
import subprocess
import sys

from host.b_board_debugger.config import DEFAULT_CONFIG, apply_arguments
from host.b_board_debugger.protocol import FrameParser, encode_request

config = copy.deepcopy(DEFAULT_CONFIG)
config.update(volume=87, velocity=[20, 500, 127, 16], gains=[150] * 12,
              notes=[60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79])
requests = [
    encode_request(1, -1, 'get'),
    encode_request(2, 0, 'apply', apply_arguments(config)),
    encode_request(3, 1, 'strike', '2 260'),
    encode_request(4, -1, 'status'),
    encode_request(5, 1, 'apply', 'volume=50 notes=60,60,62,63,64,65,66,67,68,69,70,71'),
    encode_request(6, 0, 'apply', 'volume=99'),
    encode_request(7, 1, 'apply', 'channel=3'),
    encode_request(8, 2, 'save'),
    encode_request(9, 0, 'get'),
    b'harp channel 0\n',
    b'harp channel\n',
    b'harp channel 15\n',
    b'harp get\n',
    b'harp channel 16\n',
    b'harp save\n',
]
result = subprocess.run([sys.argv[1]], input=b''.join(requests), capture_output=True, check=True, timeout=5)
parser = FrameParser()
wire = b'ordinary UART boot log\n' + result.stdout
frames = []
for offset in range(0, len(wire), 11):
    frames += parser.feed(wire[offset:offset + 11])
assert len(frames) == 15 and not parser.bad_frames
assert frames[0]['config'] == DEFAULT_CONFIG
assert frames[1]['config'] == config and frames[1]['revision'] == 1
assert frames[3]['state']['velocity'][1] == 108 and frames[3]['state']['notes'][1] == 62
assert frames[4]['code'] == 'invalid_config' and frames[4]['config']['volume'] == 87
assert frames[5]['code'] == 'revision_conflict' and frames[5]['revision'] == 1
assert frames[6]['ok'] and frames[6]['revision'] == 2 and frames[6]['config']['channel'] == 3
assert frames[7]['ok'] and frames[7]['revision'] == 2
assert frames[8]['config']['volume'] == 87 and frames[8]['config']['channel'] == 3
assert frames[0]['midi_channel_id'] == 1 and frames[8]['midi_channel_id'] == 2
assert frames[9]['config']['channel'] == 1 and frames[9]['midi_channel_id'] == 0
assert frames[10]['config']['channel'] == 1 and frames[10]['revision'] == 3
assert frames[11]['config']['channel'] == 16 and frames[11]['midi_channel_id'] == 15
assert frames[12]['config']['channel'] == 16 and frames[12]['revision'] == 4
assert not frames[13]['ok'] and frames[13]['code'] == 'bad_request'
assert frames[14]['ok'] and frames[14]['revision'] == 4 and frames[14]['midi_channel_id'] == 15
print('PASS real PC request/fragment parser <-> real C B config/engine/protocol: 15 transactions including serial MIDI selection/query/save')
