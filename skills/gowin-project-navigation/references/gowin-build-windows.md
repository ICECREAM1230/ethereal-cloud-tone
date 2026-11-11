# Run Gowin implementation on Windows

Use this only for Gowin synthesis or implementation on Windows. Use a local `gowin-check` wrapper if the installation provides one, or invoke the selected project's Tcl script with Windows `gw_sh.exe`. Resolve paths and tool version; do not hardcode a drive letter.

```powershell
& '<installed-Gowin>\IDE\bin\gw_sh.exe' '<project>\scripts\run_project.tcl' syn
```

This repository's script sets its top, SystemVerilog standard, and implementation options. Check its contents, selected `.gprj`, and Git status before running it; Gowin writes results under the project package. The presence of `gw_sh.exe` alone does not establish a successful synthesis or PnR run. For a different project, use its vetted Tcl flow or the installed Gowin Tcl manual; do not reuse this project's top or options.
