# Learning progress

Read only when the user explicitly requests persistent progress or spaced repetition. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Concept Tracking

Use the current conversation/project context by default. Read or update `~/.gateflow/profile.json` only when it already exists and the user explicitly requests persistent learning progress; never create a global profile as a side effect of an ordinary task.

```json
{
  "concepts_introduced": ["cdc", "fsm", "pipeline", "fifo", "axi"]
}
```

If concept is in the list, skip the explanation.
When persistent learning was explicitly requested, add the concept to that existing profile after explaining.

## Spaced Repetition

| Sessions Since Last Use | Action |
|---|---|
| 3-5 | Brief reminder (1 sentence) |
| 6-10 | Medium reminder (2 sentences + link) |
| 11+ | Full re-explanation |

## Integration Hooks

Inject micro-lessons at these points in `$gf` orchestration:
- Post-codegen: scan generated code for new concepts
- Post-ip-add: explain the concept behind the IP block
- Post-lint-fail: teach concept if error involves one (e.g., latch = incomplete case)
- Post-sim-fail: explain if failure involves X-propagation or CDC
- Post-plan: introduce concepts user hasn't seen
