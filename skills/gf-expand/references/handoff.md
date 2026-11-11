# Enriched handoff

Read only when passing clarified requirements to another workflow. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### Step 5: Handoff to Target

**For Skills:**
```
Invoke the named skill:
  skill: "gf"  (or gf-lint, gf-sim, etc.)
  args: "[context summary]"
```

**For Agents:**
```
Delegate to an available worker when useful:
  description: "Create FIFO with valid/ready interface"
  prompt: |
    ## Task
    Create a synchronous FIFO module with valid/ready handshaking.

    ## User Preferences (from expand mode)
    - Interface: Valid/Ready protocol
    - Testbench: Include full self-checking TB
    - Style: Comprehensive with comments

    ## Specifications
    [Details from clarification]

    ## Expected Output
    - rtl/fifo.sv - The FIFO module
    - tb/tb_fifo.sv - Self-checking testbench
```
