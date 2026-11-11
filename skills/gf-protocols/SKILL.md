---
name: gf-protocols
description: >
  Protocol scaffolding library. Generates correct, readable protocol
  interface scaffolds (AXI4-Lite, SPI, UART, I2C, AXI4-Full, AXI-Stream,
  Wishbone) with testbench templates and integration examples.
  Example: "create an I2C master interface", "scaffold AXI-Stream source"
---

Read [the shared execution contract](../gf/references/codex-runtime.md) once per task; reuse it when already read. Read only the reference matching the current operation, and only its relevant section. Examples do not require extra deliverables or authorize writes.

# GateFlow protocol scaffolds

Generate a scaffold for the requested protocol and interface contract. These are starting points, not verified production IP. Use `gf-ip` only if an actual installed library block is relevant; do not require an inventory scan for every custom interface.

Before generating, read only the selected protocol's reference:

| Protocol | Required reference | Optional starting example |
|---|---|---|
| AXI4-Lite | [Rules](references/axi4-lite.md) | [Scaffold](references/axi4-lite-scaffold.md) |
| AXI4-Full | [Rules](references/axi4-full.md) | — |
| AXI-Stream | [Rules](references/axi-stream.md) | — |
| SPI | [Rules](references/spi.md) | [Scaffold](references/spi-scaffold.md) |
| UART | [Rules](references/uart.md) | [Scaffold](references/uart-scaffold.md) |
| I2C | [Rules](references/i2c.md) | [Scaffold](references/i2c-scaffold.md) |
| Wishbone | [Rules](references/wishbone.md) | — |

Match widths, reset, clock, timing and backpressure to project requirements. Read [output examples](references/scaffold-output.md) only when choosing additional deliverables or producing structured output. Validate the generated behavior with relevant lint/self-checking simulation; keep unexecuted checks explicit.
