# Timing and SDC

## Constraint quick index

| Need | Commands and examples | Exact syntax |
|---|---|---|
| Base or generated clock | [Clocks](#clocks) | [SUG940, pp.53-57](manuals/markdown/SUG940.md#pdf-page-63) |
| Source latency or uncertainty | [Clock margin](#clock-margin) | [pp.58-61](manuals/markdown/SUG940.md#pdf-page-68) |
| External input/output timing | [IO delays](#io-delays) | [pp.63-66](manuals/markdown/SUG940.md#pdf-page-73) |
| Asynchronous or exclusive clocks | [Clock groups](#clock-groups) | [pp.61-63](manuals/markdown/SUG940.md#pdf-page-71) |
| Exclude a real false path | [False paths](#false-paths) | [pp.66-68](manuals/markdown/SUG940.md#pdf-page-76) |
| Allow multiple sampling cycles | [Multicycle paths](#multicycle-paths) | [pp.69-70](manuals/markdown/SUG940.md#pdf-page-79) |
| Explicit path delay budget | [Delay limits](#delay-limits) | [pp.68-69](manuals/markdown/SUG940.md#pdf-page-78) |
| Timing and exception reports | [Reports](#reports) | [pp.71-79](manuals/markdown/SUG940.md#pdf-page-81) |
| Device timing conditions | `set_operating_conditions` | [pp.79-80](manuals/markdown/SUG940.md#pdf-page-89) |

Examples use illustrative names and values. Replace them with actual mapped objects and timing requirements. Check syntax support in the installed version.

### Clocks

```tcl
# A real 200 MHz clock at port clk.
create_clock -name core_clk -period 5 [get_ports {clk}]
# A divider output with a known 2:1 relationship.
create_generated_clock -name half_clk -source [get_ports {clk}] -divide_by 2 [get_pins {u_div/Q}]
```

Verify PLL frequency, phase, and duty cycle in Clock Summary. Preserve relationships between related outputs. Check `-add` when multiple clocks share an object. `derive_clocks -freq` sets a global target; it does not replace a complete clock model.

### Clock margin

```tcl
set_clock_latency -source 0.5 [get_clocks {core_clk}]
set_clock_uncertainty 0.2 -setup -from [get_clocks {core_clk}]
set_clock_uncertainty 0.1 -hold -from [get_clocks {core_clk}]
```

Use the actual source delay and margin. Do not reduce uncertainty or choose a favorable operating model to hide failures. Check device-specific setup and hold conditions.

### IO delays

```tcl
set_input_delay -clock core_clk -max 2.0 [get_ports {data_in}]
set_input_delay -clock core_clk -min 0.5 [get_ports {data_in}]
set_output_delay -clock core_clk -max 1.2 [get_ports {data_out}]
set_output_delay -clock core_clk -min -0.3 [get_ports {data_out}]
```

Derive the numbers from the external device and board timing. Set min and max explicitly: this Gowin syntax copies one to the other when only one is given. Do not apply data input delay to a clock input. Use `-clock_fall` and `-add_delay` when required by the interface.

### Clock groups

```tcl
set_clock_groups -asynchronous -group [get_clocks {clk_a}] -group [get_clocks {clk_b}]
```

Use `-exclusive` for clocks that cannot operate together. Group only clocks whose relationship justifies excluding inter-group analysis. CDC circuits still need independent verification.

### False paths

```tcl
# Limit an asynchronous input exception to its first synchronizer stage.
set_false_path -from [get_ports {async_in}] -to [get_pins {sync_meta/D}]
```

Check the exact matched objects. `-setup` affects setup and recovery; `-hold` affects hold and removal. Omitting both affects all four. A false path removes checks; it does not repair a circuit or prove CDC safety.

### Multicycle paths

Use this only when the source remains stable and the receiver deliberately samples later. For a same-frequency three-cycle case, verify a paired constraint such as:

```tcl
set_multicycle_path -from [get_regs {src_q}] -to [get_regs {dst_q}] -setup -end 3
set_multicycle_path -from [get_regs {src_q}] -to [get_regs {dst_q}] -hold -start 2
```

Derive the launch/capture edges before selecting `-start` or `-end`. Do not copy the N/N-1 pair into unrelated clock relationships or use it merely because a path is slow.

### Delay limits

```tcl
set_max_delay -from [get_regs {src_q}] -to [get_regs {dst_q}] 6.0
set_min_delay -from [get_regs {src_q}] -to [get_regs {dst_q}] 0.5
```

Use a real delay budget and check interaction with other exceptions. Max delay affects setup/recovery; min delay affects hold/removal.

### Reports

```tcl
report_timing -setup -max_paths 20
report_timing -hold -max_paths 20
report_timing -recovery -max_paths 10
report_timing -removal -max_paths 10
report_exceptions -setup
report_exceptions -hold
```

Useful object queries are `get_regs`, `get_pins`, `get_ports`, `get_clocks`, and `get_nets`. Confirm wildcard matches after synthesis; `*` can cross hierarchy. Do not assume options from another vendor are supported.

## Diagnose paths

Inspect Clock Summary, analysis coverage, endpoint counts, frequency summary, and negative slack before individual paths. `Actual Fmax` covers the relevant synchronous paths. An empty timing report may mean missing coverage, disconnected logic, or trimmed logic.

For each important path, record endpoints, launch/capture clocks and edges, check type, slack, logic depth, cell delay, net delay, skew, and major fanout. Rank path groups by violations and system goals.

| Dominant delay | Change to try |
|---|---|
| Deep logic | Pipeline, balance arithmetic, group priority selection, or reduce critical-path sharing |
| BSRAM/DSP output | Use its output register or a nearby downstream stage |
| High-fanout net | Localize controls or replicate selected drivers |
| Long route with modest fanout | Improve placement locality |
| Clock skew or hold | Check dedicated routes, clock relationships, IO phase, and backend hold repair |
| Missing or broad constraints | Correct clocks, IO budgets, matched objects, and exception scope |

Treat synthesis timing as an estimate. Confirm the result with post-route STA.

## Acceptance

Compare baseline and candidate under the same target and valid constraints. Check behavior, latency, input interval, mapped resources, and absorbed registers.

Report post-route setup/hold slack, TNS or violating endpoints, applicable recovery/removal checks, minimum pulse width, constraint coverage, and exception scope. Preserve the tool's actual metric names.

If reset checks are explicitly deferred, limit the affected paths and mark them pending. Keep normal data/control STA and connectivity checks active.

State any uncovered IO, deferred checks, or incomplete corners. Fmax, exit status, and bitstream generation alone do not prove timing closure.
