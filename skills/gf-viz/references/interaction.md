# Navigation and edge cases

Read only when menus, navigation, emphasis or missing-map behavior need detail. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Color/Emphasis Vocabulary

Apply these consistently across all views:

| Element | Symbol | Style |
|---------|--------|-------|
| Top module | `◆` | **Bold** |
| Mid module | `■` | Standard |
| Leaf module | `□` | Light |
| Input port | `→` | Green emphasis |
| Output port | `←` | Yellow emphasis |
| Bidir port | `↔` | Cyan emphasis |
| FSM indicator | `↻` | Standard |
| Reset state | `◉` | **Bold/highlighted** |
| Clean/pass | `✓` | Green |
| Warning | `⚠` | Yellow/amber |
| Info/stat | `●` | Standard |
| Transition | `──►` | Standard |

**Depth cues in hierarchy:** Top-level bold, mid standard, leaf dimmed.

---

## Interaction Model

### Menu Navigation

After every render, show a navigation footer with numbered options:
- `[H]` Home - return to dashboard
- `[1]` `[2]` `[3]` - switch between views
- `[↑]` Parent - navigate up in hierarchy (detail card only)

### Free-Form Queries

Always accept natural language alongside menus:
- "show <module_name>" → Module Detail Card
- "show <fsm_name>" → FSM Viewer for that FSM
- "which modules use <module>?" → filtered hierarchy
- "trace <signal> from <module_a> to <module_b>" → signal path analysis
- "explain <aspect>" → reads RTL source, reasons about it
- "add assertions to <module>" → hands off to sv-verification agent
- "back" → previous view
- "home" → dashboard

### Agent Handoff

For queries that go beyond visualization:
- "explain" / "why" → delegate to a worker with role `sv-understanding` when useful
- "add assertions" → delegate to a worker with role `sv-verification` when useful
- "fix" / "refactor" → delegate to a worker with role `sv-refactor` when useful

When handing off, pass the current visualization context (which module, which view) so the agent has full context.

---

## Auto-Trigger After `$gf-architect`

When used as a final step in `gf-architect`, render ONLY the Overview Dashboard (View 1) as a compact summary. Do not show the full interactive menu - just the dashboard with a note:

```
Run `$gf-viz` to explore interactively.
```

---

## Edge Cases

- **Empty map:** "No codebase map found. Run `$gf-architect` first."
- **No FSMs detected:** FSM section shows "No state machines detected in this codebase."
- **Single module:** Hierarchy view shows just the one module. Skip instance table.
- **Module not found:** "Module '<name>' not found in map. Available modules: <list>"
- **Very deep hierarchy (>6 levels):** Render full tree but note: "Deep hierarchy detected. Use 'show <module>' to focus on a subtree."
- **Very wide hierarchy (>10 siblings):** Show first 8, then "... and N more. Use 'show <parent>' to see all."

---
