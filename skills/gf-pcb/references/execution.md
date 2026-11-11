# PCB execution and output

Read only when using discovered KiCad tools or producing requested design files. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Tool Detection

```bash
which kicad-cli
```

If not found:
```
---GATEFLOW-RESULT---
STATUS: BLOCKED
DETAILS: KiCad not installed. Install for PCB design.
  macOS: brew install --cask kicad
  Linux: sudo apt install kicad
  Download: https://www.kicad.org/download/
---END-GATEFLOW-RESULT---
```

## Workflow

1. **Parse request** — What board? Components? Constraints?
2. **Web search** — Reference designs, datasheets, footprints
3. **Run the `pcb-designer` responsibility** — Generate schematic + PCB
4. **Run verification loop**:
   a. KiCad DRC (Design Rule Check)
   b. KiCad ERC (Electrical Rule Check)
   c. AI Review (power, decoupling, floating pins, voltages)
   d. If errors → fix and re-verify (max 3 iterations)
5. **Label output** with verification status and confidence
6. **Deliver** with mandatory disclaimer

## Output Files

```
project/
├── schematic.kicad_sch     # Schematic (S-expression)
├── board.kicad_pcb         # PCB layout
├── bom.csv                 # Bill of materials
└── verification_report.md  # DRC/ERC/AI review results
```

## Result Format

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
CONFIDENCE: high | medium | low
VERIFICATION:
  DRC: PASS/FAIL (N errors, M warnings)
  ERC: PASS/FAIL (N errors, M warnings)
  AI_REVIEW: PASS/FAIL (N/M checks passed)
FILES: [generated files]
DETAILS: [summary]
---END-GATEFLOW-RESULT---
```

## KiCad CLI Quick Reference

```bash
# DRC
kicad-cli pcb drc --format json --severity-all --exit-code-violations --output drc.json board.kicad_pcb

# ERC
kicad-cli sch erc --format json --severity-all --exit-code-violations --output erc.json design.kicad_sch

# BOM
kicad-cli sch export bom --fields "Reference,Value,Footprint,${QUANTITY},Manufacturer,MPN" \
  --group-by "Value,Footprint" --exclude-dnp --output bom.csv design.kicad_sch

# Gerbers + Drill
kicad-cli pcb export gerbers --output gerbers/ board.kicad_pcb
kicad-cli pcb export drill --output gerbers/ --format excellon board.kicad_pcb

# 3D Model
kicad-cli pcb export step --output board.step board.kicad_pcb
```

Exit codes: 0 = clean, 5 = violations found.
