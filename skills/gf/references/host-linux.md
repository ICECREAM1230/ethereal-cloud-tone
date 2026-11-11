# Linux software invocation

Read only when software executes on Linux. RTL/interface/constraint rules and verification boundaries remain in the task skill. Use the actual host; native Linux needs no WSL routing. Read WSL device-access guidance only if WSL is actually detected and the requested operation needs it.

## Configured Linux host: isolated tool entry points

Configure the task-local `OSS_CAD` and `GOWIN_SH` entry points as described in the collection README. The source machine used Ubuntu 24.04; verify the selected installation on this host:

- Invoke OSS CAD Suite tools through `"${OSS_CAD}" <tool> <arguments>`, which loads the tool environment on demand.
- Invoke the Gowin CLI through `"${GOWIN_SH}" <path-to-Tcl-script>`; use the existing Gowin launcher for the GUI.

Keep Codex/VS Code in the normal system environment. Do not add suite/Gowin directories to global PATH or LD_LIBRARY_PATH, edit shell startup, or source .bashrc to make a tool discoverable. Prefix the OSS CAD Suite backend commands shown in other GateFlow examples with the OSS launcher on this host; its child environment also applies to tools spawned by sby or other orchestrators. Run each noninteractive tool command through the entry point rather than relying on activation in a previous shell call. These paths are local configuration, not defaults for another host.

```bash
"${OSS_CAD}" verilator --version
"${OSS_CAD}" yosys -V
"${OSS_CAD}" sby --version
"${GOWIN_SH}" "$verified_project_script" syn
```

The Gowin argument syn above belongs to a project script that accepts it; inspect the actual Tcl before selecting stages or arguments. Reuse the local wrapper's child-only library/Qt setup and check that it matches the selected Gowin installation. A missing wrapper is an entry-point/configuration gap, not proof that the underlying software is absent; inspect known configuration without restoring global PATH changes.

## Find and activate only the requested tool

1. Reuse the task's confirmed absolute executable or vetted project launcher; check that it still exists and matches the required version.
2. Otherwise query PATH for the requested tool, then inspect known installation roots and paths in project launchers/configuration. A PATH miss means undiscovered or inactive, not necessarily uninstalled. Avoid unrelated tool inventories or broad home/drive scans.
3. Validate that selected native executable's version/help, working directory and dependencies. Record its identity once and reuse it until inputs or environment change. A successful startup does not prove license, device support, library setup or a build.
4. Activate an existing SDK/runtime only for the task and within a subprocess or documented task-local shell. Do not install software or edit shell startup/global library settings to satisfy a lookup.

Distinguish shell resolution from the raw executable: Bash `type -t` identifies a function and `type -P` finds the executable on PATH. Reuse a verified user function in its matching interactive shell and preserve existing startup configuration. Noninteractive calls may not load `.bashrc` or inherit the function; use an explicit launcher/profile for that execution scope.

On other Linux hosts, examples for the requested tool only:

```bash
command -v verilator
"$verified_verilator" --version
```

For Gowin build or IP calls, resolve `gowin-project-navigation` from the installed catalog and read its `references/gowin-build-ubuntu.md`. It covers known installation discovery and optional process-local startup profiles; startup failures lead to its Linux troubleshooting leaf. Do not assume a Windows executable, shell alias, or an experiment directory is a portable launcher.

For GAO/GVIO, constraint-tool interfaces or authorized Programmer calls, read `gowin-project-navigation`'s `references/gowin-tool-automation.md`; these are separate tools, not inferred `gw_sh` Tcl commands. GUI procedures are outside these skills.

The source-machine preference recorded on 2026-10-08 was OSS CAD Suite Verilator first; ModelSim/Questa was secondary and absent there. This does not establish availability on another host. On the configured Linux host, use `"${OSS_CAD}" verilator` and inspect actual model compatibility before selecting a fallback. Do not install a simulator or repeatedly probe an already confirmed absence.

## Preserve command and output identity

Pass arguments as arrays or separately quoted words, retain the intended cwd, and use a task-isolated output directory for simultaneous checks. A unique log does not isolate a tool's `impl/` or `obj_dir/`. Preserve the actual complete input baseline before copying a dirty project; commit-only copies omit necessary local sources/configuration.

Capture the tool's exit status before another command. Direct redirection preserves it; if streaming with `tee`, use `pipefail` and capture `PIPESTATUS[0]` immediately, plus logging failures. Verify expected outputs and assertions separately. Never delete prior user outputs to make a check appear fresh.

Classify discovery, dynamic loading, display/plugin, permission, license and design failures separately. Use current session permissions; a historical sandbox error does not justify escalation or a license change on this host.
