# FuseSoC execution

Read only when running or generating a selected build target. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Running FuseSoC

```bash
fusesoc run --target=sim vendor:lib:design           # Simulate
fusesoc run --target=sim --tool=icarus vendor:lib:design  # Specific simulator
fusesoc run --target=synth_ice40 vendor:lib:design    # Synthesize
fusesoc run --target=lint vendor:lib:design           # Lint only
fusesoc core list                                      # List cores
fusesoc library add name https://github.com/org/repo  # Add library
```

## Tool Backend Options

### icestorm
- `pnr`: `next` (nextpnr) or `arachne`
- `nextpnr_options`: CLI args for nextpnr-ice40
- `yosys_synth_options`: Extra synth_ice40 options

### trellis (ECP5)
- `nextpnr_options`: CLI args for nextpnr-ecp5
- `yosys_synth_options`: Extra synth_ecp5 options

### verilator
- `mode`: `binary`, `cc`, `lint-only`
- `verilator_options`: Extra CLI args

### vivado
- `part`: FPGA part number
- `synth`: `vivado` or `yosys`

## Auto-Generation

Scans project, reads `.gateflow/project.yaml`, generates .core file.

## GATEFLOW-RESULT Integration

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
FILES: [generated .core file]
DETAILS: [summary]
---END-GATEFLOW-RESULT---
```
