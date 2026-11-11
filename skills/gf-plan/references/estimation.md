# Estimates and risks

Read only when area, power, latency or explicit risk estimates are requested. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Power Estimation

| Signal Type | Typical Activity Factor |
|---|---|
| Clock | 1.0 |
| Data bus (random) | 0.5 |
| Address bus (sequential) | 0.1-0.3 |
| Control signals (FSM) | 0.05-0.2 |
| Enable/valid | 0.1-0.5 |
| Reset | ~0.0 |

Clock gating candidates: idle modules (30-60% savings), conditionally active blocks (20-40%), low-duty FSMs (10-30%).

Rules of thumb (FPGA, 28nm): LUT ~10uW at 100MHz alpha=0.5, FF ~5uW, BRAM ~1-3mW/block active, DSP ~5-10mW active, I/O ~1-5mW/pin.

## Area Estimation

| Construct | LUTs | FFs | Notes |
|---|---|---|---|
| N-bit register | 0 | N | |
| N-bit adder | N/2 | 0 | Carry chain |
| N-bit counter | N/2 | N | Adder + register |
| N-bit 4:1 MUX | N | 0 | 1 LUT per bit |
| NxM multiplier | 0 | 0 | Uses DSP if >18 bits |

Common blocks: UART TX ~40 LUTs/25 FFs, SPI Master ~60/40, Sync FIFO (32x8) ~15/25, AXI-Lite (8 reg) ~100/350.

## Latency Budget

| Rule | Budget |
|---|---|
| Input/output boundaries | 1 cycle each |
| BRAM read | 1-2 cycles |
| Multiply (DSP) | 1 cycle |
| CDC crossing | Derive from the selected handshake/FIFO/synchronizer and clock relationship |
| Deep mux (>6 levels) | +1 cycle per 6 |

## Risk Assessment Template

| Risk | Category | Probability | Impact | Mitigation |
|---|---|---|---|---|
| Timing closure failure | Timing | Medium | High | Pipeline early, budget 20% slack |
| Area exceeds device | Area | Low | High | Estimate early, track per module |
| CDC metastability/data incoherence | Functional | Medium | Critical | Choose CDC by signal type: single-bit level synchronizer, pulse/toggle handshake, Gray counter, or async FIFO; run CDC checks |
| Verification incomplete | Schedule | High | Medium | Define coverage goals early |

Severity: High probability + High impact = Critical. Low + Low = Low.

---

## Host capabilities

Use the filesystem search, file-reading, editing, shell, and collaboration capabilities actually exposed by the current Codex session. Do not assume named host tools from another agent runtime.
- **Delegation**: Use the available worker mechanism for codebase mapping when useful
- **User clarification**: Ask focused questions when requirements are materially unclear
- **Skill invocation**: Invoke `gf-architect` or hand off to the GateFlow execution orchestrator

---

*Remember: A good plan prevents rework. Hardware bugs are expensive. Plan thoroughly, implement confidently.*
