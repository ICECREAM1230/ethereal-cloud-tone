# Handoff schemas

Read only when delegating bounded work or a consumer requires a structured handoff. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Handoff Protocol

### To Invoke a Skill:
```
Invoke the named skill:
  skill: "<skill-name>"
  args: "<context>"
```

### To Invoke an Agent:
```
Delegate to an available worker:
  description: "<brief description>"
  worker_role: "<agent-name>"
  prompt: |
    ## Task
    [Clear task description]

    ## Context
    - Original request: [query]
    - User preferences: [from expand mode]
    - Relevant files: [paths]

    ## Constraints
    [Any constraints]

    ## Expected Output
    [What to deliver]
```

---

## Handoff Context Schema

```json
{
  "original_query": "User's exact words",
  "interpreted_intent": "CREATE_RTL|DEBUG|etc",
  "confidence": 0.85,
  "expand_mode": {
    "questions": ["Q1", "Q2"],
    "answers": ["A1", "A2"],
    "selected_option": "A"
  },
  "user_preferences": {
    "scope": "single_module|multi_file",
    "verification": "none|lint|full",
    "style": "minimal|comprehensive"
  },
  "files": {
    "relevant": ["path/to/file.sv"],
    "codebase_map": ".gateflow/map/CODEBASE.md"
  },
  "constraints": {
    "must_lint": true,
    "must_simulate": false
  },
  "return_conditions": {
    "on_complete": "report_to_user",
    "on_error": "report_error",
    "on_clarification": "return_to_router"
  }
}
```

---

## Return Status Handling

After target completes, expect return in format:

```
---GATEFLOW-RETURN---
STATUS: complete|needs_clarification|error|handoff
SUMMARY: [Brief summary]
FILES_CREATED: [list]
NEXT_TARGET: [if handoff]
---END-GATEFLOW-RETURN---
```

| Status | Action |
|--------|--------|
| complete | Report success to user |
| needs_clarification | Re-enter expand mode |
| error | Report error, suggest fixes |
| handoff | Chain to next target |

---
