# Scan a Gowin project on Windows

Use this only for a project scan executed on Windows. The scanner requires Windows PowerShell 5 or newer and reads files without changing them. Resolve the script relative to this skill, not the target project.

```powershell
& '<skill-directory>\scripts\find-gowin-project.ps1' -Root '<workspace-directory>' -Project '<absolute-project.gprj>' -AsJson
```

Omit `-Project` only to discover projects below an explicitly scoped root; `-Root` may also name a `.gprj` directly. Request `-AsJson` only for machine-readable output. Replace placeholders with observed paths. Run scanner tests only when changing the scanner.
