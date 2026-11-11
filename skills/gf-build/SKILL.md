---
name: gf-build
description: "Use when authorized RTL construction has multiple independent components with agreed interfaces and useful dependency coordination. Bounded changes to an existing design do not require this build workflow."
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow component builds

For an existing-design local edit without an explicit planning/build request, use [local edits](../gf/references/local-edits.md) and return after the selected checks. Multiple edited files do not by themselves require this workflow.

Build a single module directly. For multiple components, agree interfaces/shared types, order dependencies, give one writer each file/output, integrate, then run the checks needed for the requested outcome. Delegation is conditional on availability, authorization and independent work; it is not a minimum worker count.

Use the active project baseline, including relevant uncommitted/untracked inputs. Keep parallel outputs isolated. Gowin implementation follows the [vendor contract](../gf/references/gowin-vendor-flow.md), not a generic Yosys/nextpnr substitute.

| Situation | Reference |
|---|---|
| Coordinating dependent components | [Build phases](references/phases.md) |
| Assigning an authorized worker a module or TB | [Prompt examples](references/worker-prompts.md) |
| Dependency scheduling, contention or repeated failures | [Coordination](references/coordination.md) |
| Authorized incremental cache or structured progress/report | [Cache and reporting](references/cache-and-report.md) |

For verification load only `gf-lint`/`gf-sim` or the actually requested specialist. Re-run failed checks after fixes. Report the resulting artifacts, real validation status and remaining gaps; worker completion does not establish PASS.
