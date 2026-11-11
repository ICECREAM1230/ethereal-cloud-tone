# Electrical checks and constraint examples

Read only when selecting electrical settings, output format or PMOD mappings. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Safety

- Never guess pin assignments
- Verify the affected I/O standard against the board, device/package and bank supply using the target tool's constraint syntax. Preserve valid unrelated settings during local edits.
- Check compatibility of the affected signals with the bank supply and any bank-level restrictions; different standard names alone do not prove incompatibility.
- Select pulls from signal direction, board wiring and required idle behavior. Active-low polarity alone does not require a pull-up on an output.
- Choose drive, slew and termination from applicable board/device evidence; there is no universal LED or SPI drive setting.

## GATEFLOW-RESULT Format

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
FORMAT: xdc | pcf | lpf | cst
BOARD: <board name>
PINS_MAPPED: <count>
PINS_UNMAPPED: <count>
FILE: <constraint file path>
DETAILS: <summary>
---END-GATEFLOW-RESULT---
```

## I/O Standard Reference

| IOSTANDARD | Voltage | Max Speed | Typical Use |
|---|---|---|---|
| LVCMOS33 | 3.3V | ~100 MHz | GPIO, LEDs, UART, SPI |
| LVCMOS25 | 2.5V | ~150 MHz | Mixed-voltage |
| LVCMOS18 | 1.8V | ~200 MHz | Modern peripherals |
| LVDS_25 | 2.5V diff | ~1 Gbps | High-speed serial |
| SSTL15 | 1.5V | ~800 MHz | DDR3 |
| TMDS_33 | 3.3V | ~750 Mbps | HDMI/DVI |

## Common Pin Mapping Mistakes

1. **Unverified electrical settings** -> Check the affected port's I/O standard, bank supply and board connection; do not silently rely on an unknown default.
2. **Bank incompatibility** -> Check the device's shared-bank restrictions for the selected standards and supply.
3. **Undefined input level** -> Check external bias and device handling before selecting a pull or unused-pin policy.
4. **Unverified termination** -> Use the interface and board/device requirements for the affected link.
5. **Arbitrary DRIVE/SLEW** -> Use supported values justified by the load and signal requirements; preserve unaffected settings.
6. **Missing timing coverage** -> For a requested timing result, check applicable clock and I/O constraints and report uncovered paths.

## PMOD Mapping Patterns

Standard PMOD pinout: pins 1-4 (top row I/O), 5 (GND), 6 (VCC), 7-10 (bottom row I/O), 11 (GND), 12 (VCC).

| PMOD Type | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
|---|---|---|---|---|
| Type 2 (SPI) | CS_N | MOSI | MISO | SCLK |
| Type 3 (UART) | CTS | TXD | RXD | RTS |
| Type 6 (I2C) | SCL | SDA | INT_N | RST_N |
