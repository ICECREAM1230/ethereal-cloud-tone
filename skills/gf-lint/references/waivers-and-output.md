# Waivers and output modes

Read only when a justified waiver, control file or machine-readable output is required. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Warning Suppression

### Inline Pragma
```systemverilog
/* verilator lint_off WIDTH */
assign narrow = wide;
/* verilator lint_on WIDTH */
```

### Control File
```
lint_off -rule WIDTH -file "*.sv" -match "Operator ASSIGN*"
```

## Error Message Format

```
%Error: file.sv:42:5: syntax error, unexpected ';'
%Warning-WIDTH: file.sv:25:15: Operator ASSIGN expects 8 bits
```

### Exit Codes
| Code | Meaning |
|---|---|
| 0 | Success (no errors or fatal warnings) |
| 1 | Errors or warnings (warnings are fatal by default) |

Use `-Wno-fatal` to make warnings non-fatal.

## Machine-Readable Output

```bash
verilator --lint-only --diagnostics-sarif *.sv  # SARIF JSON output
```
