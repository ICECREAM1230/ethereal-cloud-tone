---
name: gf-summary
description: "Summarize Verilator, lint, or simulation output into a readable, actionable format. Use when the user wants to understand build output, lint errors, or simulation results from a Verilator or EDA tool run."
---
Read [the Codex execution contract](../gf/references/codex-runtime.md) before this workflow. Preserve analysis/plan/annotation scope. The `sv-*`, `pcb-designer`, and `worker_role` labels below are responsibilities, not registered tools or agent types. Perform work directly unless bounded delegation is available and authorized; pass ordinary task prompts using the actual host tool. Read named skills from the catalog, not a fictional `Skill` API. Shell examples marked `bash` require a POSIX shell; Windows uses the PowerShell discovery in the contract. Persist maps, plans, profiles and configuration only when requested or necessary for authorized implementation.


# GateFlow Summary Skill

Summarizes Verilator lint and simulation output into a clean, readable report.

## Instructions

When invoked, summarize supplied or existing evidence. Run a new lint/simulation/EDA check only when the user requests execution. Preserve the original diagnostic code, file/line, and a short exact excerpt where useful. State tool/version, command or procedure, selected source set/top/includes/defines, exit code, log/report path, and freshness when known. Never expand a `PASS` beyond the tool, source set, and checks that actually ran.

### Summary Format

```
## Lint Summary: <filename>

| Status | Count |
|--------|-------|
| Errors | X |
| Warnings | X |
| Clean | Yes/No |

### Issues Found

#### Errors
- **Line X**: <error description> - <fix suggestion>

#### Warnings
- **Line X**: <warning description> - <fix suggestion>

### Recommendations
1. <prioritized fix>
2. <next fix>
```

## Trigger Patterns

Activate when user says:
- "summarize lint"
- "lint summary"
- "what's wrong with"
- "show lint results"

## Example

**Input:** Verilator output with WIDTH and UNUSED warnings

**Output:**
```
## Lint Summary: sync_fifo.sv

| Status | Count |
|--------|-------|
| Errors | 0 |
| Warnings | 2 |
| Clean | No |

### Warnings
- **Line 15**: WIDTH - Signal 'count' width mismatch (8 vs 4 bits)
  - Fix: Change `logic [7:0] count` to `logic [3:0] count`

- **Line 32**: UNUSED - Signal 'debug_flag' is never used
  - Fix: Remove the signal or add `/* verilator lint_off UNUSED */`

### Recommendations
1. Fix WIDTH warning first (affects functionality)
2. Clean up UNUSED signals
```

## Usage

```
$gf-summary                    # Summarize last lint output
$gf-summary sync_fifo.sv       # Summarize available evidence for this file; run lint only if requested
```

When a new lint run is requested for a file path, run an appropriate command after resolving the active source set, includes, defines, language mode, and top:
```bash
verilator --lint-only -Wall <file> 2>&1
```

Then parse and present the summary.
