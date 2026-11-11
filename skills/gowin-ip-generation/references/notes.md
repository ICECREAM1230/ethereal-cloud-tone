# Usage Notes

For installation/ABI limits and the distinction between archived and public-package validation, read [Public package](public-package.md) before executing this adapter.

Tested on Linux x86_64 with `/home/ice/Gowin/V1.9.12.04/IDE`. The script calls Gowin's Qt plugins without desktop interaction. Environment changes apply only to child processes. Some vendor generators run GowinSynthesis to create IP files as part of generation.

## Input files and parameters

| IP | Input names |
|---|---|
| Advanced FIR Filter | `coefficients` |
| EDID PROM | `initialization` |
| Equalizer | `coefficients_a`, `coefficients_b` |
| IIR Filter | `feedforward`, `feedback` |
| PDM2PCM | `coefficients` |

Add an option such as `--input coefficients=/path/to/coefficients.txt`. Use a real, nonempty file. Do not set file-path controls through `--params`; that would bypass file copying and hashing.

- Use JSON booleans and numbers. The string `"false"` is not a boolean.
- Use `fields[].key` for controls with duplicate names. Do not set internal controls such as `qt_spinbox_lineedit`.
- Gowin checks file formats, coefficient counts, and channel settings. The script sets parameters in order and checks live limits. If a later setting changes an earlier value, it reports `PARAMETER_NOT_RETAINED`.
- `--preset smoke` selects a small MIPI test configuration. It does not describe the user's board wiring. Do not use it automatically in a real design.
- Use `--params @/path/to/params.json` for long parameter lists. `run --request job.json` is also available, but is not needed for simple calls.
- Run one request at a time. If another request is active, the script returns `GENERATOR_BUSY`. Wait and retry.
- The script does not overwrite nonempty folders, change the input `.gprj`, add IP to a real project, or run Programmer.

## Result states

| State | What to do |
|---|---|
| `GENERATED` | Generation checks passed. Check that the settings match the user's design. |
| `INPUT_REQUIRED` / `INPUT_FILE_INVALID` | Supply or fix the real input file. |
| `FILE_INPUT_REQUIRES_MAPPING` | Use `--input` instead of a file-path control. |
| `CONFIGURATION_REJECTED` | Read `vendor_message` and fix the settings. |
| `PARAMETER_UNAVAILABLE` / `PARAMETER_NOT_RETAINED` | Check live fields, dependencies, and setting order. Do not silently use defaults. |
| `MODULE_FIXED` | Omit the module name and use the vendor's fixed name. |
| `CATALOG_ENTRY_UNRESOLVED` / `UNHANDLED_DIALOG` | Check the saved schema and logs. The current entry was not handled. |
| `TIMEOUT` | Check whether the generator is still working. Increase `--timeout` if needed. Do not call the IP unsupported. |
| `VENDOR_PROCESS_CRASH` | Keep the logs and partial files. Do not report success. |
| `VENDOR_ENTRY_STUB` | The installed plugin's create function returns without generating. Read the library path, hash, and function evidence. |
| `NO_REGISTERED_SUPPORTED_DEVICE` | Check the installed device list and the requested target. |

Exit codes: 0 means success; 2 means a request or input problem; 3 means generation is not confirmed; 4 means a tool error. Early checks may return only JSON on standard output. Once generation starts, the tool also saves `response.json`, the schema, vendor logs, and IPC files.

`parameters_verified_in_widgets` only checks control values. Check IPC and public HDL for their design meaning. Keep IPC sections separate: a SerDes protocol section must not replace the parent General/Config identity. Fixed names and nested output folders are valid; use the `files` list rather than requiring four matching file names for every IP.

## Known limits

- In the checked local libraries, Digital Pre-Distortion and MIPI CPHY RX have empty create functions, and `isSupported()` returns false. The tool reports `VENDOR_ENTRY_STUB`. This does not describe other Gowin versions or tools.
- Some larger FIR configurations did not finish within the test timeout. Their status remains unconfirmed.
- An early parallel SerDes test crashed after writing files. Serial BCDR tests passed. Keep calls serial; other SerDes protocols are not all verified.
- Tests covered FFT, EDID, MIPI RX/TX, and BCDR. There are also EMPU M1 and CLKDIV generation records. The new package has not retested all 271 IPs, all parameters, or all devices. Old failure records are not a permanent blacklist.
