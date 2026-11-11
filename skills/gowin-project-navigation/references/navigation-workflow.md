# Shared Gowin navigation rules

Read for project-aware changes, constraints, reports or maintenance. Software invocation uses only the matching task/host guide in the entrypoint.

For a bounded existing-design edit, use [bounded edits](local-edits.md) first and return when its selected checks complete. The workflow below is for broader navigation or requested implementation evidence.

## Core workflow

1. Identify the selected `.gprj` from an explicit path. If discovery is needed, search only below an explicitly scoped root; do not choose a project from historical directory names.
2. Treat that `.gprj` as authoritative. Read only the relevant `Device` or enabled `FileList` entries, resolve relative paths from its directory, and keep separate projects separate.
3. Before editing RTL or constraints, decide whether the change may require generated IP or a Gowin implementation result.
   For pads connected to another device, read [CST physical constraints](cst-physical-constraints.md) for both-end IO and supply-state checks before selecting electrical settings.
4. If generated memory/FIFO, clocking, DSP or PHY configuration/interface is actually affected, read [Gowin IP checks](gowin-ip-checks.md) before changing that interface. A local RTL/CST/SDC edit does not load IP checks just because the project contains IP. Requested official implementation uses the selected stage/build guide; generated-IP inspection expands only when relevant to that stage or a real failure.
   For locating a generator or creating/regenerating IP, read [Official IP generation](../../gowin-ip-generation/SKILL.md) for generator calls, input files, settings, and generation errors. Keep generation instructions in that Skill.
5. Read the [IP capability index](gowin-ip-capability-index.md) only when the user asks what Gowin IP types exist or an unfamiliar function must be matched to a possible Gowin IP. Do not read it for a known type such as ROM, RAM, FIFO, PLL/clocking, multiplier/DSP, or PHY; use Gowin IP checks directly.
6. Inspect only the evidence needed for the question. Simple path, membership, and device queries need no reference. Otherwise read the single best-matching reference first and expand only when the evidence crosses modes.

## Read details on demand

| Current need | Resource |
|---|---|
| Classify sources, generated artifacts, reports, IP outputs, or protected files | [Artifact map](project-artifact-map.md) |
| Reconcile manifest and GUI results, inspect process settings or stale evidence, or run the scanner | [Project inspection](project-inspection.md) |
| Use Git evidence, preserve project-package boundaries, move paths, or recover from file locks | [Project maintenance](project-maintenance.md) |
| Trace the selected top, constraints, ports, or clock source | [Build tracing](build-tracing.md) |
| Plan a consequential RTL/CDC/timing contract change beyond a bounded edit | [RTL change planning](rtl-change-cost-review.md) |
| Locate the installed IP generator or automate authorized IP generation | [Official IP generation](../../gowin-ip-generation/SKILL.md) |
| Automate project creation, file management, synthesis, PnR, or bitstream with Gowin Tcl | [Project and build Tcl](tcl-project-build.md) |
| Create, review, or diagnose SDC clocks, I/O delays, timing exceptions, or STA queries | [SDC timing constraints](sdc-timing-constraints.md) |
| Create or review CST pin, I/O, placement, grouping, clock-routing, or configuration-network constraints | [CST physical constraints](cst-physical-constraints.md) |
| Review connected-device IO across configured, reset and unpowered states | [CST physical constraints](cst-physical-constraints.md) |
| Preserve synthesis objects or control fanout, hierarchy, resource inference, FSMs, or I/O inference | [Synthesis attributes](synthesis-attributes.md) |
| Change or validate an affected generated-IP resource/interface | [Gowin IP checks](gowin-ip-checks.md) |
| Discover IP types for an unfamiliar function, or answer what Gowin IP categories exist; skip this for a known IP type | [IP capability index](gowin-ip-capability-index.md) |
| GAO/GVIO automation, constraint-editor roles or authorized programming | [CLI tools and Tcl orchestration](gowin-tool-automation.md) |
| Locate manuals or verify version-specific rules | [Documentation index](document-index.md) |

## Essential boundaries

- The selected `.gprj` controls the device and active files; generated settings and reports describe a tool run and do not supersede it or prove current-source correctness.
- Command references assume Arora V by default, but the exact part, package, device version, and tool version from project evidence always take precedence. Use non-Arora fallback notes only for a selected non-Arora device.
- Keep Gowin project Tcl, SDC, CST, and HDL/GSC synthesis attributes in their own languages and files.
- Third-party lint or simulation may supplement the workflow, but it does not replace Gowin Software for generated IP or implementation results.
- A Programmer executable or USB permission rule does not establish board access. Identify the connected device and validate the selected cable before any authorized programming operation.
- Preserve generated and protected files. Do not decode or reconstruct encrypted implementations.
- If a required device, IP-interface or electrical decision remains unresolved, record the unknown and evidence, pause the current task and ask the developer. Do not proceed on an assumed choice or start separate work while waiting.
- Answer concisely with each relevant path, role, evidence source, and caveat. Distinguish observations from inferences.
