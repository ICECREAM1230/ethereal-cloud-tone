# Pipeline planning

Read only when designing pipeline stages or backpressure. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### Pipeline Design

```markdown
## Pipeline: data_processor

### Pipeline Stages
| Stage | Latency | Function | Inputs | Outputs |
|-------|---------|----------|--------|---------|
| S0 | 1 | Input register | data_in | data_s0 |
| S1 | 1 | Transform | data_s0 | data_s1 |
| S2 | 1 | Output register | data_s1 | data_out |

### Pipeline Diagram
​```mermaid
flowchart LR
    subgraph S0[Stage 0]
        R0[Input Reg]
    end
    subgraph S1[Stage 1]
        ALU[Transform]
    end
    subgraph S2[Stage 2]
        R2[Output Reg]
    end

    IN[data_in] --> R0
    R0 --> ALU
    ALU --> R2
    R2 --> OUT[data_out]

    V0[valid_s0] --> V1[valid_s1] --> V2[valid_out]
    R2 -.->|ready| ALU -.->|ready| R0
​```

### Backpressure Handling
- Valid propagates forward
- Ready propagates backward
- Skid buffer at output for timing
```

---
