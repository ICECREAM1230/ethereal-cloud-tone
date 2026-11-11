# Saved map data

Read only when reading an existing map rather than deriving a view from current RTL. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Data Extraction

### Reading CODEBASE.md

Extract from frontmatter:
- `total_files`, `total_tokens`, `commit`, `last_mapped`

Extract from Module Index table:
- Module names, types, files, port summaries

Extract from Warnings section:
- Lint warnings, undriven signals, CDC issues

### Reading hierarchy.md

Extract from Mermaid flowchart:
- Parent→child relationships
- Instance names

Extract from Instance Table:
- Full parent, instance, module, parameters data

### Reading fsm.md

Extract for each FSM:
- FSM name, parent module
- State list with encoding
- Transition table (from, to, condition, output)
- Reset state

### Reading modules/*.md

Extract per module:
- Parameters table
- Ports table (name, direction, width, description)
- Clock/reset info
- Instance list
- Assertion/coverage info

---
