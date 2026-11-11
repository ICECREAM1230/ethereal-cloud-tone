# Gowin SDC Timing Constraint Syntax

Use this reference for SDC and STA commands. It assumes Arora V by default. Resolve the exact device, top, clock sources, and post-synthesis object names from the selected `.gprj` and current reports. CST physical constraints and Gowin project Tcl are documented separately.

Sources: `SUG940-2.0.4E`, Chapters 3–5; prerequisites cross-checked against `SUG113-1.3E`, Chapter 4. Time values are in ns.

For a bounded edit, read only the affected command and selector sections. Resolve top-level ports from current RTL and use appropriate synthesis evidence for internal objects; validate actual collections in the tool when that is needed for the requested claim. Changes invalidate affected timing reports, but do not automatically require full synthesis/PnR/STA. Source-only checks must be reported at that scope. Broader clock/CDC changes or requested timing closure load the necessary domain and official-stage guidance.

## Object collections

| Collection | Objects |
|---|---|
| `get_clocks` / `all_clocks` | clocks |
| `get_ports` / `all_inputs` / `all_outputs` | top-level ports |
| `get_regs` | flip-flops |
| `get_pins` | instance pins |
| `get_nets` | nets |

`?` matches one character. `*` matches zero or more characters and can cross hierarchy. Confirm that every collection is non-empty and valid for the command. Exception `-through` selectors accept only nets and pins.

## `create_clock`

```tcl
create_clock \
  -name <clock_name> \
  -period <positive_ns> \
  [-waveform {<rise> <fall>}] \
  <objects> [-add]
```

- `<objects>`: `get_ports|get_nets|get_regs|get_pins|all_inputs|all_outputs`.
- Default waveform is `{0 period/2}`.
- A later clock with the same name replaces the earlier one.
- A second different clock on the same object requires `-add`; otherwise it is ignored.
- The Gowin manual does not confirm virtual clocks; do not create a clock without an object.
- Without a user clock constraint, Arora software may create a 100 MHz default. That is not a board-level requirement.

```tcl
create_clock -name sys_clk -period 10 -waveform {0 5} [get_ports {clk}]
```

Source: SUG940 §3.7.1, §4.1.2, §5.2.1.

## `create_generated_clock`

```tcl
create_generated_clock \
  -name <clock_name> \
  -source <clock_source> \
  [-master_clock <clock>] \
  [-edges {<e1> <e2> <e3>}] \
  [-edge_shift {<s1> <s2> <s3>}] \
  [-divide_by <positive_integer>] \
  [-multiply_by <positive_integer>] \
  [-duty_cycle <0..100>] \
  [-phase <degrees>] [-offset <ns>] [-invert] [-add] \
  <objects>
```

- `-source`: `get_ports|get_nets|get_regs|get_pins`. Use `-master_clock` when multiple clocks reach the source.
- `-edges` requires three increasing positive integers. `-edge_shift` requires `-edges`.
- The edges/edge-shift form is mutually exclusive with divide, multiply, duty, phase, and offset options.
- A later generated clock on the same object requires `-add`. A generated clock with the master clock's name is ignored.

```tcl
create_generated_clock -name div2_clk \
  -source [get_ports {clk_in}] -divide_by 2 [get_pins {u_div/clk_out}]
```

Source: SUG940 §3.7.1, §5.2.2.

## Clock quality

### `set_clock_latency`

```tcl
set_clock_latency -source \
  [-rise | -fall] [-late | -early] \
  <delay_ns> <objects> [-clock <clock_list>]
```

Gowin calculates network latency automatically; the manual confirms only user source latency. Objects can be clocks, ports, nets, registers, pins, or `all_clocks`.

```tcl
set_clock_latency -source -late 0.33 [get_clocks {sys_clk}]
set_clock_latency -source -early 0.11 [get_clocks {sys_clk}]
```

### `set_clock_uncertainty`

```tcl
set_clock_uncertainty <uncertainty_ns> \
  [-setup | -hold] \
  [-from <clock> | -rise_from <clock> | -fall_from <clock>] \
  [-to <clock> | -rise_to <clock> | -fall_to <clock>]
```

- At least one from/to option is required, using `get_clocks`.
- `-setup` covers setup/recovery; `-hold` covers hold/removal. Omitting both covers all four analyses.
- The ordinary from/to examples use the To-side value when both apply. Edge-specific wording is damaged in the conversion; check the original PDF for that case.

Source: SUG940 §5.2.3–§5.2.4.

## `set_clock_groups`

```tcl
set_clock_groups \
  (-asynchronous | -exclusive) \
  -group <clocks> -group <clocks> ...
```

- `-asynchronous`: independent asynchronous sources.
- `-exclusive`: clocks that cannot be active together.
- Inter-group paths are not analyzed; intra-group paths remain analyzed.
- This is a high-priority exclusion. Do not widen groups without proving the relationship.

```tcl
set_clock_groups -asynchronous \
  -group [get_clocks {clk_a}] -group [get_clocks {clk_b}]
```

Source: SUG940 §3.7.1, §3.8, §5.2.5.

## I/O delays

```tcl
set_input_delay  -clock <clock_name> <delay_ns> \
  [-clock_fall] [-max | -min] [-rise | -fall] \
  [-add_delay] [-source_latency_included] <ports>

set_output_delay -clock <clock_name> <delay_ns> \
  [-clock_fall] [-max | -min] [-rise | -fall] \
  [-add_delay] [-source_latency_included] <ports>
```

