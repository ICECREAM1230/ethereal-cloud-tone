---
name: gf-architect
description: >
  Codebase architect - Maps and documents SystemVerilog projects.
  This skill should be used when the user wants to understand a codebase structure,
  generate architecture documentation, or onboard to a new RTL project.
  Example requests: "map this codebase", "document the architecture", "show module hierarchy"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow architecture mapping

Use the selected manifest/top and current source set. For Gowin, enabled `.gprj` entries and the exact device remain authoritative. Answer a focused hierarchy/FSM question directly from relevant RTL; a full persisted map is a separate deliverable.

1. Establish scope, active sources, includes/defines/parameters and current input identity.
2. Analyze only the requested modules/relationships. Existing maps are evidence only within their recorded input identity; compare with current RTL before using them.
3. Return source-backed findings. Persist `.gateflow/map/` only when requested or necessary for authorized implementation; describe gaps/unknowns.

| Requested detail | Reference |
|---|---|
| Multi-file scanning, partitioning or report synthesis | [Mapping workflow](references/mapping.md) |
| A saved map's layout and main index | [Map index](references/map-index.md) |
| Hierarchy/signal-flow documentation | [Hierarchy and signals](references/hierarchy-and-signals.md) |
| Clocks, resets or CDC map | [Clock domains](references/clock-domains.md) |
| State-machine extraction or documentation | [FSM schema](references/fsm.md) |
| Packages/types/functions/macros | [Language objects](references/language-objects.md) |
| Verification/interfaces/classes/generate/DPI | [Advanced objects](references/verification-objects.md) |
| Module pages or compilation recipe | [Modules and recipe](references/modules-and-recipe.md) |
| Textual scan pattern examples | [Scan patterns](references/scan-patterns.md) |
| Saved-map refresh, metadata, tracing or diagnostic gaps | [Incremental mapping](references/incremental-and-diagnostics.md) |

Delegate independent analysis only when authorized and useful. A diagram or source scan is not functional, CDC or implementation verification.
