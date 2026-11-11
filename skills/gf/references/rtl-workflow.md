# Portable RTL workflow

Use this path for authorized portable RTL creation/repair. The [execution contract](codex-runtime.md) governs scope, host selection and evidence; reuse it when already read. Official Gowin implementation uses the [vendor contract](gowin-vendor-flow.md).

For a bounded patch to existing RTL, follow [local edits](local-edits.md) and finish there after the selected checks. Use the steps below for new RTL or broader work; an output register or status LED addition does not require all these branches.

## Direct path

1. Read relevant RTL/project instructions and current working inputs. Reuse supplied requirements; ask one focused question only for a consequential unknown.
2. For a small, fully specified module or FSM, work directly. Keep a short in-chat design when useful; a separate saved plan, codebase map, IP scan or worker is not a prerequisite.
3. For a complex multi-module design, establish interfaces/dependencies with `gf-plan`; use `gf-build` only when authorized independent construction is useful. Planning alone does not authorize implementation.
4. Implement only the requested change, preserving existing conventions, generated/protected IP and unrelated work. For a bug, reproduce the reported behavior with a targeted check before correcting it when feasible.
5. Select checks by affected behavior and the requested evidence; an existing focused check is preferable to a new test environment. Run the checks required by the requested outcome. Use `gf-lint` for lint and `gf-sim` for self-checking RTL simulation; select the real source/model boundary before tool discovery. A verification-only request does not authorize functional repair.
6. After an authorized fix, re-run the failed check. Inspect exit status and expected outcomes; completion or `$finish` alone does not prove functional PASS. Stop repeated no-progress retries and report the unresolved evidence/decision.
7. Report what changed, the actual checks/results and remaining gaps. Analysis/explanation needs no generated artifacts; a structured result block is required only when its consumer requests one.

## Conditional detail

Read one matching reference, only when the stated branch occurs:

| Branch | Reference |
|---|---|
| Multi-component coordination, authorized workers or structured orchestration | Relevant section of [coordination](rtl-coordination.md) |
| Missing requirements that cannot be inferred | Relevant scenario in [intake](rtl-intake.md) |
| An actual check/diagnostic needs command detail | Relevant section of [verification examples](rtl-verification.md); prefer the selected verification skill's current host guide |
| A worked example for a particular request | Matching example in [request examples](rtl-request-examples.md) |
| Authorized implementation of an existing plan | [Plan execution](rtl-plan-execution.md) |
| User requests additional workflow tips/commands | [Command discovery](rtl-command-discovery.md) |

Do not load all linked skills/references merely because they are listed. Read host-call detail only when executing software. Existing authorization remains valid; downloads/installations, hardware programming and publication retain their separate scope boundaries.
