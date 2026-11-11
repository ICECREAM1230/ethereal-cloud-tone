# Gowin IP Checks

Use when a generated-IP resource, configuration or interface is actually being selected, changed, integrated or validated. Existing local RTL/CST/SDC edits use [bounded edits](local-edits.md); a constraint filename or an unaffected IP elsewhere in the project is not a trigger.

## Gowin Tool Boundary

Gowin-generated IP, including memory/FIFO, PLL/OSC/CLKDIV, DSP/multiplier blocks, encrypted netlists, and generated primitive wrappers, must be validated with Gowin Software.

Do not treat Verilator, Yosys, Quartus, Vivado, or other third-party tools as proof that Gowin IP compiles or implements correctly. They may not support Gowin primitives, protected files, generated sources, or project metadata.

For claims about implemented Gowin IP, pin placement/electrical configuration, routing, timing or bitstream results, use current official evidence. Source/static checks may support an edit-only result at their stated scope. When an official run is needed, confirm the selected `.gprj`, exact device, top and relevant inputs/options, then read only the matching [Ubuntu](gowin-build-ubuntu.md) or [Windows](gowin-build-windows.md) invocation guide.

Select the smallest official stage set that establishes the requested conclusion and has valid fresh prerequisites. A project script may expose `syn`, `pnr` or `all`; verify its accepted arguments first. Reuse suitable fresh synthesis for a constraint-only PnR/STA check. Use a complete source-to-implementation run only when requested or required by actual invalidated prerequisites. A local edit or stale historical report alone does not request any build stage.

If a different project has no vetted Tcl script, use the installed Gowin Tcl documentation to construct a project-specific invocation. Do not reuse this repository's top or options for that project. Gowin Programmer and USB permissions are separate from implementation; never infer programming success from a build.

## Validation levels and evidence binding

Report only the levels actually completed; keep their evidence separate.

| Level | Required evidence and limit |
|---|---|
| IP generation | Actual saved IPC/backend configuration, generated wrapper and instance template match the required device, geometry, modes and ports. File existence or exit code 0 does not prove that requested settings took effect. |
| Functional simulation | Identify the actual model and its version/hash, test inputs and observed results. Official-model simulation checks covered behavior; it does not establish physical timing, pin configuration or board behavior. |
| Official synthesis | Current Gowin synthesis log, netlist and resource report identify the intended device, top and sources. Synthesis success does not establish final pin placement or timing closure. |
| PnR and STA | Current implementation reports establish actual pin locations/electrical settings and the constrained setup/hold results. Check clock coverage, rejected constraints and relevant warnings; a small isolated probe is not evidence for the complete design. |
| Device loading and identity | Bind the programmed file hash, target/cable identity, operation and actual tool result to the run. Status/user codes or a firmware startup identity are not byte-for-byte readback verification unless that verification actually ran. |
| Board function | Record the runtime/protocol/electrical or audio checks actually measured. Loading, startup, a network connection or a protocol self-test does not establish every application function. |

Bind each claim to a reproducible run:

- Record the executable path/version, working directory, command, actual project/IP options, necessary non-sensitive environment, exit code and raw logs. Keep version-specific library/CLI workarounds in the matching tool reference; do not alter installed libraries or licenses to hide a failure.
- Identify the selected manifest and resolved enabled sources, generated IP, CST/SDC, build Tcl and effective options with a source/configuration snapshot or hashes; bind the resulting netlist, reports and programming artifact hashes to that input. A commit alone does not identify a dirty working tree.
- Read the actual saved configuration and reports, including ignored/rejected settings and warnings. Use [official IP generation](../../gowin-ip-generation/SKILL.md) for IPC assertions; successful commands and printed property tables are insufficient.
- If source/configuration hashes or tool inputs have changed, retain old evidence as historical. Rerun the affected stage before claiming current validation when that validation is required; an edit-only request may instead report implementation NOT_RUN. Do not silently reuse a stale report or mix a source snapshot with another build's artifact.

## Choose Generated IP or RTL

Prefer Gowin-generated resources for memory, ROM, asynchronous FIFO, PLL/OSC/CLKDIV, DSP or multiplier blocks, and PHY resources.

Use handwritten RTL for control state machines, packet or frame formatting, PCM aggregation, and simple handshake bridges. A protocol is not by itself a reason to use generated IP. Use protocol IP only when Gowin provides it and the user chooses it.

## IP Generation Handoff

Before integrating generated IP, specify its required configuration, including:

- module name
- width and depth
- clock domains
- reset behavior
- FWFT/output-register options
- latency assumptions
- expected port interface

Use the user-provided or generated instantiation template as the authority for port names, reset polarity, FWFT behavior, register options, and latency.

If no confirmed template is available and generation is within the user's authorized scope, use the installed official generator to produce the actual configuration and template in an isolated project; read [Official IP generation](../../gowin-ip-generation/SKILL.md). If the user has reserved generation for themselves, or the required tool/device support is unavailable, record the requirements, available evidence and specific handoff needed. Pause the current task and ask the developer to resolve the missing interface or required decision. Do not edit integration RTL against an assumed interface or continue separate work while waiting.

Do not invent or hand-write a replacement for Gowin IP unless the user explicitly requests a source-level fallback.
