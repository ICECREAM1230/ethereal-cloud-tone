# Option comparison examples

Read only when a real design tradeoff needs user selection. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### Step 3: Present Options with Trade-offs

Based on user answers, present 2-3 implementation options:

```markdown
Based on your answers, here are your options:

## Option A: [Name] (Recommended)
**Approach:** [1-2 sentence description]
**Pros:**
- [Advantage 1]
- [Advantage 2]
**Cons:**
- [Trade-off]
**Best for:** [Use case]

## Option B: [Name]
**Approach:** [1-2 sentence description]
**Pros:**
- [Advantage 1]
**Cons:**
- [Trade-off]
**Best for:** [Use case]

## Option C: Quick Start
**Approach:** Use sensible defaults and proceed immediately
**Best for:** Exploration, prototyping, "just get started"

Which approach would you like? (A/B/C)
```

### Step 4: Build Enriched Handoff Context

After user selects option, build context:

```json
{
  "original_query": "User's original words",
  "clarifications": {
    "questions": ["Q1", "Q2"],
    "answers": ["A1", "A2"]
  },
  "selected_option": "A",
  "resolved_intent": "CREATE_RTL",
  "specifications": {
    "interface": "valid_ready",
    "include_testbench": true,
    "component_type": "fifo"
  },
  "constraints": ["Must be synthesizable", "Lint clean"]
}
```
