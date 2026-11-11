# Planning Gowin RTL Changes

Read this reference only when a task changes RTL, changes a clock-domain crossing, or edits timing constraints for the selected `.gprj`.

## Establish the affected design contract

Before editing, determine only the facts that can affect the requested change:

- the selected `.gprj` and the enabled top, source, and constraint files involved;
- any official Gowin or board-vendor example supplied by the user;
- each affected clock and reset domain, including source, actual frequency, derivation, crossings, and synchronization method;
- whether the change depends on generated IP and, if so, its confirmed module interface;
- what result must be validated.

Keep this inventory limited to the affected blocks. Do not map unrelated domains or files.

## Reuse official interfaces

When an official or board-vendor reference module is available, preserve its port directions, handshake semantics, reset convention, naming, and transfer sequence unless the requested behavior requires a change.

State which interface behavior is retained and which behavior changes. Do not introduce a new interface abstraction unless the reference design cannot satisfy a stated requirement.

## Resolve clocks and constraints

Before changing CDC logic or timing constraints:

1. Confirm each affected clock's source, actual frequency, and derivation path.
2. Identify the source and destination domain of each affected crossing and its synchronization method.
3. Match constraint names and case to actual top-level ports or generated clocks.

Treat comments, signal names, and pin assignments as clues, not proof. Treat an unconstrained generated clock as an implementation risk until Gowin timing reports confirm the path and frequency.

## Set the validation target

Choose the smallest result that proves the requested change:

- RTL behavior;
- synthesis and module connectivity;
- pins, constraints, placement, and routing;
- timing and bitstream generation.

Do not run a full implementation when an earlier result is sufficient. Do not treat third-party lint or simulation as proof that a Gowin project implements correctly.

## Report the result

Separate observed project evidence from inference. Report the retained interface contract, intentional changes, unresolved inputs, validation performed, and any risk that still depends on Gowin timing or implementation results.
