# Gowin CST Physical Constraint Syntax

Use only the section for the affected CST statement. CST is neither `gw_sh` Tcl nor SDC. Resolve the exact part/package, top and enabled CST from the selected `.gprj`. For top-level port assignments use current RTL names; for placed internal primitive/net objects use the appropriate current synthesis evidence. A known local port constraint edit does not require a fresh synthesis or PnR run merely to edit the source.

Primary source: `SUG1018-1.7E` for Arora V. Generic fallback: `SUG935-1.6.1E`. Pin, bank, location, and electrical legality remains device-specific and must be checked against the selected pinout, datasheet, and tool version.

## Common rules

- End every statement with `;`.
- Put constrained object names in double quotes.
- Do not interchange ports, primitive instances, nets, and groups.
- Use current RTL names for top-level ports; use current synthesis names for placed internal instances/nets and re-check those after flattening or optimization.
- The manuals do not define a universal last-wins rule for conflicting physical constraints. Avoid conflicts.

## `IO_LOC`: package pin placement

```text
IO_LOC "<port>" <io_location>[,<candidate>...] [exclusive];
```

Multiple candidates are comma-separated. `exclusive` reserves the listed locations for this port. Every location must exist on the exact selected package.

```text
IO_LOC "clk" G2;
IO_LOC "data_in" P10,N10 exclusive;
```

Source: SUG1018/SUG935 `IO_LOC` appendix entry.

## `IO_PORT`: electrical properties

```text
IO_PORT "<port>" <attribute> = <value> [<attribute> = <value> ...];
```

```text
IO_PORT "data_in" IO_TYPE = LVCMOS33 PULL_MODE = NONE;
```

Common property names include `IO_TYPE`, `DRIVE`, `PULL_MODE`, `PULL_STRENGTH`, `HYSTERESIS`, `OPEN_DRAIN`, `SINGLE_RESISTOR`, `DIFF_RESISTOR`, `PCI_CLAMP`, `THRESHOLD_OFFSET`, and `VREF`.

- Legal values depend on device, bank, VCCIO, direction, and I/O standard.
- Differential modes can restrict hysteresis, keeper, termination, and true-differential support.
- Do not invent an attribute value from another part or from a GUI label.

Source: SUG1018 `IO_PORT`; UG304 Chapters 2–3.

## Connected IO and supply states

Before changing a pad connected to another device, identify the actual board/package revisions and wiring. Check both endpoints: direction, output-enable polarity and gating, drive, pulls/keeper, IO standard and supply/bank voltage. Include USB, boot/strapping and configuration functions; an unused HDL signal does not imply an unbiased or undriven physical pad.

Evaluate the relevant states separately: normal configured operation, reset/boot, FPGA before configuration, either endpoint unpowered, and supply transitions. Check vendor limits for hot-socket behavior, injection/back-power and absolute maximum ratings where those states occur. Configured RTL/CST alone does not establish the states before configuration or with one supply absent. If required wiring, supply or pad behavior remains unresolved, record the unknown and evidence, pause the current task and ask the developer; do not choose a presumed safe setting or continue separate work.

For the configured design:

- `PULL_MODE=NONE` removes internal bias; it does not change RTL direction or disable an active output driver. Verify direction and actual output enable at both ends; peripheral routing/matrix IDs alone do not prove that a pad is driving.
- Do not assume unused GPIOs have no pull. Check the installed version's unused-pin policy before reusing a connected pad; global unused-pin options may affect unrelated pins.
- When verifying the implemented pad configuration, inspect current final PnR port/pin tables for location/bank, direction, drive, pull mode, open-drain and relevant termination. Cross-check the peer's runtime configuration or measurements when that endpoint is in scope. These are prerequisites for implementation/electrical claims, not automatic build steps for every source edit.
- Keep configuration-report evidence separate from independent electrical measurements. No-pull passive configuration does not remove an attached USB branch/capacitance or prove signal integrity, safe supply sequencing or absence of back-power.

Observed on GW5AST-138C with Linux V1.9.12.04: explicit unused top-level input ports plus `PULL_MODE=NONE` remained in the final PnR pin tables as `in / Drive OFF / Pull NONE`, although their no-load IBUF instances were removed. Verify those final report fields in the current complete project; do not require an IBUF instance or generalize this observation to another device/version.

## `INS_LOC`: primitive placement

```text
INS_LOC "<primitive_instance>" <location> [exclusive];
```

```text
INS_LOC "u_pll" PLL_L[0];
INS_LOC "u_lut" R5C10[0][A];
```

- The object must be a post-synthesis primitive instance.
- Logic, PLL, BSRAM, DSP/DSPLite, IOLOGIC, and clock resources use different location formats.
- Arora V can include `MIPI_DPHY_RX`, `DSPLite`, and `SCRAMBLE`. `MIPI_DPHY_RX` uses `QUAD[n]`; `SCRAMBLE` uses `SCRM[n]`. Supported devices and `exclusive` rules must be checked in SUG1018.
- IOLOGIC placement uses a concrete package I/O location.

Source: SUG1018/SUG935 `INS_LOC` appendix entry.

## Absolute groups

```text
GROUP <group_name> = { "<instance_pattern>" ... } [exclusive];
GRP_LOC <group_name> <location_or_region> [exclusive];
```

```text
GROUP grp_data = { "u_data/*" } exclusive;
GRP_LOC grp_data R[2:3]C[2:4];
```

`GROUP` and `GRP_LOC` are both required. `GROUP ... exclusive` restricts object membership; `GRP_LOC ... exclusive` reserves the region for the group. They are not interchangeable.

## Relative groups

```text
REL_GROUP <group_name> = { "<instance>" ... };
INS_RLOC "<instance>" <relative_location>;
```

