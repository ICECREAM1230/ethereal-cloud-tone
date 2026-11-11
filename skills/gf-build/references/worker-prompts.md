# Worker prompt examples

Read only when authorized parallel workers need explicit component/testbench tasks. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Agent Prompt Templates

### sv-codegen Component Prompt

```markdown
## Component: [NAME]

## Context
Part of: [parent design]
Package: [package to import]

## Specification
[Detailed functional spec]

## Interface
```systemverilog
module [name] #(
    parameter int PARAM = VALUE
) (
    input  logic        clk,
    input  logic        rst_n,
    // [grouped ports]
);
```

## Requirements
1. [Requirement]
2. [Requirement]

## Output
Write to: [path]
```

### sv-testbench Component Prompt

```markdown
## Testbench for: [DUT_NAME]

## DUT Location
rtl/[dut].sv

## Test Scenarios
1. [Test case 1]
2. [Test case 2]
3. [Edge case]

## Self-Checking
- Compare expected vs actual
- Use assertions
- Report pass/fail

## Output
Write to: tb/tb_[dut].sv
```

---
