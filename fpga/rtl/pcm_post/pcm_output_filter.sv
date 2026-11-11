`default_nettype none
module pcm_output_filter #(
    parameter bit ENABLE_OUTPUT_TILT = 1'b1,
    parameter integer OUTPUT_TILT_SHIFT = 3
) (
    input wire sysclk,rst_n,sample_tick,
    input wire signed [23:0] selected_left,selected_right,additive_left,additive_right,
    input wire additive_selected,additive_bypass_tilt,effective_test_tone,selected_fault,local_switch_mute,output_filter_rst_n,
    input wire fm_native_clk,fm_filter_rst_n,fm_frame_tick,fm_output_mute,
    input wire signed [23:0] fm_raw_left,fm_raw_right,
    output wire signed [23:0] pcm_left,pcm_right,fm_pcm_left,fm_pcm_right
);
    logic [23:0] tone_phase;
    always_ff @(posedge sysclk or negedge rst_n)
        if (!rst_n)           tone_phase <= '0;
        else if (sample_tick) tone_phase <= tone_phase + 24'd262144;

    logic signed [17:0] tone_q216;
    osc_core u_tone_osc (
        .phase_next (tone_phase),      // advanced above, not here (#128)
        .duty       (24'sd0),
        .wave       (2'd3),            // sine (quarter-wave LUT)
        .sample_out (tone_q216)
    );
    // Sine spans ±32767, which fits 16 signed bits exactly, so the
    // concat IS the <<8 with the sign landing at bit 23.
    wire signed [23:0] tone_sample = {tone_q216[15:0], 8'b0};

    //----------------------------------------------------------------
    // Output tilt (output_tilt.sv): one-pole 6 dB/oct lowpass on the
    // mix, corner ≈ 2 kHz — Thor's ear-tuned warm stop. Error
    // feedback inside the module makes it settle to EXACT zero on
    // silence (#102). Sits BEFORE the test-tone mux so the purity
    // reference stays unfiltered.
    //----------------------------------------------------------------
    logic signed [23:0] lpf_l, lpf_r;
    // Flush the filter state as well as the FM windows. Otherwise an exact
    // Panic mute could expose the preceding filter tail after re-enabling.
    // Unselected DDS/KS keeps the original reset and numerical path.
    output_tilt #(.SHIFT(OUTPUT_TILT_SHIFT),.ENABLE(ENABLE_OUTPUT_TILT)) u_tilt_l (
        .clk   (sysclk),
        .rst_n (output_filter_rst_n),
        .tick  (sample_tick),
        .in    (selected_left),
        .out   (lpf_l)
    );
    output_tilt #(.SHIFT(OUTPUT_TILT_SHIFT),.ENABLE(ENABLE_OUTPUT_TILT)) u_tilt_r (
        .clk   (sysclk),
        .rst_n (output_filter_rst_n),
        .tick  (sample_tick),
        .in    (selected_right),
        .out   (lpf_r)
    );

    // A selected FM fault/Panic suppresses filter tails and diagnostic tone.
    // The original DDS/KS mux and sample values are unchanged when unselected.
    wire signed [23:0] out_left  = (selected_fault || local_switch_mute) ? 24'sd0 :
                                  effective_test_tone ? tone_sample :
                                  (additive_selected && additive_bypass_tilt) ? additive_left : lpf_l;
    wire signed [23:0] out_right = (selected_fault || local_switch_mute) ? 24'sd0 :
                                  effective_test_tone ? tone_sample :
                                  (additive_selected && additive_bypass_tilt) ? additive_right : lpf_r;
    assign pcm_left = out_left;
    assign pcm_right = out_right;


    wire signed [23:0] filtered_fm_left,filtered_fm_right;
    output_tilt #(.SHIFT(OUTPUT_TILT_SHIFT),.ENABLE(ENABLE_OUTPUT_TILT)) u_fm_tilt_l(.clk(fm_native_clk),.rst_n(fm_filter_rst_n),
        .tick(fm_frame_tick),.in(fm_raw_left),.out(filtered_fm_left));
    output_tilt #(.SHIFT(OUTPUT_TILT_SHIFT),.ENABLE(ENABLE_OUTPUT_TILT)) u_fm_tilt_r(.clk(fm_native_clk),.rst_n(fm_filter_rst_n),
        .tick(fm_frame_tick),.in(fm_raw_right),.out(filtered_fm_right));
    assign fm_pcm_left=fm_output_mute ? 24'sd0 : filtered_fm_left;
    assign fm_pcm_right=fm_output_mute ? 24'sd0 : filtered_fm_right;

endmodule
`default_nettype wire
