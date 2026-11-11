"""GUI launcher that prefers PySide6 and falls back to Tkinter."""

from __future__ import annotations


def main() -> int:
    try:
        from .pyside_gui import main as gui_main
    except ModuleNotFoundError as exc:
        if exc.name != "PySide6":
            raise
        from .gui import main as gui_main

    return gui_main()


if __name__ == "__main__":
    raise SystemExit(main())
