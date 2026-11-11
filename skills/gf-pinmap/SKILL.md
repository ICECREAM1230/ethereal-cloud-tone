---
name: gf-pinmap
description: >
  Board-aware pin mapping. Generates FPGA constraint files (.xdc/.pcf/.lpf/.cst)
  with correct pin assignments, I/O standards, and drive strength for target boards.
  Example: "map SPI to PMOD JA on Arty A7", "generate constraints for my iCEBreaker"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow pin mapping

A change to a known existing pin/LED mapping uses [local edits](../gf/references/local-edits.md). Reuse supplied verified board/pin data and check only the affected ports, pins and electrical conditions. Board-database discovery and a complete constraint-file generation workflow apply only when that information or deliverable is needed.

Identify the exact board/revision, FPGA/package, RTL ports and requested peripherals from project evidence. For Gowin, the selected `.gprj` device and source/constraint list remain authoritative. Reuse verified board evidence already supplied or present in the project. Inspect an actually present curated database only if needed facts are missing; do not assume one is installed.

- For board discovery, curated data or fallback evidence: [board evidence](references/board-evidence.md).
- For electrical compatibility, output formats or PMOD mapping: relevant section of [electrical checks](references/electrical-and-formats.md).

Never guess pins or bank voltages. Preserve the documented user-confirmation boundary for unverified fallback pin data. Generate constraints only within authorized scope and cross-check every mapped RTL port, I/O standard and voltage bank. Programming is a separate authorization.
