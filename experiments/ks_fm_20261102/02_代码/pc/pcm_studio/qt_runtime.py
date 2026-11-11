"""Set real wheel plugin paths; Qt .conf misparses this host's semicolon mount."""
from pathlib import Path
import os
import ctypes
import sys

_native_handles=[]


def configure_qt():
    os.environ['PYQTGRAPH_QT_LIB']='PySide6'
    if sys.platform.startswith('linux'):
        # User-scoped Ubuntu runtime; no system package/permission changes.
        bundled=Path(__file__).resolve().parents[1]/'runtime/linux/usr/lib/x86_64-linux-gnu/libxcb-cursor.so.0'
        try:_native_handles.append(ctypes.CDLL('libxcb-cursor.so.0',mode=ctypes.RTLD_GLOBAL))
        except OSError:
            if bundled.is_file():_native_handles.append(ctypes.CDLL(str(bundled),mode=ctypes.RTLD_GLOBAL))
    import PySide6
    from PySide6.QtCore import QCoreApplication
    package=Path(PySide6.__file__).resolve().parent
    candidates=(package/'Qt/plugins',package/'plugins')
    plugins=next((p for p in candidates if (p/'platforms').is_dir()),None)
    if plugins is None:raise RuntimeError('PySide6 安装中缺少 Qt 平台插件，请重新运行安装脚本')
    QCoreApplication.setLibraryPaths([str(plugins)])
