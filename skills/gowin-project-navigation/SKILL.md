---
name: gowin-project-navigation
description: Use when locating Gowin project inputs and device settings, tracing RTL and constraints, inspecting build reports, or finding local build and programming tools.
---

# Gowin Project Navigation

Use the selected `.gprj` for the exact device and enabled inputs; scope discovery to the requested root. Simple path, membership and device questions need no extra reference.

On a Linux host configured as described in the collection README, invoke the Gowin CLI through `"${GOWIN_SH}" <path-to-Tcl-script>`; use the existing Gowin launcher for the GUI. The wrapper supplies the verified child-only environment, so do not restore global Gowin PATH/library settings or source shell startup for agent calls. Other hosts or explicitly selected installations use their confirmed launchers.

Choose the actual execution host automatically unless the user specifies another target. Reuse confirmed tools for the task; do not ask for an OS choice. Read only the task and host needed below.

Classify the edit scope first. For an existing output register, known physical/timing-constraint adjustment or a few status LEDs, start with [bounded edits](references/local-edits.md) and read only the affected topic. A Gowin project or constraint file alone does not trigger IP checks, complete RTL planning or an implementation run.

| Need | Read |
|---|---|
| Organize SDC/CST by module, merge constraints before builds, or connect generated constraint files | [Modular constraints with pure Tcl](references/modular-constraints.md) |
| Broader source tracing, RTL/IP planning, report inspection or project maintenance | [Shared navigation rules and task index](references/navigation-workflow.md) |
| A generated IP resource/configuration/interface is actually affected | [IP checks](references/gowin-ip-checks.md) before interface design |
| Create or regenerate IP; find a generator or set IP parameters | [gowin-ip-generation](../gowin-ip-generation/SKILL.md) |
| Linux software invocation for project/build Tcl | [Linux Gowin calls](references/gowin-build-ubuntu.md) |
| Linux startup, synthesis, PnR/bitstream or Tcl errors | [Linux build troubleshooting](references/gowin-linux-troubleshooting.md); read only the failing stage |
| GAO/GVIO, constraint-tool roles or authorized Programmer automation | [CLI tools and Tcl orchestration](references/gowin-tool-automation.md) |
| Linux read-only project scan | [Linux scanner](references/project-scan-ubuntu.md) |
| Windows build or scan, only when Windows is the execution target | [Windows build](references/gowin-build-windows.md) or [Windows scanner](references/project-scan-windows.md) |

Preserve generated/protected IP and keep separate projects separate. Portable checks supplement official Gowin implementation evidence. For an unresolved required device, interface or electrical decision, record the evidence, pause the task and ask one question. Building does not authorize programming hardware.
