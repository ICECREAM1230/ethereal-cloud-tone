`default_nettype none
// One effect worker and one held output slot, independent of source cadence.
// Packet identity is preserved verbatim. Backpressure never overwrites PCM.
module pcm_frame_effect #(
    parameter bit ENABLE_REVERB=1'b1, REVERB_COMPACT=1'b0
)(
    input wire clk_i,rst_n_i,
    input wire [111:0] data_i,
    input wire valid_i,
    output wire ready_o,
    output logic [111:0] data_o,
    output logic valid_o,
    input wire ready_i,
    input wire [15:0] wet_q15_i,damp_q15_i,
    output wire online_o,late_o,clipped_o,
    output wire [127:0] stats_o
);
    logic [63:0] identity;
    logic signed [23:0] left,right;
    logic start,working,online;
    wire signed [23:0] wet_left,wet_right;
    wire core_valid,core_ready,core_late,core_clip;
    logic [31:0] accepted,completed,stalls;
    assign ready_o=online && core_ready && !working && !valid_o;
    assign online_o=online;
    assign late_o=core_late;
    assign clipped_o=core_clip;
    reverb_audio_stage #(.ENABLE(ENABLE_REVERB),.RUNTIME_PARAMS(1'b1),.COMPACT(REVERB_COMPACT),.INPUT_DELAY_CYCLES(1)) u_effect(
        .clk(clk_i),.rst_n(rst_n_i),.sample_tick(start),
        .wet_q15(wet_q15_i),.damp_q15(damp_q15_i),.pcm_l_in(left),.pcm_r_in(right),
        .pcm_l_out(wet_left),.pcm_r_out(wet_right),.out_valid(core_valid),
        .ready(core_ready),.late(core_late),.clipped(core_clip));
    always_ff @(posedge clk_i or negedge rst_n_i)begin
        if(!rst_n_i)begin
            identity<=0;left<=0;right<=0;start<=0;working<=0;online<=0;
            data_o<=0;valid_o<=0;accepted<=0;completed<=0;stalls<=0;
        end else begin
            start<=0;
            if(core_ready)online<=1;
            if(valid_o && ready_i)valid_o<=0;
            if(valid_i && ready_o)begin
                identity<=data_i[111:48];left<=$signed(data_i[47:24]);right<=$signed(data_i[23:0]);
                start<=1;working<=1;accepted<=accepted+32'd1;
            end
            if(valid_o && !ready_i && stalls!=32'hffffffff)stalls<=stalls+32'd1;
            if(core_valid)begin
                data_o<={identity,wet_left,wet_right};valid_o<=1;working<=0;completed<=completed+32'd1;
            end
        end
    end
    assign stats_o={accepted,completed,stalls,26'd0,core_clip,core_late,valid_o,working,core_ready,online};
// synthesis translate_off
`ifndef SYNTHESIS
    always @(posedge clk_i)if(rst_n_i && core_valid && valid_o && !ready_i)
        $fatal(1,"PCM worker output overwritten");
`endif
// synthesis translate_on
endmodule
`default_nettype wire
