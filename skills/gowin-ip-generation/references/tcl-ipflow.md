# Tcl IPFlow and GowinModGen

Read this only for an explicit Tcl request, a known working IPFlow configuration, or a case the bundled generator does not handle. Use the main Skill for normal generation. Tcl support varies by IP; a failed Tcl route does not prove that the plugin generator cannot create the IP.

These notes apply to the checked Linux Gowin V1.9.12.04 installation. Check local help and the matching SUG1220 chapter 4 before using a command on another version.

## Find the tools

Check the selected Gowin installation's `IDE/bin`, `IDE/ipcore`, and `IDE/doc`. A missing PATH entry does not mean the tools are missing. On this host, run Tcl through `"${GOWIN_SH}" <script.tcl>` so the library environment stays local to the child process.

- `gw_sh` provides Tcl/IPFlow. Check `create_ipc -h` and `generate_target -h`.
- `GowinModGen` reads a vendor `.mod` file. Its checked usage is `GowinModGen <-do><MODFILE> [-v] [-h]`. Use a file made by the same vendor version or defined by its manual; do not guess its format.
- The name `gowinIPEn` does not establish that it is a generator.
- Read the actual `.ipspec` for the IP ID, supported devices, and properties. For example, the installed `RAM_SDPB/sdpb.ipspec` uses ID `sdp`.

## Isolated generation

Use the exact part and revision from the selected project or user request. Create a new isolated project and output folder. Do not overwrite existing IP or use the formal project as a scratch project: `generate_target` adds files to the current project.

The sequence is: create/open isolated project, create/read IPC, query/set properties, check the saved IPC, then generate. Keep related settings together when they have joint constraints.

Before generation, check the saved module name, IP type, device, width/depth, read mode, reset, and initialization settings. Stop if they differ from the request. After generation, check the configuration, wrapper, and instance template again. Use their actual ports and model behavior for integration.

| Command | Purpose |
|---|---|
| `create_ipc -name <ipName> -module_name <moduleName> [-language <Verilog|VHDL>] [-file_name <name>] [-dir <path>] [-force]` | Create IPC. A module name cannot start with a digit. |
| `read_ipc <file>` | Read IPC. |
| `get_ips <module_name ...>` | Get IP objects. |
| `list_property <object>` | List options. |
| `report_property <object>` | Print names, types, and values. Its return string may be empty. |
| `set_property [-dict <args>] <name> <value> <objects>` | Set IP properties such as `CONFIG.Property`. |
| `generate_target <objects> [-force]` | Generate files and add them to the current project. `-force` can overwrite. |
| `write_ip_tcl [-ip_name <newName>] [-multiple_files] [-force] [<tcl_filename>] <objects>` | Export Tcl. Check local help for filename placement; the manual has conflicting examples. |

Save commands, actual configuration, logs, exit status, and output hashes. Generation and project integration are separate actions. Report simulation, synthesis, PnR/STA, and board results only when those checks actually ran. Respect a user request to handle generation themselves.

## Known Tcl pitfalls

- In this version, rejected settings may leave defaults in place while Tcl continues and exits 0. Read the saved IPC; neither exit status nor a printed property table proves the request took effect.
- SDP checks that A/B capacities match. Setting width and depth one at a time previously left a default `2x1` RAM. Setting both geometries together with `set_property -dict` produced the requested `16x65536` RAM.
- `report_property` printed data but returned an empty string in the observed run. No working `get_property` was available there. Read IPC values and interpret enums using the selected IP definition.
- `GowinModGen -h` printed usage with exit code 1. `--help` was treated as a MODFILE and caused `MG2000`. Check text and exit status together.
- Apply runtime/library fixes only to child processes. For a startup problem, use the navigation Skill's [Linux troubleshooting guide](../../gowin-project-navigation/references/gowin-linux-troubleshooting.md).

## Recorded SDP example

This is a historical case, not a default or a newly rerun test:

```tcl
set_property -dict {
    CONFIG.Address_Depth_A 65536
    CONFIG.Address_Depth_B 65536
    CONFIG.Data_Width_A 16
    CONFIG.Data_Width_B 16
    CONFIG.Read_Mode_B Bypass
    CONFIG.Reset_Mode SYNC
} [get_ips ks_delay_ram]
report_property [get_ips ks_delay_ram]
# Check the saved IPC before generate_target.
```

The recorded 2026-10-03 case used `GW5AST-LV138PG484AC1/I0`, revision C, with no audio initialization file. Generation, official-model full-address simulation, and isolated Gowin synthesis passed. It was not integrated into the formal project; PnR/STA and hardware were not checked.

For that configuration, Bypass still had a synchronous one-cycle read; `CEB=0` held the output and `OCE` did not affect it. SYNC reset cleared the output, not RAM contents. Restart needs explicit clear/refill behavior; do not rely on same-address read/write behavior. Verify other configurations against their own templates and models.

Recorded evidence came from a source-machine experiment directory dated 2026-10-03. That directory is not part of this public skill package; the old absolute project path is intentionally not an executable example.

The original records named `README.md`, `generate.tcl`, help/generation/simulation/synthesis logs, a snapshot manifest, and `ks_delay_ram_65536_verified/`. Treat them as evidence for that case only, not as current project paths or authorization.
