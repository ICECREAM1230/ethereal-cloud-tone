# FSM view

Read only when showing states, transitions or a state diagram. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

Source precedence: derive states, transitions, reset and outputs from current active RTL; existing map pages are optional inputs only after freshness is checked. The Data sources and map-output wording below describes the saved-map case. A request naming one FSM or asking only for a transition table needs no picker, overview or map creation.

## View 3: FSM Viewer

**Data sources:** `fsm.md`, per-module pages

**When multiple FSMs exist, show picker first:**

```
══ STATE MACHINES ══════════════════════════════════════════

  [1] ↻ <fsm_name>  (<module>)   N states
  [2] ↻ <fsm_name>  (<module>)   N states
  [3] ↻ <fsm_name>  (<module>)   N states

  Pick a number, or: "show <fsm_name>"
```

**Single FSM rendering:**

```
══ FSM: <fsm_name> ═════════════════════════════════════════
   Module: <module> │ Encoding: N-bit │ Reset: → <reset_state>

                    <condition>
   ┌──────┐  ─────────────►  ┌───────┐
   │      │                  │       │
   │  S1  │                  │  S2   │
   │  ◉   │                  │       │
   └──────┘                  └───┬───┘
      ▲                          │ <condition>
      │                          ▼
   ┌──────┐                  ┌───────┐
   │      │  ◄───────────   │       │──┐
   │  S4  │   <condition>   │  S3   │  │ <self-loop cond>
   │      │                  │       │◄─┘
   └──────┘                  └───────┘

── TRANSITIONS ────────────────────────────────────────────
  ┃ From  │ To    │ Condition     │ Output          ┃
  ┃ S1    │ S2    │ ...           │ ...             ┃
  ┃ S2    │ S3    │ ...           │ ...             ┃
  ┃ ...   │ ...   │ ...           │ ...             ┃

── STATE DETAILS ──────────────────────────────────────────
  ◉ S1   Reset state. <description>
    S2   <description>
    S3   <description>
    S4   <description>

═══════════════════════════════════════════════════════════
  [H] Home  [1] Hierarchy  [3] Module: <parent_module>
  Or: "show another FSM", "explain the S2→S3 transition"
```

**Layout rules for FSM box diagrams:**
- **2-4 states:** Arrange in a line or L-shape
- **4-6 states:** Arrange in a 2x2 or 2x3 grid
- **7+ states:** Use transition table only (too complex for ASCII boxes)
- Reset state always marked with `◉`
- Self-loops shown as `──┐` / `◄─┘` back to same box
- Transition arrows use `──►` with condition labels

**Behaviors:**
- "explain <from>→<to> transition" triggers analysis using RTL source
- "show module" cross-links to parent module's detail card
- Output column in transition table filled when map data includes it

---
