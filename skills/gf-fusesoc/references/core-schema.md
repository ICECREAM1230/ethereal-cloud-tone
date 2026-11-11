# FuseSoC core examples

Read only when creating or editing a core file. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## .core File Template

```yaml
CAPI=2:
name: ::my_project:1.0.0
filesets:
  rtl:
    files: [rtl/top.sv, rtl/fifo.sv]
    file_type: systemVerilogSource
  tb:
    files: [tb/tb_top.sv]
    file_type: systemVerilogSource
  constraints:
    files:
      - constraints/arty_a7.xdc: {file_type: xdc}
targets:
  sim:
    filesets: [rtl, tb]
    toplevel: tb_top
    default_tool: verilator
  synth:
    filesets: [rtl, constraints]
    toplevel: top
    default_tool: vivado
    tools:
      vivado:
        part: xc7a35ticsg324-1L
```

## Complete .core Schema

```yaml
CAPI=2
name: vendor:library:name:version    # VLNV identifier (required)
description: "Brief description"

filesets:
  rtl:
    file_type: systemVerilogSource
    files:
      - rtl/top.sv
      - rtl/core.sv
    depend:
      - ">=vendor:lib:dep:1.0"
  tb:
    files: [tb/tb_top.sv]
    file_type: systemVerilogSource
  constraints_ice40:
    files: [constraints/ice40.pcf]
    file_type: PCF
  constraints_ecp5:
    files: [constraints/ecp5.lpf]
    file_type: LPF

targets:
  default:
    filesets: [rtl]
    toplevel: top
  sim:
    default_tool: verilator
    filesets: [rtl, tb]
    toplevel: tb_top
    tools:
      verilator:
        mode: cc
        verilator_options: [--trace, --coverage]
      icarus:
        iverilog_options: [-g2012]
  synth_ice40:
    default_tool: icestorm
    filesets: [rtl, constraints_ice40]
    toplevel: top
    tools:
      icestorm:
        pnr: next
        nextpnr_options: [--hx8k, --package, ct256, --freq, "48"]
  synth_ecp5:
    default_tool: trellis
    filesets: [rtl, constraints_ecp5]
    toplevel: top
    tools:
      trellis:
        nextpnr_options: [--25k, --package, CABGA256]
  synth_vivado:
    default_tool: vivado
    filesets: [rtl]
    toplevel: top
    tools:
      vivado:
        part: xc7a35tcpg236-1
  lint:
    default_tool: verilator
    filesets: [rtl]
    toplevel: top
    tools:
      verilator:
        mode: lint-only

parameters:
  WIDTH:
    datatype: int
    paramtype: vlogparam
    default: 8
    description: "Data width"
```
