# Tracing Gowin build inputs

Read this only when diagnosing the selected top, constraint objects, or active clock path.

## Selected top

- Start from the selected `.gprj` and its enabled HDL files.
- Corroborate the selected top with available process settings, the generated synthesis `.prj`, and synthesis reports. Treat generated evidence as a record of its run, not proof about current sources.
- When evidence conflicts, inspect enabled top-level candidates and their instantiated hierarchy. Report the conflict instead of choosing from filenames alone.

## Constraints

- Locate enabled `.cst` and `.sdc` files through the project manifest.
- Match constraint object names and case against actual ports of the selected top.
- Report missing, disabled, unmatched, or stale constraints explicitly. For file roles or generated-report locations, read the [artifact map](project-artifact-map.md) only if needed.

## Clocks

- Trace the relevant signal from its top-level input, oscillator, or IP output through RTL assignments, module ports, clock buffers, dividers, PLLs, and clock switching.
- Corroborate the path with timing constraints and reports when available. Treat comments, signal names, and board-pin assignments as clues rather than proof.
- Follow the relevant signal chain; do not recursively scan unrelated design logic.
