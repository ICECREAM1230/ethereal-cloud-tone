# Additional views

Read only when tracing signals, showing timing, map differences, connections or searches. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## View 5: Signal Path Trace

Triggered by: "trace data_in from top to digest_out"

Renders signal path across module boundaries as ASCII with boxes for modules, arrows for signals, and `◈` markers for registered boundaries (pipeline stages). Shows hop count and pipeline stage summary.

## View 6: Timing Diagram

Triggered by: "timing uart_tx" or "timing fsm tx_state"

ASCII waveforms: `┌─┐└─┘` for clock, `───` high, `___` low, `╡val╞` for bus/enum values. Auto-generates from FSM data or known protocol patterns. Accepts WaveJSON input for custom diagrams.

## View 7: Diff View

Triggered by: "diff" or "what changed"

Shows structural changes between map snapshots: `+ ADDED`, `~ MODIFIED` (with specific change: port/instance/FSM/parameter), `- REMOVED`. Requires previous snapshot at `.gateflow/map/.prev_*`.

## View 8: Port Connection Matrix

Triggered by: "matrix uart_ctrl" or "connections"

Table showing which parent signals connect to which instance ports. Bottom row shows connected/unconnected counts. Separate section lists all unconnected ports with `⚠` warning. Compact dot-matrix variant for large designs: `●` connected, `○` not connected.

## Search

Triggered by: "find modules with FSM", "find signals named *_valid"

| Query | Finds |
|---|---|
| `find modules with <clock>` | Modules using specific clock |
| `find modules with fsm` | All modules containing FSMs |
| `find modules with >20 ports` | Large interface modules |
| `find signals named <glob>` | Signal name pattern match |
| `find instances of <module>` | All instantiations |
| `find unconnected ports` | Floating ports |
| `find cdc crossings` | Clock domain crossings |
