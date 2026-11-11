#!/usr/bin/env python3
"""Run the actual bridge with a generated profile99/backend1 catalog.

All mutations are confined to a temporary copy. A deliberate old lookup bug
must fail, then the unchanged production bridge must pass the same lifecycle.
"""
import json
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import tempfile

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
package = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="s3-fm-profile-identity-") as folder:
    root = Path(folder)
    for name in ("engine_link.c", "engine_link.h", "engine_fm.inc.h", "engine_ks.inc.h",
                 "fm_engine.h", "fm_contract.h", "spi_regs.h", "event_bus.c", "event_bus.h", "status_led.h", "midi_parser.h"):
        (root / "main").mkdir(exist_ok=True)
        shutil.copy2(package / "main" / name, root / "main" / name)
    for header in (package / "main").glob("*.h"):
        shutil.copy2(header, root / "main" / header.name)
    shutil.copytree(package / "contract/midi", root / "contract/midi")
    catalog = json.loads((package / "contract/fm/schema/backend-profiles.json").read_text())
    catalog["profiles"][0]["id"] = 99
    profile_path = root / "profile99.json"
    profile_path.write_text(json.dumps(catalog, indent=2) + "\n")
    subprocess.run([sys.executable, "-B", str(package / "contract/fm/tools/gen_fm_contract.py"),
                    "--schema", str(package / "contract/fm/schema/fm-control.schema.json"),
                    "--profiles", str(profile_path), "--out", str(root / "contract/fm/generated")], check=True)
    command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-I" + str(package / "tests/host/stubs"), "-I" + str(root / "main"),
               str(package / "tests/host/fake_queue.c"), str(package / "tests/host/fake_runtime.c"),
               str(package / "tests/host/fake_performance_link.c"),
               str(package / "tests/host/test_fm_engine.c"), str(root / "main/event_bus.c"),
               str(package / "main/control_source.c"),
               "-o", str(root / "lifecycle")]
    source = root / "main/engine_fm.inc.h"
    correct = source.read_text()
    old_bug = correct.replace("fm_profiles[i].backend_id==caps[2]", "fm_profiles[i].id==caps[2]")
    assert old_bug != correct
    source.write_text(old_bug)
    subprocess.run(command, check=True)
    red = subprocess.run([str(root / "lifecycle")], capture_output=True, text=True)
    assert red.returncode != 0 and "Assertion" in red.stderr, red
    print("Profile99/backend1: deliberate profile-ID-as-backend mutation rejected")
    source.write_text(correct)
    subprocess.run(command, check=True)
    subprocess.run([str(root / "lifecycle")], check=True)
    print("Profile99/backend1: actual generated C and S3 SPI lifecycle PASS")
