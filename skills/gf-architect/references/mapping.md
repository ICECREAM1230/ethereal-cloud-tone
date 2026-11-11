# Mapping workflow

Read only when scanning or mapping multiple design units. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Workflow

### Step 1: Check for Existing Map

```bash
ls .gateflow/map/CODEBASE.md 2>/dev/null
```

**If exists:** Check for changes since last map:
```bash
# Read last commit from metadata
last_commit=$(cat .gateflow/map/.last_scan_commit 2>/dev/null)
git diff --name-only $last_commit HEAD -- "*.sv" "*.svh" 2>/dev/null
```
- If no changes: "Map is up to date"
- If changes: Proceed with incremental update (only remap changed files)

**If not exists:** Proceed to full mapping.

### Step 2: Scan Codebase & Token Budgeting

```bash
mkdir -p .gateflow/map/modules
```

**Scan files with token counts:**
```bash
find . \( -name "*.sv" -o -name "*.svh" \) -not -path "./.gateflow/*" | while read f; do
  tokens=$(wc -c < "$f" | awk '{print int($1/4)}')
  echo "$tokens $f"
done | sort -rn
```

**Build assignment table:**
| File | Tokens | Assignment |
|------|--------|------------|
| top.sv | 50000 | Agent 1 |
| uart_tx.sv | 8000 | Agent 1 |
| hmac_core.sv | 120000 | Separate bounded analysis (LARGE - use targeted search) |

### Step 3: Handle Large Files (>80k tokens)

**For files exceeding 80k tokens, use chunked analysis:**

1. **Use targeted text search (prefer `rg`) to extract structure** before reading the full file:
```bash
# Get module declaration
grep -n "^\s*module\s" large_file.sv

# Get ports
grep -n "(input|output|inout)" large_file.sv

# Get instances
grep -n "^\s*\w\+\s\+\w\+\s*(" large_file.sv
```

2. **Read focused sections** using the host's available line-range or chunking mechanism:
```
Read the first section for the header and ports.
Read later sections around the declarations and processes found by search.
... continue until covered
```

3. **When delegation is available, authorized, and worthwhile, assign a bounded file analysis with a search-first strategy**

### Step 4: Spawn Parallel Subagents

When delegation is available, dispatch independent work items in parallel.

Use the platform's available delegation mechanism with a read-only analysis role. Do not hard-code a provider-specific tool name or model.

**Example - spawn 3 agents in ONE message:**

```
Work item 1:
  description: "Analyze UART files"
  prompt: |
    Read and analyze these SystemVerilog files:
    - rtl/uart_pkg.sv
    - rtl/uart_tx.sv
    - rtl/uart_rx.sv

    For EACH file, extract:
    1. Module/Package name
    2. Purpose (one-line)
    3. Ports table: name, direction, width
    4. Parameters: name, type, default
    5. Instances: what it instantiates
    6. FSM states (if any)
    7. Clock/Reset signals
    8. Package imports

    Return structured markdown.

Work item 2:
  description: "Analyze SHA files"
  prompt: |
    Read and analyze these SystemVerilog files:
    - rtl/sha2_pad.sv
    - rtl/sha2_core.sv

    [Same extraction request...]

Work item 3:
  description: "Analyze a large file with targeted search"
  prompt: |
    This file is large. Use targeted text search to extract structure first:
    - rtl/hmac_core.sv (120k tokens)

    1. Search for module declarations
    2. Search for ports
    3. Search for instances
    4. Read specific sections if needed

    Return structured markdown.
```

### Step 5: Synthesize Reports

After all subagents complete:

1. **Merge** all reports
2. **Build hierarchy** from instance data
3. **Create diagrams** (Mermaid)
4. **Identify cross-cutting concerns** (clocks, CDC)
5. **Write output files**

---
