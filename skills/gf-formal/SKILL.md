---
name: gf-formal
description: >
  Formal verification from natural language. Generates SVA properties,
  configures SymbiYosys, runs proofs, and explains results.
  Example: "formally verify the FIFO never overflows"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow formal verification

Resolve the exact module, properties, environment assumptions, reset/initial-state model and proof boundary. On a Linux host configured as described in the collection README use `"${OSS_CAD}" sby <arguments>`; its child environment supplies Yosys/solvers. Discover only the required engine/solver; missing prerequisites are BLOCKED, not an installation request.

1. State the property and assumptions, then choose BMC, prove, cover or live for the requested conclusion.
2. Confirm the installed frontend supports the selected assertion syntax before using a template. Prepare property/config files only within authorized writing scope.
3. Run the selected task in an isolated verification directory, inspect its actual status/log/trace, and report proof scope, assumptions, bounds, tool/version, input identity and remaining gaps.

| Need | Reference |
|---|---|
| A selected .sby mode/task configuration | Relevant template in [SBY configurations](references/sby-templates.md) |
| A matching property pattern | Relevant pattern in [SVA examples](references/sva-patterns.md); verify frontend support |
| Engine/solver choice | [Engines and strategy](references/engines-and-strategy.md) |
| An actual counterexample or induction failure | [Counterexamples](references/counterexamples.md) |
| Equivalence or advanced proof extensions | [Extensions](references/extensions.md) |

Formal is not mandatory for every RTL/FSM request. A bounded result, unreachable cover or vacuous property is not an unbounded functional proof. Verification alone does not authorize functional RTL repair. Use PASS/FAIL/BLOCKED/NOT_RUN and retain counts of proved/failed/covered properties when requested.
