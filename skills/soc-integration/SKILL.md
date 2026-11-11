---
name: soc-integration
description: >
  SoC IP integration — IP procurement and qualification, IP configuration,
  bus fabric setup, top-level RTL integration, and chip-level simulation.
  Use when assembling a SoC from multiple IP blocks, configuring an AXI
  bus interconnect, integrating memory macros, or running chip-level tests.
license: MIT
metadata:
  author: chuanseng-ng
  upstream-version: "1.0.0"
  upstream-commit: "441cad1ffb4a6c980b82a55374a4d7222a237192"
  local-adaptation: "codex-gowin-ae350-20261028"
---

# Skill: SoC IP Integration

## Invocation

This is a standalone Codex installation. Execute the relevant stages below in
the current session; no `digital-chip-design-agents:*` registered agent is
required. Delegate only when useful and permitted by the current task and host.
For a targeted question or bounded change, use only the applicable stages.

For Gowin / AE350 work, first read
[references/gowin-ae350.md](references/gowin-ae350.md). Its platform-specific
rules qualify the generic ASIC, AXI and Arm examples below. Use the existing
Gowin skills for vendor project, IP, synthesis and timing operations.

The original skill and orchestrator are preserved in `references/upstream/`
for provenance, not as additional execution entrypoints. Host, user and project
instructions take precedence over their generic workflow requirements.

## Pre-run Context

Read the current project instructions, configuration and relevant `.ai/` notes.
Use the actual project inputs to establish the current baseline. If the project
already uses upstream `memory/soc/` or `memory/firmware/` records, consult those
as supplementary context; do not create a second record system for a question.

## Purpose
Assemble a complete SoC from first-party RTL, licensed hard/soft IPs, and
memory macros. Covers IP procurement, bus fabric configuration, top-level
integration, and chip-level simulation sign-off.

---

## Supported EDA Tools

### Open-Source
- **Verilator** (`verilator`) — fast simulation of the integrated top-level
- **cocotb** — Python co-simulation for bus-fabric and peripheral tests
- **FuseSoC** (`fusesoc`) — IP package manager and build system for SoC integration
- **Edalize** — EDA tool abstraction layer used with FuseSoC

### Proprietary
- **Synopsys VCS** (`vcs`, dialect `synopsys`) — chip-level simulation with coverage
- **Cadence Xcelium** (`xrun`, dialect `cadence`) — multi-language chip-level simulation
- **Siemens Questa** (`vsim`, dialect `siemens`) — chip-level simulation and protocol checking

---

## Stage: ip_procurement

### IP Qualification Checklist
- [ ] Deliverable format: RTL (.v/.sv), GDSII, or encrypted netlist?
- [ ] Technology node: certified for target process?
- [ ] Timing libraries: SS/TT/FF corners available?
- [ ] LEF/DEF: available for PD flow?
- [ ] Simulation models: behavioural/RTL for verification?
- [ ] UPF: power intent delivered?
- [ ] Databook: register map, timing diagrams, integration guide?
- [ ] DFT: scan-enabled? BIST available?
- [ ] Silicon proven: on which node?
- [ ] Support SLA: bug-fix and update commitment?

### Hard IP vs Soft IP
| Aspect | Hard IP (GDSII) | Soft IP (RTL) |
|--------|----------------|---------------|
| Area | Fixed | Synthesis-dependent |
| Timing | Characterised libs only | Optimisable |
| PD effort | Place as macro | Full PD flow |
| Customisation | None | Parameterisable |

### Memory Macro Qualification
1. Verify compiler-generated views: .lib, .lef, .v (behavioural)
2. Check access time vs target frequency
3. Verify retention/power-down modes (for UPF power domains)
4. Confirm MBIST ports available

### Output Required
- IP qualification report per IP
- IP deliverable checklist (all views received)
- IP risk register (gaps in deliverables)

---

## Stage: ip_configuration

### Domain Rules
1. Configure each IP per its databook for target use case
2. Parameterise data widths, FIFO depths, feature enables
3. Verify configured timing against the actual project's relevant clock-domain constraints; use `design_state.constraints.clock.clk_mhz` only in projects adopting that upstream schema
4. Verify interface widths match bus fabric port requirements
5. Generate integration wrapper if IP port names differ from system conventions

### Output Required
- Configured IP files and wrappers
- Timing verification report (configured corner)

---

## Stage: bus_fabric_setup

### Bus Fabric Selection
| Fabric Type | Use Case | Bandwidth |
|-------------|---------|-----------|
| AXI4 Crossbar | High-bandwidth data paths | High |
| AXI4-Lite | Low-bandwidth control registers | Low |
| APB3 | Peripheral register access | Low |
| NoC | Many-core topologies | Very High |

### Domain Rules
1. Non-overlapping address regions for all slaves
2. AXI width adapters for any width mismatches
3. Async bridges for all cross-clock-domain connections
4. QoS: assign traffic class for latency-sensitive masters
5. Error handling: out-of-range address → DECERR response defined

### Memory Map Validation
- No address region overlaps
- All peripherals accessible from all required masters
- Reserved regions return DECERR
- No unintended address aliasing

### QoR Metrics to Evaluate
- Address decode: complete, no gaps, no overlaps
- All IP blocks connected with correct port width
- CDC bridges in place for all clock crossings

### Output Required
- Bus fabric configuration file
- Memory map document (final, versioned)
- Address decoder verification report

---

## Stage: top_integration

