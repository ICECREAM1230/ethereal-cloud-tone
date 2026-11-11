---
name: gf
description: "Use for SystemVerilog/RTL edits, creation, debugging and requested verification or FPGA implementation. Covers small existing-design changes as well as larger work; preserves official Gowin project and model boundaries."
---

# GF — SystemVerilog Development

Read [the shared execution contract](references/codex-runtime.md) once for the task. Select the actual execution host automatically; load host-call details only when executing software. Use current project evidence and existing authorization.

On a Linux host configured as described in the collection README, follow [the isolated tool entry points](references/host-linux.md): OSS CAD Suite calls use `"${OSS_CAD}" <tool> <arguments>` and Gowin CLI calls use `"${GOWIN_SH}" <path-to-Tcl-script>`.

For a selected Gowin `.gprj`, its device and enabled `FileList` remain authoritative. Preserve generated/protected IP. GateFlow lint and module simulation cannot establish official implementation timing or board behavior.

Choose the task scope before the domain route. For a bounded edit to existing RTL or constraints, read [local edits](references/local-edits.md) first and stop after its relevant checks. Examples include an output register, one physical constraint or a few status LEDs, including in a Gowin project. A new keyword, more than one file or "edit and test" does not select the complete workflow.

| Requested work | Route |
|---|---|
| Gowin IP work or requested official synthesis, PnR, STA, bitstream, CLI debug/programming | Use `gowin-project-navigation` and [the shared vendor contract](references/gowin-vendor-flow.md); then read only the selected host's call guide |
| Portable RTL creation, bug repair or execution of an authorized plan | [RTL workflow](references/rtl-workflow.md); load verification skills when needed |
| Lint selected portable RTL | [gf-lint](../gf-lint/SKILL.md) |
| Module simulation or vendor-IP system simulation | [gf-sim](../gf-sim/SKILL.md), which selects the simulation boundary first |
| Architecture mapping or design planning | [gf-architect](../gf-architect/SKILL.md) or [gf-plan](../gf-plan/SKILL.md) |
| Formal properties or protocol scaffolds | [gf-formal](../gf-formal/SKILL.md) or [gf-protocols](../gf-protocols/SKILL.md) |
| Authorized parallel module construction | [gf-build](../gf-build/SKILL.md); keep one writer per file/output |

Read only the matching route. Shared RTL, CDC, interface, algorithm and evidence rules apply across hosts. Clarify consequential unknowns one at a time; a required unresolved decision pauses the task. Re-run the failed check after a fix and report actual PASS/FAIL/BLOCKED/NOT_RUN with current input and output identity.
