---
name: gf-lint
description: >
  SystemVerilog lint checker with structured output for orchestration.
  Runs Verilator lint, categorizes errors/warnings, explains issues,
  and returns a parseable result block for `$gf` orchestration.
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow lint

1. Resolve the requested top, active files, compile order, includes, defines, parameters, language and libraries from real project evidence. Gowin uses enabled `.gprj` entries.
2. Select the actual host/tool. On a Linux host configured as described in the collection README run `"${OSS_CAD}" verilator --lint-only -Wall <resolved options and files>`; read the matching host guide in the execution contract only when needed. A PATH miss is not proof of absence; unavailable prerequisites are BLOCKED, with no automatic installation.
3. Capture the exit code and actual diagnostics. Review warnings in context; preserve behavior and use a waiver only with a specific justification. Verification-only requests do not authorize logic fixes.
4. Report selected inputs, tool/version, command, exit code, findings and freshness. PASS covers only executed lint, not simulation, CDC, synthesis or timing.

| Need | Reference |
|---|---|
| Meaning of a diagnostic actually present | Relevant entry in [warnings](references/warnings.md) |
| Requested structured result/examples | [Reporting](references/reporting.md) |
| Waivers, control files or machine-readable output | [Output options](references/waivers-and-output.md) |

Re-run lint after an authorized fix; do not treat a formatted result block as stronger evidence than the actual tool run.
