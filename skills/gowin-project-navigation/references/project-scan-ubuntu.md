# Scan a Gowin project on Ubuntu 24.04

Use this only for a project scan executed on Ubuntu. The Python 3 scanner uses the standard library and reads files without changing them. Resolve the script relative to this skill, not the target project.

```bash
python3 '<skill-directory>/scripts/find_gowin_project.py' \
  --root '<workspace-directory>' --project '<absolute-project.gprj>' --json
```

Omit `--project` only to discover projects below an explicitly scoped root; `--root` may also name a `.gprj` directly. Request `--json` only for machine-readable output. Replace placeholders with observed paths. A Windows absolute path in the manifest is unavailable as a Linux-relative source until resolved on this host. Run scanner tests only when changing the scanner.
