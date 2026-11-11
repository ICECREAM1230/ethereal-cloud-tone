#!/usr/bin/env python3
"""Read-only generated contract check, usable from a standalone S3 package."""
from pathlib import Path
import hashlib
import subprocess
import sys

sys.dont_write_bytecode = True
PACKAGE = Path(__file__).resolve().parents[1]
SNAPSHOT = PACKAGE / "contract/fm"
cmd = [sys.executable, "-B", str(SNAPSHOT / "tools/gen_fm_contract.py"),
       "--schema", str(SNAPSHOT / "schema/fm-control.schema.json"),
       "--profiles", str(SNAPSHOT / "schema/backend-profiles.json"),
       "--out", str(SNAPSHOT / "generated"), "--check"]
result = subprocess.run(cmd)
if result.returncode:
    raise SystemExit(result.returncode)
# An isolated restored package checks its frozen copy. In the canonical repo,
# additionally refuse any drift from the shared owner's actual generated input.
upstream = next((parent / "development/tang_mega_138k_synth/interfaces/fm_control"
                 for parent in PACKAGE.parents
                 if (parent / "development/tang_mega_138k_synth/interfaces/fm_control").is_dir()), None)
if upstream is not None:
    files = ["schema/fm-control.schema.json", "schema/backend-profiles.json",
             "tools/gen_fm_contract.py", "tools/fm_codegen_v1.py"]
    files += [str(p.relative_to(SNAPSHOT)) for p in (SNAPSHOT / "generated").rglob("*") if p.is_file()]
    for relative in files:
        frozen, shared = SNAPSHOT / relative, upstream / relative
        if not shared.is_file() or hashlib.sha256(frozen.read_bytes()).digest() != hashlib.sha256(shared.read_bytes()).digest():
            raise SystemExit(f"FM shared contract drift: {relative}; obtain the owner's frozen handoff before resync")
    print("FM snapshot matches the shared owner's inputs and all generated languages")
else:
    print("Standalone S3 package: frozen generated contract check passed")
