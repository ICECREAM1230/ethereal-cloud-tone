# Local Gowin documentation index

Check `IDE/doc/EN/` or `IDE/doc/CN/` under the selected Gowin installation. A workspace copy such as `Gowin软件参考资料/EN/` is a fallback, not a universal path. Search by document number if the installed version differs.

The index below routes questions to locally observed Gowin V1.9.12.04 PDFs. It does not claim that every option applies to every device or software version. For device-resource questions, require an exact part number and consult the matching family guide.

## Core software and design-flow manuals

| Document | Use for |
|---|---|
| `SUG100-4.4.7E_Gowin Software User Guide.pdf` | GUI, project creation/open/edit/manage, project files, project-file encryption, process view, archive/restore, output files and simulation files; especially sections 4.3.2, 4, 6, 7 and 8 |
| `SUG113-1.3E_Gowin FPGA Design User Guide.pdf` | Overall design planning, design constraint flow, timing closure and report analysis |
| `SUG1220-2.1E_Gowin Software Tcl Commands User Guide.pdf` | `encrypt` (section 3.3.2), `create_project`, `open_project`, `add_file`, `import_files`, `run`, `set_file_prop`, IP/project automation and Tcl execution |
| `SUG501-1.6E_Gowin Software Quick Installation Guide.pdf` | Installation and setup only |
| `SUG918-2.0E_Gowin Software Quick Start Guide.pdf` | End-to-end first project and basic flow |
| `RN791-1.0E_ Gowin Software License Update Release Note.pdf` | License/update history; not a project-format authority |

## Constraints, synthesis and analysis

| Document | Use for |
|---|---|
| `SUG1541-1.0E_Gowin Software Physical Constraints User Guide.pdf` | FloorPlanner, CST creation/editing, I/O and primitive/group/resource/clock constraints |
| `SUG940-2.0.5E_Gowin Design Timing Constraints User Guide.pdf` | SDC, clock/generated-clock/I/O/exception constraints and STA report structure |
| `SUG550-2.4E_GowinSynthesis User Guide.pdf` | GowinSynthesis inputs/outputs, generated `.prj`/netlist/report, synthesis options, `.gsc`/attribute constraints and inference constraints |
| `SUG282-2.6.1E_Gowin Power Analyzer User Guide.pdf` | Power-analysis inputs, flow and reports |
| `SUG114-3.5E_Gowin Analyzer Oscilloscope User Guide.pdf` | `.rao`/`.gao`, RTL/post-synthesis capture and waveform export; chapter 5 documents `gao_sh` command-line capture |
| `SUG1189-1.4.3E_Gowin Virtual Input Output User Guide.pdf` | `.gvio` configuration and debug dependencies; section 4.1.1 describes a GUI executable, not a verified headless read/write interface |
| `SUG755-1.2.2E_Gowin HDL Schematic Viewer User Guide.pdf` | HDL/netlist schematic viewing |
| `SUG937-1.1E_Gowin Software User Messages Reference.pdf` | Gowin software messages and error/warning lookup |

For programming automation, locate `Programmer/doc/EN/SUG502-2.5.2E_Gowin Programmer User Guide.pdf` under the selected installation; chapter 4 covers `programmer_cli`. Verify its own version/help, device names and operation numbers. See [CLI tools and Tcl orchestration](gowin-tool-automation.md) for Linux observations and execution boundaries.

## HDL and primitive references

| Document | Use for |
|---|---|
| `SUG949-2.2E_Gowin HDL Coding User Guide.pdf` | Gowin-oriented HDL coding, inference and simulation/synthesis mismatch guidance |
| `SUG283-3.6.1E_Gowin Primitives User Guide.pdf` | Primitive names and semantics; confirm device support |
| `TN735-1.0E_GowinSynthesis LUT5 and LUT6 Inference Application Note.pdf` | LUT5/LUT6 inference details |

## Device-specific manuals

The directory also contains BSRAM, clock, DSP, CFU, GPIO, User Flash, ADC and MIPI guides for Gowin and Arora V families. These are intentionally not treated as generic project-structure authorities. Do not use one to answer a part-specific resource question until the part number and family are known.

## Citation convention

Use `Document number/version — section title` in answers and provide a clickable local file link in the host-required format. Select the revision matching the installed tool and device; a newer PDF is not automatically applicable to an older tool. If a generated artifact disagrees with a manual, report the observed artifact and version difference. Read only the manual and sections needed for the current question.
