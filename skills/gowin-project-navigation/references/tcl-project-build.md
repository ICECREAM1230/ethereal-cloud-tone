# Gowin Project and Build Tcl Syntax

Use this reference for `gw_sh` and Gowin Software Tcl that changes or builds a project. It assumes Arora V by default, but the selected `.gprj` remains authoritative for the full part number, device version, top, enabled files, and implementation state. SDC, CST, and HDL/GSC synthesis attributes are documented separately.

Sources: `SUG1220` Chapters 2–4 and `SUG100` Chapter 4; check the editions bundled with the installed Gowin version before relying on version-sensitive options. `<...>` is required and `[...]` is optional. Examples use paths relative to the working directory; resolve them before execution. For host-specific invocation, read only the matching [Ubuntu](gowin-build-ubuntu.md) or [Windows](gowin-build-windows.md) guide. For Linux build failures and observed Tcl version differences, read the relevant stage in [Linux troubleshooting](gowin-linux-troubleshooting.md).

## Project lifecycle

| Syntax | Effect and prerequisites | Minimal example |
|---|---|---|
| `source <file>` | Execute a Tcl script. Commands inside it retain their own project prerequisites. | `source scripts/build.tcl` |
| `create_project [-name <name>] [-dir <path>] [-pn <part>] [-device_version <value>] [-force]` | Create a project. `-force` can overwrite an existing project. | `create_project -name demo -dir demo -pn <full_part> -device_version <value>` |
| `open_project <file> ...` | Open a `.gprj`. Check the installed manual or local help for optional arguments. | `open_project demo/demo.gprj` |
| `set_device [-device_version <value>] <part_number>` | Set the target device. Re-run synthesis and PnR after changing it. | `set_device -device_version <value> <full_part>` |
| `saveto [-all_options] <file>` | Export the current project as reconstructive Tcl. It does not replace source and constraint archival. | `saveto -all_options demo/export.tcl` |
| `run close` | Close the current project. There is no documented `close_project`. | `run close` |

Treat `create_project -force`, device changes, and overwrite options as state-changing operations. Do not run them against an unresolved path or project.

## Files and constraints

For module-based SDC/CST sources and build-time merging, read [modular constraints with pure Tcl](modular-constraints.md). That workflow keeps an explicit fragment manifest and enables one merged file per constraint type; creating those files alone does not change the active project or its startup hooks.

| Syntax | Effect and cautions |
|---|---|
| `add_file [-type <type>] [-disable] <file ...>` | Add files to the project. Manual-listed types include `verilog`, `vhdl`, `sv`, `vg`, `cst`, `sdc`, `gao`, `gpa`, and `gsc`; these are version-sensitive. Linux V1.9.12.04 rejects `-type sv`: use `-type verilog` and `-verilog_std sysv2017` for the verified SystemVerilog flow. Normally only the intended file of each constraint type should be enabled. |
| `rm_file <files>` | Remove files from the project list. The Tcl manual does not confirm disk deletion; never use it as a file-delete command. |
| `set_file_enable <file> <true|false>` | Enable or disable a project file. Disabled files do not participate in implementation. |
| `set_file_prop <file> [-lib <name>]` | Set file properties; `-lib` applies to VHDL. The manual's example and syntax disagree on argument order, so check `set_file_prop -h`. |
| `set_csr <file>` | Select a CSR file. |
| `import_files [-file <file ...>] [-dir <path>] [-fileList <list>] [-force]` | Copy files into project `src`. `-force` overwrites name collisions. With no options, copy files previously named by `add_file`. |

```tcl
add_file -type verilog rtl/top.sv
set_option -verilog_std sysv2017
add_file -type cst constraints/top.cst
add_file -type sdc constraints/top.sdc
set_file_enable top.sdc true
```

## Project and implementation options

Syntax: `set_option [options]`. Re-run the affected stage after changing an option. Never derive a Tcl option from a GUI label.

### Global and synthesis options

| Option | Values and effect |
|---|---|
| `-output_base_name <name>` | Output basename. |
| `-global_freq <default|value>` | Global frequency. The Arora V documented default is 100 MHz; explicit SDC clocks can override it. |
| `-synthesis_tool <value>` | Select the synthesis engine using the installed help/project value. The manual spelling `GowinSynthesis` is rejected by Linux V1.9.12.04; the verified new project retains its actual default `GowinSyn`. Do not insert the rejected spelling into a build script. |
| `-top_module <name>` | Select the top module/entity. |
| `-opt_goal <auto|area|timing>` | Optimization goal; default `auto`. |
| `-include_path {<path1>;<path2>}` | Include-path list. |
| `-verilog_std <v1995|v2001|sysv2017>` | Verilog/SystemVerilog standard; default `v2001`. |
| `-vhdl_std <vhd1993|vhd2008|vhd2019>` | VHDL standard; default `vhd1993`. |
| `-print_all_synthesis_warning <0|1>` | Print all synthesis warnings; default `0`. |
| `-disable_io_insertion <0|1>` | `1` disables automatic I/O insertion; default `0`. |
| `-netlist_hierarchy <0|1>` | `0` flattens, `1` preserves hierarchy; default `1`. This changes post-synthesis object names. |
| `-looplimit <N>` | RTL loop limit; default `2000`. |
| `-max_fanout <N>` | Synthesis fanout target; default `1000`. |
| `-rw_check_on_ram <0|1>` | Add RAM same-address read/write bypass logic; default `0`. |
| `-dsp_style <auto|logic|dsp>` | DSP inference style. |
| `-ram_style <auto|block_ram|distributed_ram|registers>` | RAM inference style. |
| `-rom_style <auto|block_rom|distributed_rom|logic>` | ROM inference style. |
| `-shift_reg <auto|block_ram|distributed_ram|registers>` | Shift-register implementation style. |
| `-map_option <1|2|3|4>` | LUT mapping mode; higher modes may spend resources for timing. Default `1`. |

