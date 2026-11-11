# Complex RTL coordination

Read only when planning multi-component work or coordinating authorized parallel workers. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Decision Framework

### Step 0: Detect Vendor Project Mode

Before applying the generic RTL workflow, scan the requested project root for a `.gprj`. If a `.gprj` is present, follow **Gowin Project Mode** above. Ask only the missing project-specific questions (exact Arora V part/version, requested official stage, and whether required models and a compatible simulation command are available); do not route an official Gowin build to the generic Verilator/Yosys path by default.

### Step 1: Confirm SystemVerilog Task

When user makes a request, first confirm it's an SV task. If confirmed, proceed to Step 2.

### Step 2: Clarify Requirements When Needed

**Ask focused questions before delegating when needed:**

```
Ask questions such as:

For Creation requests:
- "What interface protocol?" (AXI, Wishbone, custom, none)
- "Include testbench?" (Yes with self-checking, Yes basic, No)
- "Parameterized?" (Yes fully, Some params, Fixed)
- "Clock domain?" (Single, Multiple with CDC, Async)

For Debug requests:
- "What behavior do you see vs expect?"
- "Any specific signals to focus on?"

For Planning requests:
- "Any constraints?" (Area, timing, power)
- "Integration needs?" (Standalone, part of larger system)
```

### Step 3: Plan First (for creation tasks)

For complex implementation, plan before code generation; perform the planning role directly or delegate when appropriate:
This is the same planning phase exposed by `$gf-plan`.

```
Delegate to an available worker with the role `sv-planner`:
  prompt: |
    Plan the implementation for: [user request]
    Requirements gathered:
    - [answers to the clarification questions]
    Create a detailed plan before any code is written.
```

### Step 4: Build in Parallel (for creation tasks)

After planning interfaces, build directly or delegate independent components when authorized and useful.
If the user selected **Single-threaded** build mode, skip sv-orchestrator and
run the `sv-codegen` responsibility (and sv-testbench if requested) sequentially.
This is the same build phase exposed by `$gf-build`.

---

## Orchestration Loop

```
┌─────────────────────────────────────────────────────────────────┐
│                   ORCHESTRATION LOOP                             │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ 0. CLARIFY REQUIREMENTS WHEN NEEDED                       │   │
│  │    Ask focused questions before delegating                 │   │
│  └──────────────────────────────────────────────────────────┘   │
│                          ↓                                       │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ 1. PLAN FIRST (for creation tasks)                        │   │
│  │    Run the `sv-planner` responsibility with gathered requirements            │   │
│  └──────────────────────────────────────────────────────────┘   │
│                          ↓                                       │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ 2. BUILD (parallel for creation tasks)                    │   │
│  │    Run the `sv-orchestrator` responsibility to decompose + build             │   │
│  └──────────────────────────────────────────────────────────┘   │
│                          ↓                                       │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ 3. VERIFY (via Skills)                                    │   │
│  │    Skill: gf-lint → structured result                     │   │
│  │    Skill: gf-sim  → structured result                     │   │
│  └──────────────────────────────────────────────────────────┘   │
│                          ↓                                       │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ 4. ASSESS (see Result Parsing Protocol)                    │   │
│  │    STATUS: PASS  → Next phase or DONE                     │   │
│  │    STATUS: FAIL  → Run the `sv-debug` responsibility (fix directly within authorized scope or delegate when useful)    │   │
│  │    STATUS: BLOCKED → Report to user                         │   │
│  └──────────────────────────────────────────────────────────┘   │
│                          ↓                                       │
│                    (loop until done)                             │
└─────────────────────────────────────────────────────────────────┘
```

### Result Parsing Protocol

Verification skills (`gf-lint`, `gf-sim`) may return `GATEFLOW-RESULT` blocks. Work performed under responsibility labels such as `sv-codegen` or `sv-debug` may use a `GATEFLOW-RETURN` block when structured orchestration is requested. The labels are not registered workers. Parse any block that is actually returned and verify its evidence.

**Block formats:**

