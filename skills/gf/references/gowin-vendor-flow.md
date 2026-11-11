# Gowin Vendor Flow Contract

This reference defines how GateFlow works with an official Gowin Software project, especially an Arora V project. It is a routing and evidence contract, not a replacement for the installed Gowin manual or the exact device guide.

For a bounded RTL/CST/SDC edit without a requested implementation result, use [local edits](local-edits.md) first. The stages below describe dependencies and evidence levels, not a checklist every Gowin edit must execute. Invalidated prior reports require a qualification of claims, not automatic rebuilding.

## Authority and flow boundary

Use this mode whenever the project contains a Gowin `.gprj`. A source-only, newly created, or not-yet-built project may have no `impl/` directory; that absence does not disable the official flow.

| Concern | Authority | GateFlow responsibility |
|---|---|---|
| Device, enabled sources, constraints, IP references | `<project>.gprj` | Read and report the exact entries |
| Top-level/process options | `impl/*_process_config.json` and generated process files | Use as supplementary evidence; do not treat it as the manifest |
| Gowin IP configuration | `.ipc` plus the generated wrapper/template | Advise on integration and parameters; never edit generated implementation |
| Official synthesis/PnR/bitstream | Gowin Software via `gw_sh`/Tcl | Drive or prepare the command; report actual outputs |
| Functional vendor-model simulation | Exact models/libraries and verified simulator capability | Prefer Verilator when compatible; prepare ModelSim/Questa only as the secondary path |
| Portable RTL/module simulation | GateFlow `gf-lint`/`gf-sim` and Verilator | Verify selected logic and testbenches |

The `.gateflow/project.yaml` file may hold optional GateFlow context, but it must not replace or silently rewrite the Gowin project model. Do not auto-promote a broad scan of `**/*.v`/`**/*.sv` into the official source list: disabled files, include paths, generated IP, encrypted sources, and file order matter.

## Project discovery

1. Locate the `.gprj` and establish its directory as the project root.
2. Read `Device` and every `FileList` entry, including its enable state and path.
3. Read the same-name `.gprj.user` only for UI/result aliases, and verify that referenced results still exist.
4. Read `impl/*_process_config.json` and `impl/pnr/cmd.do` only as saved process evidence. Preserve exact option spelling and casing.
5. Inspect `src/` for RTL, `.cst`, `.sdc`, `.ipc`, IP wrappers/templates, simulation models, and debug configuration.
6. Inspect `impl/gwsynthesis/` and `impl/pnr/` for reports, logs, netlists, timing data, and programming images.
7. For Arora V, require the exact part number and device version from the project. The family name alone is insufficient for Tcl options, primitives, pins, timing, or IP support.

If a read-only navigation report is needed, use `gowin-project-navigation` and preserve its distinction between source inputs and generated artifacts.

## Official build flow

The preferred implementation path is to drive the existing `.gprj`; do not rebuild the project from a guessed file list.

Select software calls from the actual execution host, only when execution is needed:

- Linux: read [Linux host calls](host-linux.md), then `gowin-project-navigation`'s `references/gowin-build-ubuntu.md` for the selected installation/launcher and project Tcl. Load Linux troubleshooting only after a matching startup failure.
- Windows: read [official Windows calls](gowin-vendor-windows.md) only when Windows is the target.

Record the selected executable's absolute path/version and validate the installed Tcl help/manual (SUG1220 or the matching local edition). Discover only requested tools. A PATH miss does not prove absence; inspect known installation/project launchers before reporting BLOCKED. A startup probe does not establish device, library, license or implementation success.

Quote every path with correct Tcl list/bracing semantics and never interpolate an untrusted path directly into Tcl source. Do not assume that `run`, its stage names, `run close`, or any switch exists in another installed release. Use only commands confirmed by that release's help/manual. Run only the stages needed for the request, while honoring dependencies and freshness. A source or synthesis-option change invalidates synthesis and downstream evidence; a placement/constraint change invalidates PnR, STA, and bitstream evidence; an IP configuration change first requires IP regeneration. A full run is appropriate when the requested conclusion spans the whole flow, but is not mandatory for every inspection or partial build.

For an IP-generation operation, inspect the installed Tcl help and the project’s `.ipc`/wrapper first. The command family commonly includes `read_ipc`, `get_ips`, `set_property`, and `generate_target`, but parameter names and supported targets are version- and IP-dependent. Never fabricate a parameter name from a generic FIFO description.

