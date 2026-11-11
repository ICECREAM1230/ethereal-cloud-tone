---
name: gf-cocotb
description: >
  Python-based testbench generation using Cocotb. Alternative to
  SystemVerilog testbenches for Python-native hardware engineers.
  Example: "create a cocotb test for the FIFO", "write Python testbench"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow Cocotb tests

Use for requested Python-based RTL testbenches. Resolve the DUT, simulator, source/model set, clock/reset and expected behavior before choosing a template. Check the actual installed Cocotb version/API and simulator compatibility; missing dependencies are BLOCKED, not automatic installation.

Create self-checking tests within authorized writing scope, with deterministic setup, bounded waits and scheduler-safe sampling. Run the selected tests when requested and report actual checks/results; merely reaching the end is not functional PASS.

| Need | Reference |
|---|---|
| Minimal test structure | [Basic test](references/basic-test.md) |
| Makefile or pytest runner | [Runners](references/runners.md) |
| FIFO stimulus/checks | [FIFO test](references/fifo-test.md) |
| Valid/ready backpressure | [Handshake test](references/handshake-test.md) |
| State-machine test example | [FSM test](references/fsm-test.md) |
| An installed cocotbext library | [Protocol libraries](references/protocol-libraries.md) |
| Choosing a testbench approach or structured reporting | [Selection and results](references/selection-and-results.md) |

Templates are starting points: match current API, reset/encoding and DUT timing before execution. Software calls follow the selected host guide; preserve vendor model boundaries.