```text
REL_GROUP grp_pipe = { "u0" "u1" };
INS_RLOC "u0" R0C0;
INS_RLOC "u1" R0C1;
```

Every member should have an `INS_RLOC`. The generic manual's relative-coordinate example is internally inconsistent; check the original PDF and current tool when exact geometry matters.

Source: SUG1018/SUG935 group appendix entries.

## `LOC_RESERVE`: resource reservation

```text
LOC_RESERVE <location>[,<location>...] [ -LUT | -REG ];
```

```text
LOC_RESERVE R2C3[0]-LUT;
LOC_RESERVE R2C3, R3C4;
```

`-LUT/-REG` applies only to CFU locations. Do not overlap a reservation with an explicit instance placement.

Source: SUG1018/SUG935 `LOC_RESERVE` appendix entry.

## `USE_VREF_DRIVER`: external VREF

```text
USE_VREF_DRIVER <vref_name> [<location>];
```

```text
USE_VREF_DRIVER vref0 L14;
IO_LOC "dq0" L13;
IO_PORT "dq0" IO_TYPE = SSTL18_I VREF = vref0;
```

The VREF location must support the required I/O resource. The VREF and consuming port must be in the same bank. VREF names must be unique.

Source: SUG1018/SUG935 `USE_VREF_DRIVER` appendix entry.

## `CLOCK_LOC`: clock-network routing

Arora V syntax:

```text
CLOCK_LOC "<net>" BUFG[0-15] = <signal_type>;
CLOCK_LOC "<net>" BUFG = <signal_type>;
CLOCK_LOC "<net>" LOCAL_CLOCK;
```

`signal_type` is `CLK`, `CE`, `SR`, or `LOGIC`; combine types with `|`.

```text
CLOCK_LOC "sys_clk_net" BUFG[0] = CLK;
CLOCK_LOC "slow_local" LOCAL_CLOCK;
```

- The object is a post-synthesis net, not a port or primitive.
- `LOCAL_CLOCK` has no `= signal_type` suffix.
- Arora V documentation lists `BUFG[0-15]`, `BUFG`, and `LOCAL_CLOCK`; do not apply generic `BUFS` or `[0-7]` ranges.

Source: SUG1018 `CLOCK_LOC`; generic fallback in SUG935 Appendix A.8.

## GCLK and HCLK primitive placement

```text
INS_LOC "<clock_primitive_instance>" <position>;
```

- Arora V GCLK primitives are `DCS` and `DCE`. Legal positions and indices vary across 15K, 25K, 60K, 75K, and 138K devices; use SUG1018 Table 2-1 and UG306.
- Arora V HCLK primitives include `CLKDIV` and `DLLDLY`. Side and index ranges are device-specific; use SUG1018 Table 2-2.
- Placement does not define a clock's frequency or relationship. Those belong in SDC.

## `USE_ADC_SRC`: Arora V ADC input source

```text
USE_ADC_SRC bus<n> <location>;
```

Use a `bus<n>` value from the selected pinout's `ADC_INPUT` column and a pin-name location. SUG1018 writes the range as `bus0~n`. The manual excludes GW5A(S)(T)-138 and GW5AT-75; verify the full part number before use.

Source: SUG1018 §2.7.

## `NET_NO_REP`: disable net replication

```text
NET_NO_REP "<net>"[,"<net>"...];
```

This reduces optimization freedom. Use it only when replication violates a proven functional or physical requirement; it is not a substitute for HDL/GSC keep or preserve attributes.

Source: SUG1018 §2.8.

## `NET_LOC`: configuration networks

```text
NET_LOC "<net>" <virtual_config_location>;
```

Common Arora V targets:

| Function | Target keyword |
|---|---|
| JTAGSEL_N / RECONFIG_N | `V_JTAGSELN` / `V_RECONFIGN` |
| JTAG | `V_TCK`, `V_TMS`, `V_TDI` |
| SSPI | `V_SSPISI`, `V_SSPISO`, `V_SSPIWPN`, `V_SSPICLKHOLDN`, `V_SSPICLK`, `V_SSPICSN` |
| MODE | `V_MODE0`, `V_MODE1`, `V_MODE2`, `V_MODE_LD` |
| I2C | `V_SCL`, `V_SDA` |

```text
NET_LOC "reconfig_req" V_RECONFIGN;
```

Use these only when internal logic takes control of the corresponding configuration function. Review the programming and boot design at the same time. The converted source inserted spaces into some `V_*` tokens; the keywords above intentionally contain no spaces.

Source: SUG1018 §2.9–§2.10.

## Non-Arora V fallback

- Generic Gowin `CLOCK_LOC` may support `BUFG[0-7]` and `BUFS`.
- Older GCLK primitives may be `DCS/DQCE`, with `LEFT/RIGHT` or quadrant locations.
- Generic HCLK positions may use `TOPSIDE[0-1]`, `BOTTOMSIDE[0-1]`, `LEFTSIDE[0-1]`, and `RIGHTSIDE[0-1]`.
- Apply these rules only when the selected `.gprj` identifies a non-Arora V device, then read the matching SUG935 entry.

## Before writing

- Obtain the exact part, package, and device version from `.gprj`.
- Resolve top-level port names from current RTL, and placed internal instance/net names from appropriate synthesis evidence.
- Validate pins, banks, VCCIO, VREF, differential capability, and dedicated resources against the selected device.
- Keep CST, SDC, Tcl, and HDL/GSC attributes in their own files/languages.
- Invalidate affected prior implementation/timing evidence after changes. Run the necessary official PnR/STA stages and inspect current reports when the user requests implemented-placement/timing validation or a project rule requires it; for edit-only scope report static checks and implementation NOT_RUN.