Use absolute paths in generated Tcl, keep temporary experiments in a copied project, and do not let `gw_sh` and the GUI write the same `impl/` directory concurrently. Do not delete user sources or constraints as part of cleanup. Generated `impl/` results are evidence and may be regenerated; source IP configuration and project files are not disposable.

## Official stages and evidence

Track the official flow as `DISCOVER -> VALIDATE_DEVICE -> GENERATE_IP -> SYNTHESIS -> PNR -> STA -> BITSTREAM`. A request may stop at any stage, resume from fresh upstream outputs, or skip `GENERATE_IP` when no IP needs regeneration. Do not claim a later stage from an older artifact when a relevant upstream input changed.

For every requested stage, record:

- the exact command or Tcl procedure and working directory;
- executable path/version and exit code;
- log/report paths and the expected output paths;
- input identity, including `.gprj`, enabled source/constraint entries, device/version, `.ipc` when relevant, and process options;
- output freshness as `current`, `stale`, or `unknown`, with its basis. Timestamps are useful but weaker than embedded run metadata or recorded hashes.

Report a stage as successful only when it actually ran (or has explicitly verified current evidence acceptable to the request) and its required outputs are present. Typical evidence includes:

- synthesis: `impl/gwsynthesis/` log/report and generated netlist;
- PnR: `impl/pnr/` placement/routing report, log, and routed database/netlist expected by the installed flow;
- STA: timing report plus the active `.sdc`/`.cst` constraints and routed result;
- bitstream: the exact `.fs`, `.bin`, or `.binx` path registered or produced by the current run;
- settings: the current process configuration and, where useful, `cmd.do`;
- freshness: output modification time is not older than the relevant source/configuration changes.

Use the common four execution statuses:

- `PASS`: the requested stage ran and met its stated checks, or a read-only evidence request verified acceptable current artifacts;
- `FAIL`: the stage ran and returned an error or did not produce a required output;
- `BLOCKED`: the stage was requested but a required executable, license, device database, IP model, library, permission, or input prevented execution;
- `NOT_RUN`: the stage was outside the requested scope or was deliberately not executed; never infer success.

`STALE` is a freshness value, not a fifth execution status. A stale required output cannot support `PASS`; report the stage as `NOT_RUN` when no execution was requested, or `BLOCKED` when the requested execution cannot proceed.

A `.fs`/`.bin` file alone does not prove timing closure. Before reporting STA success, verify that each intended primary clock has a clock constraint, generated clocks are related to their source clocks, input/output delays cover the relevant interfaces, and every false-path or multicycle exception is justified. Report setup and hold evidence, including WNS/TNS (and failing endpoint counts when available), unconstrained paths/clocks, ignored constraints, and the exact current `.sdc`/`.cst` inputs. Do not treat an estimated frequency or an old timing report as sign-off.

## Functional simulation and secondary simulator

Verilator is the preferred functional simulator, using the installed OSS CAD Suite on the current Linux host. A `.gprj`, a Gowin primitive or a vendor library name alone does not require ModelSim/Questa. Resolve the exact testbench, enabled sources, generated models, family/version library, includes and defines before deciding compatibility.

The project-specific simulation command or adapter consumes official project data:

1. Resolve the `.gprj` root, enabled `FileList`, include paths, top/testbench, and generated IP directories. Build a reviewable ordered source/model manifest.
2. Identify the actual functional models and any protected content. A legitimate plaintext model may be usable with Verilator when the selected version supports its constructs and required behavior; compile and check the real model rather than declaring all vendor IP unsupported.
3. For a compatible functional source set, prepare and run Verilator using [Linux simulation calls](../../gf-sim/references/simulation-linux.md). Preserve vendor files, isolate outputs and verify expected-value checks. Do not exclude required behavior, use unsupported-feature blackboxing, or blanket-suppress diagnostics to manufacture PASS. A warnings-as-errors stop is distinct from an unsupported-model error; inspect the actual diagnostic before selecting the secondary simulator. A narrowly justified warning waiver must retain the original diagnostic and cannot bypass unsupported required behavior.
4. Select ModelSim/Questa only as a secondary path when protected models, unsupported required constructs/languages, simulator-specific dependencies, or requested SDF timing semantics require it, or the user explicitly selects it. Confirm that this simulator can actually handle the exact model/library/version.
5. Keep logs, waveforms and check results in the simulator work directory. Record which models and behavior ran; never confuse simulation files with `impl/pnr/` outputs.

