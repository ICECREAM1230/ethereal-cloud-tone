# RTL clarification examples

Read only when consequential requirements are missing. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Expand Mode Questions

### For Creation Requests

```
Ask the user:
  questions:
    - question: "What is the target width/depth/size?"
      header: "Size"
      options:
        - label: "Small (8-bit, shallow)"
          description: "Simple, minimal resources"
        - label: "Medium (32-bit, moderate)"
          description: "Balanced performance/area"
        - label: "Large (64-bit+, deep)"
          description: "High throughput"
        - label: "Parameterized"
          description: "Configurable at instantiation"
      multiSelect: false

    - question: "What interface style?"
      header: "Interface"
      options:
        - label: "Valid/Ready"
          description: "Standard handshake protocol"
        - label: "AXI-Stream"
          description: "ARM standard streaming"
        - label: "Simple enable"
          description: "Basic control signals"
        - label: "Custom"
          description: "Specify your own"
      multiSelect: false

    - question: "Include testbench?"
      header: "Testbench"
      options:
        - label: "Yes, self-checking"
          description: "Automated pass/fail verification"
        - label: "Yes, basic"
          description: "Stimulus only, manual checking"
        - label: "No"
          description: "RTL only"
      multiSelect: false

    - question: "Build mode?"
      header: "Build Mode"
      options:
        - label: "Parallel (default)"
          description: "Decompose and build components concurrently"
        - label: "Single-threaded"
          description: "Sequential build (no parallel agents)"
      multiSelect: false
```

### For Debug Requests

```
Ask the user:
  questions:
    - question: "What symptom are you seeing?"
      header: "Symptom"
      options:
        - label: "X-values in output"
          description: "Unknown/undefined signals"
        - label: "Wrong output value"
          description: "Defined but incorrect"
        - label: "Simulation hangs"
          description: "Never reaches $finish"
        - label: "Assertion failure"
          description: "SVA or immediate assert"
      multiSelect: false
```

### For Bug Reports (Test-First Flow)

```
Ask the user:
  questions:
    - question: "What is the expected behavior?"
      header: "Expected"
      options:
        - label: "I'll describe it"
          description: "Let me explain what should happen"
        - label: "Follow spec/docs"
          description: "Behavior defined in documentation"
        - label: "Match reference"
          description: "Should match another implementation"
      multiSelect: false

    - question: "Can you describe how to trigger the bug?"
      header: "Trigger"
      options:
        - label: "Specific sequence"
          description: "I know the exact steps"
        - label: "Certain input values"
          description: "Happens with specific data"
        - label: "Timing-dependent"
          description: "Race condition or edge case"
        - label: "Random/intermittent"
          description: "Hard to reproduce reliably"
      multiSelect: false
```

---
