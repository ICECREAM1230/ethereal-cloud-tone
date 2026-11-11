# Executing an existing RTL plan

Read only when implementation of an existing plan is authorized. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Check for Existing Plan

```bash
ls .gateflow/plans/*.md 2>/dev/null
```

Existing plans are context. Execute only with current implementation authorization.

## Check for Codebase Map

For codebase-wide tasks:
```bash
ls .gateflow/map/CODEBASE.md 2>/dev/null
```

If missing and needed, invoke `$gf-architect` first.

---

## Quick Reference

### Common Lint Fixes

| Warning | Fix |
|---------|-----|
| UNUSED | Remove or `/* verilator lint_off UNUSED */` |
| WIDTH | Add explicit sizing: `[7:0]` |
| CASEINCOMPLETE | Add `default:` |
| LATCH | Add default assignment |
| BLKSEQ | Use `<=` in `always_ff` |

---

## Execution from Plan

When implementation of an existing `$gf-plan` plan is authorized:

```
1. Read .gateflow/plans/<name>.md
2. For each phase:
   a. Run the appropriate responsibility
   b. Run verification specified
   c. If issues, run the `sv-refactor` responsibility
   d. Update progress
3. Report completion
```

---

## Tools Available

- Use filesystem search, file reading, editing, shell execution, and collaboration only through capabilities actually exposed by the current Codex session.
- Prefer `rg`/`rg --files` when available; use the matching host-call leaf for path and quoting rules.
- **Skill**: Invoke skills:
  - `gf-lint` - Lint with structured output
  - `gf-sim` - Simulation with structured output
  - `gf-plan` - Planning for complex tasks
  - `gf-architect` - Codebase mapping
- **Gowin official flow**: `gw_sh`/Tcl for an existing `.gprj`; discover the installed path and verify with `gw_sh -h` before relying on version-sensitive options
- **Vendor-model functional simulation**: Verilator first after checking actual model compatibility; ModelSim/Questa (`vsim`/`vlog`) second through the project-specific adapter when required
- **User clarification**: ask before delegating only when requirements or trade-offs are unclear

---