### Domain Rules
1. Top module: wiring only — no logic at top level
2. All IPs instantiated once (no unintended duplicates)
3. All ports connected — lint check for unconnected active signals
4. Clock generation: PLL/mux module at or near top level
5. Reset generation: synchroniser per domain at top level
6. Tie cells: VDD/VSS tie-offs for all floating inputs
7. Scan chain: SI/SO routed through scan backbone
8. JTAG: TDI/TDO routed through JTAG chain
9. Instantiate every IP with named port connections — a positional connection silently
   mis-wires when an IP revision reorders its ports
10. Lint `soc_top.sv` in the context of the full SoC filelist, compiled as one unit. Linted
    alone, every IP is an unknown module and port-width mismatches go unchecked
11. An IP that is deliberately black-boxed (hard macro, analog block, encrypted IP) is a stub:
    undriven-net findings on its outputs are not connectivity errors. Record them as
    informational and name the stub, so a real unconnected port is not lost among them. A
    stub is benign only if you can name the macro or IP it stands for: an unknown module that
    resolves to first-party RTL in this repository is a missing filelist entry (rule 12)
12. An IP or module is not integrated until every tool's source list can see it. Simulation,
    lint, synthesis, PD and formal usually read separate lists, often Makefile variables, and a
    module missing from one is black-boxed by sv2v and yosys without an error while the
    chip-level regression, reading another list, stays green. Enumerate every list and confirm
    each new file is in each, or state why not; for `.f` lists run
    `python3 <skill-dir>/scripts/check_design_inputs.py <filelist.f> --rtl-dir <rtl>
    --list <name>=<file> ...` (see bundled [script](scripts/check_design_inputs.py);
    `.gprj` inputs remain governed by the Gowin project manifest)
13. Where a downstream flow converts the RTL before synthesis (sv2v, Surelog/UHDM), run the
    conversion on the SoC filelist and parse the output with the consuming tool
    (`sv2v <files> > out.v && yosys -q -p 'read_verilog out.v; hierarchy -check -top soc_top'`).
    A construct legal in SystemVerilog and not in Verilog-2005 passes lint and chip-level
    simulation and fails only there

### Integration Checklist (per IP)
- [ ] Correct module name and parameters
- [ ] All active ports connected
- [ ] Clock: correct domain
- [ ] Reset: correct domain and polarity
- [ ] Power ports: correct UPF domain
- [ ] Scan: SI/SO connected
- [ ] Source files registered in every tool's source list (simulation, lint, synthesis, PD, formal)

### Common Integration Bugs
| Bug | Consequence |
|-----|------------|
| Wrong clock domain | Metastability in silicon |
| Reset polarity inversion | Block never exits reset |
| Unconnected valid/enable | Block runs freely or never |
| AXI address offset wrong | Peripheral at wrong base address |
| Module missing from the synthesis or PD source list | Silently black-boxed: wrong netlist behind a green simulation regression |
| SystemVerilog construct the converter cannot lower | Synthesis or PD front-end syntax error that lint and simulation never see |

### Output Required
- Top-level RTL (soc_top.sv)
- Integration lint report
- IP connectivity summary

---

## Stage: chip_level_sim

### Required Tests
1. Boot test: CPU boots from reset vector, executes code
2. Peripheral access: read/write all peripheral registers
3. DMA: transfers between memory regions verified
4. Interrupt: each peripheral can interrupt CPU and ISR fires
5. Multi-master: concurrent bus access from multiple masters
6. Clock switching: clock mux operates correctly
7. Power modes: enter and exit sleep/deep-sleep
8. Reset: warm and cold reset; all blocks re-initialise

### QoR Metrics to Evaluate
- All peripheral register tests: PASS
- Boot test: CPU reaches application code
- DMA: correct data at correct address
- No AXI protocol violations (checker clean)
- No X propagation at key outputs after reset

### Output Required
- Chip-level simulation report
- Per-test pass/fail log
- Protocol checker clean report

---

## Stage: integration_signoff

### Sign-off Checklist
- [ ] All IP qualification issues resolved
- [ ] Memory map: final, agreed, no overlaps
- [ ] Top-level lint: 0 unconnected active ports
- [ ] CDC: 0 violations at chip level
- [ ] All chip-level simulation tests: PASS
- [ ] AXI protocol checker: clean
- [ ] Every module visible to every tool's source list, exemptions named with a reason
- [ ] Converted RTL parses in each downstream front-end, or the check is reported NOT RUN

### Output Required
- Integration sign-off report
- Final memory map document
- Integrated SoC RTL package (ready for synthesis)

---

## Constraint Validation

For a full SoC integration, establish each relevant clock and its relationship
from the actual project SDC/PLL/IP configuration. Use an existing project state
schema when present. Missing `design_state.json` alone does not block an existing
Gowin project or a targeted reference question. Ask only for required design
intent that cannot be determined from the available project.

For projects using the original pipeline, the relevant schema excerpt is in
[references/upstream-constraints.md](references/upstream-constraints.md).
Its ASIC defaults do not override the FPGA project's constraints or acceptance
criteria. Separate CPU, AHB, APB and audio clocks rather than assuming one MHz.

## Project Records

Record meaningful implementation decisions, measured validation, unresolved
coverage and resumption state in the project's existing notes (normally `.ai/`
under this user's AGENTS.md). A reference-only answer does not require a run
state file. Do not write to Codex's global memory directory as part of this skill.
Upstream run-state examples are retained in `references/upstream/skill-original.md` for
projects that explicitly use that workflow.
