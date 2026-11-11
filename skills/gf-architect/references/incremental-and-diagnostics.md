# Map metadata and diagnostics

Read only when refreshing a saved map or diagnosing mapping gaps. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Save Metadata After Mapping

```bash
# Save commit hash
git rev-parse HEAD > .gateflow/map/.last_scan_commit

# Save timestamp
date -u +"%Y-%m-%dT%H:%M:%SZ" > .gateflow/map/.last_scan

# Save file hashes for non-git change detection
find . -name "*.sv" -o -name "*.svh" | xargs md5 > .gateflow/map/.file_hashes
```

---

## Incremental Update Mode

When updating existing map:

1. Get changed files: `git diff --name-only <last_commit> HEAD -- "*.sv"`
2. Spawn subagents ONLY for changed file groups
3. Merge new analysis with existing map files
4. Update frontmatter timestamps
5. Regenerate affected diagrams

---

## Quality Warnings

Report in CODEBASE.md under "## Warnings":
- Inferred latches (always without default)
- Missing resets on sequential logic
- Unconnected ports
- CDC crossings without synchronizers
- Unused signals/parameters
- Missing top module

---

## Delegation Budget Reference

Keep each delegated work item within the platform's available context and token budget. Split very large files into focused, read-only analysis units and reserve enough context for synthesis. Do not require a named model; inherit the user's current model/settings unless the user explicitly chooses otherwise.

---

## Troubleshooting

**File too large for single agent:**
- Use targeted text search to extract structure first
- Read focused line ranges with the capabilities available in the current host
- Assign dedicated subagent

**Too many files:**
- Increase subagent count
- Focus on RTL, skip testbenches
- Use glob patterns to filter

**No git available:**
- Fall back to file hash comparison
- Store .file_hashes for change detection

---

## Verilator JSON Mode

When Verilator is available, use `--json-only` for more accurate mapping:
```bash
verilator --json-only --json-only-output design.tree.json --no-json-edit-nums -Wall <files>.sv
```

Key AST nodes: MODULE (modules), VAR (signals with ioDirection), CELL (instances), ASSIGNW/ASSIGNDLY (connections). Benefits over regex: resolves parameters/generates, captures elaborated hierarchy, typed width info. Falls back to regex if Verilator unavailable.

## Complexity Metrics

Add to each module page and CODEBASE.md summary:

| Metric | What It Measures |
|---|---|
| CC (Cyclomatic) | Decision points in combinational logic |
| FSM_STATES | Number of FSM states |
| ALWAYS | Number of always blocks |
| PORTS | Port count |
| INSTANCES | Submodule count |
| SLOC | Non-blank non-comment lines |

Composite: `(CC*3) + (FSM_STATES*2) + ALWAYS + (PORTS/5) + (INSTANCES*2)`. Rating: 1-10 Low, 11-30 Medium, 31-60 High, 61+ Critical.

## Signal Tracing

Build directed connectivity graph (nodes=signals, edges=assignments+port bindings). BFS forward from source, BFS backward from destination. Output trace path with module crossings and pipeline stage markers.

## Diff-Aware Mapping

After each map, save snapshot. On next map, compare module-level: ADDED/MODIFIED/REMOVED with specific change type (port, instance, FSM, parameter). Output to `.gateflow/map/CHANGES.md`.

## Dependency Graph Output

Generate `.gateflow/map/dependencies.md` with Mermaid: solid arrows for instantiation, dashed for imports, subgraphs for clock domains, classDef for top/mid/leaf/package.
