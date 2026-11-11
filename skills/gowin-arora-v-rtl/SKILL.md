---
name: gowin-arora-v-rtl
description: Write and optimize Verilog/SystemVerilog for Gowin Arora V FPGAs. Use for CFU and carry-chain design, BSRAM/SSRAM and DSP mapping, pipelines, clocks, SDC constraints, and timing closure.
---

# Gowin Arora V RTL

Prioritize performance. Added latency, pipeline changes, and handshake changes are allowed. Update callers and keep data and control aligned. Respect explicit limits on latency, throughput, external protocols, and numeric precision.

## Find the right topic

Read the selected `.gprj` and its enabled inputs to identify the full part, device revision, speed grade, top module, tool version, clocks, and targets. Load only the reference needed for the task. Read manual pages through the page reader in [Manual lookup](references/sources.md), rather than loading a whole manual.

| Task | Reference |
|---|---|
| RTL, bit widths, reset, FSMs, and pipeline behavior | [RTL coding](references/rtl-coding.md) |
| Muxes, counters, carry chains, packing, and fanout | [CFU structure](references/cfu-structure.md) |
| RAM/ROM modes, ports, collisions, reset, and inference | [Memory](references/memory.md) |
| Multipliers, MACs, signed arithmetic, and DSP registers | [DSP](references/dsp.md) |
| PLLs, clock routing, clock switching, and CDC | [Clocks and CDC](references/clocks-cdc.md) |
| SDC commands and examples | [Constraint quick index](references/timing.md#constraint-quick-index) |
| Critical paths and timing closure | [Timing analysis](references/timing.md#diagnose-paths) |
| Functional tests, mapped resources, and acceptance | [Validation](references/validation.md) |
| Exact device support or command syntax | [Manual lookup](references/sources.md) |

## Apply the change

1. Define the required result, transaction order, input interval, stalls, reset behavior, and numeric limits.
2. For new RTL, design around hardware ports and registers. For existing RTL, identify whether the bottleneck is logic, routing, fanout, a hard-block output, or clocking. Make a focused change that addresses it.
3. Verify behavior and mapped primitives. Check resource counts, register absorption, latency, and collisions. Use post-route STA to confirm timing.

Use the `.gprj` as the project manifest and GowinSynthesis with the official Gowin implementation flow. Preserve generated and protected IP. Use `gowin-project-navigation` when project Tcl or IP operations are needed.

Report the change, its hardware reason, latency and throughput changes, completed checks, and pending results. Distinguish estimated resources and timing from measured results.
