#!/usr/bin/env python3
"""Build this isolated project only; retain artifacts, remove owned intermediates."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def sources():
    return sorted(p for p in ROOT.rglob("*") if p.is_file() and not any(x in p.relative_to(ROOT).parts for x in ("verification", "build", "__pycache__", ".ai")) and p.name not in ("sdkconfig", "sdkconfig.old"))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console", choices=("usb", "uart"), default="usb")
    parser.add_argument("--idf", type=Path, default=Path(os.environ["IDF_PATH"]) if os.environ.get("IDF_PATH") else None)
    parser.add_argument("--config", type=Path, help="Optional explicit SDKCONFIG copy; original is never edited")
    parser.add_argument("--output", type=Path, help="New result directory; existing directories are not overwritten")
    args = parser.parse_args()
    if args.idf is None or not (args.idf / "export.sh").is_file():
        parser.error("Activate ESP-IDF 6.1 (IDF_PATH) or pass --idf to its directory")
    expected_console = "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y" if args.console == "usb" else "CONFIG_ESP_CONSOLE_UART_DEFAULT=y"
    if args.config and expected_console not in args.config.read_text().splitlines():
        parser.error("--config console choice does not match --console; select the actual console explicitly")
    out = args.output.resolve() if args.output else ROOT / "verification" / ("build-" + args.console)
    out.mkdir(parents=True, exist_ok=args.output is None)
    scratch = Path(tempfile.mkdtemp(prefix="fdc2214_s3_" + args.console + "_"))
    sdkconfig = scratch / "sdkconfig"
    if args.config:
        shutil.copy2(args.config, sdkconfig)
    defaults = str(ROOT / "sdkconfig.defaults") + ";" + str(ROOT / "config" / (args.console + ".defaults"))
    # Shell text is constant. Paths and arguments are passed positionally.
    command = ["bash", "-lc", 'source "$1/export.sh" >/dev/null && shift && exec idf.py "$@"', "fdc-build",
               str(args.idf), "-B", str(scratch / "build"), "-D", "SDKCONFIG=" + str(sdkconfig),
               "-D", "SDKCONFIG_DEFAULTS=" + defaults, "build"]
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources()}
    started = time.monotonic()
    with (out / "build.log").open("w") as log:
        proc = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        print("Build PID", proc.pid, "log", out / "build.log", flush=True)
        result = proc.wait()
    generated = sdkconfig.read_text() if sdkconfig.exists() else ""
    actual_console = next((name for name in ("CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y", "CONFIG_ESP_CONSOLE_UART_DEFAULT=y", "CONFIG_ESP_CONSOLE_UART_CUSTOM=y") if name in generated.splitlines()), "unknown")
    console_matches = actual_console == expected_console
    retained = {}
    if result == 0:
        artifacts = out / "artifacts"
        artifacts.mkdir(exist_ok=True)
        for name in ("fdc2214_raw_debug.bin", "fdc2214_raw_debug.elf", "fdc2214_raw_debug.map",
                     "bootloader/bootloader.bin", "partition_table/partition-table.bin", "flasher_args.json", "compile_commands.json"):
            src = scratch / "build" / name
            dest = artifacts / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dest)
            retained[str(dest.relative_to(out))] = hashlib.sha256(dest.read_bytes()).hexdigest()
        shutil.copy2(sdkconfig, artifacts / "sdkconfig")
    drift = [n for n, h in hashes.items() if hashlib.sha256((ROOT / n).read_bytes()).hexdigest() != h]
    report = {"result": "PASS" if result == 0 and not drift and console_matches else "FAIL", "exit": result,
              "command": command, "cwd": str(ROOT), "pid": proc.pid,
              "seconds": time.monotonic() - started, "input_sha256": hashes,
              "input_drift": drift, "retained_sha256": retained,
              "requested_console": args.console, "actual_console": actual_console,
              "console_matches": console_matches,
              "copied_config_sha256": hashlib.sha256(args.config.read_bytes()).hexdigest() if args.config else None,
              "generated_config_sha256": hashlib.sha256(generated.encode()).hexdigest(),
              "scope": "Actual ESP-IDF build only; no serial port, flash, reset or device operation"}
    # Retain the failed build too for diagnosis. Never clean an existing user build.
    if result == 0:
        shutil.rmtree(scratch)
        report["owned_build_removed"] = not scratch.exists()
    else:
        report["failed_build_retained"] = str(scratch)
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(report["result"], out / "report.json", flush=True)
    return report["result"] != "PASS"

if __name__ == "__main__":
    raise SystemExit(main())
