# Open-source PnR examples

Read only when the user selects a supported open-source implementation flow. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Supported Targets

| FPGA Family | Tool | Command |
|------------|------|---------|
| Lattice iCE40 | nextpnr-ice40 | `nextpnr-ice40 --up5k --package sg48 --json synth.json --pcf constraints.pcf --asc output.asc` |
| Lattice ECP5 | nextpnr-ecp5 | `nextpnr-ecp5 --85k --package CABGA381 --json synth.json --lpf constraints.lpf --textcfg output.config` |
| Gowin official project | Gowin Software (`gw_sh`) | Open the existing `.gprj`, then run the requested official process through Tcl |
| Gowin open-source flow (explicit opt-in) | nextpnr-himbaechel | `nextpnr-himbaechel --device <exact-part> --vopt family=<family> --vopt cst=<file> --json synth.json --write routed.json` |

**NOT Supported**: Xilinx (use Vivado), Intel (use Quartus). For these, GateFlow generates TCL scripts instead.

## Workflow

For an official Gowin project, first read the `.gprj` and route to `gw_sh`/Tcl. Do not require a Yosys `synth.json` or silently fall back to nextpnr. Use the open-source prerequisites below only after the user explicitly selects that flow.

1. Check project target (board.yaml -> pnr_target)
2. Verify synthesis output exists (synth.json from Yosys)
3. Verify constraint file exists
4. Run nextpnr with correct flags
5. Report timing and utilization
6. Generate bitstream if P&R succeeds

## Bitstream Generation

After P&R:
- iCE40: `icepack output.asc output.bin`
- ECP5: `ecppack output.config output.bit`
- Gowin official: produced by Gowin Software according to the `.gprj` process configuration
- Gowin open-source: produced by the selected nextpnr-himbaechel/packing flow

## Result Format

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
TIMING:
  Fmax: 125.3 MHz
  Slack: +2.1 ns
UTILIZATION:
  LUTs: 142 / 5280 (2.7%)
  FFs: 87 / 5280 (1.6%)
FILES: [output files]
DETAILS: [summary or timing violations]
---END-GATEFLOW-RESULT---
```

## Common Flags

| Flag | Purpose |
|---|---|
| `--freq <mhz>` | Target frequency |
| `--seed <n>` / `-r` | RNG seed / randomize |
| `--opt-timing` | Post-placement timing opt |
| `--report <file>` | JSON timing/utilization |
| `--timing-allow-fail` | Proceed despite violations |
| `--router2-heatmap <prefix>` | Congestion heatmaps |
| `--placer heap` | Heap placer (default) |

## Target Details

### iCE40
Devices: `--lp1k`, `--lp8k`, `--hx1k`, `--hx8k`, `--up5k`
Output: `--asc <file>` | Constraints: `--pcf <file>`
Full pipeline: `yosys -p "synth_ice40 -json out.json" design.v && nextpnr-ice40 --hx8k --package ct256 --json out.json --pcf pins.pcf --asc out.asc && icepack out.asc out.bin`

### ECP5
Devices: `--12k`, `--25k`, `--45k`, `--85k`
Output: `--textcfg <file>` | Constraints: `--lpf <file>`, `--sdc <file>`
Full pipeline: `yosys -p "synth_ecp5 -json out.json" design.v && nextpnr-ecp5 --25k --package CABGA256 --json out.json --lpf pins.lpf --textcfg out.config && ecppack out.config out.bit`

### Gowin open-source flow (explicit opt-in)
Device: `--device <part>` (e.g., `GW1NR-LV9QN88PC6/I5`)
Family: `--vopt family=<fam>` (for C-silicon, e.g., `GW1N-9C`)
Constraints: `--vopt cst=<file>` (mandatory)
Full pipeline: `yosys -p "synth_gowin -json out.json" design.v && nextpnr-himbaechel --device <exact-part> --vopt family=<family> --vopt cst=pins.cst --json out.json --write routed.json && gowin_pack -d <family> -o out.fs routed.json`

For the official flow, use the existing `.gprj` instead of this pipeline:

```tcl
open_project {<verified-absolute-project.gprj>}
run pnr
run close
```

This is only a procedure shape to validate against the installed Gowin Tcl help/manual. Quote all paths with Tcl list/bracing semantics, do not interpolate untrusted paths, and do not assume these procedure or stage names across versions. Run only the required fresh stages; a full run is not mandatory for every request. See [`gf/references/gowin-vendor-flow.md`](../../gf/references/gowin-vendor-flow.md) for the stage and evidence contract. Do not report an open-source PnR result as an official Gowin build result.
