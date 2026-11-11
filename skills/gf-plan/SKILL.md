---
name: gf-plan
description: "Use when the user requests a design plan or a new RTL architecture needs consequential interface, clock-domain or multi-module decisions. A bounded register, constraint or status-LED edit does not need this skill automatically."
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow design planning

For an existing-design local edit without an explicit planning/build request, use [local edits](../gf/references/local-edits.md) and return after the selected checks. Multiple edited files do not by themselves require this workflow.

Plan at the depth of the requested design. A small module/FSM can use a short in-chat plan; a multi-module design needs interfaces, dependencies and a verification strategy. Planning alone does not authorize implementation or saved files.

1. Reuse supplied requirements and current project evidence. Ask one question only for a consequential unresolved choice.
2. Specify behavior, interface/latency, clocks/resets and acceptance checks. For an FSM, enumerate states, transition conditions/priorities and output timing; mark unresolved behavior.
3. Return the requested plan. Create a map or plan file only when requested or needed for authorized implementation. Handoff only after implementation is authorized.

Read only the applicable topic:

| Need | Reference |
|---|---|
| Missing requirements or existing integration context | [Planning inputs](references/requirements.md) |
| Multi-module hierarchy or bus architecture | [Architecture](references/architecture.md) |
| Clock/reset-domain design | [Clocks and resets](references/clocks-and-resets.md) |
| A worked FSM planning example | [FSM](references/fsm.md) |
| Pipeline stages/backpressure | [Pipeline](references/pipeline.md) |
| Ports, register maps, transaction/descriptor formats | [Specification examples](references/specification.md) |
| Multi-file implementation phases/dependencies | [Implementation planning](references/implementation.md) |
| Test/property/coverage planning | [Verification](references/verification.md) |
| A requested saved plan or handoff checklist | [Plan output](references/plan-output.md) |
| Requested area/power/latency/risk estimates | [Estimation](references/estimation.md) |

For detailed RTL design patterns use the relevant section of [design patterns](references/design-patterns.md); for a specific SV construct use [SV constructs](references/sv-constructs.md). Read [build/tool planning](references/build-and-tools.md) or [DFT/review checklists](references/dft-and-checklists.md) only when that concern is in scope. Example parameters and coverage percentages are not project requirements.
