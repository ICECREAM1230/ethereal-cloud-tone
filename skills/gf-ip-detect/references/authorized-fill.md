# Authorized gap implementation

Read only when the user requests implementation and any selected IP passes the availability gate. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### Step 4: Auto-Fill (with user approval)

Treat every match as a suggestion until `gf-ip` discovers a real block directory, `block.yaml`, sources, and applicable verification evidence. If no such block exists, report the opportunity without offering a verified replacement. Auto-fill also requires an explicit implementation request; a scan/diagnosis request remains read-only.

Present findings and ask:
```
Found 2 missing modules and 1 stub:

1. fifo_controller → 92% match with fifo_sync IP block
   Action: `$gf-ip add fifo_sync` and rename to fifo_controller? [Y/n]

2. spi_peripheral → 88% match with spi_master IP block
   Action: `$gf-ip add spi_master` and adapt ports? [Y/n]

3. uart_wrapper → Empty stub, matches uart IP pattern
   Action: Generate implementation using uart IP as base? [Y/n]

4. CRITICAL: CDC crossing at top.sv:55 without synchronizer
   Action: Insert cdc_2ff synchronizer? [Y/n]
```

---

## Auto-Fill Dispatch

When the user has explicitly authorized implementation and the selected block passed the availability gate, perform the appropriate responsibility directly or delegate it when available:

### For IP Block Matches
```
Delegate to an available worker with role `sv-codegen`:
  prompt: |
    Implement module <name> based on the <ip_block> IP block.
    Adapt ports to match the instantiation in <file>:<line>.
    Original ports: <detected_ports>
    IP block ports: <ip_ports>
    Generate the module, then create a testbench.
```

### For Stubs
```
Delegate to an available worker with role `sv-codegen`:
  prompt: |
    Implement the stub module at <file>.
    The module signature is already defined:
    <module_signature>

    Based on the port names and context, this appears to be a <type>.
    Implement full functionality, following the existing codebase patterns.
```

### For CDC Issues
```
Delegate to an available worker with role `sv-refactor`:
  prompt: |
    Add CDC synchronization at <file>:<line>.
    Signal <signal> crosses from <src_clk> to <dst_clk> domain
    without synchronization.

    Insert a cdc_2ff synchronizer (from GateFlow IP library).
    Ensure the synchronizer is properly reset.
```

---

## Integration with `$gf` Orchestrator

The `$gf` orchestrator can invoke IP detection:
- Before any new feature implementation (check what exists)
- After codebase mapping (enrich with IP analysis)
- When user says "what's missing" or "scan for gaps"

## Integration with `$gf-architect`

Combine with codebase mapping:
1. `$gf-architect` maps the module hierarchy
2. `$gf-ip-detect` overlays IP analysis on the map
3. Result: hierarchy + IP opportunities + CDC issues

---

## Commands

- "Scan for missing IP blocks" → full detection + report
- "Auto-fill missing modules" → detect, verify actual block availability/evidence, then implement only with authorization
- "What IP blocks does my project need?" → detection + suggestions
- "Find CDC issues" → focused CDC crossing analysis
- "Replace ad-hoc code with verified IP" → pattern match, verify an actual block and its evidence, then propose or perform the authorized change

---

## Auto-Suggestion Integration

| Detected Pattern | Suggested Command | Parameters |
|---|---|---|
| Sync FIFO signals | `$gf-ip add fifo_sync` | WIDTH=detected, DEPTH=next_pow2 |
| Async FIFO signals | `$gf-ip add fifo_async` | WIDTH=detected, DEPTH=8 |
| 2FF CDC crossing | `$gf-ip add cdc_2ff` | default |
| Multi-bit CDC handshake | `$gf-ip add cdc_handshake` | WIDTH=detected |
| UART pattern | `$gf-ip add uart` | CLK_FREQ=detected, BAUD=115200 |
| SPI pattern | `$gf-ip add spi_master` | CLK_DIV=computed |
| AXI register pattern | `$gf-ip add axi4lite_slave` | ADDR_WIDTH=detected |
| Button debounce | `$gf-ip add debouncer` | DEBOUNCE_MS=20 |
