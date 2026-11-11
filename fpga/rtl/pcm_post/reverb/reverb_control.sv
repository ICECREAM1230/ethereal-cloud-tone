`default_nettype none
// RVR1 v1 control only. ACK means the synth parameter pair was applied;
// effect initialization, FM delivery and audible output are separate facts.
module reverb_control #(
    parameter logic [15:0] DEFAULT_WET_Q15 = 16'd8192,
    parameter logic [15:0] DEFAULT_DAMP_Q15 = 16'd8192
) (
    input wire cfg_clk_i, synth_clk_i, rst_n_i, cfg_cs_n_i,
    input wire write_enable_i,
    input wire [15:0] write_addr_i,
    input wire [31:0] write_data_i,
    input wire read_req_toggle_i,
    input wire [15:0] read_addr_i,
    output logic read_selected_o,
    output logic [31:0] read_data_o,
    output wire [15:0] wet_q15_o, damp_q15_o
);
    // Both ends asynchronously forget an old transaction, even with either
    // clock stopped. Release uses the local clock and does not depend on ACK.
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic cfg_reset_meta, cfg_reset_sync;
    (* syn_preserve = 1, ASYNC_REG = "TRUE" *) logic synth_reset_meta, synth_reset_sync;
    always_ff @(posedge cfg_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin cfg_reset_meta <= 0; cfg_reset_sync <= 0; end
        else begin cfg_reset_meta <= 1; cfg_reset_sync <= cfg_reset_meta; end
    end
    always_ff @(posedge synth_clk_i or negedge rst_n_i) begin
        if (!rst_n_i) begin synth_reset_meta <= 0; synth_reset_sync <= 0; end
        else begin synth_reset_meta <= 1; synth_reset_sync <= synth_reset_meta; end
    end

    logic [31:0] shadow_params;
    logic shadow_valid;
    // One immutable source slot: legal sequence[30:0], wet[15:0], damp[15:0].
    // No payload can change until synth applies/rejects it and ACK returns.
    logic [62:0] held_command;
    logic req_cfg, ack_synth;
    (* ASYNC_REG = "TRUE" *) logic ack_meta, ack_sync;
    (* ASYNC_REG = "TRUE" *) logic ready_meta, ready_sync;
    (* ASYNC_REG = "TRUE" *) logic fault_meta, fault_sync;
    (* ASYNC_REG = "TRUE" *) logic req_meta, req_sync;
    logic ready_q, busy_q, fault_q;
    logic [31:0] active_params_q, applied_sequence_q, generation_q;
    (* ASYNC_REG = "TRUE" *) logic cs_meta, cs_sync;
    logic frame_armed;
    // A reset in a long CS-low burst must not reinterpret its remaining
    // bytes as a new startup shadow/COMMIT. Require observed physical CS idle
    // before READY, using the running synth clock, never CS as a new clock.
    always_ff @(posedge synth_clk_i or negedge synth_reset_sync) begin
        if (!synth_reset_sync) begin cs_meta <= 0; cs_sync <= 0; frame_armed <= 0; end
        else begin
            cs_meta <= cfg_cs_n_i; cs_sync <= cs_meta;
            if (cs_sync) frame_armed <= 1;
        end
    end
    wire source_idle = req_cfg == ack_sync;
    wire legal_shadow = write_data_i[31:16] <= 16'd32768 &&
                        write_data_i[15:0] <= 16'd28672;
    wire legal_sequence = !write_data_i[31] && (write_data_i[30:0] != 0);

    always_ff @(posedge cfg_clk_i or negedge cfg_reset_sync) begin
        if (!cfg_reset_sync) begin
            shadow_params <= 0; shadow_valid <= 0;
            held_command <= 0; req_cfg <= 0;
            ack_meta <= 0; ack_sync <= 0;
            ready_meta <= 0; ready_sync <= 0;
            fault_meta <= 0; fault_sync <= 0;
        end else begin
            ack_meta <= ack_synth; ack_sync <= ack_meta;
            ready_meta <= ready_q; ready_sync <= ready_meta;
            fault_meta <= fault_q; fault_sync <= fault_meta;
            if (write_enable_i && write_addr_i == 16'h0725) begin
                // Preparing a later shadow cannot overwrite the held slot.
                // Invalid writes invalidate the previous shadow as well.
                shadow_valid <= ready_sync && legal_shadow;
                if (ready_sync && legal_shadow) shadow_params <= write_data_i;
            end else if (write_enable_i && write_addr_i == 16'h0726 &&
                         ready_sync && !fault_sync && source_idle &&
                         shadow_valid && legal_sequence) begin
                held_command <= {write_data_i[30:0], shadow_params};
                req_cfg <= ~req_cfg;
                shadow_valid <= 0;
            end
        end
    end

    always_ff @(posedge synth_clk_i or negedge synth_reset_sync) begin
        if (!synth_reset_sync) begin req_meta <= 0; req_sync <= 0; end
        else begin req_meta <= req_cfg; req_sync <= req_meta; end
    end

    wire legal_held = (held_command[62:32] != 0) &&
                      (held_command[31:16] <= 16'd32768) &&
                      (held_command[15:0] <= 16'd28672);
    always_ff @(posedge synth_clk_i or negedge synth_reset_sync) begin
        if (!synth_reset_sync) begin
            ready_q <= 0; busy_q <= 0; fault_q <= 0; ack_synth <= 0;
            active_params_q <= {DEFAULT_WET_Q15, DEFAULT_DAMP_Q15};
            applied_sequence_q <= 0; generation_q <= 0;
        end else begin
            ready_q <= frame_armed;
            if (!ready_q && frame_armed) begin
                generation_q <= generation_q + 32'd1;
            end else if (ready_q && !busy_q && req_sync != ack_synth) begin
                busy_q <= 1;
                generation_q <= generation_q + 32'd1;
            end else if (busy_q) begin
                busy_q <= 0;
                ack_synth <= req_sync;
                // BUSY falls on this edge, including a duplicate/rejection.
                // Every public change on an edge shares one generation step.
                generation_q <= generation_q + 32'd1;
                if (!fault_q) begin
                    if (!legal_held) fault_q <= 1;
                    else if (applied_sequence_q == {1'b0, held_command[62:32]}) begin
                        // Retain the most recent applied identity. A retry is
                        // acknowledged without writing the parameter pair.
                        if (active_params_q != held_command[31:0]) fault_q <= 1;
                    end else begin
                        active_params_q <= held_command[31:0];
                        applied_sequence_q <= {1'b0, held_command[62:32]};
                    end
                end
            end
        end
    end
    assign wet_q15_o = active_params_q[31:16];
    assign damp_q15_o = active_params_q[15:0];

    // Match the existing synth_control_registers fetch latency. Both outputs
    // latch together, so a prefetch address cannot switch a streaming word.
    (* ASYNC_REG = "TRUE" *) logic read_req_meta;
    logic read_req_sync, read_req_previous, read_pending;
    always_ff @(posedge synth_clk_i or negedge synth_reset_sync) begin
        if (!synth_reset_sync) begin
            read_req_meta <= 0; read_req_sync <= 0; read_req_previous <= 0;
            read_pending <= 0; read_selected_o <= 0; read_data_o <= 0;
        end else begin
            read_req_meta <= read_req_toggle_i;
            read_req_sync <= read_req_meta;
            read_req_previous <= read_req_sync;
            read_pending <= read_req_sync != read_req_previous;
            if (read_pending) begin
                read_selected_o <= read_addr_i[15:3] == 13'h0e4;
                case (read_addr_i[2:0])
                    3'd0: read_data_o <= 32'h52565231;
                    3'd1: read_data_o <= 32'h00010000;
                    3'd2: read_data_o <= {29'd0, fault_q, busy_q, ready_q};
                    3'd3: read_data_o <= active_params_q;
                    3'd4: read_data_o <= applied_sequence_q;
                    3'd7: read_data_o <= generation_q;
                    default: read_data_o <= 0;
                endcase
            end
        end
    end
`ifndef SYNTHESIS
    initial begin
        assert (DEFAULT_WET_Q15 <= 16'd32768 && DEFAULT_DAMP_Q15 <= 16'd28672)
            else $fatal(1, "RVR1 defaults outside contract range");
    end
`endif
endmodule
`default_nettype wire
