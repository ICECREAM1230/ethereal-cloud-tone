# Focused question examples

Read only when the consequential missing information is not available from context. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Workflow

### Step 1: Acknowledge and Frame

```
I'd like to help you with [brief summary of what you understood].
Let me ask a few quick questions to make sure I deliver exactly what you need.
```

### Step 2: Ask Clarifying Questions (2-3 max)

Ask the user targeted questions based on detected intent:

#### For Ambiguous Creation vs Debug:
```
questions:
  - question: "Are you creating something new or working with existing code?"
    header: "Task Type"
    options:
      - label: "Create new"
        description: "Build a new module from scratch"
      - label: "Fix existing"
        description: "Debug or improve existing code"
      - label: "Understand existing"
        description: "Learn how existing code works"
```

#### For Creation Tasks:
```
questions:
  - question: "What interface protocol should this use?"
    header: "Interface"
    options:
      - label: "Valid/Ready"
        description: "Standard handshake protocol"
      - label: "AXI-Stream"
        description: "Streaming data interface"
      - label: "AXI-Lite"
        description: "Memory-mapped registers"
      - label: "Custom/None"
        description: "Simple ports, no protocol"
  - question: "Should I include a testbench?"
    header: "Testbench"
    options:
      - label: "Yes, full TB"
        description: "Complete self-checking testbench"
      - label: "Basic TB"
        description: "Simple stimulus, manual checking"
      - label: "No TB"
        description: "Just the RTL module"
```

#### For Debug Tasks:
```
questions:
  - question: "What behavior are you seeing?"
    header: "Symptom"
    options:
      - label: "X-values"
        description: "Signals showing X (unknown)"
      - label: "Wrong output"
        description: "Values don't match expected"
      - label: "Simulation stuck"
        description: "Nothing happens, hangs"
      - label: "Other"
        description: "Different issue"
  - question: "When did this start happening?"
    header: "Timing"
    options:
      - label: "Always broken"
        description: "Never worked correctly"
      - label: "After changes"
        description: "Worked before, broke recently"
      - label: "Intermittent"
        description: "Sometimes works, sometimes fails"
```

#### For Planning Tasks:
```
questions:
  - question: "What level of design detail do you need?"
    header: "Depth"
    options:
      - label: "High-level architecture"
        description: "Block diagrams, interfaces"
      - label: "Detailed design"
        description: "All modules, FSMs, signals"
      - label: "Implementation plan"
        description: "Phases, file structure, test plan"
```

## Question Templates by Scenario

### "Help me with X" (ambiguous)
1. What do you want to do with X? (create/fix/understand)
2. [Based on answer, ask relevant follow-up]

### "Create a [component]" (needs specs)
1. What interface protocol?
2. Key parameters? (width, depth, etc.)
3. Include testbench?

### "Fix this" (needs diagnosis)
1. What's the symptom?
2. What did you expect?
3. Any recent changes?

### "Work on [project]" (scope unclear)
1. Which part specifically?
2. What's the goal? (new feature, bug fix, cleanup)

## Important Rules

1. **Max 3 questions** - Don't overwhelm user
2. **Provide sensible defaults** - "Quick Start" option always available
3. **Be specific** - "What interface?" not "Tell me more"
4. **Remember answers** - Build comprehensive context
5. **Handoff with full context** - Target should have everything needed

---

## Additional Scenario Templates

### Formal Verification
- "What properties to verify?" -> Safety / Protocol compliance / Functional correctness / Suggest for me (multiSelect)
- "Proof depth?" -> Quick check (BMC 20 cycles) / Full proof (unbounded) / Cover + Prove

### Synthesis
- "Target FPGA?" -> iCE40 / ECP5 / Gowin / Artix-7 / Generic estimate
- "Optimization goal?" -> Minimum area / Maximum frequency / Low power / Balanced

### Board Targeting
- "Which board?" -> iCEBreaker / Tang Nano 9K / Arty A7 / Other
- "Peripherals needed?" -> LEDs+buttons / UART / SPI+I2C / HDMI+VGA (multiSelect)

### Protocol Choice
- "Protocol?" -> AXI4-Lite / AXI-Stream / Wishbone / Valid/Ready / Help me choose
- "Data flow?" -> Register read/write / Streaming / Burst transfers / Request/response
