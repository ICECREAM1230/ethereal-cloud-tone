# Classification examples

Read only when additional intent examples are needed. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Few-Shot Classification Examples

### Clear RTL Creation (confidence: 0.95)
**Query:** "I need a 4-stage pipeline register with valid/ready"
**Intent:** CREATE_RTL
**Reasoning:** User explicitly requests creation of specific RTL component

### Debug Request (confidence: 0.92)
**Query:** "My simulation is stuck, nothing happens after reset"
**Intent:** DEBUG
**Reasoning:** Describes failure symptom, needs diagnosis

### Bug Report (confidence: 0.95)
**Query:** "Bug: output goes X when valid deasserts early"
**Intent:** BUG_REPORT
**Reasoning:** User reports specific reproducible bug with trigger condition. Use test-first flow.

### Bug Report variant (confidence: 0.90)
**Query:** "There's a bug where the counter wraps incorrectly at 255"
**Intent:** BUG_REPORT
**Reasoning:** Describes specific incorrect behavior. Write test first, then fix.

### End-to-End Request (confidence: 0.90)
**Query:** "Create a FIFO and make sure it works"
**Intent:** ORCHESTRATE
**Reasoning:** Wants both creation AND verification

### Planning Request (confidence: 0.88)
**Query:** "How should I design a DMA controller?"
**Intent:** PLAN
**Reasoning:** Asks "how should I design" - seeking architecture guidance

### Understanding Request (confidence: 0.93)
**Query:** "What does the state machine in uart_tx.sv do?"
**Intent:** EXPLAIN
**Reasoning:** Asks "what does X do" about existing code

### Ambiguous Request (confidence: 0.45)
**Query:** "Help me with the FIFO"
**Intent:** AMBIGUOUS
**Reasoning:** Could be create, fix, understand, or debug - need clarification

### Verification Request (confidence: 0.91)
**Query:** "Add assertions to verify the AXI protocol"
**Intent:** VERIFY
**Reasoning:** Explicitly requests assertions for protocol verification

### Refactor Request (confidence: 0.88)
**Query:** "This code has too many lint warnings, clean it up"
**Intent:** REFACTOR
**Reasoning:** Wants code cleaned up due to lint issues

### Learning Request (confidence: 0.94)
**Query:** "I want to practice writing FSMs"
**Intent:** LEARN
**Reasoning:** Explicitly wants to practice/learn

---
