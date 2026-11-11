# Scaffold output contract

Read only when choosing scaffold deliverables or returning structured results. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Available Protocols

| Protocol | Priority | Scaffold Includes |
|----------|----------|-------------------|
| AXI4-Lite | 1 | Slave register interface + master BFM + TB |
| SPI | 2 | Master + slave + loopback TB |
| UART | 3 | TX + RX + loopback TB |
| I2C | 4 | Master + slave model + TB |
| AXI4-Full | 5 | Slave + burst master BFM + TB |
| AXI-Stream | 6 | Source + sink + passthrough + TB |
| Wishbone | 7 | Slave + master + TB |

## Usage

When user requests a protocol interface:
1. Check if the IP library has a complete block (`$gf-ip list`)
2. If available as IP: suggest `$gf-ip add <block>` instead
3. If not available or user wants custom: generate scaffold

## Scaffold Generation

Each scaffold includes:
- RTL skeleton with correct port names and widths
- Signal timing comments (when to assert/deassert)
- Testbench template with BFM (Bus Functional Model)
- Integration example showing how to wire into a design

## GATEFLOW-RESULT Integration

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
PROTOCOL: axi4lite | spi | uart | i2c | wishbone | axi_stream
FILES: [generated files]
DETAILS: [summary]
---END-GATEFLOW-RESULT---
```
