---
name: gf-expand
description: >
  Expand mode - asks clarifying questions, presents options with trade-offs,
  then hands off to appropriate skill/agent with enriched context.
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow focused clarification

Use existing conversation/project evidence first. If the requested outcome is clear, route directly with `gf-router`. Ask one question at a time only when a missing decision changes the result; a required unresolved decision pauses the task.

Present options only for an actual tradeoff. Preserve prior answers and authorization in the handoff; do not restart intake or turn an analysis request into implementation. Treat example defaults as suggestions, never inferred board/device or interface requirements.

| Need | Reference |
|---|---|
| A question for a specific consequential unknown | Relevant scenario in [questions](references/questions.md) |
| Comparing viable approaches | [Options](references/options.md) |
| Passing clarified context to another workflow | [Handoff](references/handoff.md) |
| User-requested quick-start suggestions | [Defaults](references/defaults.md) |
