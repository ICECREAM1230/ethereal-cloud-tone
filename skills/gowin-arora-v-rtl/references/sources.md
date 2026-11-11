# Manual lookup

The public package contains document locators and version/hash indices only. The official PDFs and complete Markdown conversions are not bundled. Obtain the matching edition lawfully from the selected Gowin installation's `IDE/doc` or the official links in `manuals/manifest.json`; set `GOWIN_MANUAL_DIR` to an existing external directory containing `ID.pdf` or `pdf/ID.pdf`. Optional external conversions go in `markdown/ID.md`. No download, installation or copying is performed automatically. The locator anchors below retain the original PDF page references. Keep the selected part and tool version in scope.

| Topic | Document locator | Recorded edition |
|---|---|---|
| Structure and timing optimization | SUG113: [RTL](manuals/markdown/SUG113.md#pdf-page-10), [timing](manuals/markdown/SUG113.md#pdf-page-21) | 1.3 |
| Synthesis inference and attributes | SUG550: [inference](manuals/markdown/SUG550.md#pdf-page-14), [attributes](manuals/markdown/SUG550.md#pdf-page-39) | 2.4 |
| SDC syntax and STA reports | SUG940: [commands](manuals/markdown/SUG940.md#pdf-page-63), [reports](manuals/markdown/SUG940.md#pdf-page-46) | 2.0.5 |
| HDL inference patterns | [SUG949E](manuals/markdown/SUG949E.md#pdf-page-11) | 2.2E |
| BSRAM and SSRAM | UG300E: [BSRAM](manuals/markdown/UG300E.md#pdf-page-12), [SSRAM](manuals/markdown/UG300E.md#pdf-page-54) | 1.3.6E |
| CFU, carry chains, and registers | UG303E: [CFU](manuals/markdown/UG303E.md#pdf-page-11), [registers](manuals/markdown/UG303E.md#pdf-page-34) | 1.0E |
| Clock networks, DCE/DCS, and PLLs | UG306E: [clocks](manuals/markdown/UG306E.md#pdf-page-12), [DCE](manuals/markdown/UG306E.md#pdf-page-19), [DCS](manuals/markdown/UG306E.md#pdf-page-22), [PLL](manuals/markdown/UG306E.md#pdf-page-43) | 1.0.9E |
| GW5AST resources and DSP structure | DS1239E: [resources](manuals/markdown/DS1239E.md#pdf-page-11), [DSP](manuals/markdown/DS1239E.md#pdf-page-27) | 1.0.3E |

After configuring `GOWIN_MANUAL_DIR`, run from this skill directory to read a short page range:

```bash
python3 scripts/read_manual.py --list
python3 scripts/read_manual.py SUG940 --pages 53-54 --verify
python3 scripts/read_manual.py SUG940 --pdf-pages 64
```

`--pages` uses printed body page numbers. `--pdf-pages` uses PDF page numbers. An independently available matching external Markdown conversion is preferred; otherwise the reader requires `pdftotext` and reads the external PDF. `--pdf-text` forces PDF extraction. `--verify` checks the PDF and any conversion actually used against the recorded hashes. Missing external documents are reported explicitly, without treating a locator as manual text.

External conversion preserves text, not every table or diagram layout. Inspect the matching PDF visually when layout matters. Official URLs may change edition; four of the eight endpoints were fetched on 2026-11-12 with matching edition/page metadata, while the SUG113/SUG940/UG300E lookups timed out and SUG550 returned 403; these failures are recorded in the manifest and do not establish removal. Detailed DSP primitives, GPIO/IODELAY, full tool Tcl, or other parts may need additional matching documentation.
