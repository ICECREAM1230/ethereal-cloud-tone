# Manufacturing packaging

Read only when manufacturing output is explicitly requested. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Manufacturing Package Script

```bash
mkdir -p output/{gerbers,assembly,docs}
kicad-cli pcb export gerbers --output output/gerbers/ board.kicad_pcb
kicad-cli pcb export drill --output output/gerbers/ --format excellon board.kicad_pcb
kicad-cli pcb export pos --format csv --output output/assembly/positions.csv board.kicad_pcb
kicad-cli sch export bom --exclude-dnp --output output/assembly/bom.csv design.kicad_sch
kicad-cli sch export pdf --output output/docs/schematic.pdf design.kicad_sch
kicad-cli pcb export step --output output/docs/board.step board.kicad_pcb
```
