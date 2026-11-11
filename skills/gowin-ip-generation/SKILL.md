---
name: gowin-ip-generation
description: Use when generating Gowin FPGA IP or fixing IP generation errors on the local Linux Gowin V1.9.12.04 installation.
---

# Gowin IP Generation

For installation/ABI limits and the distinction between archived and public-package validation, read [Public package](references/public-package.md) before executing this adapter.

Use the bundled script to generate the requested IP. Do not run a full IP scan or a full FPGA build for a simple generation request. Follow the user request and project rules for any later build or integration.

## Quick start

```bash
python3 "${SKILLS_ROOT}/gowin-ip-generation/scripts/gowin_ip.py" generate \
  --ip FFT/fft.ipspec --module my_fft \
  --part GW5AST-LV138PG484AC1/I0 --device-version C \
  --params '{"spinInWidth":18}' \
  --output "${IP_OUTPUT_DIR:?choose a new isolated output directory}"
```

Use a new or empty output folder. Read `status`, `configuration_files`, `files`, and `effective_modules` in the JSON result. Check the actual settings and files before reporting success.

## Look up details only when needed

Use the same script with:

- `list --query FIFO` to find an IP. Use the full `ip` path it returns.
- `describe --ip <ip-path>` to find required input files.
- `inspect --ip <ip-path> --output <new-folder>` to read parameter names, choices, and ranges. Add `--params` to preview dependent settings without generating files.
- `--project <project.gprj>` to read the target device from an existing project. The script does not change that project. Otherwise, pass the exact part and revision. An automatically chosen part is only a test target.

## Key notes

- Supply real coefficient or initialization files with `--input`, such as `--input coefficients=/path/to/coefficients.txt`. The script copies and hashes them. Missing files do not mean an IP is unsupported.
- Set parameters in JSON order: enable features first, then set their dependent values. If Gowin changes a requested value, fix the settings before generating. Do not override reserved output, module, or file-name controls.
- Some IPs, such as EMPU, have fixed top-module names. Omit `--module` and read `effective_modules`. Check the file list instead of requiring a `.vo` with the requested name.
- For SerDes protocols, use a full path such as `SERDES_IP/IPlib/BCDR/bcdr.ipspec`. The script uses the parent SerDes generator.
- Generation, parameter meaning, simulation, synthesis, and place-and-route are separate checks. Exit code 0 or a few output files do not prove all of them passed. A `.vo` extension does not prove encryption.
- A timeout, missing setting, or unresolved dialog does not prove that the IP is unsupported. Read the logs and fix the specific issue.
- This package is tested on the local Gowin version only. It does not cover every device or parameter combination.

Read [notes and limits](references/notes.md) when a call fails. Read [maintenance](references/maintenance.md) only when changing the scripts or rebuilding the bridge.

For an explicit Tcl request or a verified IPFlow configuration, read [Tcl/IPFlow and GowinModGen](references/tcl-ipflow.md). Normal generation uses the bundled script above.
