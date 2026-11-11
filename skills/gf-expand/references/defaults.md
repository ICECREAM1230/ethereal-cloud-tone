# Quick-start examples

Read only when the user requests default suggestions; verify applicability before using example values. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Quick Start Defaults

| Scenario | Defaults |
|---|---|
| Creation | 32-bit, parameterized, valid/ready, self-checking TB, parallel build |
| Debug | Run existing TB, collect waveform, auto-diagnose |
| Formal | BMC depth 20, z3, safety properties auto-detected |
| Synthesis | Generic target, balanced optimization |
| Board | Auto-detect from project.yaml, fallback iCEBreaker |
| Protocol | Valid/Ready for single module, AXI-Stream for pipelines, AXI4-Lite for registers |

## Follow-Up Decision Trees

**Creation:** Single module + no bus -> sv-codegen. Single module + bus -> protocol choice. Multi-component -> gf-plan then gf-build.

**Debug:** X-values from start -> check resets. Wrong output consistent -> logic error. Simulation hangs -> infinite loop or deadlock.

**Formal:** Safety properties -> BMC then prove. Protocol compliance -> load SVA templates. Not sure -> auto-analyze design for FIFO/FSM/handshake patterns.
