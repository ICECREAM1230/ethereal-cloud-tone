# Detailed specification examples

Read only when specifying ports, registers, transaction timing or descriptor formats. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Phase 4: Detailed Specification

### Port Specification

```markdown
## Module: dma_top

### Parameters
| Name | Type | Default | Description |
|------|------|---------|-------------|
| NUM_CHANNELS | int | 4 | Number of DMA channels |
| DATA_WIDTH | int | 32 | Data bus width |
| ADDR_WIDTH | int | 32 | Address width |
| DESC_DEPTH | int | 16 | Descriptor FIFO depth |

### Ports
| Name | Dir | Width | Description |
|------|-----|-------|-------------|
| clk | in | 1 | System clock |
| rst_n | in | 1 | Active-low async reset |
| s_axi_* | in/out | - | AXI-Lite slave (config) |
| m_axi_* | in/out | - | AXI master (memory) |
| irq | out | NUM_CHANNELS | Per-channel interrupt |
```

### Register Map (if applicable)

```markdown
## Register Map

Base Address: 0x0000

| Offset | Name | Access | Reset | Description |
|--------|------|--------|-------|-------------|
| 0x00 | CTRL | RW | 0x0 | Control register |
| 0x04 | STATUS | RO | 0x0 | Status register |
| 0x08 | IRQ_EN | RW | 0x0 | Interrupt enable |
| 0x0C | IRQ_STATUS | RW1C | 0x0 | Interrupt status |
| 0x10 | DESC_PTR | RW | 0x0 | Descriptor pointer |

### CTRL Register (0x00)
| Bits | Name | Access | Reset | Description |
|------|------|--------|-------|-------------|
| 0 | EN | RW | 0 | DMA enable |
| 1 | START | RW | 0 | Start transfer (auto-clear) |
| 7:4 | CH_SEL | RW | 0 | Channel select |
| 31:8 | RSVD | RO | 0 | Reserved |
```

### Timing Diagrams

```markdown
## Timing: Write Transaction

​```wavedrom
{ signal: [
  { name: 'clk',     wave: 'P........' },
  { name: 'valid',   wave: '0.1....0.' },
  { name: 'ready',   wave: '0..1.0.1.' },
  { name: 'data',    wave: 'x.3....x.', data: ['D0'] },
  { name: 'transfer',wave: '0...1..0.' }
]}
​```

**Rules:**
- Data stable while valid high
- Transfer occurs when valid AND ready
- Producer holds valid until ready
```

### Protocol Specification

```markdown
## Protocol: Descriptor Format

### Descriptor Word 0 (Control)
| Bits | Field | Description |
|------|-------|-------------|
| 0 | VALID | Descriptor valid |
| 1 | LAST | Last descriptor in chain |
| 2 | IRQ_EN | Generate interrupt on complete |
| 15:8 | BURST_LEN | Burst length (0 = 1 beat) |
| 31:16 | RSVD | Reserved |

### Descriptor Word 1 (Source Address)
| Bits | Field | Description |
|------|-------|-------------|
| 31:0 | SRC_ADDR | Source address |

### Descriptor Word 2 (Destination Address)
| Bits | Field | Description |
|------|-------|-------------|
| 31:0 | DST_ADDR | Destination address |

### Descriptor Word 3 (Next Pointer)
| Bits | Field | Description |
|------|-------|-------------|
| 31:0 | NEXT_PTR | Next descriptor address (0 = end) |
```

---
