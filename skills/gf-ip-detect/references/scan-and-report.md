# Scanning and reporting

Read only when producing an IP-gap inventory/report. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Detection Workflow

### Step 1: Scan Codebase

```
Read the selected active HDL source set
        |
Extract: module definitions (name, ports, parameters)
        |
Extract: module instantiations (what's used)
        |
Extract: signal patterns (FIFO, CDC, protocol)
        |
Build dependency graph
```

### Step 2: Identify Gaps

For each instantiated module:
1. Is it defined in the project? → if no, **MISSING**
2. Is it a known IP block from GateFlow library? → suggest `$gf-ip add`
3. Is it a vendor primitive? → flag for review
4. Is it defined but empty/stub? → **NEEDS IMPLEMENTATION**

For each signal pattern:
1. Does it match a standard IP pattern? → suggest replacement
2. Is the implementation ad-hoc? → suggest verified IP block
3. Is there a CDC crossing without synchronizer? → **CRITICAL: suggest cdc_2ff**

### Step 3: Report

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
ACTION_REQUIRED: true | false
SCAN_RESULTS:
  total_modules: 15
  defined: 12
  missing: 2
  stubs: 1

MISSING_MODULES:
  - name: fifo_controller
    instantiated_in: rtl/top.sv:42
    ports: [clk, rst_n, wr_en, wr_data, rd_en, rd_data, full, empty]
    suggested_ip: fifo_sync (92% match)

  - name: spi_peripheral
    instantiated_in: rtl/top.sv:67
    ports: [clk, rst_n, sclk, mosi, miso, cs_n, tx_data, rx_data]
    suggested_ip: spi_master (88% match)

STUBS:
  - name: uart_wrapper
    file: rtl/uart_wrapper.sv:1
    status: "Module defined but body is empty (TODO marker at line 15)"
    suggested_action: "Implement using uart IP block as base"

PATTERN_MATCHES:
  - pattern: "Ad-hoc 2FF synchronizer at rtl/sync.sv:10"
    suggestion: "Replace with verified cdc_2ff IP block"

  - pattern: "CDC crossing without synchronizer at rtl/top.sv:55"
    severity: CRITICAL
    suggestion: "Add cdc_2ff between clk_a and clk_b domains"

IP_OPPORTUNITIES:
  - "3 modules could use GateFlow IP blocks (fifo_sync, spi_master, cdc_2ff)"
  - "1 stub module needs implementation (uart_wrapper)"
  - "1 critical CDC issue detected"
---END-GATEFLOW-RESULT---
```
