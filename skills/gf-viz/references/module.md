# Module detail view

Read only when showing a module detail card. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## View 4: Module Detail Card

**Data sources:** `modules/<module_name>.md` (primary), `hierarchy.md`, `fsm.md`, `signals.md`

```
╔══════════════════════════════════════════════════════════╗
║  <module_name>                              <TYPE_BADGE> ║
║  <file_path>:<line_range>                                ║
╠══ PARAMETERS ════════════════════════════════════════════╣
║  ┃ Name     │ Type │ Default     │ Description       ┃  ║
║  ┃ ...      │ ...  │ ...         │ ...               ┃  ║
╠══ PORTS ═════════════════════════════════════════════════╣
║  → <name>     input   <width>   <description>           ║
║  → <name>     input   <width>   <description>           ║
║  ← <name>     output  <width>   <description>           ║
║  ← <name>     output  <width>   <description>           ║
╠══ INTERNALS ═════════════════════════════════════════════╣
║                                                          ║
║  Clock : <clock_name> (<domain info>)                    ║
║  Reset : <reset_name> (<type>)                           ║
║  FSM   : ↻ <fsm_name> → <state_list>                    ║
║  Inst  : <instance_count> (<list or "none (leaf)")>      ║
║                                                          ║
╠══ CONNECTIONS ═══════════════════════════════════════════╣
║                                                          ║
║  Instantiated by:                                        ║
║    ■ <parent_module> as <instance_name>                  ║
║      .<port>(<signal>) .<port>(<signal>)                 ║
║      .<port>(<signal>) .<port>(<signal>)                 ║
║                                                          ║
╠══ HEALTH ════════════════════════════════════════════════╣
║  <✓ or ⚠> port connection status                         ║
║  <✓ or ⚠> lint status                                    ║
║  <✓ or ⚠> assertion coverage                             ║
║                                                          ║
╚══════════════════════════════════════════════════════════╝
  [H] Home  [1] Hierarchy  [2] FSM: <fsm_name>
  [↑] Parent: <parent_module>
  Or: "show ports", "explain the handshake", "add assertions"
```

**Port direction symbols:**
- `→` inputs
- `←` outputs
- `↔` bidirectional (inout)

**Type badges:** `TOP`, `MID`, `LEAF`

**Behaviors:**
- Connections section shows **actual signal bindings** from parent instantiation
- If module is instantiated multiple times, show each instance
- FSM line cross-links to FSM viewer
- Parent name cross-links to parent's detail card
- "add assertions" can hand off to `sv-verification` agent
- "explain the handshake" reads RTL source to reason about protocol

---
