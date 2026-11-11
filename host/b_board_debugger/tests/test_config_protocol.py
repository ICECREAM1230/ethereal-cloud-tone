"""Behavioral protocol tests; no physical serial port is used."""
import copy
import json
import unittest

from host.b_board_debugger.config import (
    DEFAULT_CONFIG, ConfigError, apply_arguments, effective_notes,
    import_config, validate_config, velocity_for,
)
from host.b_board_debugger.protocol import FrameParser, encode_request


class ConfigTests(unittest.TestCase):
    def test_current_board_encoder_pins_allow_midi_channel_edit(self):
        config = copy.deepcopy(DEFAULT_CONFIG)
        config['pins'] = [[13, 14, 12], [40, 39, 41], [47, 21, 38], [2, 42, 1]]
        config['channel'] = 16
        self.assertEqual(validate_config(config), config)
        self.assertEqual(apply_arguments(config, ['channel']), 'channel=16')

    def test_shorter_default_shade_produces_larger_velocity(self):
        points = ((5, 127), (20, 127), (100, 109), (260, 72), (500, 16), (1000, 16))
        for duration, expected in points:
            with self.subTest(duration_ms=duration):
                self.assertEqual(velocity_for(DEFAULT_CONFIG, duration), expected)

    def test_defaults_and_inverse_curve_gain_and_muted_pitch(self):
        config = copy.deepcopy(DEFAULT_CONFIG)
        config.update(velocity=[20, 500, 127, 16], transpose=24)
        config['notes'][-1] = 127
        config['gains'][0] = 200
        validate_config(config)
        self.assertEqual(velocity_for(config, 20, 0), 127)
        self.assertEqual(velocity_for(config, 500, 0), 32)
        self.assertEqual(velocity_for(config, 260, 1), 72)
        self.assertEqual(effective_notes(config)[-1], -1)

    def test_all_ranges_and_duplicate_or_partial_pin_binding_are_rejected(self):
        invalid = [
            ('notes', [60] * 12), ('notes', list(range(11))),
            ('gains', [24] * 12), ('gains', [201] * 12),
            ('velocity', [20, 20, 1, 127]), ('velocity', [1, 60001, 1, 127]),
            ('velocity', [1, 2, 0, 127]), ('gate', 9), ('gate', 5001),
            ('channel', 0), ('channel', 17), ('transpose', -25),
            ('fine', 101), ('volume', True), ('volume', 128),
            ('polarity', 2), ('debounce', 0), ('enc_edges', 3),
            ('enc_reverse', 16), ('route', 'exec'),
            ('pins', [[8, -1, -1]] + [[-1] * 3] * 3),
            ('pins', [[8, 12, 13], [8, 14, 21], [-1] * 3, [-1] * 3]),
            ('pins', [[43, 12, 13]] + [[-1] * 3] * 3),
        ]
        for key, value in invalid:
            with self.subTest(key=key, value=value):
                config = copy.deepcopy(DEFAULT_CONFIG)
                config[key] = value
                with self.assertRaises(ConfigError):
                    validate_config(config)
        config = copy.deepcopy(DEFAULT_CONFIG)
        config['extra'] = 1
        with self.assertRaises(ConfigError):
            validate_config(config)

    def test_apply_arguments_are_ascii_and_flatten_pins_without_code(self):
        config = copy.deepcopy(DEFAULT_CONFIG)
        config['pins'][0] = [8, 12, 13]
        args = apply_arguments(config, keys=['notes', 'pins', 'volume'])
        self.assertIn('pins=8,12,13,-1,-1,-1,-1,-1,-1,-1,-1,-1', args)
        self.assertIn('volume=100', args)
        self.assertEqual(encode_request(4, 7, 'apply', args).split()[:4],
                         [b'rpc', b'4', b'7', b'apply'])
        with self.assertRaises(ValueError):
            encode_request(1, -1, 'strike', '1 20\nrpc 2 -1 save')
        with self.assertRaises(ValueError):
            encode_request(0, -1, 'get')

    def test_import_requires_full_known_config_and_rejects_duplicate_json_keys(self):
        imported = import_config(json.dumps(DEFAULT_CONFIG))
        self.assertEqual(imported, DEFAULT_CONFIG)
        imported['notes'][0] = 1
        self.assertEqual(DEFAULT_CONFIG['notes'][0], 60)
        with self.assertRaises(ConfigError):
            import_config('{"volume":90,"volume":80}')
        with self.assertRaises(ConfigError):
            import_config('{"volume":90}')


class FramingTests(unittest.TestCase):
    def test_fragmented_combined_repl_ansi_and_bad_frames(self):
        parser = FrameParser()
        first = b'\x1b[32mharp> \x1b[0m@B {"v":1,"type":"reply","id":12,'
        self.assertEqual(parser.feed(first), [])
        frames = parser.feed(b'"ok":true,"revision":4}\r\nnoise\n@B {broken}\n'
                             b'@B {"v":1,"type":"state","uptime_ms":42}\n')
        self.assertEqual([x['type'] for x in frames], ['reply', 'state'])
        self.assertEqual(frames[0]['id'], 12)
        self.assertGreaterEqual(parser.bad_frames, 1)

    def test_hostile_stream_is_bounded_and_next_good_frame_recovers(self):
        parser = FrameParser(max_frame=2048)
        for _ in range(100):
            parser.feed(b'x' * 999)
        self.assertLessEqual(parser.buffered_bytes, 2048)
        frames = parser.feed(b'\n@B {"v":1,"type":"reply","id":2,"ok":true}\n')
        self.assertEqual(frames[0]['id'], 2)
        self.assertEqual(parser.feed(b'@B {"v":99,"type":"reply"}\n'), [])

    def test_invalid_utf8_duplicate_keys_nan_and_nonobjects_are_ignored(self):
        parser = FrameParser()
        bad = [b'@B {"v":1,"type":"reply","id":1,"id":2}\n',
               b'@B []\n', b'@B {"v":1,"type":"state","x":NaN}\n',
               b'@B {"v":1,"type":"reply","x":"\xff"}\n']
        for frame in bad:
            self.assertEqual(parser.feed(frame), [])


if __name__ == '__main__':
    unittest.main()
