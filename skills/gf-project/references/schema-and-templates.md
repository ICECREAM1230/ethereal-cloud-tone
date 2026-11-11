# Project schema and examples

Read only when creating or editing authorized GateFlow configuration. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Schema

```yaml
name: my-project
top_module: top
hdl: systemverilog  # systemverilog | verilog | vhdl
target:
  board: null       # arty-a7-35t, icebreaker, etc.
  device: null      # xc7a35ticsg324-1L, ice40hx8k, etc.
  clock_freq: null  # 100MHz, 12MHz, etc.
sources: []         # derived from the selected active manifest
constraints: null   # path to constraint file
ip_blocks: []       # installed IP block names
vendor_flow:
  tool: null         # gowin-official | generic-oss | null
  project_file: null # path to the authoritative .gprj, when present
  simulator_adapter: null # project-specific simulation adapter, when used; Verilator first
```

## Extended Schema

```yaml
simulation:
  tool: verilator        # verilator | iverilog | vcs | modelsim | questa | adapter
  timeout: 100000        # max cycles
  trace_format: fst      # vcd | fst
  defines: []            # compile-time defines

synthesis:
  tool: yosys            # yosys | vivado | quartus | gowin
  target_family: null    # ice40 | ecp5 | gowin | xilinx
  optimization: area     # area | speed | balanced

verification:
  formal_engine: sby
  formal_depth: 50
  coverage_goal: 90
  lint_tool: verilator
```

## Project Templates

### iCE40 FPGA
```yaml
name: my-project
top_module: top
hdl: systemverilog
target: {board: icebreaker, device: ice40up5k-sg48, clock_freq: 12MHz}
simulation: {tool: verilator, trace_format: fst}
synthesis: {tool: yosys, target_family: ice40, optimization: area}
```

### Simulation Only
```yaml
name: sim-project
top_module: top
hdl: systemverilog
target: {board: null}
simulation: {tool: verilator, timeout: 500000, trace_format: fst, defines: [SIMULATION, DEBUG]}
synthesis: {tool: null}
```
