`default_nettype none
module pcm_postprocess #(
    parameter bit ENABLE_REVERB=1'b1,SHARED_REVERB=1'b1,
    parameter bit ENABLE_OUTPUT_TILT=1'b1,COMMON_PCM_PIPELINE=1'b0,
    parameter integer OUTPUT_TILT_SHIFT=3
) (
    input wire sysclk,rst_n,sample_tick,
    input wire audio_clk_i,audio_rst_n_i,
    output wire [127:0] synth_stats_o,fm_stats_o,audio_stats_o,
    input wire signed [23:0] selected_left,selected_right,additive_left,additive_right,
    input wire additive_selected,additive_bypass_tilt,effective_test_tone,selected_fault,local_switch_mute,output_filter_rst_n,
    input wire fm_native_clk,fm_filter_rst_n,fm_frame_tick,fm_output_mute,
    input wire signed [23:0] fm_raw_left,fm_raw_right,
    output wire signed [23:0] pcm_left,pcm_right,fm_pcm_left,fm_pcm_right,
    input wire route_fm_i,
    input wire [15:0] wet_q15_i,damp_q15_i
);
    wire signed [23:0] filtered_left,filtered_right,filtered_fm_left,filtered_fm_right;
    pcm_output_filter #(.ENABLE_OUTPUT_TILT(ENABLE_OUTPUT_TILT),.OUTPUT_TILT_SHIFT(OUTPUT_TILT_SHIFT)) u_filter(
        .sysclk(sysclk),
        .rst_n(rst_n),
        .sample_tick(sample_tick),
        .selected_left(selected_left),
        .selected_right(selected_right),
        .additive_left(additive_left),
        .additive_right(additive_right),
        .additive_selected(additive_selected),
        .additive_bypass_tilt(additive_bypass_tilt),
        .effective_test_tone(effective_test_tone),
        .selected_fault(selected_fault),
        .local_switch_mute(local_switch_mute),
        .output_filter_rst_n(output_filter_rst_n),
        .fm_native_clk(fm_native_clk),
        .fm_filter_rst_n(fm_filter_rst_n),
        .fm_frame_tick(fm_frame_tick),
        .fm_output_mute(fm_output_mute),
        .fm_raw_left(fm_raw_left),
        .fm_raw_right(fm_raw_right),
        .pcm_left(filtered_left),
        .pcm_right(filtered_right),
        .fm_pcm_left(filtered_fm_left),
        .fm_pcm_right(filtered_fm_right));
    generate if(COMMON_PCM_PIPELINE && ENABLE_REVERB && SHARED_REVERB)begin:g_common
        pcm_shared_pipeline u_pipeline(
            .global_rst_n_i(rst_n),.audio_clk_i(audio_clk_i),.audio_rst_n_i(audio_rst_n_i),
            .subtractive_clk_i(sysclk),.synth_rst_n_i(output_filter_rst_n && !local_switch_mute),.synth_tick_i(sample_tick),
            .synth_mute_i(selected_fault || local_switch_mute),.synth_bypass_i(effective_test_tone),
            .synth_left_i(filtered_left),.synth_right_i(filtered_right),
            .fm_clk_i(fm_native_clk),.fm_rst_n_i(fm_filter_rst_n),.fm_tick_i(fm_frame_tick),.fm_mute_i(fm_output_mute),
            .fm_left_i(filtered_fm_left),.fm_right_i(filtered_fm_right),.route_fm_i(route_fm_i),.wet_q15_i,.damp_q15_i,
            .synth_left_o(pcm_left),.synth_right_o(pcm_right),.fm_left_o(fm_pcm_left),.fm_right_o(fm_pcm_right),
            .synth_valid_o(),.fm_valid_o(),.synth_ready_o(),.fm_ready_o(),
            .synth_late_o(),.fm_late_o(),.synth_clipped_o(),.fm_clipped_o(),
            .synth_stats_o,.fm_stats_o,.audio_stats_o);
    end else begin:g_legacy
    assign synth_stats_o=0;assign fm_stats_o=0;assign audio_stats_o=0;
    if(SHARED_REVERB)begin:g_shared
        reverb_audio_shared_bank #(.ENABLE(ENABLE_REVERB)) u_reverb(
        .route_fm_i,.synth_valid_o(),.fm_valid_o(),
        .subtractive_clk_i(sysclk),.synth_rst_n_i(output_filter_rst_n && !local_switch_mute),.synth_tick_i(sample_tick),
        .synth_mute_i(selected_fault || local_switch_mute),.synth_bypass_i(effective_test_tone),
        .synth_left_i(filtered_left),.synth_right_i(filtered_right),
        .fm_clk_i(fm_native_clk),.fm_rst_n_i(fm_filter_rst_n),.fm_tick_i(fm_frame_tick),.fm_mute_i(fm_output_mute),
        .fm_left_i(filtered_fm_left),.fm_right_i(filtered_fm_right),.wet_q15_i,.damp_q15_i,
        .synth_left_o(pcm_left),.synth_right_o(pcm_right),.fm_left_o(fm_pcm_left),.fm_right_o(fm_pcm_right),
        .synth_ready_o(),.fm_ready_o(),.synth_late_o(),.fm_late_o(),.synth_clipped_o(),.fm_clipped_o());
    end else begin:g_dual
        reverb_audio_bank #(.ENABLE(ENABLE_REVERB)) u_reverb(
        
        .subtractive_clk_i(sysclk),.synth_rst_n_i(output_filter_rst_n && !local_switch_mute),.synth_tick_i(sample_tick),
        .synth_mute_i(selected_fault || local_switch_mute),.synth_bypass_i(effective_test_tone),
        .synth_left_i(filtered_left),.synth_right_i(filtered_right),
        .fm_clk_i(fm_native_clk),.fm_rst_n_i(fm_filter_rst_n),.fm_tick_i(fm_frame_tick),.fm_mute_i(fm_output_mute),
        .fm_left_i(filtered_fm_left),.fm_right_i(filtered_fm_right),.wet_q15_i,.damp_q15_i,
        .synth_left_o(pcm_left),.synth_right_o(pcm_right),.fm_left_o(fm_pcm_left),.fm_right_o(fm_pcm_right),
        .synth_ready_o(),.fm_ready_o(),.synth_late_o(),.fm_late_o(),.synth_clipped_o(),.fm_clipped_o());
    end
    end endgenerate

endmodule
`default_nettype wire
