# Optional Linux simulation options

Read only for a requested performance, trace, assertion or coverage configuration change. Match flags and capability claims to the selected executable/version/help. These examples do not select defaults or authorize installation; ordinary compile/run uses [Linux simulation](simulation-linux.md).

Use the `verilator_cmd` Bash argument array established by [Linux simulation](simulation-linux.md) for each command below; on a configured Linux host it is `("${OSS_CAD}" verilator)`.

## Verilator v5 Performance Options

### Multi-Threaded Simulation
```bash
"${verilator_cmd[@]}" --binary --threads N -Wall --trace <files> -o sim
```
Use `numactl` to pin to physical cores for best performance.

### Trace Formats
| Format | Flag | Size | Viewers |
|---|---|---|---|
| VCD | `--trace` | Large | Universal |
| FST | `--trace-fst` | Small | GTKWave, Surfer |

Use `--trace-fst` for large designs. Add `--trace-threads 2` to offload FST writing.

### Assertions (SVA)
```bash
"${verilator_cmd[@]}" --binary --assert <files>     # DEFAULT in v5.038+
"${verilator_cmd[@]}" --binary --no-assert <files>  # Disable for performance
```
Supports one-cycle concurrent assert/cover, `$past`, `$stable`, `$rose`, `$fell`.
Does NOT support multi-cycle sequences (SEREs).

### Code Coverage
```bash
"${verilator_cmd[@]}" --binary --coverage <files>        # All coverage
"${verilator_cmd[@]}" --binary --coverage-line <files>   # Line only
"${verilator_cmd[@]}" --binary --coverage-toggle <files> # Toggle only
```

### Maximum Performance
```bash
"${verilator_cmd[@]}" --binary -O3 --x-assign fast --x-initial fast --no-assert --threads N <files>
```


Only use performance options for a requested optimization after checking this installation; do not add packages or disable assertions merely to make a check faster.
