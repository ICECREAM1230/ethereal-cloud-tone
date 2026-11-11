# Linux Verilator functional simulation

Read only for a requested Linux compile/run of portable RTL or actual vendor functional models confirmed compatible by [the vendor contract](../../gf/references/gowin-vendor-flow.md). Use [the shared workflow](simulation-workflow.md) for file classification, expected checks and result reporting, and [Linux host calls](../../gf/references/host-linux.md) for discovery. On a Linux host configured as described in the collection README, use the confirmed OSS CAD Suite entry point even when a system Verilator is discoverable:

```bash
verilator_cmd=("${OSS_CAD}" verilator)
"${verilator_cmd[@]}" --version
```

Keep this as a Bash argument array; the launcher and tool name are separate arguments. On another Linux host, discover the requested installation and set `verilator_cmd=("$verified_verilator")` for a verified standalone executable, or use that host's explicit launcher array. A PATH miss alone does not prove absence; inspect known tool/project paths without installing software or changing global startup configuration.

On 2026-10-20, `/opt/eda/oss-cad-suite/current` resolved to `/opt/eda/oss-cad-suite/20261013/oss-cad-suite`, and the local entry point reported `Verilator 5.053 devel rev v5.052-385-gb8e7f1e4b (mod)`. The retained 20260929 installation was the version verified on 2026-10-10 (`v5.052-240-g640607a0d`); that is historical evidence, not the current selection. Resolve `current` and record the actual tool version when executing a check, since a later update can change the selection.

The suite's bin and py3bin are activated only in its child process. Do not source the suite into Codex/VS Code's parent environment or infer that activation persists across separate shell calls.

Resolve the top, source order, includes, defines and model boundary first. Use a task-isolated working directory (or the installed tool's verified output-directory option), so two checks cannot share `obj_dir`. Copy the actual dirty inputs/configuration needed by the check before switching cwd. A separate log alone does not isolate compilation outputs. Preserve quoted argument arrays rather than expanding a string of paths.

The established Verilator invocation shape, to verify against the installed help:

```bash
# Execute within the isolated working directory; source_paths is a Bash array
# of observed absolute input paths in their required compile order.
"${verilator_cmd[@]}" --binary -j 0 -Wall --trace \
  --top-module "$tb_top" "${source_paths[@]}" -o sim >compile.log 2>&1
compile_rc=$?
```

Use a fresh log/output directory and inspect `compile_rc` before running the produced executable. With `set -e`, collect status in an `if`/`else` instead of losing it to an early shell exit. Trace generation is optional. `--binary` includes timing support for testbench delays/clocks in the checked release; `--timing` is not proof of SDF timing simulation. Confirm flags with the selected executable. Official [command documentation](https://verilator.org/guide/latest/exe_verilator.html) and [language/model limitations](https://verilator.org/guide/latest/languages.html) are reference sources; local help is authoritative for this development build. Record options and tool version; add only task-required supported flags.

```bash
timeout 60 ./obj_dir/sim >simulation.log 2>&1
simulation_rc=$?
```

Record the real status before reading the log. Timeout is a failed/incomplete run, not PASS. Verify expected-value checks, failure count and completed-check count in the log; `$finish` or exit zero alone is insufficient. For `tee`, use the exit/log rules in Linux host calls. A functional PASS covers the exact source/model set and completed checks. It does not establish Gowin implementation, SDF timing verification or board behavior.

For a requested performance, trace, assertion or coverage configuration change, read [optional Linux simulation options](simulation-linux-options.md). Ordinary compile/run does not need this reference.
