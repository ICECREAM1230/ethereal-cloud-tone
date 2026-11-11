---
name: gf-fusesoc
description: "FuseSoC build system integration for GateFlow. Generates .core files and drives synthesis/simulation through Edalize backends (Vivado, Quartus, open-source tools). Use when the user needs to create a FuseSoC core file, build with Edalize, or integrate RTL into a FuseSoC project."
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow FuseSoC integration

Use the actual project manifest, compile order, target and installed FuseSoC/Edalize version. Create/edit a `.core` only within authorized scope, then run only the requested backend/target. An official Gowin `.gprj` stays authoritative; an auxiliary core does not replace it.

| Operation | Reference |
|---|---|
| Create/edit a core | [Core schema](references/core-schema.md) |
| Select types, dependency constraints or backend | Relevant section of [dependencies/backends](references/dependencies-and-backends.md) |
| Run a target or produce structured results | [Execution](references/execution.md) |

Verify backend options against installed help. Record tool/version, input identity, command, exit code and outputs. Installing a tool, programming hardware or publishing requires its own authorization.
