# Contextual explanations

Read only when a first-encounter explanation or requested exercise needs an example. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Philosophy

Teaching happens WITHIN the workflow, not in a separate mode.
The first time a user encounters a concept, explain it briefly.
Never explain the same concept twice.

## First-Encounter Explanations

When GateFlow generates code using a concept for the first time,
include a 2-3 sentence explanation marked with [Learn]:

```
Generated sync_2ff.sv

[Learn] This is a 2-flip-flop synchronizer. When a signal crosses
between clock domains, it can be captured during a metastable state.
Two back-to-back flip-flops reduce metastability probability to safe
levels. For multi-bit signals, use a Gray code FIFO or handshake instead.
```

## Concept Library

| Concept | Trigger | Explanation Focus |
|---------|---------|-------------------|
| cdc | 2FF sync, async FIFO | Metastability, why 2 FFs |
| fsm | State machine code | Two-process pattern, encoding |
| pipeline | Pipeline register, stage, valid_q | Breaks long paths into stages for higher clock frequency |
| fifo | FIFO instantiation | Full/empty, pointer math |
| axi | AXI interface | Valid/ready handshake rules |
| formal | SVA properties | Bounded proof, counterexample |
| synthesis | Yosys output | LUTs vs FFs, resource meaning |
| constraints | Pin mapping | IOSTANDARD, voltage rails |
| vhdl | VHDL code | Comparison with SystemVerilog |
| backpressure | ready signal, stall | Consumer tells producer to stop via ready deassertion |
| clock_gating | gclk, ICG cell | Stops clock to idle registers, saves dynamic power |
| reset_synchronizer | rst_sync, async assert/sync deassert | Prevents metastability on reset release |
| gray_code | bin2gray, gray2bin | Changes one bit per increment, safe for CDC pointers |
| dual_port_ram | sdp_ram, true dual-port | Two independent access ports, FPGA BRAM native |
| arbitration | arbiter, grant, round-robin | Resolves contention for shared resources |
| bus_protocol | AXI, Wishbone, APB | Defines master/slave communication rules |
| timing_closure | slack, critical path | All paths meet setup/hold at target frequency |
| register_file | regfile, read/write ports | Small fast memory with multiple read ports |
| shift_register | LFSR, serial-to-parallel | Moves data one position per clock |
| priority_encoder | casez, leading zero | Outputs index of highest-priority active bit |
| barrel_shifter | rotate, variable shift | Variable-distance shift via mux cascade |
| interrupt_controller | IRQ, pending, mask | Aggregates and prioritizes interrupt sources |
| dma | descriptor, scatter-gather | Transfers data between memory without CPU |

## Generative Exercises

When user asks to practice:
1. Generate a buggy version of a recent design
2. Ask user to find the error
3. Reveal answer with explanation

Example:
```
Want to practice? Here's a FIFO with a subtle bug.
Find what's wrong:

[buggy code]

Hint: Look at the full/empty logic...
```

## Cross-HDL Explanations

When a SV user sees VHDL for the first time:
```
[Learn] VHDL uses 'signal' instead of 'logic', and 'process'
instead of 'always_ff'. The semantics are similar — sequential
logic with sensitivity lists. VHDL is more verbose but equally
capable for synthesis.
```

## Cross-Reference Linking

When explaining a concept, append related GateFlow links:
- CDC -> "If `gf-ip` discovers an installed and evidenced `cdc_2ff` block, `$gf-ip add cdc_2ff` can integrate it with implementation authorization."
- FIFO -> "If `gf-ip` discovers an installed and evidenced `fifo_async` block, `$gf-ip add fifo_async` can integrate it with implementation authorization."
- Protocol -> "See `$gf-protocols` for AXI/SPI/UART references."
- Practice -> "Try `$gf-learn <topic>` for exercises."
