# Validation

## Functional checks

Use the actual defines, includes, initialization data, IP wrappers, and compatible device models. If a simulator cannot run a protected model, report the gap or use a supported simulator. A constant-output substitute does not validate the block.

| Change | Check |
|---|---|
| Pipeline or handshake | Values, order, valid alignment, throughput, CE stalls, backpressure, reset flushing |
| Arithmetic or DSP | Signed limits, full product, carry, overflow, rounding, saturation, recurrence |
| RAM or ROM | Initialization, ports, collisions, byte writes, CE/OCE, reset, first valid output |
| Counter, FSM, or arbiter | Enable/load/clear order, wrap, simultaneous requests, illegal-state recovery |
| Clock or CDC | Clock relationships, event delivery, bus consistency, lock, reset, restart |

When latency changes, compare transactions rather than old cycle numbers. Keep the expected algorithm and numeric behavior intact.

## Mapping checks

Check the synthesis results for:

- Actual DSP, BSRAM, SSRAM, FF, and LUT types and counts.
- Hard-block register absorption, output mode, CE/reset compatibility, memory geometry, and forwarding logic.
- Unexpected latches, loops, multiple drivers, disconnected or trimmed logic, ignored attributes, and LUT/FF growth.

Label resource estimates as estimates. Use actual mapped primitives for measured counts.

## Optimization checks

Compare the same target device, speed grade, and valid constraints. Explain how the critical structure changed and report latency, input interval, resource cost, and routing cost.

Use [timing analysis](timing.md#diagnose-paths) for post-route acceptance. Simulation checks behavior; synthesis checks mapping; post-route STA checks physical timing. A successful exit or a bitstream alone does not prove timing closure.

Report completed checks and pending results. If implementation is incomplete, mark physical timing as pending.
