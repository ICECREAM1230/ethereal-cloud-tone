// Copyright (c) 2026 ICECREAM1230
// Element SVF coefficient path and resonance input scaling.
// Coefficient stages S3/S3B/S3C followed by DDS_LATENCY-3 context delays;
// the aligned join multiplies waveform/attenuation before the 17-stage SVF.
// Copyright © 2026 Thor H. Linløkken <thj@thj.no>
// License: MIT
// Keep shallow alignment chains in flip-flops; preserve every existing clock edge.
`default_nettype none
module element_filter #(
    parameter int IDXW = $clog2(synth_pkg::NUM_ELEMENTS),
    parameter int EXTRA_LATENCY = synth_pkg::DDS_LATENCY - 3,
    parameter bit REGISTER_COEFFICIENT_INPUT = 1'b0
) (
    input wire clk, rst_n, in_valid,
    input wire [IDXW-1:0] in_index,
    input element_types_pkg::resolved_config_t config_in,
    input element_types_pkg::element_state_t state_in,
    input wire signed [17:0] oscillator_sample,
    input wire signed [23:0] oscillator_phase,
    output wire out_valid,
    output wire [IDXW-1:0] out_index,
    output wire signed [17:0] sample_out,
    output element_types_pkg::element_state_t state_out,
    output wire [7:0] atten_l, atten_r,
    output wire [5:0] gain_fraction_l, gain_fraction_r,
    output wire [16:0] linear_gain_l, linear_gain_r
);
    import element_types_pkg::*;
    // Move one budgeted context delay in front of the coefficient ROMs.
    // With no spare delay, retain the original input-to-ROM boundary.
    localparam bit PRE_ROM = REGISTER_COEFFICIENT_INPUT && (EXTRA_LATENCY > 0);
    localparam int TAIL_LATENCY = EXTRA_LATENCY - int'(PRE_ROM);
    wire coeff_valid;
    wire [IDXW-1:0] coeff_index;
    resolved_config_t coeff_config;
    element_state_t coeff_state;
    generate if (PRE_ROM) begin : g_input_register
        logic valid_q;
        logic [IDXW-1:0] index_q;
        resolved_config_t config_q /* synthesis syn_srlstyle = "registers" */;
        element_state_t state_q /* synthesis syn_srlstyle = "registers" */;
        assign coeff_valid = valid_q; assign coeff_index = index_q;
        assign coeff_config = config_q; assign coeff_state = state_q;
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) begin
                valid_q <= 1'b0; index_q <= '0;
                config_q <= '0; state_q <= '0;
            end else begin
                valid_q <= in_valid; index_q <= in_index;
                config_q <= config_in; state_q <= state_in;
            end
        end
    end else begin : g_direct_input
        assign coeff_valid = in_valid; assign coeff_index = in_index;
        assign coeff_config = config_in; assign coeff_state = state_in;
    end endgenerate
    // Match HEX digit width; padding bits never enter the datapath.
    reg [19:0] q1_lut [0:15];
    reg [19:0] gain_att_lut [0:15];
    reg [15:0] reso_att_lut [0:63];
    initial begin
        $readmemh("rtl/synthesizers/subtractive/data/q1_lut.hex", q1_lut);
        $readmemh("rtl/synthesizers/subtractive/data/att_lut.hex", gain_att_lut);
        $readmemh("rtl/synthesizers/subtractive/data/reso_att_lut.hex", reso_att_lut);
    end

    typedef struct packed {
        element_state_t state;
        logic dual;
        logic [1:0] filter_type;
        logic [7:0] atten_l, atten_r;
        logic [5:0] gain_fraction_l, gain_fraction_r;
        logic [16:0] linear_gain_l, linear_gain_r;
        logic [15:0] input_atten;
    } filter_context_t;
    logic s3_valid, s3b_valid, s3c_valid, s3d_valid;
    logic [IDXW-1:0] s3_index, s3b_index, s3c_index, s3d_index;
    filter_context_t s3_context /* synthesis syn_srlstyle = "registers" */, s3b_context /* synthesis syn_srlstyle = "registers" */, s3c_context /* synthesis syn_srlstyle = "registers" */, s3d_context /* synthesis syn_srlstyle = "registers" */;
    filter_context_t completed_s3c_context;
    filter_context_t multiply_context;
    logic [16:0] s3_q1_lut;
    logic [3:0] s3_reso_oct;
    logic signed [35:0] s3d_k /* synthesis syn_srlstyle = "registers" */;
    wire [28:0] compact_k;
    wire signed [35:0] s3c_k = $signed({7'd0, compact_k});
    // The three interpolation registers replace the full-ROM read and
    // its two K transport registers; Q1 and packet context keep their seam.
    svf_k_interpolator u_k_coeff (
        .clk(clk), .rst_n(rst_n), .in_valid(coeff_valid),
        .in_code(coeff_config.cutoff), .out_valid(), .out_k(compact_k)
    );
    logic signed [17:0] s3b_q1, s3c_q1, s3d_q1, s3d_sample;
    wire join_valid;
    wire [IDXW-1:0] join_index;
    wire signed [35:0] join_k;
    wire signed [17:0] join_q1;
    filter_context_t join_context;
    // Gain interpolation runs beside the three coefficient stages instead
    // of following the long context delay. S3 decodes endpoints, S3B keeps
    // the exact integer delta, S3C multiplies by the fractional code; the
    // next existing context edge captures the sum. This works even when
    // EXTRA_LATENCY=0 and never adds a waveform/valid/context stage.
    logic [16:0] s3_gain_lower_l, s3_gain_lower_r;
    logic [16:0] s3_gain_upper_l, s3_gain_upper_r;
    logic [16:0] s3b_gain_lower_l, s3b_gain_lower_r;
    logic [11:0] s3b_gain_delta_l, s3b_gain_delta_r;
    logic [16:0] s3c_gain_lower_l, s3c_gain_lower_r;
    logic [17:0] s3c_gain_step_l, s3c_gain_step_r;
    wire [16:0] gain_delta_l = s3_gain_upper_l - s3_gain_lower_l;
    wire [16:0] gain_delta_r = s3_gain_upper_r - s3_gain_lower_r;
    function automatic logic [16:0] attenuation_node(input logic [7:0] code);
        attenuation_node = (code == 8'hff) ? 17'd0
            : gain_att_lut[code[3:0]][16:0] >> code[7:4];
    endfunction
    always_comb begin
        completed_s3c_context = s3c_context;
        completed_s3c_context.linear_gain_l = s3c_context.atten_l == 8'hff
            ? 17'd0 : s3c_gain_lower_l + 17'(s3c_gain_step_l >> 6);
        completed_s3c_context.linear_gain_r = s3c_context.atten_r == 8'hff
            ? 17'd0 : s3c_gain_lower_r + 17'(s3c_gain_step_r >> 6);
    end
    generate if (TAIL_LATENCY == 0) begin : g_no_delay
        assign join_valid = s3c_valid;
        assign join_index = s3c_index;
        assign join_k = s3c_k; assign join_q1 = s3c_q1;
        assign join_context = completed_s3c_context;
    end else begin : g_delay
        logic [TAIL_LATENCY-1:0] valid_pipe;
        logic [IDXW-1:0] index_pipe [0:TAIL_LATENCY-1];
        logic signed [35:0] k_pipe [0:TAIL_LATENCY-1] /* synthesis syn_srlstyle = "registers" */;
        logic signed [17:0] q1_pipe [0:TAIL_LATENCY-1];
        filter_context_t context_pipe [0:TAIL_LATENCY-1] /* synthesis syn_srlstyle = "registers" */;
        assign join_valid = valid_pipe[TAIL_LATENCY-1];
        assign join_index = index_pipe[TAIL_LATENCY-1];
        assign join_k = k_pipe[TAIL_LATENCY-1];
        assign join_q1 = q1_pipe[TAIL_LATENCY-1];
        assign join_context = context_pipe[TAIL_LATENCY-1];
        always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) begin
                valid_pipe <= '0;
                for (int i = 0; i < TAIL_LATENCY; i++) begin
                    index_pipe[i] <= '0; k_pipe[i] <= '0;
                    q1_pipe[i] <= '0; context_pipe[i] <= '0;
                end
            end else begin
                valid_pipe <= (valid_pipe << 1) | TAIL_LATENCY'(s3c_valid);
                index_pipe[0] <= s3c_index; k_pipe[0] <= s3c_k;
                q1_pipe[0] <= s3c_q1; context_pipe[0] <= completed_s3c_context;
                for (int i = 1; i < TAIL_LATENCY; i++) begin
                    index_pipe[i] <= index_pipe[i-1]; k_pipe[i] <= k_pipe[i-1];
                    q1_pipe[i] <= q1_pipe[i-1]; context_pipe[i] <= context_pipe[i-1];
                end
            end
        end
    end endgenerate

    // K is already the full Q8.28 prewarped coefficient. q1 alone
    // uses octave scaling. Preserve the original decode/truncate seam.
    wire signed [17:0] q1_decoded =
        $signed({1'b0, s3_q1_lut}) >>> s3_reso_oct;
    wire signed [35:0] oscillator_product =
        oscillator_sample * $signed({1'b0, join_context.input_atten});
    always_comb begin
        multiply_context = join_context;
        multiply_context.state.phase = oscillator_phase;
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            s3_valid <= 1'b0; s3_index <= '0;
            s3_context.state <= '0; s3_context.dual <= 1'b0;
            s3_context.filter_type <= '0;
            s3_context.atten_l <= '0; s3_context.atten_r <= '0;
            s3_context.gain_fraction_l <= '0; s3_context.gain_fraction_r <= '0;
            s3_context.linear_gain_l <= '0; s3_context.linear_gain_r <= '0;
            s3_context.input_atten <= 16'hffff;
            s3_q1_lut <= '0; s3_reso_oct <= '0;
            s3b_valid <= 1'b0; s3b_index <= '0; s3b_context <= '0;
            s3b_q1 <= '0;
            s3c_valid <= 1'b0; s3c_index <= '0; s3c_context <= '0;
            s3c_q1 <= '0;
            s3d_valid <= 1'b0; s3d_index <= '0; s3d_context <= '0;
            s3d_k <= '0; s3d_q1 <= '0; s3d_sample <= '0;
            s3_gain_lower_l <= '0; s3_gain_lower_r <= '0;
            s3_gain_upper_l <= '0; s3_gain_upper_r <= '0;
            s3b_gain_lower_l <= '0; s3b_gain_lower_r <= '0;
            s3b_gain_delta_l <= '0; s3b_gain_delta_r <= '0;
            s3c_gain_lower_l <= '0; s3c_gain_lower_r <= '0;
            s3c_gain_step_l <= '0; s3c_gain_step_r <= '0;
        end else begin
            s3_valid <= coeff_valid; s3_index <= coeff_index;
            s3_q1_lut <= q1_lut[coeff_config.resonance[9:6]][16:0];
            s3_reso_oct <= coeff_config.resonance[13:10];
            s3_context.state <= coeff_state;
            s3_context.dual <= coeff_config.dual;
            s3_context.filter_type <= coeff_config.filter_type;
            s3_context.atten_l <= coeff_config.atten_l;
            s3_context.atten_r <= coeff_config.atten_r;
            s3_context.gain_fraction_l <= coeff_config.gain_fraction_l;
            s3_context.gain_fraction_r <= coeff_config.gain_fraction_r;
            s3_context.linear_gain_l <= '0; s3_context.linear_gain_r <= '0;
            s3_context.input_atten <= coeff_config.dual
                ? reso_att_lut[coeff_config.resonance[13:8]] : 16'hffff;
            s3b_valid <= s3_valid; s3b_index <= s3_index;
            s3b_context <= s3_context; s3b_q1 <= q1_decoded;
            s3_gain_lower_l <= attenuation_node(coeff_config.atten_l);
            s3_gain_lower_r <= attenuation_node(coeff_config.atten_r);
            s3_gain_upper_l <= attenuation_node(coeff_config.atten_l == 8'h00
                ? 8'h00 : coeff_config.atten_l - 8'd1);
            s3_gain_upper_r <= attenuation_node(coeff_config.atten_r == 8'h00
                ? 8'h00 : coeff_config.atten_r - 8'd1);
            // All adjacent Q16 deltas in the frozen LUT fit twelve bits.
            s3b_gain_lower_l <= s3_gain_lower_l; s3b_gain_lower_r <= s3_gain_lower_r;
            s3b_gain_delta_l <= gain_delta_l[11:0]; s3b_gain_delta_r <= gain_delta_r[11:0];
            s3c_gain_lower_l <= s3b_gain_lower_l; s3c_gain_lower_r <= s3b_gain_lower_r;
            s3c_gain_step_l <= s3b_gain_delta_l * s3b_context.gain_fraction_l;
            s3c_gain_step_r <= s3b_gain_delta_r * s3b_context.gain_fraction_r;
            s3c_valid <= s3b_valid; s3c_index <= s3b_index;
            s3c_context <= s3b_context; s3c_q1 <= s3b_q1;
            s3d_valid <= join_valid; s3d_index <= join_index;
            s3d_context <= multiply_context; s3d_k <= join_k; s3d_q1 <= join_q1;
            s3d_sample <= 18'($signed(oscillator_product >>> 16));
        end
    end

    wire [30:0] gain_context_l, gain_context_r;
    assign {atten_l, gain_fraction_l, linear_gain_l} = gain_context_l;
    assign {atten_r, gain_fraction_r, linear_gain_r} = gain_context_r;
    svf_tpt #(.IDXW(IDXW), .GAIN_W(31)) u_svf (
        .clk(clk), .rst_n(rst_n), .in_act(s3d_valid), .in_idx(s3d_index),
        .in_osc(s3d_sample), .in_k(s3d_k), .in_q1(s3d_q1),
        .in_ic1a(s3d_context.state.ic1a), .in_ic2a(s3d_context.state.ic2a),
        .in_ic1b(s3d_context.state.ic1b), .in_ic2b(s3d_context.state.ic2b),
        .in_dual(s3d_context.dual), .in_ftype(s3d_context.filter_type),
        .in_phase(s3d_context.state.phase),
        .in_gl({s3d_context.atten_l, s3d_context.gain_fraction_l, s3d_context.linear_gain_l}),
        .in_gr({s3d_context.atten_r, s3d_context.gain_fraction_r, s3d_context.linear_gain_r}),
        .out_act(out_valid), .out_idx(out_index), .out_elem(sample_out),
        .out_ic1an(state_out.ic1a), .out_ic2an(state_out.ic2a),
        .out_ic1bn(state_out.ic1b), .out_ic2bn(state_out.ic2b),
        .out_phase(state_out.phase), .out_gl(gain_context_l), .out_gr(gain_context_r)
    );
endmodule
