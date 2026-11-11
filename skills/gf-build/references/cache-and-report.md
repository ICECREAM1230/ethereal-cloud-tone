# Build cache and reporting

Read only when incremental caching or structured progress/reporting is required. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Incremental Build

Create or update `.gateflow/cache/hashes.json` only during authorized implementation and only when the cache key includes the complete source/dependency hashes plus relevant top, includes, defines, parameters, tool/version, and command/options. Otherwise treat prior entries as advisory and rerun the check.
```json
{"rtl/alu.sv": {"sha256": "a1b2...", "last_lint": "PASS", "last_sim": "PASS"}}
```

Decision: if hash unchanged AND all dependency hashes unchanged -> skip (cache hit). Otherwise re-run.

Dependency-aware invalidation: when pkg.sv changes, invalidate all importers.

---

## Build Cache Structure

```
.gateflow/cache/
  hashes.json          # hash -> result mapping
  deps.json            # dependency graph
  lint/<file>.lint     # cached lint output
  sim/<tb>/result.json # cached sim result
```

---

## Progress Visualization

```
[Phase 0] Setup         [====================] DONE  0:04
[Phase 1] Components    [==========>         ] 62%   0:30
  alu.sv                [====================] DONE
  regfile.sv            [====================] DONE
  decoder.sv            [=========>          ] 55%
[Phase 2] Dependent     [                    ] WAIT
```

---

## Return Format

```
---GATEFLOW-RETURN---
STATUS: complete
SUMMARY: Built [design] with [N] components in [M] parallel phases
PARALLELISM: [N workers or direct execution; omit any speedup claim unless measured against a comparable sequential run]
FILES_CREATED:
  - rtl/pkg.sv
  - rtl/alu.sv
  - rtl/regfile.sv
  - rtl/cpu.sv
  - tb/tb_cpu.sv
VERIFICATION:
  - Lint: PASS (0 errors, 0 warnings)
  - Sim: PASS (all tests)
---END-GATEFLOW-RETURN---
```