The source Linux host had no ModelSim installation; verify the selected host independently. When that secondary path is required, prepare the manifest and a reviewable `.do` only when writes are authorized and inputs suffice; execution remains `BLOCKED` with the exact missing simulator/model/library/license or adapter. Preparation is not simulation PASS and does not authorize installation. Use `NOT_RUN` when execution was outside scope. Gowin Tcl may orchestrate an external process using standard Tcl `exec`, but project/IP Tcl does not supply the external simulator's own commands.

For post-PnR timing simulation, require the official generated simulation netlist, SDF, matched libraries and a simulator flow verified to apply the required timing checks. Verilator's `--timing` supports testbench timing constructs; it does not by itself establish SDF back-annotation or timing-check coverage. Verify release-specific capabilities before selecting the secondary simulator. The Gowin options for producing simulation netlist/SDF must also be checked in the installed help/manual. Functional simulation, timing simulation, official STA and board behavior are separate claims.

## GateFlow verification boundaries

Use Verilator for portable RTL and for functional models whose actual compatibility and semantics have been confirmed. Verify CDC structures, protocol adapters, assertions and algorithmic modules at the requested scope. Treat known Gowin IP as a vendor dependency rather than missing GateFlow IP.

A contract-compatible stub can isolate user logic for a module test when appropriate, but its PASS covers that isolation only; it cannot establish the original IP or full integration behavior. Keep genuine model integration tests distinct. Protected `.vp/.vhdp/.vgp` content cannot be decoded, edited or replaced to make Verilator pass; use an authorized compatible model or report the required secondary flow as blocked. A filename extension alone is not a compatibility test.

## IP integration advice

GateFlow may provide design guidance without owning the IP implementation. For a FIFO, ask or derive from the surrounding interface:

- **Width:** match the logical payload and downstream packing; avoid an unnecessary width conversion that adds muxing or wastes BSRAM words.
- **Depth:** cover the maximum burst/outstanding data plus clock-domain elasticity and control margin; for an asynchronous FIFO, use the required pointer/flag margin and prefer a supported power-of-two depth when the IP architecture requires it.
- **Clocking:** use a synchronous FIFO only for one clock domain; use the IP’s asynchronous/dual-clock mode for independent clocks and verify reset/flag semantics.
- **Latency and flags:** clarify show-ahead versus registered read, full/empty timing, almost-full/almost-empty thresholds, reset behavior, and first-word fall-through before selecting parameters.
- **Device resources:** confirm that the selected Arora V IP mode maps to the intended BSRAM/DSP/clock resource; do not infer legality from a different Gowin family.

The final parameter names, port list, and generated file list must come from the project’s `.ipc`, generated template, and installed IP documentation. A recommendation is not evidence that the IP has compiled.

## Failure classification

Separate failures by layer before changing RTL:

| Observation | Likely layer | First action |
|---|---|---|
| `.gprj` file missing/disabled or IP output absent | Project/IP integration | Reconcile `.gprj` and `.ipc`; regenerate through Gowin flow |
| Gowin compile error in protected IP | Vendor IP/configuration | Check IP parameters, wrapper, device/version, and generated model; do not edit ciphertext |
| Verilator cannot parse vendor model | Simulation boundary | Exclude it from module test or use a contract-compatible stub |
| Selected simulator cannot resolve or handle a required model | Simulation setup/compatibility | Check the real model, supported constructs, library inputs and secondary-flow prerequisites |
| PnR/timing failure | Gowin implementation/constraints | Read current `impl/pnr` reports/logs and verify `.sdc`/`.cst` |
| Portable RTL lint/sim failure outside vendor boundary | User RTL/testbench | Use the normal GateFlow agent and verification loop |

Do not report a generic Verilator PASS as an official Gowin build PASS, and do not report a generated bitstream as timing-closed without current PnR evidence.

## Future adapter interface

The future semi-automatic simulation script can use this neutral contract without a fixed script name:

```text
Inputs:
  project.gprj
  exact device/version
  enabled file list and include paths
  generated IP simulation models
  matched Gowin functional model/library roots
  selected simulator/version (Verilator first; ModelSim/Questa secondary)
  top and testbench
  optional SDF and simulation defines

Outputs:
  reviewable source/model manifest
  compile/run command (Verilator), or .do script for the secondary simulator
  simulator work directory and log
  waveform path, if enabled
  status: PASS | FAIL | BLOCKED | NOT_RUN
  freshness: current | stale | unknown
```

The adapter should be deterministic, path-aware, and explicit about model compatibility and missing dependencies. Implement or change it only when required by authorized simulation work.
