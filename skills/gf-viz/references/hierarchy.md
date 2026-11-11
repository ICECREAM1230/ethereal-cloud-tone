# Hierarchy view

Read only when showing a module hierarchy. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## View 2: Hierarchy Explorer

**Data sources:** `hierarchy.md`, `modules/*.md`

```
══ MODULE HIERARCHY ════════════════════════════════════════

◆ <top_module>                                       TOP
├── ■ <inst> : <module> [PARAM=VAL]                   MID
│   ├── ■ <inst> : <module>                           MID
│   │   ├── □ <inst> : <module>                       LEAF
│   │   └── □ <inst> : <module>                       LEAF
│   └── □ <inst> : <module> [W=32, D=16]              LEAF
└── ■ <inst> : <module>                               MID
    └── □ <inst> : <module>                           LEAF

── STATS ──────────────────────────────────────────────
  Total: N modules │ Max depth: N │ Leaf count: N

── INSTANCE TABLE ─────────────────────────────────────
  ┃ Parent       │ Instance   │ Module          │ Params       ┃
  ┃ ...          │ ...        │ ...             │ ...          ┃

═══════════════════════════════════════════════════════════
  [H] Home  [2] FSMs  [3] Module detail: <name>
  Or: "show <module>", "which modules use <module>?"
```

**Module type badges:**
- `◆` **TOP** - bold, top-level module (never instantiated by others)
- `■` **MID** - standard weight, has children
- `□` **LEAF** - lighter weight, no children

**Depth cues:** Deeper modules rendered with lighter visual weight. Top pops, leaves fade.

**Behaviors:**
- Show full tree with all depth levels
- Parameters shown inline as `[PARAM=VAL]`
- Instance table shows all parent→child relationships
- "show <module>" re-renders tree rooted at that module
- "which modules use <module>?" searches instance table, lists parents

---
