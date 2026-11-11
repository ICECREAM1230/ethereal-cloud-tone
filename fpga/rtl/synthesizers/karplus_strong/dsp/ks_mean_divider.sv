// Background-only restoring divider. Negative quotients round toward -infinity.
`default_nettype none
module ks_mean_divider (
    input wire clk_i, rst_n_i, start_i,
    input wire signed [31:0] dividend_i,
    input wire [14:0] divisor_i,
    output reg busy_o, done_o,
    output reg signed [31:0] quotient_o
);
    reg [31:0] magnitude, quotient;
    reg [15:0] remainder;
    reg [14:0] divisor;
    reg negative;
    reg [5:0] count;
    wire [16:0] trial = {remainder, magnitude[31]};
    wire subtract = trial >= {2'b0, divisor};
    wire [15:0] next_remainder = 16'(subtract ? trial - {2'b0, divisor} : trial);
    wire [31:0] next_quotient = {quotient[30:0], subtract};
    always @(posedge clk_i) begin
        if (!rst_n_i) begin
            busy_o <= 0; done_o <= 0; quotient_o <= 0;
            magnitude <= 0; quotient <= 0; remainder <= 0;
            divisor <= 1; negative <= 0; count <= 0;
        end else begin
            done_o <= 0;
            if (start_i && !busy_o) begin
                magnitude <= dividend_i[31] ? -dividend_i : dividend_i;
                negative <= dividend_i[31]; divisor <= divisor_i;
                quotient <= 0; remainder <= 0; count <= 32; busy_o <= 1;
            end else if (busy_o) begin
                magnitude <= {magnitude[30:0], 1'b0};
                remainder <= next_remainder; quotient <= next_quotient;
                count <= count - 1'b1;
                if (count == 1) begin
                    busy_o <= 0; done_o <= 1;
                    quotient_o <= negative ? -$signed(next_quotient) - (next_remainder != 0) : $signed(next_quotient);
                end
            end
        end
    end
endmodule
`default_nettype wire
