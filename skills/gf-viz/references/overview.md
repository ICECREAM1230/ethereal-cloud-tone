# Overview view

Read only when the user requests a dashboard or gives no specific view. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## View 1: Overview Dashboard

**Data sources:** `CODEBASE.md` (stats, module index), `hierarchy.md` (tree), `fsm.md` (FSM list), `clock-domains.md` (clocks/CDC)

Read these files, extract the data, and render:

```
╔══ CODEBASE: <project_name> ═══════════════════════════════╗
║                                                            ║
║  ● N modules  ● N packages  ● N FSMs  ● N interfaces       ║
║  ● N clocks   ● N CDC       ● N ports   ● N warnings       ║
║                                                            ║
╠══ HIERARCHY (compact) ════════════════════════════════════╣
║                                                            ║
║  ◆ <top> ──┬── <child1> ──┬── <grandchild1>               ║
║            │              └── <grandchild2>                ║
║            └── <child2> ── <grandchild3>                   ║
║                                                            ║
╠══ FSMs ═══════════════════════════════════════════════════╣
║  ↻ <fsm_name> (<module>)  N states: S1→S2→S3→S4           ║
║  ↻ <fsm_name> (<module>)  N states: S1→S2→S3              ║
║                                                            ║
╠══ HEALTH ═════════════════════════════════════════════════╣
║  <✓ or ⚠> lint status  <✓ or ⚠> undriven  <✓ or ⚠> CDC   ║
║                                                            ║
╠═══════════════════════════════════════════════════════════╣
║  [1] Hierarchy  [2] FSMs  [3] Module detail                ║
║  Or ask anything: "show uart_tx", "trace data path"        ║
╚═══════════════════════════════════════════════════════════╝
```

**Rules:**
- Hierarchy flattened to **2 levels max** in dashboard
- FSMs show **one-line summaries** with state chain
- Stats pulled from CODEBASE.md frontmatter and module index
- Health pulled from warnings section
- If any section has no data, show "none detected" rather than omitting

---
