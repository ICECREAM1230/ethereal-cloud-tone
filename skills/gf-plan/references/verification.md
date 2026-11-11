# Verification planning

Read only when selecting checks and coverage for the planned behavior. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

### Verification Strategy

```markdown
## Verification Strategy

### Unit Tests (per module)
| Module | Test Focus | Coverage Goal |
|--------|------------|---------------|
| dma_channel | FSM transitions, counter | 100% state, 90% transition |
| dma_arbiter | Fairness, priority | All grant patterns |
| dma_axi_master | Protocol compliance | AXI assertions pass |

### Integration Tests
| Test | Description | Pass Criteria |
|------|-------------|---------------|
| basic_xfer | Single descriptor transfer | Data matches |
| chain_xfer | Linked descriptor chain | All descriptors complete |
| multi_ch | Multiple channels active | Fair arbitration |
| error_inject | Invalid descriptor | Error flag, no hang |

### Assertions
| Property | Module | Type |
|----------|--------|------|
| AXI handshake | axi_master | Protocol |
| No descriptor overrun | channel | Safety |
| FSM no deadlock | channel | Liveness |
| FIFO no overflow | engine | Safety |

### Coverage Goals
- Line coverage: >95%
- Branch coverage: >90%
- FSM state coverage: 100%
- FSM transition coverage: >95%
- Functional coverage: >98%
```

---
