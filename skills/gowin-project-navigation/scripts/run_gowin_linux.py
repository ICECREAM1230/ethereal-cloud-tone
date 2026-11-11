#!/usr/bin/env python3
"""Run an explicitly selected Linux gw_sh with optional child-only startup fixes."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ide', type=Path, required=True, help='Selected installation IDE directory')
    parser.add_argument('--cwd', type=Path, help='Tool working directory; default is caller cwd')
    parser.add_argument('--headless', action='store_true', help='Use offscreen Qt without display variables')
    parser.add_argument('--bundled-qt', action='store_true', help='Use only this installation lib/platform paths')
    parser.add_argument('--freetype', type=Path, help='Verified system freetype library to preload in the child')
    parser.add_argument('--log', type=Path, help='Capture stdout/stderr to a new file; refuse overwriting')
    parser.add_argument('tool_args', nargs=argparse.REMAINDER, help='gw_sh arguments after --')
    args = parser.parse_args(argv)
    tool_args = args.tool_args[1:] if args.tool_args[:1] == ['--'] else args.tool_args
    if not tool_args:
        parser.error('provide gw_sh arguments after --; interactive launch is not implicit')
    if not sys.platform.startswith('linux'):
        parser.error('this helper is for Linux execution')

    try:
        ide = args.ide.expanduser().resolve(strict=True)
        executable = ide / 'bin/gw_sh'
        if not executable.is_file() or not os.access(executable, os.X_OK):
            raise ValueError(f'not an executable Linux gw_sh: {executable}')
        cwd = args.cwd.expanduser().resolve(strict=True) if args.cwd else Path.cwd()
        if not cwd.is_dir():
            raise ValueError(f'not a working directory: {cwd}')
        child_env = os.environ.copy()
        if args.headless:
            child_env.pop('DISPLAY', None)
            child_env.pop('WAYLAND_DISPLAY', None)
            child_env['QT_QPA_PLATFORM'] = 'offscreen'
        if args.bundled_qt:
            libraries = ide / 'lib'
            platforms = ide / 'plugins/qt/platforms'
            if not libraries.is_dir() or not platforms.is_dir():
                raise ValueError(f'bundled lib/platform directories missing under {ide}')
            if any(separator in str(libraries) for separator in (':', ';')):
                raise ValueError('bundled library path contains an LD_LIBRARY_PATH separator')
            child_env['LD_LIBRARY_PATH'] = str(libraries)
            child_env['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(platforms)
            child_env.pop('QT_PLUGIN_PATH', None)
        if args.freetype:
            freetype = args.freetype.expanduser().resolve(strict=True)
            if not freetype.is_file():
                raise ValueError(f'not a library file: {freetype}')
            if ':' in str(freetype) or any(char.isspace() for char in str(freetype)):
                raise ValueError('freetype path contains an unescapable LD_PRELOAD separator')
            child_env['LD_PRELOAD'] = str(freetype)
        command = [str(executable), *tool_args]
        if args.log:
            log_path = args.log.expanduser().absolute()
            with log_path.open('xb') as log:
                print(f'Gowin output: {log_path}', file=sys.stderr)
                result = subprocess.run(command, cwd=cwd, env=child_env, stdout=log, stderr=subprocess.STDOUT)
        else:
            result = subprocess.run(command, cwd=cwd, env=child_env)
    except (OSError, ValueError) as error:
        print(f'Gowin launcher: {error}', file=sys.stderr)
        return 2
    # Preserve ordinary statuses; report signal termination with shell convention.
    return result.returncode if result.returncode >= 0 else 128 - result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
