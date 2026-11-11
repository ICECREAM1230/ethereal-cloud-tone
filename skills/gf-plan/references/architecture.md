# Architecture and interfaces

Read only when planning a multi-module architecture or bus interfaces. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Phase 3: Architecture Design

### Block Diagram (REQUIRED)

Include a block diagram when module relationships warrant one:

```markdown
## Block Diagram

​```mermaid
flowchart TB
    subgraph TOP[module_name]
        direction TB

        subgraph CTRL[Control Path]
            FSM[State Machine]
            REG[Config Registers]
        end

        subgraph DATA[Data Path]
            FIFO_IN[Input FIFO]
            PROC[Processing Unit]
            FIFO_OUT[Output FIFO]
        end

        FSM --> PROC
        REG --> FSM
    end

    EXT_IN[External Input] --> FIFO_IN
    FIFO_IN --> PROC
    PROC --> FIFO_OUT
    FIFO_OUT --> EXT_OUT[External Output]

    CPU[CPU/Host] <-->|AXI-Lite| REG
​```
```

### Module Hierarchy

```markdown
## Module Hierarchy

​```
dma_top                      # Top-level DMA controller
├── dma_reg_if              # AXI-Lite register interface
│   └── dma_reg_block       # Register storage
├── dma_engine              # Main DMA engine
│   ├── dma_descriptor      # Descriptor fetch/decode
│   ├── dma_channel[N]      # Per-channel logic
│   │   ├── dma_fsm         # Channel state machine
│   │   └── dma_counter     # Transfer counter
│   └── dma_arbiter         # Channel arbiter
└── dma_axi_master          # AXI master interface
​```
```

### Interface Design

**Standard Protocols:**

| Protocol | Use Case | Signals |
|----------|----------|---------|
| AXI4-Full | High-performance memory | 5 channels (AW, W, B, AR, R) |
| AXI4-Lite | Register access | Simplified 5 channels |
| AXI4-Stream | Streaming data | TVALID, TREADY, TDATA, TLAST |
| APB | Simple peripherals | PSEL, PENABLE, PWRITE, PADDR, PWDATA, PRDATA |
| Valid/Ready | Generic handshake | valid, ready, data |

**Interface Specification Template:**

```markdown
## Interfaces

### AXI-Lite Slave (Configuration)
| Signal | Dir | Width | Description |
|--------|-----|-------|-------------|
| s_axi_aclk | in | 1 | AXI clock |
| s_axi_aresetn | in | 1 | AXI reset (active-low) |
| s_axi_awaddr | in | 12 | Write address |
| s_axi_awvalid | in | 1 | Write address valid |
| s_axi_awready | out | 1 | Write address ready |
| ... | ... | ... | ... |

### AXI Master (Memory Access)
| Signal | Dir | Width | Description |
|--------|-----|-------|-------------|
| m_axi_* | ... | ... | Full AXI4 master |

### Interrupt
| Signal | Dir | Width | Description |
|--------|-----|-------|-------------|
| irq | out | 1 | Interrupt (level, active-high) |
```