- The related clock must already exist. Ports use `get_ports|all_inputs|all_outputs`.
- `-clock_fall` selects the reference-clock edge; `-rise/-fall` selects the data edge.
- `-max` covers setup/recovery and `-min` covers hold/removal. If only one is set, the same value is used for the other.
- Use `-add_delay` to preserve multiple constraints on the same port; otherwise a later constraint replaces the same class.
- `-source_latency_included` declares that the I/O delay already includes source latency.
- `set_input_delay` does not support a clock input port.

```tcl
set_input_delay  -clock io_clk 0.2 -max [get_ports {din}]
set_input_delay  -clock io_clk 0.1 -min [get_ports {din}]
set_output_delay -clock io_clk 0.2 -max [get_ports {dout}]
set_output_delay -clock io_clk 0.1 -min [get_ports {dout}]
```

Source: SUG940 §3.7.2, §5.3.

## Timing exceptions

### `set_false_path`

```tcl
set_false_path \
  [-from <from_list>] [-through <through_list>] [-to <to_list>] \
  [-setup | -hold]
```

At least one selector is required. `-from/-to` accept clocks, ports, registers, pins, and all-collections; `-through` accepts only nets and pins. Omitting setup/hold excludes setup, hold, recovery, and removal. Never use this command merely to hide unexplained negative slack.

### `set_max_delay` / `set_min_delay`

```tcl
set_max_delay [-from <list>] [-through <list>] [-to <list>] <delay_ns>
set_min_delay [-from <list>] [-through <list>] [-to <list>] <delay_ns>
```

At least one selector is required. Maximum delay covers setup/recovery; minimum delay covers hold/removal.

### `set_multicycle_path`

```tcl
set_multicycle_path \
  [-from <list>] [-through <list>] [-to <list>] \
  (-setup | -hold) (-start | -end) <integer>
```

- At least one selector is required.
- `-start` is relative to the launch clock; `-end` is relative to the latch clock.
- Evaluate setup and hold together; do not add cycles only to clear setup violations.
- SUG940 Example 5.35 omits `-start/-end` even though the syntax box requires one. Do not infer the default; check local help or the original PDF.

```tcl
set_multicycle_path -from [get_clocks {user_clk}] -setup -end 3
set_multicycle_path -from [get_clocks {user_clk}] -hold  -start 2
```

Source: SUG940 §3.7.3, §3.8, §5.4.

## Replacement and priority

- Same-name `create_clock` or `create_generated_clock`: later definition replaces earlier definition.
- Multiple clocks on one object: later clocks need `-add`.
- Multiple I/O delays on one port: use `-add_delay`; otherwise later constraints replace the same class.
- Multiple SDC files: the manual confirms only that the last-added file is enabled by default; it does not define line-by-line merging.

SUG940 §3.8 gives this low-to-high conflict priority:

1. `create_clock` / `create_generated_clock`
2. `set_multicycle_path`
3. `set_max_delay` / `set_min_delay`
4. `set_false_path`
5. `set_clock_groups`

The manual conflicts on `create_clock` versus `derive_clocks`. Prefer an explicit clock and verify the effective constraint in the reports.

## STA report commands

### `report_timing`

```tcl
report_timing \
  (-setup | -hold | -recovery | -removal) \
  [-from_clock <clock> | -rise_from_clock <clock> | -fall_from_clock <clock>] \
  [-to_clock <clock> | -rise_to_clock <clock> | -fall_to_clock <clock>] \
  [-from <list> | -rise_from <list> | -fall_from <list>] \
  [-through <list>] \
  [-to <list> | -rise_to <list> | -fall_to <list>] \
  [-max_paths <positive_integer>] \
  [-max_common_paths <positive_integer>] \
  [-max_logic_level <positive_integer>] \
  [-min_logic_level <positive_integer>] \
  [-mod_ins {<instance ...>}]
```

```tcl
report_timing -setup \
  -from_clock [get_clocks {sys_clk}] \
  -to_clock [get_clocks {sys_clk}] -max_paths 50
```

The converted syntax box and parameter table disagree on latch-clock edge option names. This reference uses `-rise_to_clock/-fall_to_clock` from the parameter table; verify against current local help.

### `report_high_fanout_nets`

```tcl
report_high_fanout_nets \
  [-max_nets <positive_integer>] \
  [-min_fanout <positive_integer>] [-max_fanout <positive_integer>] \
  [-clock_regions] [-slr] [-ascending]
```

This command reports fanout; it does not set a fanout limit.

Source: SUG940 §5.5.1–§5.5.2.

## Validate effectiveness

- Timing Constraints Report: `State=Actived` means effective; `State=Invalid` means ineffective. Inspect `Detail Command`.
- Clock Summary: confirm Base/Generated type, period, source, master, and objects.
- Negative slack means a violation. `Nothing to report!` means no matched path, not proof of correctness.
- The referenced manuals do not confirm `set_clock_transition`, `set_load`, `set_drive`, or `set_fanout_load`; do not generate them from generic SDC knowledge.

Non-Arora V fallback: the documented LittleBee default clock is 50 MHz when no user clock exists. All syntax still requires validation against the selected tool/device version.

