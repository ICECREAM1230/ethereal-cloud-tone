#!/usr/bin/env python3
"""Strict pure-C register/range/gate tests; never opens a serial device."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Result directory")
    parser.add_argument("--work-dir", type=Path, help="Existing external temporary parent")
    args = parser.parse_args()
    out = args.output.resolve() if args.output else ROOT / "verification" / "host"
    out.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="fdc2214_host_", dir=args.work_dir))
    sources = [ROOT / "tests/host/test_fdc.c", ROOT / "components/fdc2214/fdc2214.c", ROOT / "components/fdc2214/fdc_ranges.c"]
    inputs = sources + list((ROOT / "components/fdc2214/include").glob("*.h"))
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    checks = []
    try:
        command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-Wshadow",
                   "-fsanitize=address,undefined", "-g", "-I" + str(ROOT / "components/fdc2214/include"),
                   *map(str, sources), "-o", str(scratch / "host_test")]
        with (out / "run.log").open("w") as log:
            for name, cmd in [("compile", command), ("run", [str(scratch / "host_test")])]:
                proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT,
                                      env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"})
                checks.append({"name": name, "command": cmd, "exit": proc.returncode})
                if proc.returncode:
                    break
    finally:
        shutil.rmtree(scratch)
    report = {"checks": checks, "input_sha256": hashes,
              "input_drift": [n for n, h in hashes.items() if hashlib.sha256((ROOT / n).read_bytes()).hexdigest() != h],
              "temporary_directory_removed": not scratch.exists(), "scope": "Pure C with mocked register I/O, not hardware I2C or ESP32 execution"}
    report["result"] = "PASS" if len(checks) == 2 and not any(c["exit"] for c in checks) and not report["input_drift"] else "FAIL"
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print((out / "run.log").read_text())
    print(report["result"], out / "report.json")
    return report["result"] != "PASS"

if __name__ == "__main__":
    raise SystemExit(main())
