#!/usr/bin/env python3
"""Isolated observable-behavior checks; no Gowin installation or hardware writes."""

import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

HELPER = Path(__file__).with_name('run_gowin_linux.py')
KEYS = ('DISPLAY', 'WAYLAND_DISPLAY', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH',
        'QT_QPA_PLATFORM_PLUGIN_PATH', 'LD_LIBRARY_PATH', 'LD_PRELOAD')


@unittest.skipUnless(sys.platform.startswith('linux'), 'Linux launcher')
class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='gowin launcher 中文 ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ide = self.root / 'installation with spaces/IDE'
        (self.ide / 'bin').mkdir(parents=True)
        (self.ide / 'lib').mkdir()
        (self.ide / 'plugins/qt/platforms').mkdir(parents=True)
        mock = self.ide / 'bin/gw_sh'
        mock.write_text('#!' + sys.executable + '\n' + '''
import json, os, signal, sys
from pathlib import Path
if os.environ.get('MOCK_MARKER'):
    Path(os.environ['MOCK_MARKER']).write_text('ran')
if '--mock-signal' in sys.argv:
    os.kill(os.getpid(), signal.SIGTERM)
keys = ('DISPLAY', 'WAYLAND_DISPLAY', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH',
        'QT_QPA_PLATFORM_PLUGIN_PATH', 'LD_LIBRARY_PATH', 'LD_PRELOAD')
print(json.dumps({'argv': sys.argv[1:], 'cwd': os.getcwd(),
                  'env': {k: os.environ.get(k) for k in keys}}, ensure_ascii=False), flush=True)
print('mock stderr', file=sys.stderr, flush=True)
if '--mock-binary' in sys.argv:
    sys.stdout.buffer.write(b'\\xff\\x00')
sys.exit(int(os.environ.get('MOCK_RC', '0')))
''')
        mock.chmod(0o755)
        self.cwd = self.root / 'work with spaces'
        self.cwd.mkdir()
        self.env = os.environ.copy()
        for key in KEYS:
            self.env.pop(key, None)
        self.env['PYTHONDONTWRITEBYTECODE'] = '1'

    def run_helper(self, options=(), tool_args=('-h',), env=None, cwd=None):
        return subprocess.run([sys.executable, '-B', str(HELPER), '--ide', str(self.ide),
                               *map(str, options), '--', *tool_args],
                              cwd=cwd or self.cwd, env=env or self.env, capture_output=True)

    def observed(self, result):
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        return json.loads(result.stdout)

    def test_argument_roundtrip_and_explicit_cwd(self):
        payload = ('路径 with spaces.tcl', 'a"b', "c'd", 'x\\y', '$(touch injected)', '`touch injected`', '--headless')
        data = self.observed(self.run_helper(('--cwd', self.root), payload))
        self.assertEqual(data['argv'], list(payload))
        self.assertEqual(data['cwd'], str(self.root))
        self.assertFalse((self.root / 'injected').exists())
        self.assertFalse((self.cwd / 'injected').exists())

    def test_default_cwd_is_caller_not_installation(self):
        data = self.observed(self.run_helper())
        self.assertEqual(data['cwd'], str(self.cwd))

    def test_default_environment_preserves_gui(self):
        env = dict(self.env, DISPLAY=':42', WAYLAND_DISPLAY='wayland-test',
                   QT_QPA_PLATFORM='xcb', QT_PLUGIN_PATH='parent plugins',
                   QT_QPA_PLATFORM_PLUGIN_PATH='parent platforms', LD_LIBRARY_PATH='parent libs')
        data = self.observed(self.run_helper(env=env))
        self.assertEqual(data['env'], {k: env.get(k) for k in KEYS})

    def test_headless_changes_only_display_settings(self):
        env = dict(self.env, DISPLAY=':42', WAYLAND_DISPLAY='wayland-test',
                   QT_QPA_PLATFORM='xcb', QT_PLUGIN_PATH='parent plugins', LD_LIBRARY_PATH='parent libs')
        data = self.observed(self.run_helper(('--headless',), env=env))
        expected = {k: env.get(k) for k in KEYS}
        expected.update(DISPLAY=None, WAYLAND_DISPLAY=None, QT_QPA_PLATFORM='offscreen')
        self.assertEqual(data['env'], expected)

    def test_bundled_profile_is_explicit_and_keeps_display(self):
        env = dict(self.env, DISPLAY=':42', QT_PLUGIN_PATH='foreign plugins', LD_LIBRARY_PATH='foreign libs')
        data = self.observed(self.run_helper(('--bundled-qt',), env=env))
        self.assertEqual(data['env']['DISPLAY'], ':42')
        self.assertEqual(data['env']['LD_LIBRARY_PATH'], str(self.ide / 'lib'))
        self.assertEqual(data['env']['QT_QPA_PLATFORM_PLUGIN_PATH'], str(self.ide / 'plugins/qt/platforms'))
        self.assertIsNone(data['env']['QT_PLUGIN_PATH'])

    def test_freetype_is_explicit_and_overrides_child_only(self):
        with tempfile.TemporaryDirectory(prefix='gowin-preload-', dir='/tmp') as directory:
            library = Path(directory) / 'mock-freetype.so'
            library.touch()
            env = dict(self.env, LD_PRELOAD='/nonexistent-parent-preload.so')
            data = self.observed(self.run_helper(('--freetype', library), env=env))
            self.assertEqual(data['env']['LD_PRELOAD'], str(library))
            self.assertEqual(env['LD_PRELOAD'], '/nonexistent-parent-preload.so')

    def test_loader_list_separators_are_rejected(self):
        library = self.root / 'freetype.so'
        library.touch()
        log = self.root / 'not-created-preload.log'
        self.assertEqual(self.run_helper(('--freetype', library, '--log', log)).returncode, 2)
        self.assertFalse(log.exists())
        changed_ide = self.ide.with_name('IDE;separator')
        self.ide.rename(changed_ide)
        self.ide = changed_ide
        log = self.root / 'not-created-bundle.log'
        self.assertEqual(self.run_helper(('--bundled-qt', '--log', log)).returncode, 2)
        self.assertFalse(log.exists())

    def test_log_keeps_binary_output_and_nonzero_exit(self):
        log = self.root / 'fresh output.log'
        result = self.run_helper(('--log', log), ('--mock-binary',), dict(self.env, MOCK_RC='37'))
        self.assertEqual(result.returncode, 37)
        self.assertEqual(result.stdout, b'')
        content = log.read_bytes()
        self.assertIn(b'mock stderr', content)
        self.assertIn(b'\xff\x00', content)
        self.assertIn(b'"argv"', content)

    def test_existing_log_refused_without_running_tool(self):
        log = self.root / 'existing.log'
        log.write_bytes(b'prior evidence\x00')
        marker = self.root / 'ran'
        result = self.run_helper(('--log', log), env=dict(self.env, MOCK_MARKER=str(marker)))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(log.read_bytes(), b'prior evidence\x00')
        self.assertFalse(marker.exists())

    def test_missing_inputs_refused_before_creating_log(self):
        (self.ide / 'lib').rmdir()
        cases = [('--bundled-qt',), ('--freetype', self.root / 'missing.so'),
                 ('--cwd', self.root / 'missing-cwd'), ('--ide', self.root / 'missing-ide')]
        for index, options in enumerate(cases):
            with self.subTest(options=options):
                log = self.root / f'not-created-{index}.log'
                result = self.run_helper((*options, '--log', log))
                self.assertEqual(result.returncode, 2)
                self.assertFalse(log.exists())

    def test_signal_exit_is_preserved(self):
        result = self.run_helper(tool_args=('--mock-signal',))
        self.assertEqual(result.returncode, 128 + signal.SIGTERM)

    def test_parent_environment_unchanged_when_called_directly(self):
        spec = importlib.util.spec_from_file_location('gowin_launcher', HELPER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with patch.dict(os.environ, {'DISPLAY': ':42', 'QT_PLUGIN_PATH': 'foreign plugins'}, clear=False):
            before = os.environ.copy()
            log = self.root / 'direct.log'
            result = module.main(['--ide', str(self.ide), '--headless', '--bundled-qt',
                                  '--log', str(log), '--', '-h'])
            self.assertEqual(result, 0)
            self.assertEqual(dict(os.environ), before)

    def test_interactive_launch_is_not_implicit(self):
        self.assertEqual(self.run_helper(tool_args=()).returncode, 2)


if __name__ == '__main__':
    unittest.main()
