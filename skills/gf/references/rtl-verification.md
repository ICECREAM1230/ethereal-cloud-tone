# Verification and diagnostic examples

Read only when a selected check needs command/diagnostic detail. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Error Translation

When ANY verification step returns STATUS: FAIL, translate the error
before presenting to the user. Use the gf-errors skill's 3-layer protocol:

1. **WHAT**: One sentence, plain English — no tool names or jargon
2. **WHY**: Context with specific line numbers and signal names
3. **FIX**: Exact actionable change needed (file, line, what to change)

NEVER show raw Verilator/Yosys output to the user without translation.
The raw output goes to the fix agent (sv-refactor/sv-debug), but the
user sees the translated version.

## Verilator/Yosys Error Dictionary

### Verilator Patterns

| Code | WHAT | FIX |
|---|---|---|
| WIDTHTRUNC | Value truncated (target narrower) | Explicit slice: `target = source[7:0]` |
| WIDTHEXPAND | Value zero-extended (target wider) | Explicit concat: `{8'b0, source}` |
| MULTIDRIVEN | Signal driven from multiple always blocks | Single always block or CDC sync |
| COMBDLY | Non-blocking `<=` in combinational block | Use `=` in always_comb |
| INITIALDLY | Delayed assign in initial block | Use `=` not `<=` |
| ENUMVALUE | Enum assigned wrong type | Cast: `state = state_t'(value)` |
| VARHIDDEN | Inner variable shadows outer scope | Rename inner variable |
| SYNCASYNCNET | Signal used as both sync and async reset | Use consistently as one type |
| ALWCOMBORDER | Variable read before assigned in always_comb | Move assignment above first use |
| TIMESCALEMOD | Module missing timescale | Add `` `timescale 1ns/1ps `` |
| MODDUP | Module defined more than once | Remove duplicate |
| PINNOCONNECT | Instance port not connected | Connect or `.port()` |
| ASSIGNIN | Writing to input port | Change to output |
| BLKANDNBLK | Mixed blocking/non-blocking on same signal | `<=` in always_ff, `=` in always_comb |
| CASEOVERLAP | Two case items match same value | Remove duplicate |

### Yosys Patterns

| Pattern | WHAT | FIX |
|---|---|---|
| Failed to evaluate $clog2 | Argument not compile-time constant | Use localparam |
| generate for-loop not constant | Loop bound not constant | Use parameter/literal |
| System task outside initial | $display outside initial | Wrap in `` `ifdef SIMULATION `` |
| Identifier not found | Missing source file | Add to synthesis script |
| Combinational loop | Circular dependency | Insert flip-flop to break loop |

---

## Verification Commands

### Lint Check

**Invoke skill:** `gf-lint`

```
Invoke the named skill:
  skill: "gf-lint"
  args: "<files or empty>"
```

| STATUS | Action |
|--------|--------|
| PASS | Proceed to next step |
| FAIL | Run the `sv-refactor` responsibility with error context |
| BLOCKED | Report issue to user |

### Compile + Simulate

**Invoke skill:** `gf-sim`

```
Invoke the named skill:
  skill: "gf-sim"
  args: "<files or empty>"
```

| STATUS | Action |
|--------|--------|
| PASS | Report success, done |
| FAIL | Run the `sv-debug` responsibility - fix directly within authorized scope or delegate when useful |
| BLOCKED | Report setup issue to user |

### Gowin Official Build and Vendor IP

For a Gowin official project, do not substitute the generic `gf-sim`, Yosys, or nextpnr command for the vendor flow. First inspect the `.gprj` and route to the Gowin Tcl procedure in [references/gowin-vendor-flow.md](gowin-vendor-flow.md). Use `gw_sh` to open the existing project and run the requested official stages. For functional simulation prefer Verilator with compatible actual models; use the separate ModelSim/Questa adapter only as the secondary path. Preserve the distinction between:

- **PASS:** the requested Gowin stage or simulator actually ran and produced the expected evidence;
- **BLOCKED:** execution was requested but a required vendor executable, library, IP model, license, permission, or adapter was unavailable;
- **NOT_RUN:** the stage was outside scope or deliberately not executed;
- **FAIL:** the stage ran and returned a failure.

An official Gowin build is the implementation sign-off. GateFlow lint and Verilator results are module-level evidence and do not replace Gowin IP compilation, vendor-library simulation, or final PnR/timing reports.

---

## Handling failures

Separate environment, source, model and hardware failures. Diagnose and apply authorized fixes directly or through useful delegation. Rerun the failing check. If evidence no longer permits progress, report the concrete missing input instead of repeating identical attempts.