### Place, route, and report options

| Option | Values and effect |
|---|---|
| `-timing_driven <0|1>` | Timing-driven PnR; default `1`. |
| `-cst_warn_to_error <0|1>` | Promote physical-constraint warnings to errors; default `1`. |
| `-place_option <0|1|2|3|4>` | Placement algorithm; default `0`. |
| `-route_option <0|1|2>` | Routing algorithm; default `0`. |
| `-ireg_in_iob <0|1>` / `-oreg_in_iob <0|1>` / `-ioreg_in_iob <0|1>` | Place input/output/bidirectional registers in IOBs; default `1`. |
| `-replicate_resources <0|1>` | Replicate high-fanout resources; default `0`. Enable only with path evidence. |
| `-serdesRetiming <0|1>` | SerDes-related retiming; preserve documented capitalization. |
| `-co-place_io_registers <0|1>` | Co-place input/output registers. |
| `-clock_route_order <0|1>` | Order non-primitive clock routing by fanout or frequency. |
| `-route_maxfan <1..100>` | Backend fanout optimization target. |
| `-correct_hold_violation <0|1>` | Automatic hold repair; default `1`. |
| `-convert_sdp32_36_to_sdp16_18 <0|1>` | Arora V: split 32/36-bit SDPB/SDPX9B; default `0`. |
| `-inc_place <0|auto|file>` / `-inc_pnr <0|auto|file>` | Incremental implementation. The file-mode path syntax is incomplete in the manual; check local help. |
| `-gen_sdf <0|1>` | Generate `.sdf`. |
| `-gen_io_cst <0|1>` | Generate `.io.cst`. |
| `-gen_text_timing_rpt <0|1>` | Manual-listed text timing report option. Linux V1.9.12.04 rejects it as `unknown option` (isolated probe, 2026-10-08). Omit this optional switch for that version, or catch only this expected unsupported-option failure and record it; still inspect the actual timing reports. Verify support before using it on another version. |
| `-gen_verilog_sim_netlist <0|1>` / `-gen_vhdl_sim_netlist <0|1>` | Generate PnR simulation netlist. |
| `-rpt_auto_place_io_info <0|1>` | Report automatically placed I/O. The manual example names the wrong option; use this syntax. |

### Configuration pins and bitstream risk

```tcl
set_option -use_jtag_as_gpio <0|1>
set_option -use_sspi_as_gpio <0|1>
set_option -use_mspi_as_gpio <0|1>
set_option -use_ready_as_gpio <0|1>
set_option -use_done_as_gpio <0|1>
set_option -use_reconfign_as_gpio <0|1>
set_option -use_i2c_as_gpio <0|1>
```

These options can disable programming, debugging, or configuration paths. Check board connections and the programming method before enabling them.

| Option | Effect or risk |
|---|---|
| `-bit_format <txt|bin>` | Bitstream format. |
| `-bit_crc_check <0|1>` | CRC; default `1`. |
| `-bit_compress <0|1>` | Compression; default `1`. |
| `-bit_encrypt <0|1>` / `-bit_encrypt_key <key>` | Arora bitstream encryption. Do not place keys in public scripts. |
| `-bit_security <0|1>` | Security bit; default `1`. |
| `-bit_incl_bsram_init <0|1>` | Include BSRAM initialization; default `1`. |
| `-secure_mode <0|1>` | High risk: may repurpose JTAG and make a bitstream one-time programmable. Never enable without explicit authorization. |
| `-multi_boot <0|1>` plus `-multiboot_address_width`, `-multiboot_spi_flash_address`, `-multiboot_mode` | Multi-boot configuration. Values must match the flash and boot design. |
| `-mspi_jump <0|1>` plus `-merge_jumpbit`, `-mspijump_address_width`, `-mspijump_spi_flash_address`, `-mspijump_mode` | MSPI JUMP configuration. |
| `-unused_pin <default|open_drain>` | Changes unused-pin electrical behavior; do not set merely to suppress warnings. |

Query SUG1220 §3.3.20 only when the task explicitly needs SEU, background programming, external CCLK, or power-related bitstream options.

## Build execution

Syntax: `run <syn|pnr|all>`.

- `run syn`: synthesis.
- `run pnr`: place and route; requires a valid, current synthesis result.
- `run all`: complete implementation. Successful PnR produces the bitstream and implementation reports.
- The manual does not confirm `run clean`, `run stop`, or `run bitstream`; do not invent them.

```tcl
open_project demo/demo.gprj
set_device -device_version <value> <full_part>
set_option -top_module top
set_option -opt_goal timing
set_option -timing_driven 1
run all
saveto -all_options demo/export.tcl
run close
```

## IP generation

Use [gowin-ip-generation](../../gowin-ip-generation/SKILL.md) for IP creation, parameter changes, and regeneration. Its [Tcl/IPFlow reference](../../gowin-ip-generation/references/tcl-ipflow.md) owns the IP commands and saved-configuration checks. This guide covers project and build Tcl.

## Before execution

- Resolve device, top, and enabled files from the selected `.gprj`, not directory names.
- Keep only the intended `.cst`, `.sdc`, and `.gsc` enabled.
- Treat `-force`, `import_files`, configuration-pin reuse, encryption, and security options as side effects.
- For syntax conflicts noted above, run `<command> -h` and check the original PDF before mutation.