| Source | Delimiter | Required Fields |
|--------|-----------|-----------------|
| Skills (gf-lint, gf-sim) | `---GATEFLOW-RESULT---` / `---END-GATEFLOW-RESULT---` | STATUS, ERRORS, WARNINGS, FILES, DETAILS |
| Responsibility output (sv-codegen, sv-debug, etc.) | `---GATEFLOW-RETURN---` / `---END-GATEFLOW-RETURN---` | STATUS, SUMMARY, FILES_CREATED or FILES_MODIFIED |

**Extraction procedure:**

1. **Locate block** — scan agent/skill output for the opening delimiter (`---GATEFLOW-RESULT---` or `---GATEFLOW-RETURN---`)
2. **Read execution status** — PASS, FAIL, BLOCKED or NOT_RUN; worker completion does not prove checks passed.
3. **Missing/malformed block** — inspect actual evidence; formatting errors are not design failures.
4. **Freshness** — record current/stale/unknown separately. Old results cannot establish current-source PASS.



**Decision table — what to do with each STATUS:**

| STATUS | Source | Action |
|--------|--------|--------|
| `PASS` | gf-lint | Proceed to simulation (or done if sim already passed) |
| `PASS` | gf-sim | Report success, done |
| `FAIL` | gf-lint | Perform the `sv-refactor` responsibility with DETAILS + full lint output, directly or by available delegation |
| `FAIL` | gf-sim | Perform the `sv-debug` responsibility with DETAILS + simulation output, directly or by available delegation |
| `BLOCKED` | any skill | Report the issue to the user and do not retry blindly |
| `NOT_RUN` | any skill | Report why the check was outside scope or deliberately skipped; do not claim success |
| `complete` | any agent | Proceed to next phase (verify files exist first) |
| `needs_clarification` | any agent | Forward the question to the user |
| missing/unrecognized | any | Report malformed or missing status evidence; do not convert it into a design failure |

### Verification Gate

**After every agent or skill return, run this mandatory checkpoint before proceeding:**

| After Phase | Gate Condition | If Gate Fails |
|-------------|---------------|---------------|
| sv-planner | GATEFLOW-RETURN with `STATUS: complete` and plan text present | repeat the `sv-planner` responsibility with clarified requirements |
| sv-orchestrator | GATEFLOW-RETURN with `STATUS: complete` and FILES_CREATED list non-empty | repeat the `sv-orchestrator` responsibility |
| sv-codegen | Files listed in FILES_CREATED exist on disk (`ls` check) | repeat the `sv-codegen` responsibility for missing files only |
| sv-refactor | **MUST re-run lint** — never assume fix worked | If lint still FAIL, increment retry counter and repeat the `sv-refactor` responsibility with previous + new errors |
| sv-debug | GATEFLOW-RETURN with `STATUS: complete` and SUMMARY contains fix description | If `needs_clarification`, forward to user |
| gf-lint | GATEFLOW-RESULT block present with valid STATUS | If missing, inspect command evidence and re-run once only when useful; otherwise report missing result evidence |
| gf-sim | GATEFLOW-RESULT block present with valid STATUS | If missing, inspect command evidence and re-run once only when useful; otherwise report missing result evidence. `BLOCKED` and `NOT_RUN` are valid in any mode. |

**File existence validation (between build and lint):**

```bash
# After sv-codegen or sv-orchestrator, verify files exist
ls <expected_files> 2>/dev/null
```

- If all files exist → proceed to lint
- If some files missing → repeat the `sv-codegen` responsibility for missing files only (do NOT rebuild files that already exist)
- If no files exist → report missing build output; do not claim a design failure

**Key rule: sv-refactor is NEVER assumed to succeed.** Always re-run the verification step (lint or sim) that originally failed. A "complete" return from sv-refactor only means it attempted a fix, not that the fix worked.

### Retry Tracking

Maintain a progress tracker for each verification target throughout the orchestration loop:

