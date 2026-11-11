# Inspecting Gowin project evidence

Read this for manifest/result discrepancies, saved process options, or scanner use. For extensions and directory roles use [the artifact map](project-artifact-map.md); read only the relevant section.

## Source-of-truth rules

- Project membership and enablement: `.gprj`.
- Generated synthesis/PnR/bitstream process settings: `impl/*_process_config.json` or `impl/process_config.json`. Use them to report saved tool options, not to reconstruct project membership or infer unrecorded design intent. They are generated, version-sensitive and may be incomplete for that purpose.
- Tool/UI flow state and result aliases: `.gprj.user`; verify that the target file still exists.
- Synthesis options and generated synthesis input: the `.prj` under `impl/gwsynthesis/` and the synthesis report. Treat this as generated data.
- Timing intent: the user `.sdc`, then the timing report under `impl/pnr/`.
- Physical intent: the user `.cst`, then the PnR pin/constraint report.
- Final implementation status: PnR report, timing report, power report, and bitstream metadata together. A `.fs` or `.bin` file alone does not prove timing closure.
- IP-generated `.prj`/`.vg` files under `src/**/temp/**` are generated IP artifacts, not necessarily the top-level synthesis project or final netlist.
- A project may contain disabled files in `FileList`; an existing file is not automatically active in the build. Check `enable="1"`.
- A `.gprj.user` result path can be stale, and a result file can exist without being registered in that file. Check both directions.
- Process configuration keys may be case-sensitive in practice or may appear with different casing in the same JSON file. Preserve exact spelling and report conflicting case variants instead of silently merging them.
- An encrypted file is not necessarily an IP core: it can be user HDL, a Gowin IP source, or a packed design file. Confirm its role from `FileList`, generated wrappers, and the producing flow.
- If a `.vp/.vhdp/.vgp` file is referenced but missing, report a missing protected dependency rather than trying to infer its internal RTL.

## Read-only scanner

Use the scanner when the tree is unclear or a manifest/result inventory is needed. For invocation, read only the matching [Ubuntu](project-scan-ubuntu.md) or [Windows](project-scan-windows.md) guide.

Select one project explicitly and request JSON only when machine-readable output is needed. The scanners are read-only and report project roots, device declarations when present, the `.gprj` file list, registered `.gprj.user` results, simulation directories, synthesis/PnR directories, process configuration files and fields, case-variant key groups, and representative generated artifacts. Complex process-config values are retained as raw JSON; `ParseStatus` says whether top-level parsing succeeded. Sensitive process-config fields are redacted in both text and JSON output.

An explicitly selected project must be inside the supplied directory root. Inventory stops at descendant directories containing a separate `.gprj` and reports them in `NestedProjects`; their process options, counts and generated outputs are not attributed to the parent. Explicit parent `FileList` references into those directories are still reported with their original enablement and existence. Projects sharing the same directory cannot be separated by directory ownership alone; use their manifest/result references to establish provenance.

`DevicePartNumber`, `DeviceName` and `DeviceIdentifier` retain `Device`'s `pn` attribute, `name` attribute and XML text respectively. `Device` falls back in that order. An internal identifier in XML text does not establish a full package/speed-grade part number; do not infer a `create_project -pn` value from it.

Redaction decodes escaped JSON keys and checks all nested objects/arrays, preserving duplicate keys during inspection. A top-level value containing a sensitive key anywhere inside is replaced by `<redacted>`. Malformed or uninspectable JSON emits no partial fields; never reproduce parser input fragments that might contain credentials.

The scanners parse XML, normalize the observed `version="1"` declaration in memory, reject unsafe entity declarations, and never rewrite the project. Malformed XML produces an error; do not fall back to regex and report partial extraction as a valid manifest. Generated settings retain raw JSON values and report parsing status. Do not reproduce secrets from raw process files in an answer.
