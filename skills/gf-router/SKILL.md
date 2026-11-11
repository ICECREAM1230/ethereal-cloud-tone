---
name: gf-router
description: "Figures out what kind of digital hardware design task the user wants to do, then hands off to the right specialist. Use when the request is unclear, multi-step, or needs help deciding whether to simulate, synthesize, lint, or implement."
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow intent routing

Classify scope before workflow type. A bounded existing-design patch uses the local-edit route, even in a Gowin project or across RTL and constraints. A clear request needs no confidence-scoring ceremony or mandatory clarification.

| User outcome | Route |
|---|---|
| Bounded output-register, constraint or status-LED change | [Local edits](../gf/references/local-edits.md); no planning/build workflow unless a real expansion condition occurs |
| New portable RTL or broader repair | `gf` |
| Affected Gowin IP or requested official implementation result | `gowin-project-navigation` and the [vendor contract](../gf/references/gowin-vendor-flow.md) |
| Lint / simulate / prove | `gf-lint` / `gf-sim` / `gf-formal`, only as requested |
| Plan / map / visualize | `gf-plan` / `gf-architect` / `gf-viz` |
| Authorized independent multi-component construction | `gf-build` |
| Explain existing code | Read relevant sources and explain; no generated files |
| Learn or practice | `gf-learn` |
| Summarize an existing log | `gf-summary` |
| Synthesis/PnR, protocols, IP inventory or pin mapping | The matching named GateFlow skill |

For a consequential ambiguity, ask one focused question using `gf-expand`. Compound requests retain their original scope and execute in dependency order; adding "and test" selects relevant checks, not automatic orchestration. Analysis/planning never silently becomes implementation.

Only when necessary: [intent reference](references/intent-reference.md) for ambiguous classification, [examples](classification-examples.md) or [short examples](references/classification-notes.md) for difficult phrasing, [clarification](references/clarification.md) for an unresolved choice, and [handoff schema](references/handoff.md) for an actual delegated/structured handoff. Responsibility labels are not registered agent types.
