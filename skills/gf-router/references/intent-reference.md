# Intent routing details

Read only when intent classification or compound-request routing is genuinely ambiguous. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Core Responsibilities

1. **Classify Intent** - Determine what the user wants (semantic, not keyword-based)
2. **Assess Confidence** - Score how certain you are (0.0 - 1.0)
3. **Route Appropriately** - Based on confidence level
4. **Build Context** - Pass rich context to target
5. **Handle Returns** - Process completion status

---

## Classification Process

### Step 1: Analyze User Query Semantically

Consider:
- What is the user trying to accomplish? (their goal)
- Is this about creating, debugging, understanding, or verifying?
- Does this need coordination of independent components or a consequential system-wide design decision? Multiple local steps/files alone do not require orchestration.
- Are there actual additional requirements? "Change/create and test" can be a direct local edit followed by a focused check.

**DO NOT use keyword matching.** Focus on semantic meaning:
- "I need a state machine" → CREATE_RTL (even without "create" keyword)
- "This outputs garbage" → DEBUG (even without "debug" keyword)
- "Make this work" → DEBUG (implicit problem)
- "Can you help with the FIFO" → AMBIGUOUS (need more info)

### Step 2: Score Intents

Assign confidence scores (0.0 - 1.0) based on:
- How clearly the request maps to one intent
- Whether context resolves ambiguity
- Presence of implicit vs explicit requirements

### Step 3: Determine Routing Mode

```
if primary_confidence >= 0.85:
    mode = "direct"
    → handoff immediately to target

elif primary_confidence >= 0.70:
    mode = "expand"
    → ask 2-3 clarifying questions
    → present options with trade-offs
    → capture user preference
    → then handoff with enriched context

else:
    mode = "clarify"
    → ask user to rephrase or provide more detail
```

---

## Intent Categories

### Skill Intents (Synchronous, In-Context)
| Intent | Semantic Meaning | Target Skill |
|--------|------------------|--------------|
| ORCHESTRATE | Explicit complex development or independent component coordination | gf |
| LINT | Code quality check, static analysis | gf-lint |
| SIMULATE | Run simulation, check behavior | gf-sim |
| MAP | Codebase analysis, documentation | gf-architect |
| LEARN | Practice, exercises, learning | gf-learn |
| SUMMARIZE | Format/summarize output | gf-summary |

### Agent Intents (Heavy Lifting, Parallel)
| Intent | Semantic Meaning | Target Agent |
|--------|------------------|--------------|
| CREATE_RTL | Create new module/RTL code | sv-codegen |
| CREATE_TB | Create testbench/stimulus | sv-testbench |
| DEBUG | Diagnose failures, X-values, issues | sv-debug |
| BUG_REPORT | User reports specific bug behavior | gf (test-first flow) |
| VERIFY | Add assertions, coverage, properties | sv-verification |
| EXPLAIN | Understand existing code | sv-understanding |
| REFACTOR | Improve/fix/cleanup code | sv-refactor |
| DEVELOP | Complex multi-file changes | sv-developer |
| PLAN | Design/architect before coding | sv-planner |
| TUTOR | Learning review, hints, feedback | sv-tutor |
| FORMAL | Formal verification, prove properties | gf-formal |
| SYNTHESIZE | Synthesis, resource estimation | gf-synth |
| PIN_MAP | Board pinout, constraint generation | gf-pinmap |
| BOARD_QUERY | Board info, available pins | gf-boards |
| IP_ADD | Add IP block to project | gf-ip |
| PROTOCOL | Protocol interface scaffold | gf-protocols |
| VHDL_CREATE | Create VHDL module | vhdl-codegen |
| VHDL_TB | Create VHDL testbench | vhdl-testbench |
| IP_DETECT | Scan for missing IP, find gaps | gf-ip-detect |
| IP_AUTOFILL | Detect and implement missing modules | gf-ip-detect (auto-fill) |
| CDC_SCAN | Find clock domain crossing issues | gf-ip-detect (cdc-only) |

