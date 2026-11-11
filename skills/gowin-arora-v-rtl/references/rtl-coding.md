# RTL coding

## Pipelines and interfaces

Define input acceptance, output order, input interval, backpressure, reset flushing, and numeric precision before changing a pipeline. Report latency and input interval before and after the change.

- Delay data, valid, channel IDs, addresses, coefficients, write enables, and flags together.
- If CE stalls the pipeline, count latency in enabled clock edges. Keep valid and data under the same stall rule.
- For ready/valid interfaces, hold stalled outputs stable and preserve transaction order.
- Recheck feedback loops. Extra registers can change accumulators, IIR filters, phase updates, and state recurrences. Use retiming, lookahead, or interleaved independent states only when the recurrence remains correct.
- Preserve external protocol timing, numeric precision, and throughput. Adapt callers when latency or handshakes change.
- Prefer CE and pipelining when another clock domain is unnecessary.

## Registers and reset

Use the project's language standard. Use `always @*` with complete assignments for Verilog combinational logic and nonblocking assignments for registers.

Use one clock and a supported CE and set/reset structure per register. Define required power-up values. When a register has both an initial value and a reset/set value, keep them consistent. See [register coding rules](manuals/markdown/SUG949E.md#pdf-page-11).

Reset only the state that must be observable after reset. Resetting valid and control instead of every data register can reduce fanout and improve hard-block register absorption. Check invalid outputs, simulation X values, first valid data, and software readback.

Keep RAM writes separate from output-register reset. A BSRAM output reset does not clear its array; see [memory reset](memory.md#reset-and-initialization).

Avoid unintended latches, internal tristates, combinational loops, and multiple drivers. Preserve required top-level IO tristates.

## Widths and signed arithmetic

Define operand ranges, full result widths, sign extension, truncation, rounding, and saturation at each arithmetic boundary.

Check carry bits, product width, shift width, unsigned slices, unsized constants, and parameter limits. A wide destination cannot recover bits lost in a narrow intermediate result. Keep the left operand signed for arithmetic right shifts.

When reordering arithmetic, verify finite-width behavior at the limits. Reduce widths only from proven value ranges.

## FSMs and module boundaries

Keep arbitration priority distinct from mutually exclusive decoding. Do not use `full_case` or `parallel_case` to hide missing behavior.

Choose FSM encoding from decode timing, register use, reset, and illegal-state recovery. Consider one-hot encoding for timing; do not impose it based only on state count.

Group related logic and register module boundaries where that improves a real path. Account for any added interface latency.

For tests, see [validation](validation.md).
