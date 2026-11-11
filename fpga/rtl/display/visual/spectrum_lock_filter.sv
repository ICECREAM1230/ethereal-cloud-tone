`default_nettype none
// Continuous video-PLL LOCK qualification in the board 50 MHz clock domain.
// This block resets the video path only; it must not reset the audio path.
module spectrum_lock_filter #(
    parameter integer HOLD_CYCLES = 100000 // >= 1; 2 ms at 50 MHz
) (
    input  wire clk_i,
    input  wire rst_n_i,
    input  wire lock_i,
    output wire qualified_o
);
    localparam integer COUNT_WIDTH = (HOLD_CYCLES <= 1) ? 1 : $clog2(HOLD_CYCLES);
    localparam [COUNT_WIDTH-1:0] LAST_COUNT = HOLD_CYCLES - 1;
    wire async_reset_n = rst_n_i & lock_i;

    // Asynchronous assertion also catches LOCK lows between clock edges.
    // The last stage releases the counter reset synchronously. Explicit zero
    // initial values prevent startup qualification when LOCK is already high.
    (* async_reg = "true", syn_preserve = 1 *) reg [2:0] lock_sync = 3'b000;
    always @(posedge clk_i or negedge async_reset_n) begin
        if (!async_reset_n) lock_sync <= 3'b000;
        else                lock_sync <= {lock_sync[1:0], 1'b1};
    end

    reg [COUNT_WIDTH-1:0] stable_count = {COUNT_WIDTH{1'b0}};
    reg qualified = 1'b0;
    always @(posedge clk_i or negedge lock_sync[2]) begin
        if (!lock_sync[2]) begin
            stable_count <= {COUNT_WIDTH{1'b0}};
            qualified <= 1'b0;
        end else if (!qualified) begin
            if (stable_count == LAST_COUNT) qualified <= 1'b1;
            else                           stable_count <= stable_count + 1'b1;
        end
    end
    assign qualified_o = qualified;
endmodule
`default_nettype wire
