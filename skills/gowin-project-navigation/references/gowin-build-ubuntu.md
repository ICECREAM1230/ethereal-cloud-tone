# Native Linux Gowin calls

Read for actual Linux build or IP Tcl invocation; this filename is retained for existing links. Use the selected project's `.gprj` and vetted Tcl. Shared rules and version-sensitive syntax are in [project/build Tcl](tcl-project-build.md); startup failures load [Linux troubleshooting](gowin-linux-troubleshooting.md) only as needed.

## Current Ubuntu host entry point

Invoke the Gowin CLI through `"${GOWIN_SH}" <path-to-Tcl-script>`; use the existing Gowin launcher for the GUI. On a Linux host configured as described in the collection README this is the preferred entry point for Gowin V1.9.12.04, including noninteractive agent calls:

```bash
verified_gowin_sh="${GOWIN_SH:?set GOWIN_SH to the verified launcher}"
"$verified_gowin_sh" "$verified_project_script" syn
```

The stage argument belongs to the selected project's Tcl script, not a universal gw_sh switch. Inspect the script's accepted arguments and output targets first. The wrapper provides process-local PATH, freetype preload, bundled libraries and offscreen Qt. Keep Codex/VS Code in the normal system environment; do not source .bashrc or permanently prepend IDE/bin. Verify that this launcher still matches the selected installation. On another host, or when the user selects another Gowin release, discover its own confirmed launcher instead of assuming this path.

## Select the existing installation once

Reuse the task's confirmed executable/launcher. Otherwise query PATH for `gw_sh`, then inspect the user-specified installation and known paths in project launchers/configuration for native `IDE/bin/gw_sh`. PATH failure in the current invocation is not proof of missing software; don't run `.exe`/`.cmd`, install packages or change shell startup to satisfy a lookup. Check the selected version, matching `IDE/doc` and task-required tool/help only; avoid repeated setup or unrelated tool inventories. Native Linux needs no WSL device guide.

In Bash, `type -t gw_sh` identifies a resolved function and `type -P gw_sh` finds the external executable independently. A verified user-configured interactive function is a valid launcher in that shell; preserve it and reuse the confirmed setup. Noninteractive agents/scripts may not load `.bashrc` or inherit functions. For those calls use an explicit launcher with the verified process-local profile, without auto-sourcing startup files or assuming interactive success implies identical noninteractive discovery.

Resolve the installation, Tcl and cwd from observed paths. A direct read-only startup probe is appropriate if that environment is not already verified:

```bash
"$verified_gowin_sh" -h >"$fresh_probe_log" 2>&1
probe_rc=$?
```

Use a fresh task directory/log. Preserve the exit status before reading the log; `set -e` callers should capture it through `if`/`else`. A banner/help response proves only that probe. If loading or Qt startup fails, keep the original log and read the matching troubleshooting section; PATH changes cannot repair a dynamic-library conflict.

## Optional process-local launcher for other installations

Use the confirmed `"${GOWIN_SH}"` directly on the current Ubuntu host. The helper below calls the raw binary and is an alternative for another explicitly selected installation or startup diagnosis; do not use it to bypass the current wrapper's verified profile.

[run_gowin_linux.py](../scripts/run_gowin_linux.py) accepts an explicit `IDE` directory, optional cwd/new log and exact `gw_sh` arguments after `--`. It uses Python 3's standard library, retains the caller's environment by default and does not create a project or choose a Tcl stage. With `--log`, it captures raw stdout/stderr, refuses to overwrite prior evidence and returns the tool's status (signal termination uses `128+signal`).

```bash
python3 "$skill_directory/scripts/run_gowin_linux.py" \
  --ide "$verified_gowin_ide" --cwd "$probe_directory" \
  --log "$fresh_probe_log" -- -h
probe_rc=$?
```

Only after a matching startup failure, choose the explicit `--headless`, `--bundled-qt` or `--freetype` options documented in [troubleshooting](gowin-linux-troubleshooting.md). No option hardcodes an installation/version or changes the parent process. GUI display settings must not inherit the headless profile.

## Execute the requested stage with isolated ownership

Inspect the actual project Tcl before running it: it may `cd` to its own project package and write its `impl/` regardless of the shell cwd. This repository's `scripts/run_project.tcl` selects `syn|pnr|all`; another project may have different arguments/options. The invocation shape after validating that script is:

```bash
(
  cd -- "$isolated_project_directory" &&
    "$verified_gowin_sh" "$verified_project_script" syn
) >"$fresh_build_log" 2>&1
build_rc=$?
```

Reuse the already verified task-local startup profile when needed; the example alone does not add it. A separate cwd or log cannot isolate a Tcl script that opens the original `.gprj`. Before parallel checks or experiments, preserve and copy the actual dirty `.gprj`, enabled sources, include/init inputs, IP configs/models, constraints and required process settings, including files outside its directory. Verify all resolved inputs and output destinations in the copy. Do not rewrite disabled entries or silently substitute a commit-only baseline. Keep one GUI/CLI/agent writer per `.gprj`/`impl`; never clear user results to manufacture freshness.

## Judge actual outputs

Record executable/version, cwd, arguments/Tcl, actual input identity and original tool exit code. Direct redirection/the helper preserves status; if using `tee`, use `pipefail` and capture `PIPESTATUS[0]` immediately, plus any log-write failure. A later `cat`/`tee` status cannot stand in for Gowin's.

Check the requested stage's full log, expected fresh outputs and configuration assertions. Tcl can continue after a rejected property while exiting zero; inspect saved `.ipc` and generated wrapper for IP, see [IP generation](../../gowin-ip-generation/SKILL.md). For synthesis/PnR/STA use the current selected project's reports and constraints. A prior `.fs`, timestamp alone, successful help or normal process exit does not prove current build success or timing closure. Report stage and freshness separately; building a bitstream does not authorize programming hardware.
