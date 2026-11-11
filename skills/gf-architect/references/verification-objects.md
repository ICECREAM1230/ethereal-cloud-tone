# Verification and advanced object schemas

Read only when assertions, interfaces, classes, generate or DPI are present. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### verification.md

```markdown
# Verification Constructs

## Assertions (SVA)

### uart_tx assertions
| Property | Type | Description |
|----------|------|-------------|
| p_no_overflow | assert | FIFO never overflows |
| p_valid_stable | assert | valid held until ready |

## Covergroups

### cg_opcodes
**File:** tb/coverage.sv:30
**Coverpoints:** opcode, size
**Crosses:** opcode x size

## Bind Statements
| Target | Checker | File |
|--------|---------|------|
| uart_tx | uart_sva | bind.sv:5 |
```

### interfaces.md (if found)

```markdown
# Interfaces

## axi_if
**File:** rtl/axi_if.sv
**Parameters:** ADDR_WIDTH, DATA_WIDTH

### Signals
| Name | Type | Width |
|------|------|-------|
| awvalid | logic | 1 |
| awaddr | logic | [ADDR_WIDTH-1:0] |

### Modports
| Name | Signals |
|------|---------|
| master | output: aw*, w*, input: b* |
| slave | input: aw*, w*, output: b* |
```

### classes.md (if UVM found)

```markdown
# Classes

## Class Hierarchy

\`\`\`mermaid
classDiagram
    uvm_driver <|-- uart_driver
    uvm_monitor <|-- uart_monitor
\`\`\`

## uart_driver
**File:** tb/uart_driver.sv
**Extends:** uvm_driver
```

### generate.md (if found)

```markdown
# Generate Blocks

## uart_fifo generate loop
**File:** rtl/uart_fifo.sv:50

\`\`\`systemverilog
genvar i;
generate
    for (i = 0; i < DEPTH; i++) begin : gen_mem
        // memory slice
    end
endgenerate
\`\`\`
```

### dpi.md (if found)

```markdown
# DPI Functions

## Imports
| SV Name | C Name | Return | Args |
|---------|--------|--------|------|
| c_calc | calc | int | int a, int b |

## Exports
| SV Name | C Name |
|---------|--------|
| sv_notify | notify |
```
