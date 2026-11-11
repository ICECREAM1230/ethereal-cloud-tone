# Topics and grading

Read only when topic/difficulty selection, grading, challenge mode or persistent progress is requested. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Exercise Templates

### Beginner: 4-bit Counter
```
Create a 4-bit counter with:
- Synchronous reset
- Enable signal
- Wrap-around at max value
```

### Intermediate: Sync FIFO
```
Create a synchronous FIFO with:
- Parameterized WIDTH and DEPTH
- Full and empty flags
- No overflow/underflow
```

### Advanced: AXI-Lite Slave
```
Create an AXI-Lite slave with:
- 4 read/write registers
- Proper handshaking
- Address decoding
```

## Key Rules

1. **ALWAYS wait for user** after presenting exercises
2. **NEVER show solution** unless explicitly asked with `$gf-learn solution`
3. **Track progress** in `.gateflow/learn/progress.json`
4. **Encourage** - learning is hard, be supportive

## Difficulty Scaling

| Level | Score Range | Criteria |
|---|---|---|
| Beginner | 0-99 | Single always block, basic signals |
| Intermediate | 100-299 | Multi-block, FSMs, parameterization |
| Advanced | 300-499 | Multi-clock, protocols, optimization |
| Expert | 500+ | Full subsystems, cross-cutting concerns |

Advancement: +30 no hints, +20 one hint, +10 two+ hints, +10 lint-clean bonus, -10 solution revealed.

## Grading Rubric

| Grade | Criteria |
|---|---|
| A (Excellent) | Lint-clean, correct reset, parameterized, assertions included |
| B (Good) | Correct, minor lint warnings, reasonable naming |
| C (Acceptable) | Correct for basic cases, multiple warnings, hardcoded values |
| D (Needs Work) | Functional errors, missing reset, width mismatches |

Automated checks: lint (20%), functional correctness (40%), style (15%), parameterization (10%), edge cases (10%), assertions (5%).

## Progress Persistence

Store in `.gateflow/learn/progress.json`:
```json
{"user_level": "intermediate", "total_score": 185, "topics": {"basics": {"level": "intermediate", "score": 90, "exercises_completed": 3}}}
```

## Challenge Mode

`$gf-learn challenge <topic>` -- timed exercises with scoring.

| Difficulty | Time Limit | Bonus Threshold |
|---|---|---|
| Beginner | 15 min | 8 min (2x points) |
| Intermediate | 25 min | 15 min (2x points) |
| Advanced | 40 min | 25 min (2x points) |
