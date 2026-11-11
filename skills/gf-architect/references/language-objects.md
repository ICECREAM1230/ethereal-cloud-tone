# Language object schemas

Read only when packages, types, functions or macros are relevant. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### packages.md

```markdown
# Packages

## uart_pkg
**File:** rtl/uart_pkg.sv

**Exports:**
- Types: state_t, config_t
- Parameters: BAUD_RATE, DATA_BITS
- Functions: calc_divisor()

## Import Graph

\`\`\`mermaid
flowchart TD
    pkg[uart_pkg]
    tx[uart_tx.sv] -->|import| pkg
    rx[uart_rx.sv] -->|import| pkg
\`\`\`
```

### types.md

```markdown
# Type Definitions

## Structs

### request_t
**File:** pkg/types_pkg.sv:15
\`\`\`systemverilog
typedef struct packed {
    logic [7:0] addr;
    logic [31:0] data;
} request_t;
\`\`\`
**Width:** 40 bits

## Enums

| Name | Values | Width | File |
|------|--------|-------|------|
| state_t | IDLE,RUN,DONE | 2-bit | uart_pkg.sv:10 |

## Typedefs

| Alias | Base Type | File |
|-------|-----------|------|
| data_t | logic[31:0] | types.sv:5 |
```

### functions.md

```markdown
# Functions and Tasks

## Functions

### calc_crc
**File:** rtl/crc_pkg.sv:20
**Return:** logic [15:0]
**Args:** input logic [7:0] data
**Used by:** uart_tx, uart_rx

## Tasks

### send_byte
**File:** tb/uart_tb.sv:50
**Args:** input byte data
```

### macros.md

```markdown
# Preprocessor Directives

## Defines
| Macro | Value | File |
|-------|-------|------|
| DATA_WIDTH | 32 | defines.svh:1 |

## Include Graph

\`\`\`mermaid
flowchart TD
    top[top.sv] -->|include| defs[defines.svh]
    uart[uart.sv] -->|include| defs
\`\`\`

## Conditional Compilation
| Condition | Files |
|-----------|-------|
| \`ifdef SIMULATION | tb/*.sv |
```
