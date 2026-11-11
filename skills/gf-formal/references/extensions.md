# Advanced formal checks

Read only when equivalence, advanced engines or multiclock proof detail is requested. The skill entrypoint and shared execution contract govern scope; examples are conditional on the current request.

## Formal Extensions

| Directive | Purpose |
|---|---|
| `assert(expr)` | Must always be true |
| `assume(expr)` | Constrain solver inputs |
| `cover(expr)` | Reachability target |

| Attribute | Behavior |
|---|---|
| `(* anyconst *)` | Solver picks constant |
| `(* anyseq *)` | Solver picks per-cycle |

```systemverilog
`ifdef FORMAL
    initial assume(rst);
    always @(posedge clk) begin
        a_example: assert(count <= MAX);
        c_reach: cover(count == MAX);
    end
`endif
```
