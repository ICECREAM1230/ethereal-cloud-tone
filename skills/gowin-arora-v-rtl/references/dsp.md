# DSP

## Match the device

GW5AST DSPs provide 12 x 12, 27 x 18, and 27 x 36 multiplication modes, a 26-bit pre-adder, and a 48-bit ALU. M0 supports 27 x 18 or 12 x 12; M1 supports 12 x 12. Two DSPs can form a 27 x 36 multiplier. Arithmetic operands are signed. Check mode sharing and available registers before counting resources. See [GW5AST DSP structure](manuals/markdown/DS1239E.md#pdf-page-27).

Check other Arora V parts separately. Use the selected tool's primitive definitions for exact ports, cascade controls, and register modes.

## Structure and mapping

- Use explicit widths and signed types. Pipeline input selection, multiplication, addition, and outputs at the actual long paths.
- Keep CE and reset compatible across registers that should map into the DSP.
- Check existing `syn_preserve`, `syn_keep`, and `syn_dont_touch` attributes. They can prevent register absorption.
- Compare dedicated cascades with balanced, pipelined adder trees.
- Recheck MAC feedback after adding latency. Pipeline the product path without changing the accumulation recurrence.
- Reduce sharing when its muxes or schedule limit performance. Check the cost in DSPs and routing.

For unsigned arithmetic, zero-extend before using signed hardware. Full-width unsigned 27 x 18 needs signed operands of 28 x 19, so it may not fit one native 27 x 18 multiplier. `$signed` changes interpretation; it does not add a zero bit.

`syn_dspstyle` accepts `"dsp"` or `"logic"`. Apply it locally when mapping or timing justifies it. Confirm actual DSP types, counts, and absorbed registers. See [DSP inference](manuals/markdown/SUG550.md#pdf-page-28) and [syn_dspstyle](manuals/markdown/SUG550.md#pdf-page-51).

## Pipeline example

This example accepts input on `ce && in_valid`. All stages hold when CE is low. The result becomes visible after the third enabled edge, including the input edge. The consumer must use `ce && out_valid`. Add real stall storage before adapting it to ready/valid.

```verilog
module arora_signed_mult_pipe (
    input wire clk, rst, ce, in_valid,
    input wire signed [26:0] a,
    input wire signed [17:0] b,
    output wire out_valid,
    output wire signed [44:0] product
);
    reg signed [26:0] a_q = 27'sd0;
    reg signed [17:0] b_q = 18'sd0;
    reg signed [44:0] mult_q = 45'sd0;
    reg signed [44:0] out_q = 45'sd0;
    reg [2:0] valid_q = 3'b000;
    always @(posedge clk) begin
        if (rst) begin
            a_q <= 27'sd0;
            b_q <= 18'sd0;
            mult_q <= 45'sd0;
            out_q <= 45'sd0;
            valid_q <= 3'b000;
        end else if (ce) begin
            a_q <= a;
            b_q <= b;
            mult_q <= a_q * b_q;
            out_q <= mult_q;
            valid_q <= {valid_q[1:0], in_valid};
        end
    end
    assign product = out_q;
    assign out_valid = valid_q[2];
endmodule
```

The example defines behavior; synthesis must confirm the mapping. Test signed limits, CE stalls, bubbles, reset flushing, and output order. Use post-route timing to check DSP outputs, cascades, and feedback.
