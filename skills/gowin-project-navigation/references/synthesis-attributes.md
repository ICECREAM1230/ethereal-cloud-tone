# Gowin Synthesis Attributes

Use this reference only when RTL or GSC attributes are required to preserve objects, control inference, limit fanout, or change synthesis structure. These attributes are not project Tcl, SDC, or CST. Arora V is the default, but support still depends on the exact selected device and Gowin Software version.

Source: `SUG550-2.4E`, Chapter 5. Validate every structural change in the synthesis resource report and final PnR timing report.

## Syntax forms

Verilog attributes appear on the declaration before its terminating semicolon. String values use double quotes.

```verilog
wire keep_me /* synthesis syn_keep = 1 */;
reg [7:0] mem [255:0] /* synthesis syn_ramstyle = "block_ram" */;
```

GSC uses object-class keywords and `/` for hierarchy. String values are not quoted.

```text
INS "uut/reg1" syn_preserve=1;
NET "uut/data_net" syn_dont_touch=1;
PORT "out" syn_insert_pad=1;
GLOBAL syn_dspstyle=logic;
```

VHDL declares and then binds an attribute.

```vhdl
attribute syn_preserve : integer;
attribute syn_preserve of reg1 : signal is 1;
```

Rules:

- Verilog is case-sensitive, and a synthesis attribute must remain on one line.
- A statement constrains one object; write separate statements for separate objects.
- GSC supports `//` comments. Keep the terminating `;` even where a manual example omits it.
- Source priority for the same object and attribute is GSC > RTL attribute > project configuration.
- Flattening or optimization can change object names. Resolve GSC targets against the current synthesis result.

## Object preservation

| Attribute | Valid placement and effect | Risk |
|---|---|---|
| `syn_keep=1` | HDL net/signal/port. Preserve a net and inhibit net optimization or duplicate-cell merging. | Use `syn_preserve` for registers. Excess use blocks optimization. |
| `syn_preserve=1` | HDL or GSC module/entity, instance, register/signal; can be `GLOBAL`. Preserve registers against optimization or absorption. | Avoid global use without evidence. |
| `syn_dont_touch=1` | HDL or GSC register/net/signal. Convert to the corresponding primitive and suppress further signal optimization. | Stronger than keep/preserve; can degrade area and timing. |
| `syn_probe=1` or a port name | HDL wire/register/signal. Expose an internal signal as a top-level probe port. | Changes the top interface; the chosen name must not collide. |

Examples:

```verilog
wire debug_net /* synthesis syn_keep = 1 */;
reg state_q /* synthesis syn_preserve = 1 */;
```

```text
NET "u_core/debug_net" syn_dont_touch=1;
INS "u_core/state_q" syn_preserve=1;
```

Choose the weakest attribute that satisfies the verified need.

## Fanout, hierarchy, and loop expansion

| Attribute | Effect | Limits |
|---|---|---|
| `syn_maxfan=<N>` | Request register/logic replication to limit fanout. Applies to modules/entities, nets, registers/signals, ports, and `GLOBAL`. | Does not apply to clocks, control enables, resets, or sets. Very low values can cause resource growth. |
| `syn_netlist_hierarchy=0|1` | `0` flattens; `1` preserves hierarchy. Apply only to top or GSC `GLOBAL`. | Changes post-synthesis names and can invalidate GSC/CST/SDC object paths. |
| `syn_looplimit=<N>` | GSC `GLOBAL` loop limit; documented default `2000`. | Raise only after excluding an unintended or non-terminating elaboration loop. |

```text
NET "u_core/sel" syn_maxfan=10;
GLOBAL syn_netlist_hierarchy=1;
GLOBAL syn_looplimit=3000;
```

## Resource inference

| Attribute | Documented values | Effect and cautions |
|---|---|---|
| `syn_dspstyle` | `dsp`, `logic` | Select DSP hard block or logic implementation for arithmetic. Compare DSP use, LUT/ALU use, Fmax, and worst paths. |
| `syn_ramstyle` | `registers`, `block_ram`, `distributed_ram`, `rw_check`, `no_rw_check` | Select register, BSRAM, SSRAM, or same-address read/write behavior. The manual conflicts on some defaults; verify the actual inference report. |
| `syn_romstyle` | `logic`, `block_rom`, `distributed_rom` | Select logic, BSRAM, or distributed ROM. Unsupported resources can be ignored or replaced. |
| `syn_srlstyle` | `registers`, `block_ram`, `distributed_ram` | Select shift-register implementation. The manual contains a damaged `syn_ srlstyle` spelling; use `syn_srlstyle`. |

Examples:

```verilog
reg [31:0] ram [1023:0] /* synthesis syn_ramstyle = "block_ram" */;
wire [35:0] product /* synthesis syn_dspstyle = "dsp" */;
```

```text
GLOBAL syn_dspstyle=dsp;
INS "u_mem/mem" syn_ramstyle=block_ram;
```

After any inference change, compare the Resource section, clock/Fmax summary, and final PnR path type. Do not accept a fallback silently.

## FSM, reliability, and I/O inference

| Attribute or directive | Effect | Caution |
|---|---|---|
| `syn_encoding="onehot"` | Select one-hot FSM encoding. VHDL also documents `gray`; the Verilog guide documents only `onehot`. | Can improve speed at a resource/power cost. Verify the report. |
| `syn_radhardlevel=tmr|none` | Apply TMR or ordinary register implementation to modules, instances, or registers; can be `GLOBAL`. | TMR inserts triplication and voting. Re-check area and timing. The converted GSC example is damaged; verify syntax before GSC use. |
| `syn_insert_pad=0|1` | GSC `PORT`: remove or insert an I/O buffer. | Applies to input/output, not inout. |
| `syn_tlvds_io=0|1` / `syn_elvds_io=0|1` | Enable TLVDS or ELVDS buffer inference in HDL/GSC. | The slash denotes two different attribute names. Device and pin legality is still checked physically. |
| `syn_black_box` with `black_box_pad_pin` | Preserve only a black-box interface and identify external pad pins. | `black_box_pad_pin` requires `syn_black_box`; comma-separated pad names contain no spaces. |
| `full_case` | Declare that all case values are covered. | A wrong declaration can hide missing behavior; prefer an explicit `default`. |
| `parallel_case` | Request parallel rather than priority case implementation. | Use only when branches are truly mutually exclusive. |
| `translate_off/on` | Exclude enclosed HDL from synthesis. | Must be paired; excluded logic will not exist in hardware. |

## Before applying an attribute

- Confirm the earliest failing stage and the exact object in the current synthesis result.
- Do not use preservation attributes to conceal dead, undriven, or structurally incorrect RTL.
- Do not use `syn_maxfan` on clock/reset/control networks.
- Change one synthesis decision at a time, then re-run synthesis and PnR.
- Compare resource inference, object names, worst setup/hold paths, and relevant warnings before keeping the change.

Non-Arora V fallback: attribute syntax is generally shared, but the available hard resources and fallback behavior differ by device. Re-check the selected device before forcing DSP, RAM, ROM, SRL, LVDS, or TMR implementation.