### Meta Intents
| Intent | Meaning |
|--------|---------|
| AMBIGUOUS | Could map to multiple intents, expand mode |
| OUT_OF_SCOPE | Not GateFlow-related |

---

## Quick Reference

| Confidence | Mode | Action |
|------------|------|--------|
| >= 0.85 | direct | Handoff immediately |
| 0.70-0.85 | expand | Questions → Options → Handoff |
| < 0.70 | clarify | Ask user to rephrase |

| Request Pattern | Intent | Target |
|-----------------|--------|--------|
| Create/build/generate/need X | CREATE_RTL | sv-codegen |
| Create X and test it | ORCHESTRATE | gf |
| Write testbench/TB for | CREATE_TB | sv-testbench |
| Bug:/bug where/there's a bug | BUG_REPORT | gf (test-first) |
| Why is/debug/fix/broken | DEBUG | sv-debug |
| Add assertions/coverage | VERIFY | sv-verification |
| Explain/what does/how | EXPLAIN | sv-understanding |
| Refactor/clean up/lint | REFACTOR | sv-refactor |
| Plan/design/architect | PLAN | sv-planner |
| Map/analyze codebase | MAP | gf-architect |
| Lint/check quality | LINT | gf-lint |
| Simulate/run/test | SIMULATE | gf-sim |
| Learn/practice/exercise | LEARN | gf-learn |

### BUG_REPORT vs DEBUG

**BUG_REPORT** (test-first flow):
- User describes specific incorrect behavior with trigger
- "Bug: X happens when Y"
- "There's a bug where..."
- User knows what's wrong and can describe it

**DEBUG** (diagnosis flow):
- User doesn't know what's wrong
- "Why is my output X?"
- "Simulation is stuck"
- Needs investigation to find the issue

---

## Important Rules

1. **NEVER use keyword matching** - Focus on semantic meaning
2. Answer clear small tasks directly; route when useful
3. **ALWAYS build context** before handoff
4. Use expand mode when consequential requirements remain unclear
5. **Track handoff chains** to prevent circular routing

---

## Context-Dependent Routing

| Prior State | Query Pattern | Boost | Target |
|---|---|---|---|
| Code just created | "test/run/try" | +0.20 SIMULATE | gf-sim |
| Code just created | "check/lint" | +0.20 LINT | gf-lint |
| Sim just failed | "fix/debug/why" | +0.20 DEBUG | sv-debug |
| Lint just failed | "fix/clean" | +0.20 REFACTOR | sv-refactor |
| Plan just created | "build/go/do it" | +0.20 ORCHESTRATE | gf |
| Learning active | any task query | +0.15 TUTOR | sv-tutor |

## Multi-Intent Detection

| Query | Intents | Route To |
|---|---|---|
| "Create a FIFO and formally verify it" | CREATE + FORMAL | gf (orchestrate) |
| "Build and test a counter" | CREATE + SIMULATE | gf direct path; a bounded existing-counter edit uses local edits |
| "Explain the FSM then add assertions" | EXPLAIN + VERIFY | sv-understanding then sv-verification |
| "Fix lint and run simulation" | REFACTOR + SIMULATE | sv-refactor then gf-sim |
| "Create UART and SPI master" | CREATE + CREATE | gf-build (multi-component) |

Route by actual dependencies and the requested evidence. Conjunctions and CREATE + VERIFY/SIMULATE do not require orchestration. A local patch stays on the local-edit path with its selected checks; explicit formal work loads only the relevant formal branch.

## Confidence Calibration

Lower threshold to 0.75 when: user is new (first 3 sessions), destructive action, multiple valid architectures.
Raise threshold to 0.90+ when: user names exact command, references file paths, repeats previous action.

Adaptive formula: `effective = 0.85 + new_user(-0.10) + destructive(-0.10) + context(+0.05) + specificity(+0.05)`. Range [0.65, 0.95].

If top two intents within 0.10 of each other: force expand mode regardless.
