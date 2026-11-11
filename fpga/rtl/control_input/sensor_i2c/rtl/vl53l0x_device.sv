`timescale 1ns/1ps

// Fixed 33 ms, continuous back-to-back VL53L0X client. See
// docs/vl53l0x_sources.md for source versions, licenses and profile derivation.
module vl53l0x_device #(
    parameter int unsigned CLK_HZ = 50_000_000,
    parameter int unsigned MAX_BYTES = 32,
    parameter int unsigned LEN_W = $clog2(MAX_BYTES + 1),
    parameter logic [6:0] I2C_ADDR = 7'h29,
    parameter int unsigned RESET_CYCLES = CLK_HZ / 1000,
    parameter int unsigned BOOT_CYCLES = CLK_HZ / 500,
    parameter int unsigned POLL_CYCLES = CLK_HZ / 1000,
    parameter int unsigned READY_TIMEOUT_CYCLES = CLK_HZ / 2
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic restart_i,
    input  logic int_ni,
    output logic xshut_o,
    output logic int_active_o,
    output logic configured_o,
    output logic fault_o,
    output logic [3:0] fault_code_o,
    output logic [15:0] distance_mm_o,
    output logic [7:0] range_status_o,
    output logic sample_valid_o,
    output logic sample_error_o,
    output logic [31:0] sample_count_o,
    output logic req_valid_o,
    input  logic req_ready_i,
    output logic [6:0] req_address_o,
    output logic [LEN_W-1:0] req_tx_len_o,
    output logic [LEN_W-1:0] req_rx_len_o,
    output logic [8*MAX_BYTES-1:0] req_tx_data_o,
    input  logic rsp_valid_i,
    output logic rsp_ready_o,
    input  logic [8*MAX_BYTES-1:0] rsp_rx_data_i,
    input  logic [1:0] rsp_error_i
);
    localparam int unsigned RESET_LIMIT = (RESET_CYCLES < 1) ? 1 : RESET_CYCLES;
    localparam int unsigned BOOT_LIMIT = (BOOT_CYCLES < 1) ? 1 : BOOT_CYCLES;
    localparam int unsigned POLL_LIMIT = (POLL_CYCLES < 1) ? 1 : POLL_CYCLES;
    localparam int unsigned TIMEOUT_LIMIT = (READY_TIMEOUT_CYCLES < 1)
        ? 1 : READY_TIMEOUT_CYCLES;

    typedef enum logic [3:0] {
        ST_RESET, ST_BOOT, ST_FETCH, ST_ISSUE, ST_WAIT,
        ST_POLL_DELAY, ST_FILTER, ST_DRAIN, ST_FAULT
    } state_t;
    state_t state_q;
    logic [7:0] pc_q;
    logic [3:0] operation;
    logic [7:0] register_address;
    logic [15:0] value;
    logic [3:0] read_length;
    logic [2:0] capture;
    logic int_meta_q, int_sync_q;
    logic outstanding_q;
    logic poll_active_q;
    logic [31:0] timer_q, poll_timer_q;
    logic [7:0] temporary_q, stop_variable_q;
    logic [6:0] spad_count_q, spads_enabled_q;
    logic aperture_q;
    logic [47:0] spad_map_q;
    logic [5:0] spad_index_q;
    logic [15:0] pending_distance_q;
    logic [7:0] pending_status_q;
    logic keep_spad;
    // The response contract has MAX_BYTES bytes; this profile consumes <=12.
    logic unused_response_bytes;
    generate
        if (MAX_BYTES > 12) begin : g_unused_wide_response
            assign unused_response_bytes = ^{rsp_rx_data_i[8*MAX_BYTES-1:96],
                                             rsp_rx_data_i[79:48]};
        end else begin : g_unused_minimum_response
            assign unused_response_bytes = ^rsp_rx_data_i[79:48];
        end
    endgenerate

    vl53l0x_init_rom program_rom (
        .index_i(pc_q), .operation_o(operation), .register_o(register_address),
        .value_o(value), .read_len_o(read_length), .capture_o(capture)
    );

    always_comb begin
        req_address_o = I2C_ADDR;
        req_tx_len_o = LEN_W'(2);
        req_rx_len_o = '0;
        req_tx_data_o = '0;
        req_tx_data_o[7:0] = register_address;
        req_tx_data_o[15:8] = value[7:0];
        case (operation)
            4'h1: begin
                req_tx_len_o = LEN_W'(3);
                req_tx_data_o[15:8] = value[15:8];
                req_tx_data_o[23:16] = value[7:0];
            end
            4'h2, 4'h6, 4'h7, 4'hb: begin
                req_tx_len_o = LEN_W'(1);
                req_rx_len_o = LEN_W'(read_length);
                req_tx_data_o[15:8] = 8'h00;
            end
            4'h3: req_tx_data_o[15:8] = temporary_q | value[7:0];
            4'h4: req_tx_data_o[15:8] = temporary_q & value[7:0];
            4'h5: req_tx_data_o[15:8] = stop_variable_q;
            4'h9: begin
                req_tx_len_o = LEN_W'(7);
                req_tx_data_o[55:8] = spad_map_q;
            end
            default: begin end
        endcase
    end

    assign req_valid_o = (state_q == ST_ISSUE) && !restart_i && !fault_o;
    // A timed-out or restarted client continues to consume its accepted reply.
    // It never reuses that reply as the first transaction of a new generation.
    assign rsp_ready_o = outstanding_q;
    assign xshut_o = (state_q != ST_RESET) && (state_q != ST_DRAIN)
        && !restart_i && !fault_o && rst_ni;
    assign int_active_o = !int_sync_q;
    assign keep_spad = (!aperture_q || spad_index_q >= 6'd12)
        && (spads_enabled_q < spad_count_q) && spad_map_q[spad_index_q];

    task automatic latch_fault(input logic [3:0] code);
        begin
            fault_o <= 1'b1;
            fault_code_o <= code;
            configured_o <= 1'b0;
            poll_active_q <= 1'b0;
            sample_error_o <= 1'b1;
            state_q <= ST_FAULT;
        end
    endtask

    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            state_q <= ST_RESET;
            pc_q <= 8'd0;
            timer_q <= 32'd0;
            poll_timer_q <= 32'd0;
            poll_active_q <= 1'b0;
            outstanding_q <= 1'b0;
            int_meta_q <= 1'b1;
            int_sync_q <= 1'b1;
            temporary_q <= 8'd0;
            stop_variable_q <= 8'd0;
            spad_count_q <= 7'd0;
            spads_enabled_q <= 7'd0;
            aperture_q <= 1'b0;
            spad_map_q <= 48'd0;
            spad_index_q <= 6'd0;
            pending_distance_q <= 16'd0;
            pending_status_q <= 8'd0;
            configured_o <= 1'b0;
            fault_o <= 1'b0;
            fault_code_o <= 4'd0;
            distance_mm_o <= 16'd0;
            range_status_o <= 8'd0;
            sample_valid_o <= 1'b0;
            sample_error_o <= 1'b0;
            sample_count_o <= 32'd0;
        end else begin
            int_meta_q <= int_ni;
            int_sync_q <= int_meta_q;
            sample_valid_o <= 1'b0;
            sample_error_o <= 1'b0;
            if (req_valid_o && req_ready_i)
                outstanding_q <= 1'b1;
            if (rsp_valid_i && rsp_ready_o)
                outstanding_q <= 1'b0;

            if (restart_i) begin
                pc_q <= 8'd0;
                timer_q <= 32'd0;
                poll_timer_q <= 32'd0;
                poll_active_q <= 1'b0;
                configured_o <= 1'b0;
                fault_o <= 1'b0;
                fault_code_o <= 4'd0;
                distance_mm_o <= 16'd0;
                range_status_o <= 8'd0;
                sample_count_o <= 32'd0;
                temporary_q <= 8'd0;
                stop_variable_q <= 8'd0;
                spad_count_q <= 7'd0;
                aperture_q <= 1'b0;
                spad_map_q <= 48'd0;
                pending_distance_q <= 16'd0;
                pending_status_q <= 8'd0;
                if (outstanding_q && !rsp_valid_i)
                    state_q <= ST_DRAIN;
                else
                    state_q <= ST_RESET;
            end else if (poll_active_q && poll_timer_q >= TIMEOUT_LIMIT-1) begin
                if (operation == 4'h6)
                    latch_fault(4'd6);
                else if (operation == 4'h7)
                    latch_fault(4'd7);
                else
                    latch_fault(4'd8);
            end else begin
                if (poll_active_q)
                    poll_timer_q <= poll_timer_q + 32'd1;
                case (state_q)
                    ST_RESET: begin
                        if (timer_q >= RESET_LIMIT-1) begin
                            timer_q <= 32'd0;
                            state_q <= ST_BOOT;
                        end else timer_q <= timer_q + 32'd1;
                    end
                    ST_BOOT: begin
                        if (timer_q >= BOOT_LIMIT-1) begin
                            timer_q <= 32'd0;
                            state_q <= ST_FETCH;
                        end else timer_q <= timer_q + 32'd1;
                    end
                    ST_FETCH: begin
                        timer_q <= 32'd0;
                        case (operation)
                            4'h8: begin
                                spad_index_q <= 6'd0;
                                spads_enabled_q <= 7'd0;
                                state_q <= ST_FILTER;
                            end
                            4'ha: begin
                                configured_o <= 1'b1;
                                pc_q <= pc_q + 8'd1;
                            end
                            4'hc: pc_q <= value[7:0];
                            4'hf: latch_fault(4'd3);
                            default: begin
                                if ((operation == 4'h6 || operation == 4'h7
                                        || operation == 4'hb) && !poll_active_q) begin
                                    poll_timer_q <= 32'd0;
                                    poll_active_q <= 1'b1;
                                end
                                state_q <= ST_ISSUE;
                            end
                        endcase
                    end
                    ST_ISSUE: begin
                        if (req_ready_i) begin
                            timer_q <= 32'd0;
                            state_q <= ST_WAIT;
                        end else if (timer_q >= TIMEOUT_LIMIT-1)
                            latch_fault(4'd2);
                        else timer_q <= timer_q + 32'd1;
                    end
                    ST_WAIT: begin
                        if (rsp_valid_i) begin
                            timer_q <= 32'd0;
                            if (rsp_error_i != 2'd0) begin
                                case (rsp_error_i)
                                    2'd1: latch_fault(4'd1);
                                    2'd2: latch_fault(4'd2);
                                    default: latch_fault(4'd3);
                                endcase
                            end else if (operation == 4'h6 || operation == 4'h7
                                    || operation == 4'hb) begin
                                if ((operation == 4'h6 && rsp_rx_data_i[7:0] != 8'd0)
                                        || (operation != 4'h6 && rsp_rx_data_i[2:0] != 3'd0)) begin
                                    poll_active_q <= 1'b0;
                                    pc_q <= pc_q + 8'd1;
                                    state_q <= ST_FETCH;
                                end else begin
                                    timer_q <= POLL_LIMIT-1;
                                    state_q <= ST_POLL_DELAY;
                                end
                            end else begin
                                pc_q <= pc_q + 8'd1;
                                state_q <= ST_FETCH;
                                case (capture)
                                    3'd1: if (rsp_rx_data_i[7:0] != 8'hee)
                                        latch_fault(4'd4);
                                    3'd2: temporary_q <= rsp_rx_data_i[7:0];
                                    3'd3: stop_variable_q <= rsp_rx_data_i[7:0];
                                    3'd4: begin
                                        spad_count_q <= rsp_rx_data_i[6:0];
                                        aperture_q <= rsp_rx_data_i[7];
                                        if (rsp_rx_data_i[6:0] == 7'd0
                                                || rsp_rx_data_i[6:0] > 7'd48)
                                            latch_fault(4'd5);
                                    end
                                    3'd5: spad_map_q <= rsp_rx_data_i[47:0];
                                    3'd6: begin
                                        pending_status_q <= rsp_rx_data_i[7:0];
                                        pending_distance_q <= {rsp_rx_data_i[87:80],
                                                               rsp_rx_data_i[95:88]};
                                    end
                                    3'd7: begin
                                        range_status_o <= pending_status_q;
                                        if (pending_status_q[6:3] == 4'd11) begin
                                            distance_mm_o <= pending_distance_q;
                                            sample_valid_o <= 1'b1;
                                            sample_count_o <= sample_count_o + 32'd1;
                                        end else sample_error_o <= 1'b1;
                                    end
                                    default: begin end
                                endcase
                            end
                        end else if (timer_q >= TIMEOUT_LIMIT-1)
                            latch_fault(4'd2);
                        else timer_q <= timer_q + 32'd1;
                    end
                    ST_POLL_DELAY: begin
                        if (timer_q == 32'd0)
                            state_q <= ST_FETCH;
                        else timer_q <= timer_q - 32'd1;
                    end
                    ST_FILTER: begin
                        if (!keep_spad)
                            spad_map_q[spad_index_q] <= 1'b0;
                        else spads_enabled_q <= spads_enabled_q + 7'd1;
                        if (spad_index_q == 6'd47) begin
                            if ((spads_enabled_q + {6'd0, keep_spad}) != spad_count_q)
                                latch_fault(4'd5);
                            else begin
                                pc_q <= pc_q + 8'd1;
                                state_q <= ST_FETCH;
                            end
                        end else spad_index_q <= spad_index_q + 6'd1;
                    end
                    ST_DRAIN: begin
                        if (!outstanding_q || rsp_valid_i) begin
                            timer_q <= 32'd0;
                            state_q <= ST_RESET;
                        end else if (timer_q >= TIMEOUT_LIMIT-1)
                            latch_fault(4'd2);
                        else timer_q <= timer_q + 32'd1;
                    end
                    ST_FAULT: begin end
                    default: latch_fault(4'd3);
                endcase
            end
        end
    end
endmodule
