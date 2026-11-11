---
name: gf-viz
description: >
  Terminal visualization for GateFlow codebase maps. Renders module hierarchies,
  FSM state diagrams, and module detail cards as interactive ASCII/Unicode art.
  Example requests: "visualize the codebase", "show hierarchy", "show FSM", "show module detail"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow source-backed views

Render the view the user requested. Derive a focused view directly from current active RTL; use an existing `.gateflow/map/` only after checking its input identity. Create a map only when requested or needed for authorized implementation.

For a named FSM, read its state register, reset, transitions and outputs, then render only that FSM. For a named module, show that module. A dashboard/menu is appropriate when no specific view was requested.

| Requested view | Read |
|---|---|
| Overview/dashboard | [Overview](references/overview.md) |
| Module hierarchy | [Hierarchy](references/hierarchy.md) |
| FSM states/transitions | [FSM](references/fsm.md) |
| Module detail card | [Module](references/module.md) |
| Navigation/menu or rendering edge cases | [Interaction](references/interaction.md) |
| Reading existing map fields | [Map data](references/map-data.md) |
| Signal trace, timing, differences, connection matrix or search | Relevant section of [additional views](references/advanced-views.md) |

Preserve transition conditions/priorities and reset/output meaning. Label derived timing illustrations as illustrative unless backed by a trace. State missing or stale evidence. Claim an interactive program only when actually discovered and run.
