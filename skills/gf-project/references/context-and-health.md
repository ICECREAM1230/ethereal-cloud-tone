# Project context and health

Read only when deriving project context or validating existing configuration. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Auto-Detection

When this skill is invoked, it:

1. Checks if `.gateflow/project.yaml` exists
2. If absent, derive in-memory context; persist only for authorized configuration work:
   - HDL: check file extensions in project (`.sv` = systemverilog, `.v` = verilog, `.vhd` = vhdl)
   - Sources: use the selected manifest; only for source-only projects discover candidates excluding generated files and unrelated projects
   - Top module: find modules not instantiated by others
3. Use supplied board context; persist only when authorized

For an official Gowin project containing `.gprj`, change this behavior:

- set `vendor_flow.tool` to `gowin-official` and record the `.gprj` path when useful;
- derive device, enabled sources, constraints, and IP references from the `.gprj` `Device`/`FileList`;
- do not replace the authoritative file list with the broad extension scan;
- do not treat generated IP files, encrypted sources, or disabled entries as ordinary portable RTL;
- require the exact Arora V part and device version before making implementation claims.

## Usage by Other Skills

Any skill that needs project context should:

```bash
cat .gateflow/project.yaml 2>/dev/null
```

If absent, read the actual manifest without creating configuration.

## Board Memory

When a user mentions a board name in natural language (e.g., "I'm using an Arty A7"),
retain it in context; persist only when requested. Do not ask again unless
the user switches boards or starts a new project.

Board memory is per-project, not global. Different projects target different boards.

## GATEFLOW-RESULT Format

```
---GATEFLOW-RESULT---
STATUS: PASS | FAIL | BLOCKED | NOT_RUN
ACTION: CREATED | UPDATED | VALIDATED | NONE
PROJECT: <name>
FILE: .gateflow/project.yaml
BOARD: <board or null>
HDL: systemverilog | verilog | vhdl
TOP_MODULE: <module>
SOURCES: <count>
DETAILS: <summary>
---END-GATEFLOW-RESULT---
```

Use `PASS` only when the requested create/update/validation operation completed and its evidence was checked. Put the operation kind in `ACTION`; do not introduce operation names or `ERROR` as execution statuses.

## Health Check

| Check | Pass Condition |
|---|---|
| Schema valid | name, top_module, hdl exist |
| Sources exist | All listed files found |
| Top module found | grep for `module <top>` in sources |
| Constraints match target | .xdc for Xilinx, .pcf for iCE40, .lpf for ECP5, .cst for Gowin |
| Tools installed | `Get-Command gw_sh.exe` for official Gowin; use the selected simulator/synthesis tool for other modes |
