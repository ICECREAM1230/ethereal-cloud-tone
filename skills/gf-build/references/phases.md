# Build phases

Read only when coordinating multiple dependent components. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Workflow

### Step 1: Decompose

Analyze the request and identify:

```markdown
## Design Decomposition: [Name]

### Shared Resources (Phase 0)
| File | Purpose |
|------|---------|
| pkg.sv | Common types, opcodes |

### Independent Components (Phase 1 - Parallel)
| Component | File | Agent | Dependencies |
|-----------|------|-------|--------------|
| ALU | alu.sv | sv-codegen | pkg |
| RegFile | regfile.sv | sv-codegen | pkg |
| ImmGen | imm_gen.sv | sv-codegen | pkg |

### Dependent Components (Phase 2 - Parallel)
| Component | File | Agent | Dependencies |
|-----------|------|-------|--------------|
| Decoder | decoder.sv | sv-codegen | pkg |
| Control | control.sv | sv-codegen | pkg, decoder |

### Integration (Phase 3)
| File | Purpose |
|------|---------|
| top.sv | Connects all components |

### Verification (Phase 4 - Parallel)
| Testbench | Tests |
|-----------|-------|
| tb_alu.sv | ALU operations |
| tb_top.sv | Integration |
```

### Step 2: Use existing authorization

Proceed with requested implementation. Ask only about unresolved design choices; do not ask again to approve already requested work.

### Single-Module Requests

If the design decomposes to a single module:
- Treat it as Phase 1 with one component
- Implement the single module directly
- Continue with lint/testbench/sim as usual

### Step 3: Execute Phases

#### Phase 0: Setup
```bash
mkdir -p rtl tb
```
Create the shared package directly, or assign one authorized worker.

#### Phase 1: Parallel Component Build

Dispatch all independent Phase 1 work items in parallel when the host supports delegation.

```
<Work item 1>
worker_role: sv-codegen
prompt: |
  ## Component: ALU
  [full spec...]
  Write to: rtl/alu.sv
</Work item 1>

<Work item 2>
worker_role: sv-codegen
prompt: |
  ## Component: Register File
  [full spec...]
  Write to: rtl/regfile.sv
</Work item 2>

<Work item 3>
worker_role: sv-codegen
prompt: |
  ## Component: Immediate Generator
  [full spec...]
  Write to: rtl/imm_gen.sv
</Work item 3>
```

#### Phase 2: Parallel Lint

Run lint on all components:
```
Run the named skill: `gf-lint`
args: rtl/alu.sv rtl/regfile.sv rtl/imm_gen.sv
```

Parse results. For any failures, run the `sv-refactor` responsibilitys in parallel.

#### Phase 3: Integration

Either:
- Run the `sv-codegen` responsibility for top-level with component interfaces
- Or write directly if straightforward

#### Phase 4: Parallel Testbenches

Spawn testbench agents in parallel:
```
<Task 1> sv-testbench for ALU
<Task 2> sv-testbench for RegFile
<Task 3> sv-testbench for top
```

#### Phase 5: Simulation

Run simulations (can be parallel):
```
Run the named skill: `gf-sim` with `tb/tb_alu.sv rtl/alu.sv`
Run the named skill: `gf-sim` with `tb/tb_top.sv rtl/*.sv`
```

### Step 4: Report

```markdown
## Build Complete

### Files Created
| Phase | File | Status |
|-------|------|--------|
| 0 | rtl/pkg.sv | ✓ |
| 1 | rtl/alu.sv | ✓ lint-clean |
| 1 | rtl/regfile.sv | ✓ lint-clean |
| 1 | rtl/imm_gen.sv | ✓ lint-clean |
| 3 | rtl/cpu.sv | ✓ lint-clean |
| 4 | tb/tb_cpu.sv | ✓ sim-pass |

### Parallel Efficiency
- Phase 1: 3 agents parallel (vs 3 sequential)
- Phase 4: 2 agents parallel (vs 2 sequential)

### Next Steps
- Run full simulation: `verilator --binary rtl/*.sv tb/tb_cpu.sv`
- Add assertions: ask `$gf` to add assertions to `cpu.sv`
```

---
