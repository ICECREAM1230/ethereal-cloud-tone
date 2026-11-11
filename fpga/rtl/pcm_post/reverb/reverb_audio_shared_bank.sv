`default_nettype none
// One stereo effect in subtractive_clk_i. FM sample cadence is preserved by two
// held stereo-frame links; there is no fixed-rate FIFO dequeue or resampling.
// Each route change/reset clears the tail and all old in-flight frames.
// *_ready_o means initialization complete for this epoch, not per-frame idle.
module reverb_audio_shared_bank #(
    parameter bit ENABLE = 1'b1
) (
    input wire subtractive_clk_i, synth_rst_n_i, synth_tick_i,
    input wire synth_mute_i, synth_bypass_i,
    input wire signed [23:0] synth_left_i, synth_right_i,
    input wire fm_clk_i, fm_rst_n_i, fm_tick_i, fm_mute_i,
    input wire signed [23:0] fm_left_i, fm_right_i,
    input wire route_fm_i,
    input wire [15:0] wet_q15_i, damp_q15_i,
    output wire signed [23:0] synth_left_o, synth_right_o,
    output wire signed [23:0] fm_left_o, fm_right_o,
    output wire synth_valid_o, fm_valid_o,
    output wire synth_ready_o, fm_ready_o,
    output wire synth_late_o, fm_late_o,
    output wire synth_clipped_o, fm_clipped_o
);
    generate if (ENABLE) begin : enabled
        logic previous_route;
        always_ff @(posedge subtractive_clk_i or negedge synth_rst_n_i) begin
            if (!synth_rst_n_i) previous_route <= 1'b0;
            else previous_route <= route_fm_i;
        end
        wire route_stable = previous_route == route_fm_i;
        // An inactive FM fault cannot reset a working synth effect. Release
        // of the selected reset is synchronized into the effect clock.
        wire core_reset_request_n = synth_rst_n_i && !synth_bypass_i && route_stable &&
                                    (!route_fm_i || fm_rst_n_i);
        wire core_rst_n, fm_link_rst_n;
        wire core_ready, core_valid, core_late, core_clipped;
        wire signed [23:0] wet_left, wet_right;
        logic effect_online;
        always_ff @(posedge subtractive_clk_i or negedge core_rst_n) begin
            if (!core_rst_n) effect_online <= 1'b0;
            else if (core_ready) effect_online <= 1'b1;
        end
        // core_ready falls during every calculation. Only this sticky startup
        // flag may release the links; busy must never flush pending results.
        wire link_reset_request_n = core_rst_n && route_fm_i && effect_online;
        // Keep all new synchronizers in the same CDC naming family as the
        // always-present DAC tap. Common SDC role queries then remain valid
        // with either top file and with the effect compiled out.
        reverb_audio_shared_reset u_fm_reset_cdc (
            .src_clk_i(subtractive_clk_i), .src_reset_request_n_i(core_reset_request_n),
            .src_rst_n_o(core_rst_n),
            .dst_clk_i(fm_clk_i), .dst_reset_request_n_i(link_reset_request_n),
            .dst_rst_n_o(fm_link_rst_n)
        );

        logic fm_tick_d, input_pending, input_overflow;
        logic [47:0] input_pair;
        wire input_ready, input_valid;
        wire [47:0] received_pair;
        always_ff @(posedge fm_clk_i or negedge fm_link_rst_n) begin
            if (!fm_link_rst_n) begin
                fm_tick_d <= 1'b0;
                input_pending <= 1'b0;
                input_overflow <= 1'b0;
                input_pair <= 48'd0;
            end else begin
                fm_tick_d <= fm_tick_i;
                if (input_pending && input_ready) input_pending <= 1'b0;
                // FM post-tilt PCM is published on tick, accepted at tick+1.
                if (fm_tick_d) begin
                    if (!input_pending || input_ready) begin
                        input_pair <= {fm_left_i,fm_right_i};
                        input_pending <= 1'b1;
                    end else input_overflow <= 1'b1;
                end
            end
        end
        held_bus_cdc #(.WIDTH(48)) u_fm_input_cdc (
            .src_clk_i(fm_clk_i), .dst_clk_i(subtractive_clk_i),
            .rst_n_i(link_reset_request_n),
            .src_data_i(input_pair), .src_valid_i(input_pending), .src_ready_o(input_ready),
            .dst_data_o(received_pair), .dst_valid_o(input_valid), .dst_ready_i(1'b1)
        );
        // Additive direct PCM is published at tick+2. FM's received pair is
        // already held; the same three-cycle acceptance adapter serves both.
        reverb_audio_stage #(.RUNTIME_PARAMS(1'b1), .INPUT_DELAY_CYCLES(3)) u_effect (
            .clk(subtractive_clk_i), .rst_n(core_rst_n),
            .sample_tick(effect_online && (route_fm_i ? input_valid : synth_tick_i)),
            .wet_q15(wet_q15_i), .damp_q15(damp_q15_i),
            .pcm_l_in(route_fm_i ? $signed(received_pair[47:24]) : synth_left_i),
            .pcm_r_in(route_fm_i ? $signed(received_pair[23:0]) : synth_right_i),
            .pcm_l_out(wet_left), .pcm_r_out(wet_right),
            .out_valid(core_valid), .ready(core_ready), .late(core_late), .clipped(core_clipped)
        );

        logic output_pending, output_overflow;
        logic [48:0] output_pair;
        wire output_ready, output_valid;
        wire [48:0] returned_pair;
        always_ff @(posedge subtractive_clk_i or negedge link_reset_request_n) begin
            if (!link_reset_request_n) begin
                output_pending <= 1'b0;
                output_overflow <= 1'b0;
                output_pair <= 49'd0;
            end else begin
                if (output_pending && output_ready) output_pending <= 1'b0;
                if (core_valid) begin
                    if (!output_pending || output_ready) begin
                        output_pair <= {core_clipped,wet_left,wet_right};
                        output_pending <= 1'b1;
                    end else output_overflow <= 1'b1;
                end
            end
        end
        held_bus_cdc #(.WIDTH(49)) u_fm_output_cdc (
            .src_clk_i(subtractive_clk_i), .dst_clk_i(fm_clk_i),
            .rst_n_i(link_reset_request_n),
            .src_data_i(output_pair), .src_valid_i(output_pending), .src_ready_o(output_ready),
            .dst_data_o(returned_pair), .dst_valid_o(output_valid), .dst_ready_i(1'b1)
        );
        logic signed [23:0] fm_left, fm_right;
        logic fm_frame_valid, fm_frame_clipped;
        (* ASYNC_REG = "TRUE" *) logic [1:0] fm_error_sync;
        always_ff @(posedge fm_clk_i or negedge fm_link_rst_n) begin
            if (!fm_link_rst_n) begin
                fm_left <= 24'sd0;
                fm_right <= 24'sd0;
                fm_frame_valid <= 1'b0;
                fm_frame_clipped <= 1'b0;
                fm_error_sync <= 2'b00;
            end else begin
                fm_error_sync <= {fm_error_sync[0],core_late || output_overflow};
                fm_frame_valid <= output_valid;
                if (output_valid) begin
                    fm_left <= $signed(returned_pair[47:24]);
                    fm_right <= $signed(returned_pair[23:0]);
                    fm_frame_clipped <= returned_pair[48];
                end
            end
        end
        assign synth_left_o = synth_mute_i ? 24'sd0 : synth_bypass_i ? synth_left_i :
                              !route_fm_i && effect_online ? wet_left : 24'sd0;
        assign synth_right_o = synth_mute_i ? 24'sd0 : synth_bypass_i ? synth_right_i :
                               !route_fm_i && effect_online ? wet_right : 24'sd0;
        assign fm_left_o = fm_mute_i ? 24'sd0 : fm_left;
        assign fm_right_o = fm_mute_i ? 24'sd0 : fm_right;
        assign synth_valid_o = synth_bypass_i ? synth_tick_i : !route_fm_i && effect_online && core_valid;
        assign fm_valid_o = fm_frame_valid;
        assign synth_ready_o = effect_online;
        assign fm_ready_o = fm_link_rst_n;
        assign synth_late_o = !route_fm_i && core_late;
        assign fm_late_o = input_overflow || fm_error_sync[1];
        assign synth_clipped_o = !route_fm_i && core_clipped;
        assign fm_clipped_o = fm_frame_clipped;
    end else begin : disabled
        assign synth_left_o = synth_left_i;
        assign synth_right_o = synth_right_i;
        assign fm_left_o = fm_left_i;
        assign fm_right_o = fm_right_i;
        assign synth_valid_o = synth_tick_i;
        assign fm_valid_o = fm_tick_i;
        assign synth_ready_o = synth_rst_n_i;
        assign fm_ready_o = fm_rst_n_i;
        assign synth_late_o = 1'b0;
        assign fm_late_o = 1'b0;
        assign synth_clipped_o = 1'b0;
        assign fm_clipped_o = 1'b0;
    end endgenerate
endmodule
`default_nettype wire

