# Clarification examples

Read only when a consequential unknown requires one focused question. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Expand Mode Workflow

When confidence is 0.70-0.85, activate expand mode:

### Step 1: Acknowledge
```
I'd like to help you with [summary]. Let me ask a few questions to deliver exactly what you need.
```

### Step 2: Ask Clarifying Questions (2-3 max)

**For Creation Tasks:**
1. Scope: "Single module or part of larger system?"
2. Interface: "What protocol? (AXI, valid/ready, custom)"
3. Verification: "Include testbench? (yes/no)"

**For Debug Tasks:**
1. Symptom: "What exactly do you see?"
2. Expected: "What should happen instead?"
3. Context: "Any recent changes?"

**For Planning Tasks:**
1. Constraints: "Area/timing/power requirements?"
2. Integration: "Connecting to existing code?"
3. Verification: "What level of verification?"

### Step 3: Present Options

```markdown
Based on your answers, here are your options:

## Option A: [Name]
**Approach:** [Description]
**Pros:** [List]
**Cons:** [List]

## Option B: [Name]
**Approach:** [Description]
**Pros:** [List]
**Cons:** [List]

## Option C: Quick Start
**Approach:** I'll use reasonable defaults and proceed
**Best for:** Exploration, prototyping

Which approach? (A/B/C)
```

### Step 4: Build Handoff Context

After user selects, build context including:
- Original query
- Clarification responses
- Selected option
- Inferred constraints
- Expected outputs

---
