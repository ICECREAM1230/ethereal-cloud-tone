---
name: gf-pcb
description: >
  KiCad schematic and PCB generation from natural language.
  AI-verified drafts with DRC/ERC/AI review loop.
  Example: "design a breakout board for iCE40 with SPI flash and 2 PMODs"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow PCB workflow

Resolve actual KiCad project files and the requested operation. Use the installed KiCad-oriented skills for schematic/PCB work; discover only the tools needed for that operation. Keep generation, DRC/ERC, review and manufacturing outputs scoped to the request.

| Operation | Reference |
|---|---|
| Tool execution or design artifact generation | [Execution](references/execution.md); consult only the needed command in [KiCad CLI](references/kicad-cli.md) |
| Interpret actual DRC/ERC or review board constraints | Relevant section of [review](references/review.md) |
| Explicitly requested manufacturing package | [Manufacturing](references/manufacturing.md) |

State confidence/evidence and unvalidated electrical/manufacturing assumptions. Creating files does not authorize ordering, publication or hardware writes.
