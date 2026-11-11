// Copyright (c) 2026 ICECREAM1230
// Streaming element render path, from S1 RAM outputs through S10.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
// Keep shallow alignment chains in flip-flops; preserve every existing clock edge.
`default_nettype none
module element_renderer #(
    parameter int IDXW = $clog2(synth_pkg::NUM_ELEMENTS),
    parameter bit ENABLE_MIDI = 1'b0,
    parameter bit PREPARED_CONTROL = 1'b0,
    parameter bit SCALED_CUTOFF_BEND = 1'b0,
    parameter bit PIPELINED_KEYTRACK_BEND = 1'b0,
    parameter bit EXTERNAL_DDS = 1'b0,
    parameter bit ENABLE_EXPRESSION = 1'b0
) (
    input wire clk, rst_n, in_valid,
    input wire [IDXW-1:0] in_index,
    input element_types_pkg::element_config_t config_in,
    input element_types_pkg::element_state_t state_in,
    // CSP returns these words on the same edge as this module's S2
    // capture. They feed decode directly; adding a register misaligns
    // the bus modulation with the element configuration by one clock.
    input element_types_pkg::element_modulation_t modulation_in,
    // S1 runtime context; capture its mode and base+bend on the existing S2
    // edge, before the synchronous CSP sum RAM returns that element's words.
    input wire runtime_enable_in,
    input wire signed [17:0] runtime_bend_in,
    input wire signed [19:0] runtime_cutoff_bend_in,
    input wire signed [17:0] prepared_touch_cutoff_in,prepared_touch_resonance_in,
    input wire [7:0] runtime_keytrack_in,
    input wire [7:0] runtime_wheel_gain_in,
    input wire signed [17:0] runtime_wheel_wave_in,
    input wire signed [6:0] runtime_touch_x_in,runtime_touch_y_in,
    input wire signed [15:0] runtime_touch_cutoff_depth_in,runtime_touch_resonance_depth_in,
    output wire out_valid,
    output wire [IDXW-1:0] out_index,
    output wire signed [17:0] sample_left, sample_right,
    output element_types_pkg::element_state_t state_out,
    output wire dds_req_valid,
    output wire [13:0] dds_req_pitch,
    output wire [23:0] dds_req_phase,dds_req_duty,
    output wire [1:0] dds_req_wave,
    input wire dds_resp_valid,
    input wire [23:0] dds_resp_phase,
    input wire signed [17:0] dds_resp_sample
);
    import element_types_pkg::*;
    logic s2_valid;
    logic [IDXW-1:0] s2_index;
    element_config_t s2_config /* synthesis syn_srlstyle = "registers" */;
    element_state_t s2_state /* synthesis syn_srlstyle = "registers" */;
    resolved_config_t resolved_config;
    resolved_config_t base_resolved_config;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            s2_valid <= 1'b0; s2_index <= '0;
            s2_config <= '0; s2_state <= '0;
        end else begin
            s2_valid <= in_valid; s2_index <= in_index;
            s2_config <= config_in; s2_state <= state_in;
        end
    end
    effective_params u_params (
        .config_in(s2_config), .modulation_in(modulation_in),
        .config_out(base_resolved_config)
    );
    generate if (ENABLE_MIDI) begin : g_runtime_base
        logic s2_runtime_enabled;
        logic signed [18:0] s2_pitch_base;
        logic signed [20:0] s2_cutoff_base;
        logic signed [19:0] s2_cutoff_bend;
        wire signed [22:0] touch_cutoff_product_s1=runtime_touch_y_in * runtime_touch_cutoff_depth_in;
        wire signed [22:0] touch_resonance_product_s1=runtime_touch_x_in * runtime_touch_resonance_depth_in;
        logic [7:0] s2_wheel_gain;
        logic signed [17:0] s2_touch_cutoff,s2_touch_resonance;
        wire signed [25:0] cutoff_product_s1 = runtime_bend_in
            * $signed({1'b0,runtime_keytrack_in});
        wire signed [19:0] cutoff_bend_s1 = SCALED_CUTOFF_BEND
            ? runtime_cutoff_bend_in : {{2{runtime_bend_in[17]}},runtime_bend_in};
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) begin
                s2_runtime_enabled <= 1'b0;
                s2_pitch_base <= '0; s2_cutoff_base <= '0; s2_cutoff_bend <= '0;
                s2_wheel_gain<=0;s2_touch_cutoff<=0;s2_touch_resonance<=0;
            end else begin
                s2_runtime_enabled <= runtime_enable_in;
                s2_wheel_gain<=ENABLE_EXPRESSION ? runtime_wheel_gain_in:8'd0;
                s2_touch_cutoff<=PREPARED_CONTROL ? prepared_touch_cutoff_in : ENABLE_EXPRESSION ? 18'(touch_cutoff_product_s1 >>> 6):18'sd0;
                s2_touch_resonance<=PREPARED_CONTROL ? prepared_touch_resonance_in : ENABLE_EXPRESSION ? 18'(touch_resonance_product_s1 >>> 6):18'sd0;
                s2_pitch_base <= $signed({5'b0, config_in.pitch})
                    + $signed({runtime_bend_in[17], runtime_bend_in});
                if (PIPELINED_KEYTRACK_BEND) begin
                    // S1 has already captured the voice owner and coefficient.
                    // Use this existing S2 edge for the multiplier, keeping the
                    // 32-voice/64-owner muxes off its DSP input setup path.
                    s2_cutoff_base <= $signed({7'b0, config_in.cutoff});
                    s2_cutoff_bend <= PREPARED_CONTROL ? runtime_cutoff_bend_in : 20'(cutoff_product_s1 >>> 6);
                end else begin
                    s2_cutoff_base <= $signed({7'b0, config_in.cutoff})
                        + $signed({cutoff_bend_s1[19],cutoff_bend_s1});
                    s2_cutoff_bend <= '0;
                end
            end
        end
        // Do not clamp the partial base: the context RAM has already applied
        // the original note+offset clamp. CSP may still cancel a bend here.
        // For base in [0,16383], final clamp(base+sat18(CSP+bend)) equals
        // final clamp(base+CSP+bend); either sat18 rail already forces the
        // same final rail. Keep all 20 signed bits until the final clamp.
        wire signed [18:0] pitch_base = s2_runtime_enabled
            ? s2_pitch_base : $signed({5'b0, s2_config.pitch});
        wire signed [20:0] cutoff_base = s2_runtime_enabled
            ? s2_cutoff_base : $signed({7'b0, s2_config.cutoff});
        wire signed [26:0] wheel_product=$signed({1'b0,s2_wheel_gain}) * runtime_wheel_wave_in;
        wire signed [19:0] wheel_delta=ENABLE_EXPRESSION && s2_runtime_enabled ? 20'(wheel_product >>> 7):20'sd0;
        wire signed [19:0] pitch_sum = $signed({pitch_base[18], pitch_base})
            + $signed({{2{modulation_in.pitch[17]}}, modulation_in.pitch}) + wheel_delta;
        // Keep the full scaled bend until the final clamp. At high Key Track
        // a bend can exceed signed18 even though the pitch input cannot.
        wire [21:0] cutoff_a = {cutoff_base[20], cutoff_base};
        wire [21:0] cutoff_b = PIPELINED_KEYTRACK_BEND && s2_runtime_enabled
            ? {{2{s2_cutoff_bend[19]}},s2_cutoff_bend} : 22'd0;
        wire [21:0] cutoff_c = {{4{modulation_in.cutoff[17]}},modulation_in.cutoff};
        wire [21:0] cutoff_d = s2_runtime_enabled ? 22'(s2_touch_cutoff) : 22'd0;
        // Reduce all four signed terms before propagating carry. In particular,
        // touch must not follow a completed base/bend/CSP addition on the
        // coefficient-input path. Keep the original signed22 wrap and clamp.
        wire [21:0] cutoff_s1 = cutoff_a ^ cutoff_b ^ cutoff_c;
        wire [21:0] cutoff_c1 = ((cutoff_a & cutoff_b) | (cutoff_a & cutoff_c)
            | (cutoff_b & cutoff_c)) << 1;
        wire [21:0] cutoff_s2 = cutoff_s1 ^ cutoff_c1 ^ cutoff_d;
        wire [21:0] cutoff_c2 = ((cutoff_s1 & cutoff_c1) | (cutoff_s1 & cutoff_d)
            | (cutoff_c1 & cutoff_d)) << 1;
        wire signed [21:0] cutoff_sum = ENABLE_EXPRESSION
            ? $signed(cutoff_s2) + $signed(cutoff_c2)
            : $signed(cutoff_s1) + $signed(cutoff_c1);
        wire signed [19:0] resonance_sum=$signed({6'b0,s2_config.resonance})
            + 20'(modulation_in.resonance) + 20'(s2_touch_resonance);
        always_comb begin
            resolved_config = base_resolved_config;
            if(s2_runtime_enabled && ENABLE_EXPRESSION)
                resolved_config.resonance=resonance_sum<0 ? 14'd0
                    : resonance_sum>16383 ? 14'h3fff:resonance_sum[13:0];
            resolved_config.pitch = pitch_sum < 20'sd0 ? 14'd0
                : pitch_sum > 20'sd16383 ? 14'h3fff : pitch_sum[13:0];
            resolved_config.cutoff = cutoff_sum < 22'sd0 ? 14'd0
                : cutoff_sum > 22'($signed({8'b0,synth_pkg::FC_MAX}))
                    ? synth_pkg::FC_MAX : cutoff_sum[13:0];
        end
    end else begin : g_legacy_base
        assign resolved_config = base_resolved_config;
    end endgenerate

    wire oscillator_valid;
    wire signed [23:0] oscillator_phase;
    wire signed [17:0] oscillator_sample;
    assign dds_req_valid=s2_valid;
    assign dds_req_pitch=resolved_config.pitch;
    assign dds_req_phase=s2_state.phase;
    assign dds_req_duty=resolved_config.duty;
    assign dds_req_wave=resolved_config.wave;
    generate if(EXTERNAL_DDS)begin:g_external_dds
        assign oscillator_valid=dds_resp_valid;
        assign oscillator_phase=dds_resp_phase;
        assign oscillator_sample=dds_resp_sample;
    end else begin:g_internal_dds
    dds_antialias_oscillator u_dds (
        .clk(clk), .rst_n(rst_n), .in_valid(s2_valid),
        .pitch_code(resolved_config.pitch), .phase_in(s2_state.phase),
        .duty(resolved_config.duty), .wave(resolved_config.wave),
        .direct_step_enable(1'b0),.direct_step(24'd0),
        .out_valid(oscillator_valid), .phase_out(oscillator_phase),
        .sample_out(oscillator_sample)
    );
    end endgenerate

    wire filter_valid;
    wire [IDXW-1:0] filter_index;
    wire signed [17:0] filtered_sample;
    element_state_t filtered_state;
    wire [7:0] filter_atten_l, filter_atten_r;
    wire [5:0] filter_gain_fraction_l, filter_gain_fraction_r;
    wire [16:0] filter_linear_gain_l, filter_linear_gain_r;
    element_filter #(.IDXW(IDXW), .REGISTER_COEFFICIENT_INPUT(1'b1)) u_filter (
        .clk(clk), .rst_n(rst_n), .in_valid(s2_valid), .in_index(s2_index),
        .config_in(resolved_config), .state_in(s2_state),
        .oscillator_sample(oscillator_sample), .oscillator_phase(oscillator_phase),
        .out_valid(filter_valid), .out_index(filter_index), .sample_out(filtered_sample),
        .state_out(filtered_state), .atten_l(filter_atten_l), .atten_r(filter_atten_r),
        .gain_fraction_l(filter_gain_fraction_l), .gain_fraction_r(filter_gain_fraction_r),
        .linear_gain_l(filter_linear_gain_l), .linear_gain_r(filter_linear_gain_r)
    );
    stereo_gain #(.IDXW(IDXW),.FRACTIONAL_GAIN(1'b1),.PREDECODED_GAIN(1'b1)) u_gain (
        .clk(clk), .rst_n(rst_n), .in_valid(filter_valid), .in_index(filter_index),
        .sample_in(filtered_sample), .atten_l(filter_atten_l), .atten_r(filter_atten_r),
        .gain_fraction_l(filter_gain_fraction_l), .gain_fraction_r(filter_gain_fraction_r),
        .linear_gain_l(filter_linear_gain_l), .linear_gain_r(filter_linear_gain_r),
        .state_in(filtered_state), .out_valid(out_valid), .out_index(out_index),
        .sample_left(sample_left), .sample_right(sample_right), .state_out(state_out)
    );

// synthesis translate_off
`ifndef SYNTHESIS
    // DDS data and the delayed coefficient context join before multiplication.
    initial assert (synth_pkg::DDS_LATENCY == dds_antialias_pkg::OSCILLATOR_LATENCY)
        else $fatal(1, "DDS latency differs from shared schedule");
    always @(posedge clk) if (rst_n === 1'b1)
        assert (oscillator_valid == u_filter.join_valid)
            else $fatal(1, "DDS and filter coefficient latency differ");
`endif
// synthesis translate_on
endmodule
