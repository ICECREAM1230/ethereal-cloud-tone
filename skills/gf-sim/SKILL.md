---
name: gf-sim
description: >
  SystemVerilog simulator with structured output for orchestration.
  Resolves DUT/testbench and actual models, prefers Verilator from the
  installed OSS CAD Suite, and reports checked functional results. For Gowin
  projects, verifies model compatibility; ModelSim/Questa is secondary for
  required protected/unsupported dependencies or verified timing simulation.
---

# GF Sim

Read [the shared execution contract](../gf/references/codex-runtime.md) once for the task. Resolve the requested source set and testbench top from actual project/build evidence.

Prefer Verilator for functional simulation. On a Linux host configured as described in the collection README, invoke it through `"${OSS_CAD}" verilator <arguments>` as described in [Linux simulation](references/simulation-linux.md); the suite is activated only for that child process. ModelSim/Questa is secondary; discover its actual availability on the selected host. Choose the actual source/model boundary before tool discovery:

- Portable/module RTL: use [the shared simulation workflow](references/simulation-workflow.md). For actual Linux calls read [Linux simulation](references/simulation-linux.md); for Windows calls read [Windows host calls](../gf/references/host-windows.md) only on that target.
- Gowin vendor-model functional simulation: use [the vendor contract](../gf/references/gowin-vendor-flow.md), inspect exact generated models/libraries and use Verilator when their required behavior is compatible. A `.gprj` or vendor primitive alone does not force the secondary simulator.
- Protected/unsupported required models or requested SDF timing simulation: verify the secondary ModelSim/Questa flow and its prerequisites; preserve protected files and report actual gaps.

If simulation was requested and writes are authorized, prepare a reviewable manifest and the selected simulator command (`.do` only for ModelSim/Questa) when inputs suffice. An unavailable required simulator, adapter, library, model or license makes execution BLOCKED; preparation is not PASS. Missing ModelSim alone does not block a compatible Verilator test. If writing is outside scope, describe the missing inputs without creating files.

Exit zero or `$finish` alone proves completion. Functional PASS requires expected-value checks and evidence that all intended checks ran; retain input/model freshness and actual logs. Choose the host automatically, reuse confirmed tools and do not install software merely because lookup on PATH failed.
