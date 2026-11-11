# Testbench choices and results

Read only when comparing testbench approaches or producing structured results. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## When to Use

| Cocotb | SV Testbench |
|--------|-------------|
| Python (lower barrier) | SystemVerilog |
| Complex stimulus (Python libs) | Protocol + coverage |
| Slower (co-simulation) | Faster (native) |

## Decision Matrix: Cocotb vs SV TB

| Factor | Use Cocotb | Use SV/UVM |
|---|---|---|
| Team knows Python | Yes | - |
| FPGA project | Yes | - |
| Large ASIC with UVM infra | - | Yes |
| Need open-source CI | Yes | - |
| Standard protocols (AXI, SPI) | Yes (cocotbext) | - |
| Deep constrained random | - | Yes |
| Rapid iteration | Yes | - |

## GATEFLOW-RESULT Integration

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
ERRORS: <count>
WARNINGS: <count>
FILES: <test files>
DETAILS: <summary>
---END-GATEFLOW-RESULT---
```