`default_nettype none
// Independent asynchronous assertions, each released by its local clock.
// These four raw qualifiers are the same two two-flop release pipelines.
module reverb_audio_shared_reset (
    input wire src_clk_i, src_reset_request_n_i,
    input wire dst_clk_i, dst_reset_request_n_i,
    output wire src_rst_n_o, dst_rst_n_o
);
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic src_reset_meta;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic src_reset_sync;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic dst_reset_meta;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic dst_reset_sync;
    always_ff @(posedge src_clk_i or negedge src_reset_request_n_i) begin
        if (!src_reset_request_n_i) begin
            src_reset_meta <= 1'b0;
            src_reset_sync <= 1'b0;
        end else begin
            src_reset_meta <= 1'b1;
            src_reset_sync <= src_reset_meta;
        end
    end
    always_ff @(posedge dst_clk_i or negedge dst_reset_request_n_i) begin
        if (!dst_reset_request_n_i) begin
            dst_reset_meta <= 1'b0;
            dst_reset_sync <= 1'b0;
        end else begin
            dst_reset_meta <= 1'b1;
            dst_reset_sync <= dst_reset_meta;
        end
    end
    assign src_rst_n_o = src_reset_sync;
    assign dst_rst_n_o = dst_reset_sync;
endmodule
`default_nettype wire
