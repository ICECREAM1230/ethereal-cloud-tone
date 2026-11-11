---
name: gf-learn-ctx
description: >
  Contextual learning — micro-lessons embedded in workflow output.
  Explains hardware concepts on first encounter, tracks what has been
  taught, generates practice exercises on demand.
  Used internally by the GateFlow orchestrator.
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow contextual explanations

When a new concept matters to the user's current work, give a short explanation using the current conversation/project context. Skip repeated lessons; provide exercises or cross-HDL comparisons only when useful/requested.

- For a concept-specific example or requested exercise: relevant section of [micro-lessons](references/micro-lessons.md).
- Only for explicitly requested persistent progress/spaced repetition: [learning progress](references/progress.md).

Do not create a global learner profile as a side effect. Update an existing profile only when the user explicitly requests persistent learning progress. A lesson is explanation, not verification evidence.
