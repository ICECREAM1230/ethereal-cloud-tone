# RTL request examples

These examples describe broader workflows. For a bounded change to existing RTL or constraints, [local edits](local-edits.md) takes precedence: perform only the affected edit and selected checks. "Create X" and "create X and test" do not automatically request a saved plan, workers or full regression.

Read only when a matching request needs a worked workflow example. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Progress Updates

Keep user informed:

```markdown
Gathering requirements...
? Asked about size, interface, testbench needs

Planning implementation...
✓ Created plan with sv-planner

Building components in parallel...
✓ the `sv-orchestrator` responsibility divided the components

Running lint check...
⚠ 2 warnings found
  Running the `sv-refactor` responsibility to fix...
✓ Lint clean

Creating testbench...
✓ Created tb_fifo.sv (via sv-orchestrator)

Running simulation...
✗ Test failed: read data mismatch
  Running the `sv-debug` responsibility to analyze...
  Issue identified: read pointer not incrementing
  Running the `sv-refactor` responsibility to fix...
✓ Fixed, re-running...
✓ All tests pass

Done! Created:
- rtl/fifo.sv (FIFO module, 8-deep, 32-bit)
- tb/tb_fifo.sv (Self-checking testbench)
```

---

## Handling Different Request Types

### "Build/compile/simulate this Gowin project"
```
1. Detect `.gprj` and read the enabled `FileList` and exact Arora V device/version
2. Use `gw_sh`/Tcl for IP generation, official compile, synthesis, PnR, and bitstream
3. For vendor-model functional simulation, prefer compatible Verilator execution; use ModelSim/Questa second when required, following the shared vendor contract
4. If system simulation was requested and the adapter is unavailable, prepare a manifest or `.do` template when writes are authorized and report execution `BLOCKED`; otherwise describe the missing artifact without writing it
5. Use GateFlow lint/Verilator only for portable/module-level checks around the vendor boundary
6. Report stage, output paths, freshness, and evidence separately
```

### "Create X"
```
1. ASK questions about requirements
2. Run the `sv-planner` responsibility
3. Run the `sv-orchestrator` responsibility (decompose + parallel build)
   If single-threaded, run the `sv-codegen` responsibility instead
4. Lint
5. If issues → run the `sv-refactor` responsibility
6. Done (offer to create testbench)
```

### "Create X and test it"
```
1. ASK questions about requirements
2. Run the `sv-planner` responsibility
3. Run the `sv-orchestrator` responsibility → create module + TB in parallel
   If single-threaded, run the `sv-codegen` responsibility → then sv-testbench
4. Lint → if issues, run the `sv-refactor` responsibility
5. Simulate → if fails, run the `sv-debug` responsibility, then sv-refactor
6. Report results
```

### "Fix this" / "Debug this"
```
1. ASK about symptoms
2. Read the code
3. If lint issue → run the `sv-refactor` responsibility
4. If sim issue → run the `sv-debug` responsibility first, then sv-refactor
5. Verify fix
```

### "Bug: X happens when Y" (Test-First Bug Fixing)

**IMPORTANT:** When user reports a bug, do NOT jump to fixing. Write a test first.

```
1. UNDERSTAND the bug
   - What's the expected behavior?
   - What's the actual behavior?
   - What triggers it?

2. WRITE A REPRODUCTION TEST FIRST
   - Run the `sv-testbench` responsibility to create a targeted test case
   - Test MUST fail initially (proves it captures the bug)
   - Test should be minimal - isolate the bug

3. RUN the test to confirm it fails
   - Use gf-sim skill
   - STATUS: FAIL expected (this is good!)
   - If test passes, the test doesn't capture the bug - revise it

4. FIX the bug
   - Run the `sv-debug` responsibility to diagnose root cause
   - Run the `sv-refactor` responsibility to implement fix
   - fix directly within authorized scope or delegate when useful

5. VERIFY with the same test
   - Run gf-sim again
   - STATUS: PASS = bug is fixed
   - STATUS: FAIL = fix didn't work, iterate

6. RUN affected existing regression checks
   - Use the full suite only when project instructions, shared-behavior risk or the explicit request requires it
```

**Why test-first?**
- Forces understanding before fixing
- Provides objective success criteria
- Prevents "I think I fixed it" false positives
- Creates regression protection
- Subagents have clear exit condition

**Example flow:**
```
User: "Bug: FIFO outputs X when read while empty"

1. ASK: "What should happen instead? Stall? Return zero? Error flag?"
2. User: "Should stall until data available"
3. Run the `sv-testbench` responsibility:
   "Write a test that reads from empty FIFO and checks it stalls"
4. Run gf-sim → FAIL (confirms bug exists)
5. Run the `sv-debug` responsibility → "read_ptr increments even when empty"
6. Run the `sv-refactor` responsibility → adds `empty` guard to read logic
7. Run gf-sim → PASS
8. Report: "Fixed. Added test tb/test_empty_read.sv"
```

### "Explain this"
```
1. Check for codebase map
2. Run the `sv-understanding` responsibility
3. Return explanation
```

### "Add assertions to X"
```
1. ASK about what properties to verify
2. Read the module
3. Run the `sv-verification` responsibility
4. Lint to verify syntax
5. Done
```

### Complex / Multi-file
```
1. ASK about scope and constraints
2. Run the `sv-planner` responsibility
3. Run the `sv-orchestrator` responsibility for parallel component build
4. Use sv-developer only for cross-cutting edits or deep refactors
5. Verify each phase
```

---
