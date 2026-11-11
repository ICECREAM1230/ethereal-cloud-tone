`timescale 1ns/1ps

// SPDX-License-Identifier: MIT
// Derived from src/control_input/sensor_i2c/rtl/fdc2214_device.sv.
// Complete-frame strobe is independent of quality acceptance.
// Transport-independent dual-channel FDC2214 configuration/acquisition client.
// No pitch/volume mapping and no dependency on the existing audio project.
module fdc_ae350_device #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer POWERUP_CYCLES = CLK_HZ / 500,
    parameter integer POLL_CYCLES = CLK_HZ / 10_000,
    parameter integer DATA_TIMEOUT_CYCLES = CLK_HZ / 10,
    parameter logic [15:0] RCOUNT_CH0 = 16'h1000,
    parameter logic [15:0] RCOUNT_CH1 = 16'h1000,
    parameter logic [15:0] SETTLECOUNT_CH0 = 16'h0100,
    parameter logic [15:0] SETTLECOUNT_CH1 = 16'h0100,
    parameter logic [15:0] CLOCK_DIVIDERS_CH0 = 16'h2001,
    parameter logic [15:0] CLOCK_DIVIDERS_CH1 = 16'h2001,
    parameter logic [4:0] IDRIVE_CH0 = 5'd15,
    parameter logic [4:0] IDRIVE_CH1 = 5'd15,
    parameter bit USE_EXTERNAL_CLOCK = 1'b1,
    parameter bit LOW_POWER_ACTIVATION = 1'b0
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic restart_i,
    output logic req_valid_o,
    input  logic req_ready_i,
    output logic req_read_o,
    output logic [7:0] req_register_o,
    output logic [15:0] req_wdata_o,
    input  logic rsp_valid_i,
    input  logic [15:0] rsp_rdata_i,
    input  logic [1:0] rsp_error_i,
    output logic fdc_sd_o,
    output logic configured_o,
    output logic fault_o,
    output logic [3:0] fault_code_o,
    output logic [27:0] raw_ch0_o,
    output logic [27:0] raw_ch1_o,
    output logic [1:0] flags_ch0_o,
    output logic [1:0] flags_ch1_o,
    output logic [15:0] status_o,
    output logic frame_complete_o,
    output logic sample_valid_o,
    output logic sample_error_o,
    output logic [31:0] sample_count_o
);
    localparam logic [3:0] FAULT_NONE = 4'd0,
        FAULT_NACK = 4'd1, FAULT_TRANSFER_TIMEOUT = 4'd2,
        FAULT_ID = 4'd3, FAULT_VERIFY = 4'd4, FAULT_DATA_TIMEOUT = 4'd5;

    typedef enum logic [4:0] {
        POWER_WAIT, MANUFACTURER_REQUEST, MANUFACTURER_WAIT,
        DEVICE_REQUEST, DEVICE_WAIT, CONFIG_WRITE_REQUEST, CONFIG_WRITE_WAIT,
        CONFIG_READ_REQUEST, CONFIG_READ_WAIT, POLL_WAIT,
        STATUS_REQUEST, STATUS_WAIT, DATA_REQUEST, DATA_WAIT, FAULT
    } state_t;
    state_t state;
    integer unsigned wait_cycles;
    integer unsigned fresh_cycles;
    logic [3:0] rom_index;
    logic rom_valid;
    logic [7:0] rom_register;
    logic [15:0] rom_value, rom_verify_mask;
    logic [1:0] data_index;
    logic [13:0] ch0_msb, ch1_msb;
    logic [15:0] ch0_lsb, status_snapshot;
    logic [2:0] pending_errors;
    logic [1:0] pending_error_channel;
    logic [15:0] merged_status;
    logic local_rst_n;

    assign local_rst_n = rst_ni && !restart_i;
    assign fdc_sd_o = !local_rst_n;

    // STATUS reads clear latched diagnostics. Preserve errors seen before DRDY;
    // if several channels report errors, the channel field tracks the latest.
    always_comb begin
        merged_status = rsp_rdata_i;
        merged_status[11:9] = rsp_rdata_i[11:9] | pending_errors;
        if (rsp_rdata_i[11:9] == 0 && pending_errors != 0)
            merged_status[15:14] = pending_error_channel;
    end

    fdc2214_config_rom #(
        .RCOUNT_CH0(RCOUNT_CH0), .RCOUNT_CH1(RCOUNT_CH1),
        .SETTLECOUNT_CH0(SETTLECOUNT_CH0), .SETTLECOUNT_CH1(SETTLECOUNT_CH1),
        .CLOCK_DIVIDERS_CH0(CLOCK_DIVIDERS_CH0), .CLOCK_DIVIDERS_CH1(CLOCK_DIVIDERS_CH1),
        .IDRIVE_CH0(IDRIVE_CH0), .IDRIVE_CH1(IDRIVE_CH1),
        .USE_EXTERNAL_CLOCK(USE_EXTERNAL_CLOCK), .LOW_POWER_ACTIVATION(LOW_POWER_ACTIVATION)
    ) configuration_rom (
        .index_i(rom_index), .valid_o(rom_valid), .register_o(rom_register),
        .value_o(rom_value), .verify_mask_o(rom_verify_mask)
    );

    always_comb begin
        req_valid_o = 1'b0;
        req_read_o = 1'b1;
        req_register_o = 8'h00;
        req_wdata_o = rom_value;
        case (state)
            MANUFACTURER_REQUEST: begin req_valid_o = 1'b1; req_register_o = 8'h7e; end
            DEVICE_REQUEST: begin req_valid_o = 1'b1; req_register_o = 8'h7f; end
            CONFIG_WRITE_REQUEST: begin
                req_valid_o = rom_valid;
                req_read_o = 1'b0;
                req_register_o = rom_register;
            end
            CONFIG_READ_REQUEST: begin req_valid_o = rom_valid; req_register_o = rom_register; end
            STATUS_REQUEST: begin req_valid_o = 1'b1; req_register_o = 8'h18; end
            DATA_REQUEST: begin req_valid_o = 1'b1; req_register_o = {6'b0, data_index}; end
            default: begin end
        endcase
        if (!local_rst_n) req_valid_o = 1'b0;
    end

    always_ff @(posedge clk_i) begin
        if (!local_rst_n) begin
            state <= POWER_WAIT;
            wait_cycles <= 0;
            fresh_cycles <= 0;
            rom_index <= 4'd0;
            data_index <= 2'd0;
            ch0_msb <= 14'h0000;
            ch0_lsb <= 16'h0000;
            ch1_msb <= 14'h0000;
            status_snapshot <= 16'h0000;
            pending_errors <= 3'b000;
            pending_error_channel <= 2'b00;
            configured_o <= 1'b0;
            fault_o <= 1'b0;
            fault_code_o <= FAULT_NONE;
            raw_ch0_o <= 28'd0;
            raw_ch1_o <= 28'd0;
            flags_ch0_o <= 2'b00;
            flags_ch1_o <= 2'b00;
            status_o <= 16'h0000;
            frame_complete_o <= 1'b0;
            sample_valid_o <= 1'b0;
            sample_error_o <= 1'b0;
            sample_count_o <= 32'd0;
        end else begin
            frame_complete_o <= 1'b0;
            sample_valid_o <= 1'b0;
            sample_error_o <= 1'b0;
            // Saturation prevents wraparound during a prolonged data outage.
            if (configured_o && fresh_cycles < DATA_TIMEOUT_CYCLES)
                fresh_cycles <= fresh_cycles + 1;

            if (rsp_valid_i && rsp_error_i != 0 && state != FAULT) begin
                fault_o <= 1'b1;
                fault_code_o <= (rsp_error_i == 2'd1) ? FAULT_NACK : FAULT_TRANSFER_TIMEOUT;
                configured_o <= 1'b0;
                state <= FAULT;
            end else if (configured_o && fresh_cycles >= DATA_TIMEOUT_CYCLES
                         && (state == POLL_WAIT || (rsp_valid_i
                             && (state == STATUS_WAIT || state == DATA_WAIT)))) begin
                // Finish the current register transaction, then reject a late
                // frame before it can publish data and reset the deadline.
                fault_o <= 1'b1;
                fault_code_o <= FAULT_DATA_TIMEOUT;
                configured_o <= 1'b0;
                state <= FAULT;
            end else begin
                case (state)
                    POWER_WAIT: begin
                        if (wait_cycles >= POWERUP_CYCLES-1) begin
                            wait_cycles <= 0;
                            state <= MANUFACTURER_REQUEST;
                        end else wait_cycles <= wait_cycles + 1;
                    end
                    MANUFACTURER_REQUEST: if (req_ready_i) state <= MANUFACTURER_WAIT;
                    MANUFACTURER_WAIT: begin
                        if (rsp_valid_i) begin
                            if (rsp_rdata_i == 16'h5449) state <= DEVICE_REQUEST;
                            else begin
                                fault_o <= 1'b1;
                                fault_code_o <= FAULT_ID;
                                state <= FAULT;
                            end
                        end
                    end
                    DEVICE_REQUEST: if (req_ready_i) state <= DEVICE_WAIT;
                    DEVICE_WAIT: begin
                        if (rsp_valid_i) begin
                            if (rsp_rdata_i == 16'h3055) state <= CONFIG_WRITE_REQUEST;
                            else begin
                                fault_o <= 1'b1;
                                fault_code_o <= FAULT_ID;
                                state <= FAULT;
                            end
                        end
                    end
                    CONFIG_WRITE_REQUEST: if (req_ready_i) state <= CONFIG_WRITE_WAIT;
                    CONFIG_WRITE_WAIT: if (rsp_valid_i) state <= CONFIG_READ_REQUEST;
                    CONFIG_READ_REQUEST: if (req_ready_i) state <= CONFIG_READ_WAIT;
                    CONFIG_READ_WAIT: begin
                        if (rsp_valid_i) begin
                            if ((rsp_rdata_i & rom_verify_mask) != (rom_value & rom_verify_mask)) begin
                                fault_o <= 1'b1;
                                fault_code_o <= FAULT_VERIFY;
                                state <= FAULT;
                            end else if (rom_index == 4'd11) begin
                                configured_o <= 1'b1;
                                wait_cycles <= 0;
                                fresh_cycles <= 0;
                                state <= POLL_WAIT;
                            end else begin
                                rom_index <= rom_index + 1'b1;
                                state <= CONFIG_WRITE_REQUEST;
                            end
                        end
                    end
                    POLL_WAIT: begin
                        // Check freshness between transactions so a data timeout
                        // never abandons a byte transfer or exposes half a frame.
                        if (wait_cycles >= POLL_CYCLES-1) begin
                            wait_cycles <= 0;
                            state <= STATUS_REQUEST;
                        end else wait_cycles <= wait_cycles + 1;
                    end
                    STATUS_REQUEST: if (req_ready_i) state <= STATUS_WAIT;
                    STATUS_WAIT: begin
                        if (rsp_valid_i) begin
                            // In sequential mode DRDY means all active channels
                            // completed (SNOSCZ5B p32). UNREAD is diagnostic;
                            // prior STATUS reads can affect the latched flags.
                            if (rsp_rdata_i[6]) begin
                                status_snapshot <= merged_status;
                                data_index <= 2'd0;
                                state <= DATA_REQUEST;
                            end else begin
                                pending_errors <= merged_status[11:9];
                                pending_error_channel <= merged_status[15:14];
                                wait_cycles <= 0;
                                state <= POLL_WAIT;
                            end
                        end
                    end
                    DATA_REQUEST: if (req_ready_i) state <= DATA_WAIT;
                    DATA_WAIT: begin
                        if (rsp_valid_i) begin
                            case (data_index)
                                2'd0: ch0_msb <= rsp_rdata_i[13:0];
                                2'd1: ch0_lsb <= rsp_rdata_i;
                                2'd2: ch1_msb <= rsp_rdata_i[13:0];
                                2'd3: begin
                                    frame_complete_o <= 1'b1;
                                    raw_ch0_o <= {ch0_msb[11:0], ch0_lsb};
                                    raw_ch1_o <= {ch1_msb[11:0], rsp_rdata_i};
                                    flags_ch0_o <= ch0_msb[13:12];
                                    flags_ch1_o <= ch1_msb[13:12];
                                    status_o <= status_snapshot;
                                    pending_errors <= 3'b000;
                                    pending_error_channel <= 2'b00;
                                    if ((status_snapshot & 16'h0e00) != 0
                                        || ch0_msb[13:12] != 0 || ch1_msb[13:12] != 0) begin
                                        sample_error_o <= 1'b1;
                                    end else begin
                                        sample_valid_o <= 1'b1;
                                        sample_count_o <= sample_count_o + 1'b1;
                                    end
                                    fresh_cycles <= 0;
                                    wait_cycles <= 0;
                                end
                            endcase
                            if (data_index == 2'd3) state <= POLL_WAIT;
                            else begin
                                data_index <= data_index + 1'b1;
                                state <= DATA_REQUEST;
                            end
                        end
                    end
                    FAULT: begin end // Explicit reset/restart is required.
                    default: begin
                        fault_o <= 1'b1;
                        fault_code_o <= FAULT_VERIFY;
                        configured_o <= 1'b0;
                        state <= FAULT;
                    end
                endcase
            end
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (POWERUP_CYCLES < 1 || POLL_CYCLES < 1 || DATA_TIMEOUT_CYCLES < 1)
            $fatal(1, "FDC2214 wait parameters must be positive");
        if (CLOCK_DIVIDERS_CH0[9:0] == 0 || CLOCK_DIVIDERS_CH1[9:0] == 0)
            $fatal(1, "FDC2214 reference dividers must be nonzero");
        if ((CLOCK_DIVIDERS_CH0 & 16'hcc00) != 0 || (CLOCK_DIVIDERS_CH1 & 16'hcc00) != 0)
            $fatal(1, "CLOCK_DIVIDERS reserved fields must be zero");
        if (RCOUNT_CH0 < 16'h0100 || RCOUNT_CH1 < 16'h0100)
            $fatal(1, "RCOUNT 0x0000-0x00ff is reserved");
        if ((CLOCK_DIVIDERS_CH0[13:12] != 2'b01 && CLOCK_DIVIDERS_CH0[13:12] != 2'b10)
            || (CLOCK_DIVIDERS_CH1[13:12] != 2'b01 && CLOCK_DIVIDERS_CH1[13:12] != 2'b10))
            $fatal(1, "FIN_SEL must be divide-by-1 or divide-by-2");
        if (!USE_EXTERNAL_CLOCK
            && (CLOCK_DIVIDERS_CH0[9:0] < 2 || CLOCK_DIVIDERS_CH1[9:0] < 2))
            $fatal(1, "Internal oscillator needs FREF_DIVIDER >=2 for the 40MHz fREF limit");
    end
`endif
endmodule
