---
name: gf-errors
description: >
  Error translation layer for hardware tool outputs. Converts cryptic
  Verilator, Yosys, GHDL, and simulation errors into 3-layer explanations.
  Used internally by the GateFlow orchestrator.
---
Read [the Codex execution contract](../gf/references/codex-runtime.md) before this workflow. Preserve analysis/plan/annotation scope. The `sv-*`, `pcb-designer`, and `worker_role` labels below are responsibilities, not registered tools or agent types. Perform work directly unless bounded delegation is available and authorized; pass ordinary task prompts using the actual host tool. Read named skills from the catalog, not a fictional `Skill` API. Shell examples marked `bash` require a POSIX shell; Windows uses the PowerShell discovery in the contract. Persist maps, plans, profiles and configuration only when requested or necessary for authorized implementation.


# GF Errors — Hardware Error Translation

When any tool produces an error, translate it through three layers before
presenting to the user. Keep the diagnostic code, file/line, and a short relevant raw excerpt alongside the translation so the conclusion remains auditable.

Classify the failure layer before proposing an RTL change: HDL source error; missing source/include/library; license or device database; vendor primitive/protected IP boundary; invalid command/configuration; or tool crash. An X value does not prove that every register needs reset: trace its first source and distinguish intentionally invalid datapath contents from control/state that requires initialization.

## Translation Protocol

For every error from Verilator, Yosys, GHDL, or simulation:

### Layer 1 — WHAT (one sentence, plain English)
State what happened. No technical jargon. No tool names.
- BAD: "%Error: counter.sv:15:17: Cannot find signal: 'clk_in'"
- GOOD: "The signal `clk_in` doesn't exist in this module."

### Layer 2 — WHY (1-2 sentences, context)
Explain why it happened. Reference specific lines and names.
- "Your module declares its clock input as `clk` (line 5), but the
  always block on line 15 references `clk_in`. These names must match."

### Layer 3 — FIX (actionable, specific)
Tell the user exactly what to do. Include file, line, and the change.
- "Change `clk_in` to `clk` on line 15 of counter.sv."

## Common Verilator Errors

| Error Code | Layer 1 Template | Layer 2 Guidance |
|-----------|-----------------|-----------------|
| UNUSED | "Signal `{name}` is declared but never used." | Check if it should be connected or can be removed. |
| UNDRIVEN | "Signal `{name}` is never assigned a value." | It's declared but no logic drives it. |
| WIDTH | "Width mismatch: `{lhs}` is {lw} bits but `{rhs}` is {rw} bits." | Explicit sizing needed. |
| CASEINCOMPLETE | "Case statement doesn't cover all possible values." | Add a `default:` branch. |
| LATCH | "Unintended latch inferred for `{name}`." | A combinational block has paths where `{name}` isn't assigned. Add a default assignment at the top of the always_comb block. |
| BLKSEQ | "Blocking assignment used in sequential block." | Use `<=` (non-blocking) in `always_ff`, not `=` (blocking). |
| PINMISSING | "Port `{name}` on instance `{inst}` is not connected." | Either connect it or explicitly mark as unconnected. |

## Common Simulation Errors

| Symptom | Layer 1 | Layer 2 |
|---------|---------|---------|
| X-values in output | "Output `{sig}` has unknown (X) values." | Trace the first X source. Check uninitialized required control/state, invalid data sampled without a valid qualifier, incomplete assignments, memory/model behavior, and reset release; do not reset every datapath register by default. |
| Simulation hangs | "Simulation never reaches `$finish`." | Likely an infinite loop or missing exit condition in the testbench. Check for blocking waits without timeouts. |
| Assertion failure | "Assertion `{name}` failed at time {t}." | The property being checked was violated. Review the assertion condition and the signal values at the failure time. |
| Wrong output | "Output `{sig}` is {actual}, expected {expected}." | Logic error in the design or incorrect test expectations. Trace the signal back to its source. |

## Common Yosys Synthesis Errors

| Error Pattern | Layer 1 | Layer 2 |
|--------------|---------|---------|
| Module not found | "Module `{name}` was not included in synthesis." | All source files must be passed to Yosys. Check your file list. |
| Unsupported construct | "Yosys doesn't support `{construct}` in synthesis." | This SystemVerilog feature isn't available in Yosys. Rewrite using Verilog-2005 compatible constructs. |
| Combinational loop | "Circular dependency detected in combinational logic." | Signal `{sig}` feeds back to itself without a register. Add a flip-flop to break the loop. |

## Usage by Orchestrator

When the `$gf` orchestrator receives a FAIL status from gf-lint or gf-sim,
it MUST:
1. Parse the raw error output
2. Match against the tables above
3. Present the 3-layer translation to the user
4. THEN perform the applicable `sv-refactor` or `sv-debug` responsibility with the raw error

The fix agent gets the raw error. The user gets the translated version.
Both see the same issue, but the user sees it in language they understand.
