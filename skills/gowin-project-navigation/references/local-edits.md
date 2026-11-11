# Bounded Gowin edits

Use for an existing project's local RTL change, known pin/CST adjustment or status LED addition. Classify this scope before the broad navigation/IP/build workflow. A `.gprj`, multiple changed files or a constraint filename alone does not request an implementation run.

1. Reuse the selected `.gprj`; inspect only its relevant device/version and enabled source/constraint entries. Read the affected RTL/constraint statements and immediate consumers, preserving user work and generated/protected IP.
2. Resolve only the facts needed by the edit: clock/reset and intentional latency for a register; current port name and supplied package/bank/voltage evidence for a pin; state decode, polarity and wiring for LEDs. Pause for a required unresolved choice.
3. Edit directly within the requested scope. For portable logic use existing lint/targeted behavior checks. For a constraint edit check the affected syntax, object existence and physical/electrical or timing consistency. A pure CST edit needs no RTL testbench.
4. Report what was checked and stop. A source edit invalidates relevant downstream evidence but does not itself require synthesis, PnR, STA, IP regeneration or bitstream generation. Run those only when needed for the requested conclusion or an explicit project check; reuse fresh upstream stages where valid.

Read only the branch that is needed:

| Affected concern | Detail |
|---|---|
| Existing top-level pin assignment | [IO_LOC](cst-physical-constraints.md#io_loc-package-pin-placement), plus only affected electrical/bank data |
| Changed pad electrical properties | [IO_PORT](cst-physical-constraints.md#io_port-electrical-properties); connected-device supply-state checks when relevant |
| Placed internal primitive, group or routing constraint | Only that statement section in [CST](cst-physical-constraints.md) and its required synthesis evidence |
| Clock/I/O delay/timing exception edit | Only the corresponding command and object-collection sections in [SDC](sdc-timing-constraints.md) |
| A generated IP configuration/interface is actually affected | [IP checks](gowin-ip-checks.md), then that IP's generation/interface guidance if needed |
| User requests an official implementation/timing result | Selected stage in [project/build Tcl](tcl-project-build.md), then the actual host's invocation guide |

When a GateFlow task uses its local-edit guide, reuse that scope/check selection rather than restarting intake. No full map, board/IP inventory, saved plan, independent worker or final-PnR inspection is required merely to edit a known top-level port constraint. Actual implemented placement/timing claims still require current official evidence. Device programming remains separately authorized.
