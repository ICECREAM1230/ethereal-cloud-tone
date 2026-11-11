# Clock and reset planning

Read only when clock/reset domains or reset sequencing are in scope. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### Clock Domain Planning

```markdown
## Clock Domains

### Clocks
| Clock | Frequency | Domain | Modules |
|-------|-----------|--------|---------|
| clk | 100 MHz | CORE | All except mem_if |
| clk_mem | 200 MHz | MEM | mem_if, async_fifo |

### Clock Domain Crossings
| Signal | From | To | Sync Method |
|--------|------|-----|-------------|
| cmd_valid/payload | CORE | MEM | Request/acknowledge handshake; synchronize only the single-bit control and hold the payload stable by protocol |
| data[31:0] | MEM | CORE | Async FIFO |

### CDC Diagram
​```mermaid
flowchart LR
    subgraph CORE["clk domain"]
        ctrl[Controller]
    end
    subgraph MEM["clk_mem domain"]
        mem[Memory IF]
    end
    ctrl -->|"req/ack handshake"| mem
    mem -->|"Async FIFO"| ctrl
​```
```

### Reset Strategy

```markdown
## Reset Strategy

| Reset | Type | Polarity | Scope |
|-------|------|----------|-------|
| rst_n | Async assert, sync deassert | Active-low | Control/state that requires a defined startup value |
| mem_rst_n | Async assert, sync deassert | Active-low | Memory-domain resettable state |

### Reset Synchronization
- rst_n synchronized to each clock domain
- 2FF synchronizer for async reset release
- Reset only state that needs a defined value; allow datapath registers/RAMs to initialize through valid/control logic when the architecture permits it

### Reset Sequence
1. Assert rst_n (asynchronous)
2. Hold for minimum 10 cycles
3. Deassert synchronously to clk
4. Wait for PLL lock before operation
```
