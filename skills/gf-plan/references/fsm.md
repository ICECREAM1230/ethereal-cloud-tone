# FSM planning example

Read only when a state-machine planning example is needed. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

This is a layout example, not a complete project specification. Apply it only to the FSM being planned, fill every applicable state/transition/output row from the current requirements, and omit unrelated DMA details. The embedded wording does not require a saved plan or code generation.

### FSM Design

For EVERY state machine, provide:

```markdown
## FSM: dma_channel_fsm

### States
| State | Encoding | Description |
|-------|----------|-------------|
| IDLE | 3'b000 | Waiting for start |
| FETCH_DESC | 3'b001 | Fetching descriptor |
| CALC_ADDR | 3'b010 | Calculate transfer address |
| XFER | 3'b011 | Performing transfer |
| UPDATE | 3'b100 | Update descriptor |
| DONE | 3'b101 | Transfer complete |
| ERROR | 3'b110 | Error state |

### State Diagram
​```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> FETCH_DESC: start && desc_avail
    FETCH_DESC --> CALC_ADDR: desc_valid
    FETCH_DESC --> ERROR: desc_error
    CALC_ADDR --> XFER: addr_ready
    XFER --> XFER: !xfer_done
    XFER --> UPDATE: xfer_done && !last
    XFER --> DONE: xfer_done && last
    UPDATE --> FETCH_DESC: update_done
    DONE --> IDLE: clear
    ERROR --> IDLE: clear
​```

### Transitions
| From | To | Condition | Actions |
|------|-----|-----------|---------|
| IDLE | FETCH_DESC | start && desc_avail | Load desc_ptr |
| FETCH_DESC | CALC_ADDR | desc_valid | Store descriptor |
| XFER | UPDATE | xfer_done && !last | Increment count |
| XFER | DONE | xfer_done && last | Assert irq |

### Outputs per State
| State | busy | xfer_en | irq | error |
|-------|------|---------|-----|-------|
| IDLE | 0 | 0 | 0 | 0 |
| FETCH_DESC | 1 | 0 | 0 | 0 |
| XFER | 1 | 1 | 0 | 0 |
| DONE | 0 | 0 | 1 | 0 |
| ERROR | 0 | 0 | 1 | 1 |
```
