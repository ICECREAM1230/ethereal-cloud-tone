---
name: gf-project
description: >
  Manages .gateflow/project.yaml for project-specific configuration.
  Auto-detects project settings or prompts user for board, HDL, and target.
  For official Gowin projects, preserves .gprj as the authoritative project
  manifest. Used internally by other skills — not typically invoked directly.
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow project context

Use existing `.gateflow/project.yaml` when present, together with the actual manifest/build files. If absent, derive in-memory context; creating configuration requires an authorized configuration/implementation need. Reuse confirmed board context for the current project.

For Gowin, `.gprj` Device/enabled FileList entries are authoritative; process files are supplementary. Preserve generated/protected IP and exact part/device-version requirements. A generic extension scan never replaces that manifest.

- For deriving context or validating existing inputs: relevant section of [context/health](references/context-and-health.md).
- For an authorized configuration create/update: relevant section of [schema/templates](references/schema-and-templates.md).

Record the actual operation and evidence using PASS/FAIL/BLOCKED/NOT_RUN. Do not create project metadata merely because another skill requested read-only context.
