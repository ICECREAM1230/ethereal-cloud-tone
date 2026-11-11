---
name: fpga
description: Use for Gowin FPGA development involving SystemVerilog/RTL, CST/SDC constraints, timing closure, IP integration, resource optimization, or hardware debugging.
---

# Gowin FPGA Development

Use the selected Gowin `.gprj` for the exact device/package, enabled sources, IP and constraints. Match advice to that device and the installed Gowin Software version; do not assume a device family or copy another project's parameters.

For project/IP navigation or official implementation, use [gowin-project-navigation](../gowin-project-navigation/SKILL.md) and read only the reference matching the task. Keep bounded RTL edits scoped to the affected logic and relevant checks.

## Tool entry points on a configured Linux host

- Invoke OSS CAD Suite tools through `"${OSS_CAD}" <tool> <arguments>`, which loads the tool environment on demand.
- Invoke the Gowin CLI through `"${GOWIN_SH}" <path-to-Tcl-script>`; use the existing Gowin launcher for the GUI.
- Keep Codex/VS Code in the normal system environment; do not modify global PATH, LD_LIBRARY_PATH or shell startup to call these tools. For another host or a user-selected installation, resolve its actual launcher. The selected `.gprj` remains authoritative for Gowin implementation inputs and device settings.
- The OSS launcher follows `/opt/eda/oss-cad-suite/current`; record the resolved installation and actual tool version for reproducible results. Separate agent calls each use the launcher rather than relying on activation in a previous shell.

## Modular Design & Code Organization

- Structure designs into small, reusable modules to enhance readability and testability
- Start with a top-level design module and gradually break it down into sub-modules
- Use SystemVerilog constructs supported by the selected Gowin synthesis flow and simulation frontend
- Maintain consistent naming conventions across modules

## Synchronous Design Principles

- Prioritize single clock domains to simplify timing analysis
- Match reset polarity and timing to the design and Gowin primitive/IP requirements; synchronize asynchronous reset release in the affected clock domains where required
- Avoid timing hazards during synthesis
- Use proper clock domain crossing (CDC) techniques when multiple clocks are required

## Timing Closure & Constraints

- Use the enabled [CST physical constraints](../gowin-project-navigation/references/cst-physical-constraints.md) for pins and electrical settings, and [SDC timing constraints](../gowin-project-navigation/references/sdc-timing-constraints.md) for clocks, I/O timing and justified exceptions
- Match pins, bank voltages and I/O standards to the actual device/package and board connections
- Review Gowin clock/constraint coverage, setup and hold results, and critical paths in current official timing reports
- Address violations by adding pipeline stages or optimizing logic
- Justify false paths and multicycle paths from the actual transfer/clock relationship; check setup and hold together rather than hiding unexplained violations
- Report synthesis, place-and-route, bitstream generation and timing closure separately; a generated bitstream alone does not establish timing closure

## Resource Utilization & Optimization

- Optimize LUTs, flip-flops, and block RAM through efficient SystemVerilog
- Use Gowin IP and primitives supported by the selected device and installed version; preserve generated/protected files and verify configuration through [official IP generation](../gowin-project-navigation/references/gowin-ip-generation.md)
- Select synthesis and implementation options from the installed Gowin help/manual and actual project settings
- Choose inferred or generated RAM/DSP structures to match required ports, widths, latency, reset and initialization; check their actual mapping in Gowin resource reports
- Balance area vs. speed optimization based on requirements

## Power Optimization

- Prefer clock enables and reduced switching activity for inactive logic
- Use dedicated Gowin clock-control resources when the selected device and design require clock gating; avoid ordinary logic gates in clock paths
- Use power analysis or optimization capabilities confirmed for the installed Gowin version; record device, clock, activity and operating assumptions for any estimate
- Minimize switching activity in non-critical paths

## Debugging & Simulation

- Write detailed, self-checking testbenches covering typical use cases and edge cases
- Prefer OSS CAD Suite Verilator for portable RTL and compatible Gowin functional models; use assertion syntax supported by the selected frontend
- Verify actual vendor-model compatibility and preserve protected models. Requested timing simulation requires the matched netlist, SDF, libraries and a verified simulator flow
- For authorized board debugging, use Gowin GAO/GVIO capabilities supported by the installed version; follow [Gowin debug tool interfaces](../gowin-project-navigation/references/gowin-tool-automation.md) and verify probes, sample clocks and resource/timing impact
- Keep module simulation, official Gowin implementation and board observations as separate evidence. Bitstream generation does not authorize programming hardware

### Gowin Code With Icarus Verilog

- If a quick Icarus syntax/wiring check is appropriate, invoke `"${OSS_CAD}" iverilog <arguments>` on this host and resolve the required Gowin primitive/IP models first
- Small contract-compatible stubs may isolate edited connectivity for an explicitly scoped smoke check. Their result does not verify the original IP's function, model compatibility or full integration; retain those limits and use the actual Gowin flow for official implementation evidence

## Advanced Techniques

### Clock Domain Crossing
- Use synchronizers or FIFOs to handle CDC safely
- Implement proper handshaking protocols

### AXI Protocol Compliance
- Ensure proper read/write channel management and handshakes
- Optimize for high-throughput with proper burst sizing

### DMA Integration
- Configure burst transfers for maximum throughput
- Handle buffer management efficiently

### Latency Reduction
- Implement fine-tuned pipeline stages strategically
- Balance latency vs. throughput requirements
