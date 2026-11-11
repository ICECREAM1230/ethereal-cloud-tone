# Lint result examples

Read only when a consumer requests structured lint results. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### 5. Return Structured Result

**Use this format for requested structured output; otherwise retain the evidence in prose:**

```
---GATEFLOW-RESULT---
STATUS: PASS|FAIL|BLOCKED|NOT_RUN
ERRORS: <count>
WARNINGS: <count>
FILES: <comma-separated list>
DETAILS: <one-line summary>
---END-GATEFLOW-RESULT---
```

**Status definitions:**
- `PASS`: No errors, warnings OK (0-N warnings acceptable)
- `FAIL`: One or more errors found
- `BLOCKED`: Lint was requested, but a required tool, input, or permission prevented execution
- `NOT_RUN`: Lint was outside scope or was deliberately not executed

### 6. Example Output

```
Running lint on: rtl/fifo.sv, rtl/uart.sv

$ verilator --lint-only -Wall rtl/fifo.sv rtl/uart.sv

%Warning-UNUSED: rtl/fifo.sv:25: Signal 'debug_flag' is not used
%Warning-WIDTH: rtl/uart.sv:42: Operator ASSIGN expects 8 bits but got 16

## Summary

**Files:** 2 | **Errors:** 0 | **Warnings:** 2

### Warnings Explained

1. **UNUSED** (rtl/fifo.sv:25): `debug_flag` is declared but never read
   - Fix: Remove if unneeded, or suppress with lint pragma

2. **WIDTH** (rtl/uart.sv:42): Assigning 16-bit value to 8-bit signal
   - Fix: Truncate explicitly `data[7:0]` or widen the target

---GATEFLOW-RESULT---
STATUS: PASS
ERRORS: 0
WARNINGS: 2
FILES: rtl/fifo.sv,rtl/uart.sv
DETAILS: Lint passed with 2 warnings
---END-GATEFLOW-RESULT---
```

### 7. Error Case Example

```
$ verilator --lint-only -Wall rtl/broken.sv

%Error: rtl/broken.sv:10: syntax error, unexpected IDENTIFIER

---GATEFLOW-RESULT---
STATUS: FAIL
ERRORS: 1
WARNINGS: 0
FILES: rtl/broken.sv
DETAILS: Syntax error on line 10
---END-GATEFLOW-RESULT---
```

## Usage by `$gf` Orchestrator

The `$gf` skill uses this skill internally and parses the result block:

```
Parse ---GATEFLOW-RESULT--- block:
- STATUS: PASS -> proceed to next step
- STATUS: FAIL -> run the `sv-refactor` responsibility with error context
- STATUS: BLOCKED -> report issue to user
```
