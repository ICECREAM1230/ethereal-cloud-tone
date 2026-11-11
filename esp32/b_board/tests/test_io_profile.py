#!/usr/bin/env python3
"""Check the B profile against the user's confirmed revision-4 GPIO map."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
kconfig = (root / "main/Kconfig.projbuild").read_text()
block = re.search(r"(?ms)^config B_LASER_ACTIVE_LEVEL\n(.*?)(?=^config |^choice |\Z)", kconfig)
assert block, "B laser polarity setting is missing"
assert re.search(r"(?m)^\s+default 1\s*$", block.group(1)), (
    "Schematic: illumination -> LM393 low; shade -> pull-up high. "
    "B's default must measure the HIGH interval."
)
defaults = (root / "sdkconfig.defaults").read_text()
assert "CONFIG_B_LASER_ACTIVE_LEVEL=1" in defaults.splitlines()
board = (root / "main/board_io.c").read_text()
pins = re.search(r"b_laser_pins\[HARP_LASERS\]\s*=\s*\{([^}]+)\}", board)
assert pins
assert [int(v.strip()) for v in pins.group(1).split(",")] == [
    4, 5, 46, 6, 18, 7, 9, 15, 10, 16, 11, 17
], "1OUT..12OUT must follow the confirmed header GPIOs, not old IO labels"
encoders = ((13, 14, 12), (40, 39, 41), (47, 21, 38), (2, 42, 1))
for index, group in enumerate(encoders, 1):
    for signal, gpio in zip(("A", "B", "SW"), group):
        name = f"B_ENC{index}_{signal}"
        block = re.search(rf"(?ms)^config {name}\n(.*?)(?=^config |^endmenu|\Z)", kconfig)
        assert block and re.search(rf"(?m)^\s+default {gpio}\s*$", block.group(1)), name
        assert f"CONFIG_{name}={gpio}" in defaults.splitlines(), name
print("PASS: confirmed OUT1..12/EC11 GPIOs and HIGH-active build defaults")
