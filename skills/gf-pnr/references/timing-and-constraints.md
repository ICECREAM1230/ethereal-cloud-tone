# Timing and constraint reference

Read only when interpreting PnR results or preparing the selected constraint format. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Timing Analysis

### Fmax
- Maximum frequency from worst-case critical path
- Use `--freq 0` for auto-determine, `--freq N` for target

### Slack
- Positive = timing met | Negative = violation | Zero = exact
- Critical path = path with worst slack

### Improving Fmax
1. Pipeline long combinational paths
2. `--opt-timing` for post-placement optimization
3. Different seeds (`-r`) for better placement
4. Increase `--placer-heap-timingweight` (20-50)
5. `--router2-tmg-ripup` for timing-driven rerouting
6. Yosys: `-nowidelut` for ECP5 (big Fmax improvement)

## Utilization Thresholds

| Usage | Status |
|---|---|
| < 50% | Comfortable |
| 50-70% | Normal |
| 70-80% | Tight |
| > 80% | Warning: congestion likely |
| > 95% | May fail to route |

## Common Failures

| Failure | Fix |
|---|---|
| Routing congestion | Larger device, optimize synth, different seed |
| Timing violations | Pipeline, `--opt-timing`, different seed |
| Placement failure | Check constraints, try `--placer sa` |
| Combinatorial loops | Break with register, or `--ignore-loops` |

## Constraint Formats

### PCF (iCE40)
```
set_io clk 21
set_io led[0] 99
set_frequency clk 48
```

### LPF (ECP5)
```
LOCATE COMP "clk" SITE "P6";
IOBUF PORT "clk" IO_TYPE=LVCMOS33;
FREQUENCY PORT "clk" 48000000.0 HZ;
```

### CST (Gowin)
```
IO_LOC "clk" 52;
IO_PORT "clk" IO_TYPE=LVCMOS33;
```
