---
name: gf-synth
description: >
  Synthesize SystemVerilog/Verilog with Yosys for explicit open-source flows.
  For official Gowin .gprj projects, route synthesis through GowinSynthesis
  and gw_sh/Tcl; report area, timing, and resource evidence.
  Example: "synthesize my design for iCE40"
---
Read [the shared execution contract](../gf/references/codex-runtime.md) once for the task; it routes software calls by actual host. Preserve scope and use only requested tools.


# GF-Synth — Yosys Synthesis Skill

## Gowin Official-Project Boundary

This skill's generic Yosys flow is not the default implementation path for a Gowin official project. If the project contains `.gprj`, inspect the enabled `.gprj` `FileList`, exact Arora V device/version, and process configuration, then route official synthesis through `gw_sh`/Tcl and GowinSynthesis. Preserve the generated reports and netlist as evidence.

Use Yosys with `target_family: gowin` only when the user explicitly selects the open-source flow and the exact device/resources are supported. Do not feed encrypted Gowin IP or vendor-only primitives to Yosys as if they were portable RTL. See the GateFlow Gowin vendor-flow reference for the boundary and evidence rules.

## Tool Detection

For official Gowin synthesis use [the vendor contract](../gf/references/gowin-vendor-flow.md) and the selected host's Gowin guide. For an explicitly selected Yosys flow, use the matching host-call leaf to discover the existing installation and validate its version. A PATH miss alone does not prove absence. Report unavailable required dependencies as BLOCKED; do not install automatically.

## Pre-Synthesis SV Subset Check

Before synthesis, scan for unsupported constructs:
```bash
grep -rn "^\s*interface\s\|^\s*modport\s\|^\s*class\s\|^\s*bind\s" <files>
```

If found, warn user. Do NOT proceed — it will produce confusing errors.

## Workflow

1. Check project context (`.gateflow/project.yaml`) for target
2. Pre-synthesis lint for unsupported SV constructs
3. Map board to Yosys synth target
4. Run synthesis via sv-synth agent or directly
5. Parse stat output for LUT/FF/BRAM/DSP
6. Report structured result

## Result Format

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
RESOURCES:
  LUTs: N
  FFs: N
  BRAM: N
  DSP: N
TARGET: ice40 | ecp5 | gowin | xilinx | generic
FILES: [synth output files]
DETAILS: [summary or error explanation]
---END-GATEFLOW-RESULT---
```

For an official Gowin project, use `BLOCKED` when synthesis was requested but `gw_sh`, the license, device data, or a required project input is unavailable. Use `NOT_RUN` when synthesis was outside scope or deliberately skipped. Record existing synthesis-output freshness separately as `current`, `stale`, or `unknown`; `STALE` is not an execution status.
