# Exercise workflow

Read only when generating an exercise or reviewing a submitted solution. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Usage

```
$gf-learn                    # Start learning session, pick topic
$gf-learn <topic>            # Get exercises on specific topic
$gf-learn check <file>       # Submit solution for review
$gf-learn hint               # Get hint for current exercise
$gf-learn solution           # Show solution (gives up)
```

## Topics

| Topic | Exercises |
|-------|-----------|
| `basics` | Signals, always blocks, assignments |
| `fsm` | State machines, encodings, transitions |
| `fifo` | Synchronous FIFOs, pointers, full/empty |
| `pipeline` | Valid/ready, backpressure, stages |
| `cdc` | Clock domain crossing, synchronizers |
| `arbiter` | Round-robin, priority, grant logic |
| `memory` | RAMs, ROMs, register files |
| `protocol` | AXI-lite, Wishbone, handshakes |
| `verification` | Assertions, SVA properties, coverage |
| `optimization` | Timing closure, resource sharing, pipelining tradeoffs |

## Workflow

### Step 1: Generate Exercise

When user invokes `$gf-learn <topic>`:

1. Present 3-5 exercises of increasing difficulty
2. Format each exercise as:

```markdown
## Exercise X: <Title>

**Difficulty:** Beginner/Intermediate/Advanced

**Requirements:**
- [ ] Requirement 1
- [ ] Requirement 2
- [ ] Requirement 3

**Interface:**
```systemverilog
module exercise_name (
    input  logic clk,
    input  logic rst_n,
    // ... define ports
);
```

**Test Cases:**
1. When X happens, Y should occur
2. Edge case: ...

**Starter File:** `exercises/exercise_X.sv`
```

3. Create starter file in `exercises/` directory
4. **STOP and wait for user to attempt solution**

### Step 2: Wait for User

After presenting exercises, say:

```
Your turn! Edit the file and invoke `$gf-learn check exercises/exercise_X.sv` when ready.

Need help? Invoke `$gf-learn hint` for a hint.
```

**DO NOT proceed until the user submits with `$gf-learn check`**

### Step 3: Review Solution

When user invokes `$gf-learn check <file>`:

1. Delegate to an available worker with role `sv-tutor` to review, when delegation is useful
2. Present feedback without giving away answers
3. If solution passes, offer next exercise

### Step 4: Hints (on request)

When user invokes `$gf-learn hint`:

1. Give progressive hints (hint 1 is vague, hint 3 is specific)
2. Track hint count per exercise
3. Never give full solution in hints
