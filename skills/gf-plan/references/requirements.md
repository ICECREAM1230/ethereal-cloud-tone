# Planning inputs

Read only when requirements or existing integration context need clarification. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Phase 1: Understanding Requirements

### Questions to Ask

**Interface Questions:**
- What bus protocol? (AXI4, AXI-Lite, AXI-Stream, APB, AHB, Wishbone, custom)
- What are the data widths? (8, 16, 32, 64 bits)
- How many channels/ports?
- What's the throughput requirement?

**Timing Questions:**
- Target clock frequency?
- Latency budget (cycles)?
- Single or multiple clock domains?

**Constraint Questions:**
- FPGA or ASIC target?
- Area constraints?
- Power considerations?

**Integration Questions:**
- Does this connect to existing modules?
- What interfaces already exist?
- Any existing packages/types to reuse?

### Requirement Parsing

Extract from user's request:
- **What** they want (functional requirements)
- **Why** they need it (context, use case)
- **Constraints** (performance, area, power)
- **Integration points** (existing code to connect to)

---

## Phase 2: Analyze Existing Codebase

**If user has existing code:**

1. Read the affected integration points and current source/configuration: interfaces, clocks/resets, relevant hierarchy and reusable packages. Stop discovery when these facts are sufficient for the requested design.
2. Reuse a relevant existing map if it is current. A missing map does not require creating one; current RTL remains authoritative.
3. Invoke `gf-architect` only when the user requested mapping or a specific unresolved structural dependency needs it. Limit that analysis to the needed area; save a map only when requested or needed for authorized implementation.
4. Record only the integration facts needed by the plan. The layout below is illustrative:
```markdown
## Existing Infrastructure

### Clock Domains
- clk_sys (100MHz) - main system clock
- clk_mem (200MHz) - memory interface

### Available Interfaces
- AXI-Lite slave port on soc_top
- Memory interface via mem_if

### Packages to Reuse
- common_pkg: data types, constants
- axi_pkg: AXI type definitions
```

---
