# IP catalog and metadata

Read only when an actual installed block needs metadata/schema interpretation; catalog rows are not inventory. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Cataloged Block Types (availability must be discovered)

| Block | Description | Verified |
|-------|-------------|----------|
| fifo_sync | Synchronous FIFO (parameterized width/depth) | lint + sim + formal |
| fifo_async | Async FIFO with Gray code pointers (CDC) | lint + sim + formal |
| cdc_2ff | 2-flip-flop synchronizer | lint + sim + formal |
| cdc_handshake | Multi-bit handshake synchronizer | lint + sim + formal |
| uart | UART TX+RX with configurable baud | lint + sim + formal |
| spi_master | SPI master (all 4 CPOL/CPHA modes) | lint + sim + formal |
| axi4lite_slave | AXI4-Lite register slave | lint + sim + formal |
| debouncer | Button debouncer with edge detection | lint + sim + formal |

## Block Structure

Each block lives in the installed GateFlow skill's `ip/<name>/` directory (resolve the skill root from the current installation):
```
<name>/
  rtl/<name>.sv           # RTL source
  tb/tb_<name>.sv         # Self-checking testbench
  formal/<name>_props.sv  # SVA properties
  formal/<name>.sby       # SymbiYosys config
  block.yaml              # Metadata
  README.md               # Usage guide
```

## Block Metadata Schema (block.yaml)

```yaml
name: fifo_sync
version: 1.0.0
description: Synchronous FIFO with parameterized width and depth
parameters:
  WIDTH: { type: int, default: 8, description: "Data width in bits" }
  DEPTH: { type: int, default: 16, description: "FIFO depth (entries)" }
ports:
  - { name: clk, dir: input, width: 1 }
  - { name: rst_n, dir: input, width: 1 }
  - { name: wr_en, dir: input, width: 1 }
  - { name: wr_data, dir: input, width: WIDTH }
  - { name: rd_en, dir: input, width: 1 }
  - { name: rd_data, dir: output, width: WIDTH }
  - { name: full, dir: output, width: 1 }
  - { name: empty, dir: output, width: 1 }
formal_proofs:
  - p_no_overflow: "FIFO never accepts writes when full"
  - p_no_underflow: "FIFO never allows reads when empty"
dependencies: []
```

## IP Block Comparison

| Need | Use | Not | Why |
|---|---|---|---|
| Same-clock buffering | fifo_sync | fifo_async | Async has Gray code overhead |
| Cross-domain stream | fifo_async | cdc_2ff | 2FF only handles 1-bit |
| Cross-domain 1-bit flag | cdc_2ff | fifo_async | FIFO overkill for 1-bit |
| Cross-domain multi-bit (infrequent) | cdc_handshake | fifo_async | Handshake smaller |
| Cross-domain multi-bit (streaming) | fifo_async | cdc_handshake | Handshake blocks |
