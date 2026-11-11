# CFU structure

## Packing and carry chains

A CFU contains four CLSs and routing logic. Each CLS contains two LUT4s and two registers. Wider LUTs consume multiple resources. ALU mode uses dedicated carry chains for arithmetic, counters, and comparisons.

Check control sharing for the exact device. GW5AT has specific CLS2/CLS3 clock, CE, and set/reset sharing rules. Different control nets, reset types, or polarities can prevent packing. Merge controls only when behavior stays correct. See [CFU structure and control rules](manuals/markdown/UG303E.md#pdf-page-11).

## Match the structure to the path

| Bottleneck | Change to try | Check |
|---|---|---|
| Deep condition chain or wide mux | Parallel decode for exclusive conditions; grouped priority selection or pipelines otherwise | Priority and data/select alignment |
| Long arithmetic or comparison | Sized operators that infer carry chains; move input selection earlier; balance independent sums | Width, sign, carry delay, and truncation |
| Counter or recurrence | Share repeated comparisons; precompute terminal flags; use segmented carry or lookahead if needed | Enable, load, clear, and wrap behavior |
| Muxes around a shared operator | Reduce sharing on the critical path or pipeline selection separately | Throughput, resource cost, and routing |
| High-fanout net delay | Local decode, selected driver copies, or backend replication | Actual load, placement, and congestion |
| Long route with modest fanout | Improve locality between logic and hard blocks | Physical path and route length |

Use complete combinational assignments. `full_case` assumes coverage; it does not define missing simulation behavior. Test invalid and simultaneous inputs.

## Fanout attributes

`syn_maxfan` can cause logic replication. It does not apply to clocks, control enables, or set/reset signals. Apply an input-port constraint to the actual driver when needed. See [syn_maxfan](manuals/markdown/SUG550.md#pdf-page-62).

```verilog
reg select_q /* synthesis syn_maxfan = 16 */;
```

Choose the limit from the critical net and its physical loads. Avoid a small global limit that replicates unrelated logic.

Use supported clock/control routing or backend options for clocks, CE, and reset. Check actual routing and resource use.

`Replicate Resources` is available for Arora V. Confirm the tool option rather than guessing Tcl syntax from its GUI label. Check existing `syn_keep`, `syn_preserve`, and `syn_dont_touch` settings before adding them: they can block optimization or hard-block register absorption.

## Physical changes

For a hard-block output path, first consider its output register, then a nearby downstream stage. Use placement regions only when locations and resource capacity are known.

CLS use above 85% or BSRAM/DSP use above 80% can signal congestion risk. Compare post-route paths, congestion, resource placement, and the target clock.
