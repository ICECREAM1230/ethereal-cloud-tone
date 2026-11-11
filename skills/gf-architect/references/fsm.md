# FSM map schema

Read only when extracting or documenting a state machine. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### fsm.md

```markdown
# State Machines

## FSM: tx_state in uart_tx

**States:** IDLE, START, DATA, STOP
**Encoding:** 2-bit

\`\`\`mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> START: tx_valid
    START --> DATA: 1 cycle
    DATA --> DATA: bit_cnt < 7
    DATA --> STOP: bit_cnt == 7
    STOP --> IDLE: 1 cycle
\`\`\`

**Transitions:**
| From | To | Condition |
|------|-----|-----------|
| IDLE | START | tx_valid |
| START | DATA | always |
| DATA | STOP | bit_cnt == 7 |
```
