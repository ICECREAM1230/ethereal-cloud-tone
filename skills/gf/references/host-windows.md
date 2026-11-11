# Windows software invocation

Read only when tools will execute on Windows. Shared RTL, vendor boundaries and acceptance criteria remain in the task skill. Linux tasks use their POSIX examples without loading this file.

Use PowerShell literal paths or Python pathlib/subprocess argument arrays. Resolve native executable paths with `Get-Command`, then inspect the selected tool's version/help. POSIX shell examples require a confirmed POSIX environment; do not paste them into PowerShell or assume a Windows `.exe` is a Linux executable.

For simulation discovery:

```powershell
Get-Command verilator.exe -ErrorAction SilentlyContinue | Select-Object Name, Source, Version
Get-Command vsim.exe -ErrorAction SilentlyContinue | Select-Object Name, Source, Version
```

`vsim.exe` discovery does not prove that Gowin libraries or a usable license are configured. Preserve the task's compile/run arguments and apply the selected host's path, quoting and exit-code rules. For official Gowin software calls, use the Windows leaf in `gowin-project-navigation`; do not invent a generic replacement flow.

For the official Gowin discovery and Tcl example, read [official Windows calls](gowin-vendor-windows.md). These calls are not loaded on Linux.