```
| Phase   | Target         | Attempt | Status  | Notes                    |
|---------|----------------|---------|---------|--------------------------|
| Lint    | rtl/fifo.sv    | 1/3     | FAIL    | WIDTH warning on line 42 |
| Lint    | rtl/fifo.sv    | 2/3     | PASS    | Fixed by sv-refactor     |
| Sim     | tb/tb_fifo.sv  | 1/3     | FAIL    | Read data mismatch       |
```

**Retry rules:**

- **1st failure** → run the applicable fix responsibilities (`sv-refactor` for lint, `sv-debug` then `sv-refactor` for sim)
- **2nd failure** → repeat the applicable fix responsibility with additional context: include the previous attempt's error AND the new error so the agent can see what didn't work
- **3rd failure** → **STOP.** Present the situation to the user with options:
  - What has been tried (all 3 attempts with errors)
  - Suggested alternatives (different approach, relax constraints, manual intervention)

**Counter rules:**

- Counter increments on **verification steps only** (lint run, sim run), NOT on fix attempts
- Each file×phase combination has its own counter (e.g., `lint:fifo.sv` is separate from `sim:fifo.sv`)
- Counter resets if the user provides new guidance

### Example Flow (NEW - with questions and planning)

```
User: "Create a FIFO and test it"

1. ASK: "What depth and width? Interface style? Self-checking TB?"
2. User answers: "8-deep, 32-bit, valid/ready, yes self-checking"
3. Run the `sv-planner` responsibility → creates implementation plan
4. Run the `sv-orchestrator` responsibility → decomposes + builds FIFO + TB in parallel
   (If user chose single-threaded: run the `sv-codegen` responsibility, then sv-testbench)
5. Run lint → 2 warnings
6. Run the `sv-refactor` responsibility → fixes warnings
7. Run lint → clean ✓
8. Run sim → test fails
9. Run the `sv-debug` responsibility → identifies issue
10. Run the `sv-refactor` responsibility → fixes RTL
11. Run sim → passes ✓
12. Report: "Created fifo.sv, tb_fifo.sv. All tests pass."
```

---

## Agent Routing

### When to Use Each Responsibility

| Responsibility | Use When | Context to Provide |
|-------|------------|-------------------|
| `sv-planner` | Complex/new architecture needing a plan; bounded edits bypass this reference | User requirements, constraints |
| `sv-orchestrator` | Authorized multi-component construction needing coordination | Plan output, component list, constraints |
| `sv-codegen` | Component-level generation (invoked by sv-orchestrator or single-threaded mode) | Component spec, interfaces |
| `sv-testbench` | Creating testbench, stimulus | DUT file, ports, test scenarios |
| `sv-debug` | **ANY** simulation failure | Error message, failing test, code |
| `sv-verification` | Adding assertions, coverage | Module, properties to check |
| `sv-understanding` | Explaining code, architecture | File paths, specific questions |
| `sv-refactor` | **ANY** code fix needed | Lint output, code to fix |
| `sv-developer` | Complex multi-file changes | Full context, multiple files |
| `sv-formal` | Formal verification requested | Module, properties to prove |
| `sv-synth` | Synthesis or resource estimation | Module, target FPGA |
| `sv-pinmap` | Pin assignment or constraint file | Board, RTL ports |
| `sv-ip-scanner` | Scan for missing IP or CDC issues | Project path |
| `vhdl-codegen` | VHDL module creation | Requirements, VHDL-2008 |
| `vhdl-testbench` | VHDL testbench creation | DUT file, test scenarios |
| `pcb-designer` | KiCad schematic/PCB design | Board description, components |

### Delegation Pattern - Inherit Session Model

```
Delegate to an available worker with the role `sv-orchestrator` (for creation tasks):
  prompt: |
    Build the design in parallel based on this plan:
    [Plan output or key excerpts]
    Requirements:
    - [answers to the clarification questions]
    Constraints:
    - [timing/area/power/verification]

If build mode is single-threaded:
Delegate to an available worker with the role `sv-codegen`:
  prompt: |
    Create the module described in the plan:
    [Plan output or key excerpts]
    Requirements:
    - [answers to the clarification questions]
```

---
