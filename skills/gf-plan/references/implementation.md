# Implementation planning

Read only when a multi-file implementation plan or dependency graph is needed. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Phase 5: Implementation Plan

### File List

```markdown
## Files to Create

| File | Type | Agent | Phase | Description |
|------|------|-------|-------|-------------|
| rtl/dma_pkg.sv | Package | sv-codegen | 1 | Types, constants |
| rtl/dma_reg_if.sv | Module | sv-codegen | 1 | Register interface |
| rtl/dma_channel.sv | Module | sv-codegen | 2 | Single channel |
| rtl/dma_arbiter.sv | Module | sv-codegen | 2 | Channel arbiter |
| rtl/dma_engine.sv | Module | sv-codegen | 3 | Main engine |
| rtl/dma_axi_master.sv | Module | sv-codegen | 3 | AXI master |
| rtl/dma_top.sv | Module | sv-codegen | 4 | Top-level |
| tb/tb_dma_channel.sv | TB | sv-testbench | 2 | Channel TB |
| tb/tb_dma_top.sv | TB | sv-testbench | 4 | System TB |
| rtl/dma_sva.sv | SVA | sv-verification | 4 | Assertions |

## Files to Modify

| File | Change | Agent | Phase |
|------|--------|-------|-------|
| rtl/soc_top.sv | Add DMA instance | sv-codegen | 5 |
| rtl/soc_pkg.sv | Add DMA types | sv-codegen | 1 |
```

### Implementation Phases

```markdown
## Implementation Phases

### Phase 1: Foundation
**Goal:** Package and register interface
**Files:** dma_pkg.sv, dma_reg_if.sv
**Verification:** Lint clean, basic reg read/write test
**Agent:** sv-codegen → lint → sv-testbench

### Phase 2: Core Logic
**Goal:** Single channel working
**Files:** dma_channel.sv, dma_arbiter.sv
**Verification:** Channel testbench, FSM coverage
**Agent:** sv-codegen → lint → sv-testbench → sim

### Phase 3: Bus Interface
**Goal:** AXI master integration
**Files:** dma_engine.sv, dma_axi_master.sv
**Verification:** AXI protocol checks
**Agent:** sv-codegen → lint → sv-verification (protocol assertions)

### Phase 4: Integration
**Goal:** Complete DMA controller
**Files:** dma_top.sv, tb_dma_top.sv, dma_sva.sv
**Verification:** Full system test, assertion coverage
**Agent:** sv-codegen → sv-testbench → sv-verification → sim

### Phase 5: System Integration
**Goal:** DMA in SoC
**Files:** soc_top.sv (modify)
**Verification:** System-level test
**Agent:** sv-developer
```

### Dependencies

```markdown
## Dependencies

​```mermaid
flowchart TD
    PKG[dma_pkg.sv] --> REG[dma_reg_if.sv]
    PKG --> CH[dma_channel.sv]
    PKG --> ARB[dma_arbiter.sv]
    PKG --> AXI[dma_axi_master.sv]

    CH --> ENG[dma_engine.sv]
    ARB --> ENG
    AXI --> ENG

    REG --> TOP[dma_top.sv]
    ENG --> TOP

    TOP --> SOC[soc_top.sv]
​```

**Build Order:**
1. dma_pkg.sv (no deps)
2. dma_reg_if.sv, dma_channel.sv, dma_arbiter.sv, dma_axi_master.sv (parallel)
3. dma_engine.sv
4. dma_top.sv
5. soc_top.sv integration
```
