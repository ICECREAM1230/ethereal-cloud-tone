"""Read-only SDK/JDK paths; installation remains project-local in setup.py."""
import os
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
CACHE = pathlib.Path(os.environ.get('FPGA_APP_TOOLCHAIN', str(ROOT / '.toolchain'))).expanduser().resolve()
JDK = CACHE / 'jdk/jdk-17.0.20.1+1'
JDK_BIN = JDK / 'bin'
TOOLS = CACHE / 'build-tools/android-16'
ANDROID = CACHE / 'platform/android-36/android.jar'
