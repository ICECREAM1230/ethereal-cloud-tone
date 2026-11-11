# Scan patterns

Read only when textual source scanning needs pattern examples. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Regex Patterns Reference

```
# Design Units
^\s*module\s+(\w+)              # Module
^\s*interface\s+(\w+)           # Interface
^\s*package\s+(\w+)             # Package
^\s*program\s+(\w+)             # Program
^\s*class\s+(\w+)               # Class
^\s*checker\s+(\w+)             # Checker

# Ports & Parameters
(input|output|inout)\s+(logic|wire|reg)?\s*(\[.*?\])?\s*(\w+)
parameter\s+(int|logic|integer)?\s*(\w+)\s*=
localparam\s+

# Instances
^\s*(\w+)\s*(#\s*\([^)]*\))?\s+(\w+)\s*\(

# Always blocks
always_ff\s*@|always_comb|always_latch|always\s*@

# Types
typedef\s+enum
typedef\s+struct\s+packed
typedef\s+union

# Functions/Tasks
function\s+(automatic\s+)?
task\s+(automatic\s+)?

# Preprocessor
`define\s+(\w+)
`include\s+"([^"]+)"
`ifdef|`ifndef|`elsif

# Verification
assert\s+property|assume\s+property|cover\s+property
covergroup\s+(\w+)
sequence\s+(\w+)
property\s+(\w+)
bind\s+\w+

# Generate
generate|genvar

# Interface features
modport\s+\w+
clocking\s+\w+

# OOP/UVM
class\s+\w+|extends\s+\w+|virtual\s+class
constraint\s+\w+
rand\s+|randc\s+

# DPI
import\s+"DPI|export\s+"DPI
```

---
