# GF Sim Skill

Compile and run SystemVerilog simulation with structured output.

## Simulator preference and Gowin model boundary

Use Verilator first for functional RTL simulation; on the current Linux host use the installed OSS CAD Suite. ModelSim/Questa is the secondary simulator and is currently uninstalled on that host. Resolve the actual testbench/source/model set before selecting a backend.

- Portable/module RTL follows the Verilator workflow below.
- A Gowin `.gprj`, primitive or library name alone does not require the secondary simulator. Follow [the vendor contract](../../gf/references/gowin-vendor-flow.md), identify the exact generated models and matched family/version libraries, and use Verilator if their constructs and required behavior are actually compatible.
- Protected content, unsupported required constructs/languages or simulator-specific dependencies require a verified alternative model or the secondary flow. Do not decode/edit protected files, discard required models or blackbox unsupported behavior to report a full functional PASS. A stub test covers isolated user logic only.
- SDF timing simulation requires a simulator/adapter verified to apply the required back-annotation and timing checks. Verilator `--timing` alone does not establish that capability.

For requested execution, prepare the source/model manifest and selected command when writing is authorized. Prepare `.do` only for a selected ModelSim/Questa flow. Missing required secondary tools/libraries/models/licenses means `BLOCKED`; it does not justify installation or an RTL fix. A compatible Verilator test can run without ModelSim. Preparation is not simulation evidence.

## Instructions

### 1. Identify Files

**If files specified in args:**
Use provided paths. Identify the selected testbench top; file order alone is insufficient.

**If no files specified:**
Resolve the active manifest/build script, top, ordered source set, include paths, defines and required models. In a Gowin project use enabled `.gprj` entries; a bounded scan is only a candidate list for an ad-hoc portable directory.

### 2. Classify Files: DUT vs Testbench

**Testbench clues** (not a definitive classifier; synthesizable ROM initialization may use `initial`):
- `initial begin`
- `$display`, `$monitor`
- `$finish`, `$fatal`
- `$dumpfile`, `$dumpvars`
- Clock generation: `always #N clk = ~clk`
- File in `tb/` directory or named `*_tb.sv`, `tb_*.sv`

**DUT indicators** (has any of):
- `always_ff`, `always_comb`
- Synthesizable constructs only
- No `$` system tasks (except assertions)
- File in `rtl/` directory

Confirm classification against the selected testbench top and actual source dependencies; markers alone do not establish a complete compile set.

### 3–4. Compile and execute on the selected host

For Linux software calls read [Linux simulation](simulation-linux.md); for Windows calls read [Windows host calls](../../gf/references/host-windows.md) only on that target. Preserve the resolved top, file order and options, isolate the actual output directory and capture compile/run status separately.

### 5. Parse Results

**Check output for:**
- `PASS`, `SUCCESS`, `All tests passed` -> PASS
- `FAIL`, `ERROR`, `MISMATCH`, `ASSERT` -> FAIL
- `$fatal` or non-zero exit code -> FAIL
- `$finish`/exit zero alone -> execution completed, not functional PASS. Require expected-value checks, zero failures, and evidence all intended checks completed.

Record the real compile/run exit status before any logging/display command. Exit zero alone means completion; expected checks must also pass. A nonzero compile/run result or testbench timeout cannot be a functional PASS.

### 6. Return Structured Result

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
- `PASS`: Simulation completed, tests passed
- `FAIL`: Simulation failed (compile error, assertion failure, test failure)
- `BLOCKED`: Simulation was requested, but a required tool, file, model, library, license, or permission prevented execution
- `NOT_RUN`: Simulation was outside scope or was deliberately not executed

Record generated-model/netlist/SDF freshness separately as `current`, `stale`, or `unknown`. `STALE` is not an execution status; stale required inputs cannot support `PASS`.

### 7. Example: Successful Run

```
## File Classification

| File | Type | Reason |
|------|------|--------|
| rtl/fifo.sv | DUT | has always_ff, no $display |
| tb/tb_fifo.sv | TB | has $display, $finish, initial |

## Compilation

$ verilator --binary -j 0 -Wall --trace rtl/fifo.sv tb/tb_fifo.sv -o sim

(compilation output...)

## Simulation

$ ./obj_dir/sim

Test 1: Write single item... PASS
Test 2: Fill FIFO... PASS
Test 3: Overflow check... PASS
All tests passed!

---GATEFLOW-RESULT---
STATUS: PASS
ERRORS: 0
WARNINGS: 0
FILES: rtl/fifo.sv,tb/tb_fifo.sv
DETAILS: All 3 tests passed
---END-GATEFLOW-RESULT---
```

### 8. Example: Failed Run

```
## Simulation

$ ./obj_dir/sim

Test 1: Write single item... PASS
Test 2: Read back... FAIL
  Expected: 0xAB
  Got: 0x00
$fatal called at tb_fifo.sv:87

---GATEFLOW-RESULT---
STATUS: FAIL
ERRORS: 1
WARNINGS: 0
FILES: rtl/fifo.sv,tb/tb_fifo.sv
DETAILS: Test 2 failed - read data mismatch at line 87
---END-GATEFLOW-RESULT---
```

### 9. Example: Compile Error

```
$ verilator --binary -j 0 -Wall rtl/fifo.sv tb/tb_fifo.sv -o sim

%Error: rtl/fifo.sv:45: Cannot find: fifo_mem

---GATEFLOW-RESULT---
STATUS: FAIL
ERRORS: 1
WARNINGS: 0
FILES: rtl/fifo.sv,tb/tb_fifo.sv
DETAILS: Compile error - undefined reference to fifo_mem
---END-GATEFLOW-RESULT---
```

## Common Issues and Solutions

| Issue | Symptom | Solution |
|-------|---------|----------|
| Multiple tops | "Multiple top modules" | Add `--top-module <name>` |
| Missing module | "Cannot find: X" | Include file defining X |
| X-values | Output shows X | Check reset coverage |
| Timeout | Simulation hangs | Add timeout or fix FSM |
| No $finish | Runs forever | Ensure TB calls $finish |

For requested Linux performance/trace configuration, read [optional Linux simulation options](simulation-linux-options.md) only when needed. Check flags and capabilities against the installed version; do not assume every v5 build has identical support.

## Verilator SV Support

| Construct | Support |
|---|---|
| `always_comb`/`always_ff` | Full |
| Interfaces and modports | Full |
| Packages, structs, enums | Full |
| Generate | Full |
| DPI (C/C++ import/export) | Full |
| Classes | Partial |
| Constrained randomization | Partial |
| SVA (one-cycle) | Full |
| SVA (multi-cycle) | Not supported |

## Simulation Timeout

Use a bounded run and a testbench watchdog that fails the test on expiry. `$finish` in a watchdog or exit zero cannot turn a timed-out test into PASS. Host timeout calls belong in the matching invocation leaf.

## Usage by `$gf` Orchestrator

The `$gf` skill uses this skill internally and parses the result block:

```
Parse ---GATEFLOW-RESULT--- block:
- STATUS: PASS -> report success, done
- STATUS: FAIL -> perform the `sv-debug` responsibility directly, or delegate it when useful, with the failure context
- STATUS: BLOCKED -> report setup issue to user
- STATUS: NOT_RUN -> report the declared exclusion or reason execution was skipped
```
