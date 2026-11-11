# Clock-domain map schema

Read only when documenting clocks, resets or CDC. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### clock-domains.md

```markdown
# Clock Domain Analysis

## Clocks Detected
| Clock | Modules |
|-------|---------|
| clk_i | all |

## Resets Detected
| Reset | Type | Modules |
|-------|------|---------|
| rst_ni | async active-low | all |

## Clock Domain Map

\`\`\`mermaid
flowchart LR
    subgraph clk_i_domain["clk_i domain"]
        core[hmac_core]
        regs[hmac_reg_top]
    end
\`\`\`

## CDC Crossings
| Source | Dest | Signal | Sync Type |
|--------|------|--------|-----------|
| [none or list] |
```
