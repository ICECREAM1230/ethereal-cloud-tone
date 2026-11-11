---
name: gf-ip-detect
description: >
  Auto-detect IP blocks within FPGA and hardware codebases. Scans for
  module instantiations, identifies missing implementations, matches
  standard IP patterns, and dispatches agents to fill gaps.
  Example: "scan for missing IP blocks", "detect what needs implementing",
  "find unimplemented modules", "auto-fill IP blocks"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow IP inventory and gaps

Inventory the selected top/manifest, includes, defines, language/libraries and enabled sources. A bounded source scan is a reported fallback only when no active manifest exists. Known vendor primitives and generated/protected IP are dependencies, not automatically missing user modules.

1. Trace actual instantiations/definitions and relevant interface/CDC patterns.
2. Distinguish missing source-list inputs, vendor dependencies, stubs and implementation gaps. Report evidence and uncertainty.
3. Keep scan/diagnosis read-only. Auto-fill requires an implementation request and `gf-ip` availability/evidence checks for any selected library block.

| Need | Reference |
|---|---|
| Detection patterns, false positives or severity | Relevant section of [patterns](references/patterns.md) |
| Gowin IP inventory | [Gowin IP](references/gowin-ip.md) |
| Scan/report details | [Scanning](references/scan-and-report.md) |
| Authorized implementation of a confirmed gap | [Gap implementation](references/authorized-fill.md) |

Use only the relevant branch. A feature request does not automatically need a full IP inventory or saved map. Preserve the official project file list and generated/protected IP.
