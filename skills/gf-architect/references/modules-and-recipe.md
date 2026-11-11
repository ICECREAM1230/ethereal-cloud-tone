# Module pages and build recipe

Read only when producing module pages or compile-order documentation. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### recipe.md

```markdown
# Build Recipe

## Compile Order
1. Packages (no deps)
2. Interfaces
3. Leaf modules
4. Top modules

\`\`\`mermaid
flowchart LR
    pkg[uart_pkg.sv] --> leaf[uart_tx.sv]
    pkg --> leaf2[uart_rx.sv]
    leaf --> top[uart_ctrl.sv]
    leaf2 --> top
\`\`\`

## Filelists Found
| File | Entries |
|------|---------|
| rtl.f | 15 files |

## Include Paths
- ./rtl
- ./include
```

### Per-Module Pages (modules/*.md)

```markdown
# Module: uart_tx

## Overview
UART transmitter with configurable baud rate.

## Location
- **File:** rtl/uart_tx.sv
- **Lines:** 1-145

## Parameters
| Name | Type | Default | Description |
|------|------|---------|-------------|
| CLK_FREQ | int | 100000000 | System clock Hz |
| BAUD | int | 115200 | Baud rate |

## Ports
| Name | Dir | Width | Description |
|------|-----|-------|-------------|
| clk | input | 1 | System clock |
| rst_n | input | 1 | Active-low reset |
| tx_data | input | [7:0] | Byte to send |
| tx_valid | input | 1 | Data valid |
| tx_ready | output | 1 | Ready for data |
| tx | output | 1 | Serial output |

## Instances
None (leaf module)

## FSM
State type: tx_state_t {IDLE, START, DATA, STOP}
[Link to fsm.md#uart_tx]

## Clock/Reset
- Clock: clk (single domain)
- Reset: rst_n (async active-low)

## Assertions
- p_valid_hold: valid held until ready
```

---
