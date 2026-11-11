`default_nettype none
module pt8211_tx #(
    // Default: 73.734 MHz / (12*2*32). Native FM: 12.288 MHz / (2*2*32).
    parameter integer BCK_HALF_CYCLES = 12
) (
    input  wire               clk_i,
    input  wire               rst_n_i,
    input  wire signed [15:0] left_i,
    input  wire signed [15:0] right_i,
    output wire               bck_o,
    output wire               ws_o,
    output wire               data_o,
    // Stable before the edge that captures the next stereo pair. Tap users
    // can capture the full PCM24 on this edge alongside the DAC truncation.
    output wire               frame_accept_o,
    output reg                frame_latched_o,
    // Standard I2S: low=left, WS changes one BCK before the next MSB.
    output wire               i2s_ws_o
);
    localparam integer DIV_WIDTH = BCK_HALF_CYCLES > 1 ? $clog2(BCK_HALF_CYCLES) : 1;
    localparam logic [DIV_WIDTH-1:0] HALF_LAST = DIV_WIDTH'(BCK_HALF_CYCLES - 1);
`ifndef SYNTHESIS
    initial if (BCK_HALF_CYCLES < 1)
        $fatal(1, "pt8211_tx BCK_HALF_CYCLES must be positive");
`endif
    reg [DIV_WIDTH-1:0] half_div_r;
    reg       bck_r;
    reg [4:0] bit_index_r;
    reg signed [15:0] left_latched_r, right_latched_r;
    reg ws_r, data_r;
    wire [4:0] next_bit_w = bit_index_r + 1'b1;

    assign bck_o  = bck_r;
    assign ws_o   = ws_r;
    // The same 16-bit serial words also drive PCM5102A (32 BCK/frame).
    assign i2s_ws_o = (bit_index_r < 5'd15) || (bit_index_r == 5'd31);
    assign data_o = data_r;
    assign frame_accept_o = rst_n_i && (half_div_r == HALF_LAST) && bck_r &&
                            (bit_index_r == 5'd31);

    always @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin
            half_div_r      <= '0;
            bck_r           <= 1'b0;
            bit_index_r     <= 5'd31;
            left_latched_r  <= 16'sd0;
            right_latched_r <= 16'sd0;
            ws_r            <= 1'b0;
            data_r          <= 1'b0;
            frame_latched_o <= 1'b0;
        end else begin
            frame_latched_o <= 1'b0;
            if (half_div_r == HALF_LAST) begin
                half_div_r <= '0;
                bck_r <= ~bck_r;
                if (bck_r) begin
                    bit_index_r <= next_bit_w;
                    if (next_bit_w == 5'd0) begin
                        left_latched_r  <= left_i;
                        right_latched_r <= right_i;
                        ws_r            <= 1'b0;
                        data_r          <= right_i[15];
                        frame_latched_o <= 1'b1;
                    end else if (next_bit_w < 5'd16) begin
                        ws_r   <= 1'b0;
                        data_r <= right_latched_r[15-next_bit_w];
                    end else begin
                        ws_r   <= 1'b1;
                        data_r <= left_latched_r[31-next_bit_w];
                    end
                end
            end else begin
                half_div_r <= half_div_r + 1'b1;
            end
        end
    end
endmodule
`default_nettype wire
