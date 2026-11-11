# Official Gowin calls on Windows

Read only for software executing on Windows. The shared vendor contract controls project authority, stages, IP protection and evidence; use `gowin-project-navigation` and its Windows build leaf for the selected project. Discover only the tools required by the requested stage.

Discover the actual executables on Windows before running anything:

```powershell
$tools = 'gw_sh.exe', 'vsim.exe', 'verilator.exe'
$tools | ForEach-Object {
    Get-Command $_ -ErrorAction SilentlyContinue | Select-Object Name, Source, Version
}
```

Also inspect the exact executable paths already recorded by the project, environment, or user. `gw_sh.exe` is commonly below the Gowin `IDE\bin` directory, and `vsim.exe` below a ModelSim/Questa `win32`/`win64` directory, but do not recursively scan unrelated drives. Validate each selected executable with its own `-h`, `-help`, or `-version` output and record the absolute path and version. A command found by name is not proof that the required Gowin device database or simulator libraries are configured.

After verifying the installed Gowin Tcl help (SUG1220 or the matching local manual), a project invocation may have this shape:

```text
gw_sh.exe -h
```

```tcl
open_project {C:/absolute path/to/project/project.gprj}
run all
run close
```

