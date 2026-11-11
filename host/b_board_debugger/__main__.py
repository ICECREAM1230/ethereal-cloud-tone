"""python3 -m host.b_board_debugger [--demo]"""
import argparse
import sys


def main():
    parser = argparse.ArgumentParser(description='B 板 Tk 桌面调试台 · UART0 / CH343 115200')
    parser.add_argument('--demo', action='store_true', help='纯软件演示；不枚举或打开真实串口')
    args = parser.parse_args()
    try:
        import tkinter as tk
        import serial  # Verify dependency before constructing the window; no I/O.
        from .gui import DebuggerApp
        root = tk.Tk()
        DebuggerApp(root, demo=args.demo)
        root.mainloop()
    except ImportError as exc:
        print(f'缺少依赖：{exc}；请查看 host/b_board_debugger/README.md', file=sys.stderr)
        return 2
    except tk.TclError as exc:
        print(f'无法启动桌面窗口：{exc}；需要图形会话，自动测试可用 xvfb-run。', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
