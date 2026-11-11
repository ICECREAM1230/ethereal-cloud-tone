# Hierarchy and signals schemas

Read only when documenting hierarchy or signal flow. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### hierarchy.md

```markdown
# Module Hierarchy

## Top Modules
Modules never instantiated by others: [list]

## Hierarchy Tree

\`\`\`mermaid
flowchart TD
    top[hmac]
    top --> core[u_core: hmac_core]
    top --> regs[u_regs: hmac_reg_top]
    core --> sha[u_sha: sha2_multimode]
\`\`\`

## Instance Table
| Parent | Instance | Module | Parameters |
|--------|----------|--------|------------|
| hmac | u_core | hmac_core | - |
| hmac | u_regs | hmac_reg_top | - |
```

### signals.md

```markdown
# Signal Flow Analysis

## Port Summary by Module

### hmac_core
| Port | Dir | Width | Connected To |
|------|-----|-------|--------------|
| clk_i | input | 1 | top.clk |
| data_o | output | [31:0] | regs.wdata |

## Data Flow Diagram

\`\`\`mermaid
flowchart LR
    subgraph Input
        msg[msg_fifo]
    end
    subgraph Core
        pad[sha2_pad]
        hash[sha2_core]
    end
    subgraph Output
        digest[digest_reg]
    end
    msg --> pad --> hash --> digest
\`\`\`

## Unconnected Ports
- [none or list]
```
