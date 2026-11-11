// A frame request starts the existing local slot schedule. The compute
// clock continues running between frames; no sample-rate clock is created.
`default_nettype none
module pcm_request_drum #(
    parameter integer CYCLES = synth_pkg::DRUM_CYCLES,
    parameter integer NUM_LANES = synth_pkg::NUM_ELEMENTS
) (
    input wire clk_i, rst_n_i,
    input wire request_valid_i,
    input wire [63:0] request_tag_i,
    output wire request_ready_o,
    output wire [63:0] frame_tag_o,
    output wire [9:0] slot_o,
    output wire sample_tick_o, lane_enter_o
);
    localparam logic [9:0] LAST_SLOT = 10'(CYCLES-1);
    logic running;
    logic [9:0] slot_r;
    logic [63:0] tag_r;
    // Assert without a clock; qualify requests after one local release edge.
    // Keep the high-fanout raw reset out of the slot/accept data path.
    logic request_qualified;
    always_ff @(posedge clk_i or negedge rst_n_i)
        if (!rst_n_i) request_qualified <= 1'b0;
        else request_qualified <= 1'b1;
    wire accept = request_valid_i && request_ready_o;
    assign request_ready_o = request_qualified && !running;
    // Present slot zero on the accepting edge, then slots 1..CYCLES-1.
    assign slot_o = accept ? 10'd0 : slot_r;
    assign sample_tick_o = accept;
    assign lane_enter_o = (accept || running) && (slot_o < NUM_LANES);
    assign frame_tag_o = accept ? request_tag_i : tag_r;
    always_ff @(posedge clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin running<=1'b0;slot_r<=LAST_SLOT;tag_r<=64'd0;end
        else if (accept) begin running<=1'b1;slot_r<=10'd1;tag_r<=request_tag_i;end
        else if (running) begin
            if (slot_r==LAST_SLOT) running<=1'b0;
            else slot_r<=slot_r+10'd1;
        end
    end
// synthesis translate_off
`ifndef SYNTHESIS
    initial begin
        assert(CYCLES>=2 && CYCLES<=1024 && NUM_LANES>=1 && NUM_LANES<=CYCLES)
            else $fatal(1,"invalid PCM request drum schedule");
    end
`endif
// synthesis translate_on
endmodule
`default_nettype wire
