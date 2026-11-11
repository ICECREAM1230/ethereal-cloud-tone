---
name: gf-pnr
description: >
  Place and route with nextpnr for explicit open-source FPGA targets.
  For official Gowin .gprj projects, use Gowin Software through gw_sh/Tcl;
  keep nextpnr as an opt-in alternative.
  Example: "place and route for iCEBreaker", "run P&R targeting ice40"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow place and route

For an official Gowin `.gprj`, use the [vendor contract](../gf/references/gowin-vendor-flow.md) and the selected host's Gowin guide; enabled project inputs, exact device/version and fresh upstream outputs are authoritative. Nextpnr/Yosys are not prerequisites or automatic fallbacks.

For an explicitly selected supported open-source target, verify the exact device, synthesis output, constraints and installed backend, then read the relevant target in [open-source execution](references/open-source-execution.md). Use [timing/constraints](references/timing-and-constraints.md) only for the requested format or actual report diagnosis.

Run only requested stages with fresh prerequisites. Capture tool/version, input identity, command, exit code, timing/utilization and outputs. Distinguish timing failure from execution completion; never describe an open-source result as official Gowin evidence. Bitstream creation does not authorize programming hardware.
