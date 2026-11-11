# Types, dependencies and backends

Read only when selecting file types, dependencies or an Edalize backend. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## File Types

| Type | Extensions |
|---|---|
| `verilogSource` | .v, .vh |
| `systemVerilogSource` | .sv, .svh |
| `vhdlSource` | .vhd, .vhdl |
| `xdc` | Vivado constraints |
| `PCF` | iCE40 constraints |
| `LPF` | ECP5 constraints |
| `CST` | Gowin constraints |

## Edalize Backends

| Backend | Tool | Use Case |
|---------|------|----------|
| verilator | Verilator | Simulation + lint |
| icarus | Icarus Verilog | Simulation |
| vivado | Xilinx Vivado | Synth + P&R |
| quartus | Intel Quartus | Synth + P&R |
| yosys | Yosys | Open-source synth |

## Dependency Management

```yaml
depend:
  - vendor:lib:uart:1.0         # Exact
  - ">=vendor:lib:spi:2.0"     # Minimum
  - "^vendor:lib:i2c:1.2"      # Semver compatible
  - "~vendor:lib:fifo:1.2.3"   # Patch only
```

| Operator | Meaning |
|---|---|
| `=` (default) | Exact match |
| `>=` | At least |
| `^` | Semver compatible (>=X.Y.Z, <X+1.0.0) |
| `~` | Patch only (>=X.Y.Z, <X.Y+1.0) |
