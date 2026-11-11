# Warning reference

Read only when the actual log contains a diagnostic needing explanation. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### 4. Explain Common Warnings

For each warning type found, explain:

| Warning | Meaning | Fix |
|---------|---------|-----|
| UNUSED | Signal declared but not used | Remove signal or suppress with `/* verilator lint_off UNUSED */` |
| UNDRIVEN | Signal never assigned a value | Assign the signal or connect it |
| WIDTH | Bit width mismatch in operation | Add explicit sizing: `[7:0]` |
| CASEINCOMPLETE | Case statement missing items | Add `default:` branch |
| LATCH | Inferred latch from incomplete if/case | Add default assignment at start of always_comb |
| BLKSEQ | Blocking assignment in always_ff | Use `<=` (non-blocking) in sequential blocks |
| PINCONNECTEMPTY | Module port left unconnected | Connect or explicitly mark as `.*` |
| IMPLICIT | Implicit wire declaration | Declare with `logic` or `wire` |

## Verilator v5 New Warnings

### Lint Warnings (enabled by -Wall)
| Code | Description |
|---|---|
| ASSIGNEQEXPR | `=` used inside expression (may mean `==`) |
| ALWNEVER | `always @*` has empty event list |
| ALWCOMBORDER | Variable set after use in `always_comb` |
| IMPLICITSTATIC | Task/function variable implicitly static |
| WIDTHEXPAND | Value is zero-expanded |
| WIDTHTRUNC | Value is truncated |
| ENUMVALUE | Enum assigned incompatible type |
| CASEOVERLAP | Case values overlap |
| CASTCONST | `$cast` always succeeds or fails |
| CMPCONST | Comparison always yields constant |
| MISINDENT | Misleading indentation |
| MULTIDRIVEN | Signal driven by multiple always blocks |
| SYNCASYNCNET | Signal mixed sync/async resets |
| INFINITELOOP | Loop condition always true |
| SELRANGE | Selection index out of bounds |

### Style Warnings (enabled by -Wall)
| Code | Description |
|---|---|
| ASCRANGE | Packed vector with ascending range `[0:7]` |
| DECLFILENAME | Module name doesn't match filename |
| DEFPARAM | Deprecated `defparam` |
| GENUNNAMED | Generate block unnamed |
| IMPORTSTAR | `import pkg::*` in `$unit` scope |

### v5 Behavior Changes
- WIDTH is now a meta-category: disabling WIDTH disables WIDTHEXPAND + WIDTHTRUNC
- `--assert` enabled by default since v5.038
- `--lint-only` now implies `--timing`
