#!/usr/bin/env python3
"""Keep immutable Factory fallback identical to the shipped TOML inputs."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--check", action="store_true")
args = parser.parse_args()
lines = [
    "// Copyright (c) 2026 ICECREAM1230",
    "// Factory examples adapted from Ethereal Cloud Tone; Copyright © 2026 Thor H. Linløkken <thj@thj.no>",
    "// License: MIT",
]
for name in ("Supersaw", "Galaxy"):
    text = (ROOT / "storage/Patches/Factory/Pads" / f"{name}.toml").read_text()
    lines.append(
        f"static const char dds_factory_{name.lower()}[] =\n"
        + "".join(json.dumps(line, ensure_ascii=False) + "\n" for line in text.splitlines(keepends=True))
        + ";"
    )
expected = "\n".join(lines) + "\n"
target = ROOT / "main/dds_factory.inc.h"
if args.check:
    if not target.exists() or target.read_text() != expected:
        raise SystemExit("DDS Factory generated header differs; run tools/generate_dds_factory.py")
    print("DDS Factory: embedded Supersaw/Galaxy exactly match TOML seed inputs")
else:
    target.write_text(expected)
