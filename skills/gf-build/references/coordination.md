# Coordination and recovery

Read only when scheduling dependencies, shared outputs or failure recovery. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Parallelism Rules

1. **Same phase = parallel** - Components with no dependencies spawn together
2. **Different phase = sequential** - Wait for previous phase to complete
3. **Lint = batch** - Run on all files at once, or parallel per file
4. **Fix = parallel** - Delegate only independent fixes when useful
5. **Sim = parallel** - Each testbench can run independently

---

## Error Recovery

| Error | Action |
|-------|--------|
| Agent timeout | Retry once, then ask user |
| Lint failure | Run the `sv-refactor` responsibility, re-lint |
| Sim failure | Run the `sv-debug` responsibility, then sv-refactor |
| Integration mismatch | Check interfaces, fix manually |
| 2 consecutive failures | Ask user for guidance |

---

## Dependency Graph Algorithm

Use Kahn's algorithm (BFS topological sort) to identify parallel phases:
1. Build adjacency list and in-degree for each component
2. Initialize queue with all in-degree=0 components
3. Drain queue level by level -- each level is a parallel phase
4. If any component still has in_degree>0 after all levels: cycle detected

---

## Resource Contention Rules

| Rule | Description |
|---|---|
| One writer per file | Each file owned by exactly one agent |
| Write-before-read | Writers in earlier phase than readers |
| Shared package pattern | Shared types go to pkg.sv in Phase 0 |
| No implicit deps | Agent prompts list files to CREATE, READ, and NEVER MODIFY |

---
