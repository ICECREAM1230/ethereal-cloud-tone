`default_nettype none
// One selected source, one low-frequency effect, original source output rate.
// The ordinary/FМ input adapters differ only in their source publication delay.
module pcm_shared_pipeline(
    input wire global_rst_n_i,audio_clk_i,audio_rst_n_i,
    input wire subtractive_clk_i,synth_rst_n_i,synth_tick_i,synth_mute_i,synth_bypass_i,
    input wire signed [23:0] synth_left_i,synth_right_i,
    input wire fm_clk_i,fm_rst_n_i,fm_tick_i,fm_mute_i,
    input wire signed [23:0] fm_left_i,fm_right_i,
    input wire route_fm_i,
    input wire [15:0] wet_q15_i,damp_q15_i,
    output wire signed [23:0] synth_left_o,synth_right_o,fm_left_o,fm_right_o,
    output wire synth_valid_o,fm_valid_o,synth_ready_o,fm_ready_o,
    output wire synth_late_o,fm_late_o,synth_clipped_o,fm_clipped_o,
    output wire [127:0] synth_stats_o,fm_stats_o,audio_stats_o
);
    logic previous_route;
    always_ff @(posedge subtractive_clk_i or negedge synth_rst_n_i)
        if(!synth_rst_n_i)previous_route<=0;else previous_route<=route_fm_i;
    wire reset_request_n=global_rst_n_i && audio_rst_n_i && synth_rst_n_i && !synth_bypass_i &&
                         (previous_route==route_fm_i) && (!route_fm_i || fm_rst_n_i);
    wire audio_reset_n;
    jt51_reset_sync u_audio_reset(.clk_i(audio_clk_i),.rst_n_i(reset_request_n),.rst_n_o(audio_reset_n));
    (* ASYNC_REG="TRUE" *)logic route_meta,route_sync;
    always_ff @(posedge audio_clk_i or negedge audio_reset_n)
        if(!audio_reset_n)begin route_meta<=0;route_sync<=0;end
        else begin route_meta<=route_fm_i;route_sync<=route_meta;end
    wire online,worker_ready,worker_valid,worker_late,worker_clip;
    wire [111:0] synth_packet,fm_packet,result;
    wire synth_packet_valid,fm_packet_valid,synth_return_ready,fm_return_ready;
    wire synth_link_reset_n=audio_reset_n && online && !route_fm_i;
    wire fm_link_reset_n=audio_reset_n && online && route_fm_i && fm_rst_n_i;
    wire signed [23:0] synth_left,synth_right,fm_left,fm_right;
    wire synth_valid,fm_valid;
    pcm_frame_link #(.INPUT_DELAY_CYCLES(3)) u_synth_link(
        .global_rst_n_i(global_rst_n_i),.link_reset_n_i(synth_link_reset_n),
        .src_clk_i(subtractive_clk_i),.dst_clk_i(audio_clk_i),.sample_tick_i(synth_tick_i),
        .left_i(synth_left_i),.right_i(synth_right_i),.left_o(synth_left),.right_o(synth_right),
        .valid_o(synth_valid),.online_o(synth_ready_o),.stats_o(synth_stats_o),
        .worker_data_o(synth_packet),.worker_valid_o(synth_packet_valid),.worker_ready_i(worker_ready && !route_sync),
        .result_data_i(result),.result_valid_i(worker_valid && !route_sync),.result_ready_o(synth_return_ready));
    pcm_frame_link #(.INPUT_DELAY_CYCLES(1)) u_fm_link(
        .global_rst_n_i(global_rst_n_i),.link_reset_n_i(fm_link_reset_n),
        .src_clk_i(fm_clk_i),.dst_clk_i(audio_clk_i),.sample_tick_i(fm_tick_i),
        .left_i(fm_left_i),.right_i(fm_right_i),.left_o(fm_left),.right_o(fm_right),
        .valid_o(fm_valid),.online_o(fm_ready_o),.stats_o(fm_stats_o),
        .worker_data_o(fm_packet),.worker_valid_o(fm_packet_valid),.worker_ready_i(worker_ready && route_sync),
        .result_data_i(result),.result_valid_i(worker_valid && route_sync),.result_ready_o(fm_return_ready));
    // RVR1 retains its existing subtractive-domain apply/ACK contract.
    // The worker receives one coherent wet/damp pair through held payload CDC.
    // Tail and route resets must not clear the globally applied parameters.
    wire [31:0] worker_params;
    held_bus_cdc #(.WIDTH(32)) u_params(
        .src_clk_i(subtractive_clk_i),.dst_clk_i(audio_clk_i),
        .rst_n_i(global_rst_n_i && audio_rst_n_i),
        .src_data_i({wet_q15_i,damp_q15_i}),.src_valid_i(1'b1),.src_ready_o(),
        .dst_data_o(worker_params),.dst_valid_o(),.dst_ready_i(1'b1));
    pcm_frame_effect u_worker(
        .clk_i(audio_clk_i),.rst_n_i(audio_reset_n),
        .data_i(route_sync?fm_packet:synth_packet),.valid_i(route_sync?fm_packet_valid:synth_packet_valid),
        .ready_o(worker_ready),.data_o(result),.valid_o(worker_valid),
        .ready_i(route_sync?fm_return_ready:synth_return_ready),
        .wet_q15_i(worker_params[31:16]),.damp_q15_i(worker_params[15:0]),
        .online_o(online),.late_o(worker_late),.clipped_o(worker_clip),.stats_o(audio_stats_o));
    wire synth_status_reset_n,fm_status_reset_n;
    jt51_reset_sync u_synth_status_reset(.clk_i(subtractive_clk_i),.rst_n_i(global_rst_n_i),.rst_n_o(synth_status_reset_n));
    jt51_reset_sync u_fm_status_reset(.clk_i(fm_clk_i),.rst_n_i(global_rst_n_i),.rst_n_o(fm_status_reset_n));
    (* ASYNC_REG="TRUE" *)logic [1:0] synth_late_sync,fm_late_sync,synth_clip_sync,fm_clip_sync;
    always_ff @(posedge subtractive_clk_i or negedge synth_status_reset_n)
        if(!synth_status_reset_n)begin synth_late_sync<=0;synth_clip_sync<=0;end
        else begin synth_late_sync<={synth_late_sync[0],worker_late};synth_clip_sync<={synth_clip_sync[0],worker_clip};end
    always_ff @(posedge fm_clk_i or negedge fm_status_reset_n)
        if(!fm_status_reset_n)begin fm_late_sync<=0;fm_clip_sync<=0;end
        else begin fm_late_sync<={fm_late_sync[0],worker_late};fm_clip_sync<={fm_clip_sync[0],worker_clip};end
    assign synth_left_o=synth_mute_i?24'sd0:synth_bypass_i?synth_left_i:synth_left;
    assign synth_right_o=synth_mute_i?24'sd0:synth_bypass_i?synth_right_i:synth_right;
    assign fm_left_o=fm_mute_i?24'sd0:fm_left;
    assign fm_right_o=fm_mute_i?24'sd0:fm_right;
    assign synth_valid_o=synth_bypass_i?synth_tick_i:synth_valid;
    assign fm_valid_o=fm_valid;
    assign synth_late_o=synth_late_sync[1] || (synth_stats_o[31:0]!=0);
    assign fm_late_o=fm_late_sync[1] || (fm_stats_o[31:0]!=0);
    // Clip is already sticky until the processing epoch resets. This output
    // is diagnostic only; coherent SPI status uses the held audio snapshot.
    assign synth_clipped_o=!route_fm_i && synth_clip_sync[1];
    assign fm_clipped_o=route_fm_i && fm_clip_sync[1];
endmodule
`default_nettype wire
