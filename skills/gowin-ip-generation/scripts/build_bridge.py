#!/usr/bin/env python3
"""Optional rebuild; existing packaged bridge needs no development headers."""
import os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parent
headers=Path(os.environ.get('QT5_INCLUDE_DIR','/usr/include/x86_64-linux-gnu/qt5'))
ide=Path('/home/ice/Gowin/V1.9.12.04/IDE')
if not (headers/'QtCore/qobject.h').exists():raise SystemExit('Rebuild requires compatible Qt 5 development headers; set QT5_INCLUDE_DIR. The packaged bridge is ready to run on the verified host.')
# Write to a new file before replacing the library, so an in-use old mapping
# is not truncated. Do not rebuild while generator requests are running.
target=root/'bin/libgowin_ip_bridge.so.new'
args=['g++','-std=c++17','-fPIC','-shared','-O1','-DQT_NO_KEYWORDS',*[f'-I{p}' for p in [headers,headers/'QtCore',headers/'QtGui',headers/'QtWidgets']],str(root/'src/bridge.cpp'),'-o',str(target),*[str(ide/'lib'/name) for name in ['libQt5Widgets.so.5','libQt5Gui.so.5','libQt5Core.so.5']],'-ldl']
p=subprocess.run(args,capture_output=True,text=True);(root/'build.log').write_text(p.stdout+p.stderr)
if p.returncode:raise SystemExit(p.returncode)
target.replace(root/'bin/libgowin_ip_bridge.so')
print('Bridge rebuilt; verify generation before using this new binary.')
