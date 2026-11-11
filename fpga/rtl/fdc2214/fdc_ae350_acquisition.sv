`timescale 1ns/1ps

// SPDX-License-Identifier: MIT
// FDC-only physical acquisition. No processor or old RTL mapping algorithm.
// Reference cam2dvi byte/bit engine, open-drain pads and staged startup.
// The FDC-specific register map and immutable frame contract are preserved.
// No pitch/volume mapping and no dependency on the existing audio project.
module fdc_ae350_acquisition #(
    parameter bit ENABLE = 1'b0,
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 12_500,
    parameter integer POWERUP_CYCLES = CLK_HZ / 500,
    parameter integer RESET_HOLD_CYCLES = CLK_HZ / 500,
    parameter integer CONFIG_DELAY_CYCLES = (CLK_HZ / 1000) * 60,
    parameter integer POLL_CYCLES = CLK_HZ / 10_000,
    parameter integer TRANSFER_TIMEOUT_CYCLES = CLK_HZ / 50,
    parameter integer DATA_TIMEOUT_CYCLES = CLK_HZ / 10,
    parameter logic [6:0] I2C_ADDR = 7'h2a,
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
    inout wire scl_io,
    inout wire sda_io,
    output wire fdc_addr_o,
    input wire fdc_int_ni,
    output wire fdc_int_active_o,
    output logic scl_drive_low_o,
    output logic sda_drive_low_o,
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
    output wire transport_error_o,
    output wire [1:0] transport_error_code_o,
    output logic sample_valid_o,
    output logic sample_error_o,
    output logic [31:0] sample_count_o
);
    wire scl_pad_i, scl_pad_o, scl_padoen_o;
    wire sda_pad_i, sda_pad_o, sda_padoen_o;
    assign scl_pad_i = scl_io;
    assign scl_io = ~scl_padoen_o ? scl_pad_o : 1'bz;
    assign sda_pad_i = sda_io;
    assign sda_io = ~sda_padoen_o ? sda_pad_o : 1'bz;
    assign scl_drive_low_o = ~scl_padoen_o;
    assign sda_drive_low_o = ~sda_padoen_o;
    assign fdc_addr_o = I2C_ADDR[0];
    generate if (ENABLE) begin : g_enabled
    // Reference top.v uses 100,000/3,000,000 ticks on a 50 MHz clock:
    // release peripheral shutdown at 2 ms, release IIC reset at 60 ms.
    logic [31:0] startup_count;
    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni) startup_count <= 0;
        else if (restart_i) startup_count <= 0;
        else if (startup_count < CONFIG_DELAY_CYCLES) startup_count <= startup_count + 1'b1;
    end
    wire sensor_rst_n = rst_ni && !restart_i && startup_count >= RESET_HOLD_CYCLES;
    wire transport_rst_n = rst_ni && !restart_i && startup_count >= CONFIG_DELAY_CYCLES;
    (* ASYNC_REG = "TRUE" *) logic [1:0] int_sync;
    always_ff @(posedge clk_i) begin
        if (!rst_ni || restart_i) int_sync <= 2'b11;
        else int_sync <= {int_sync[0], fdc_int_ni};
    end
    assign fdc_int_active_o = !int_sync[1];
    // Single-FDC composition; future shared FDC/ToF composition must preserve
    // this device frame contract and drain a shared transaction before restart.
    logic req_valid, req_ready, req_read, rsp_valid;
    logic [7:0] req_register;
    logic [15:0] req_wdata, rsp_rdata;
    logic [1:0] rsp_error;


    fdc_ae350_device #(
        .CLK_HZ(CLK_HZ),
        .POWERUP_CYCLES(POWERUP_CYCLES),
        .POLL_CYCLES(POLL_CYCLES),
        .DATA_TIMEOUT_CYCLES(DATA_TIMEOUT_CYCLES),
        .RCOUNT_CH0(RCOUNT_CH0),
        .RCOUNT_CH1(RCOUNT_CH1),
        .SETTLECOUNT_CH0(SETTLECOUNT_CH0),
        .SETTLECOUNT_CH1(SETTLECOUNT_CH1),
        .CLOCK_DIVIDERS_CH0(CLOCK_DIVIDERS_CH0),
        .CLOCK_DIVIDERS_CH1(CLOCK_DIVIDERS_CH1),
        .IDRIVE_CH0(IDRIVE_CH0),
        .IDRIVE_CH1(IDRIVE_CH1),
        .USE_EXTERNAL_CLOCK(USE_EXTERNAL_CLOCK),
        .LOW_POWER_ACTIVATION(LOW_POWER_ACTIVATION)
    ) device (
        .clk_i(clk_i),
        .rst_ni(sensor_rst_n),
        .restart_i(restart_i),
        .req_valid_o(req_valid),
        .req_ready_i(req_ready),
        .req_read_o(req_read),
        .req_register_o(req_register),
        .req_wdata_o(req_wdata),
        .rsp_valid_i(rsp_valid),
        .rsp_rdata_i(rsp_rdata),
        .rsp_error_i(rsp_error),
        .fdc_sd_o(fdc_sd_o),
        .configured_o(configured_o),
        .fault_o(fault_o),
        .fault_code_o(fault_code_o),
        .raw_ch0_o(raw_ch0_o),
        .raw_ch1_o(raw_ch1_o),
        .flags_ch0_o(flags_ch0_o),
        .flags_ch1_o(flags_ch1_o),
        .status_o(status_o),
        .frame_complete_o(frame_complete_o),
        .sample_valid_o(sample_valid_o),
        .sample_error_o(sample_error_o),
        .sample_count_o(sample_count_o)
    );

    fdc_cam_i2c_reg16_master #(
        .CLK_HZ(CLK_HZ), .I2C_HZ(I2C_HZ),
        .TRANSFER_TIMEOUT_CYCLES(TRANSFER_TIMEOUT_CYCLES), .I2C_ADDR(I2C_ADDR)
    ) register_master (
        .clk_i(clk_i), .rst_ni(transport_rst_n),
        .req_valid_i(req_valid), .req_ready_o(req_ready), .req_read_i(req_read),
        .req_register_i(req_register), .req_wdata_i(req_wdata),
        .rsp_valid_o(rsp_valid), .rsp_rdata_o(rsp_rdata), .rsp_error_o(rsp_error),
        .scl_i(scl_pad_i), .sda_i(sda_pad_i),
        .scl_pad_o(scl_pad_o), .sda_pad_o(sda_pad_o),
        .scl_padoen_o(scl_padoen_o), .sda_padoen_o(sda_padoen_o)
    );
    assign transport_error_o = transport_rst_n && rsp_valid && rsp_error != 0;
    assign transport_error_code_o = transport_error_o ? rsp_error : 2'b00;
    end else begin : g_disabled
        assign scl_pad_o = 1'b0;
        assign sda_pad_o = 1'b0;
        assign scl_padoen_o = 1'b1;
        assign sda_padoen_o = 1'b1;
        assign fdc_int_active_o = 1'b0;
        assign fdc_sd_o = 1'b1;
        assign configured_o = 1'b0;
        assign fault_o = 1'b0;
        assign fault_code_o = 4'd0;
        assign raw_ch0_o = 28'd0;
        assign raw_ch1_o = 28'd0;
        assign flags_ch0_o = 2'd0;
        assign flags_ch1_o = 2'd0;
        assign status_o = 16'd0;
        assign frame_complete_o = 1'b0;
        assign transport_error_o = 1'b0;
        assign transport_error_code_o = 2'd0;
        assign sample_valid_o = 1'b0;
        assign sample_error_o = 1'b0;
        assign sample_count_o = 32'd0;
    end endgenerate
`ifndef SYNTHESIS
    initial begin
        if (RESET_HOLD_CYCLES < 1 || CONFIG_DELAY_CYCLES < RESET_HOLD_CYCLES)
            $fatal(1, "Invalid staged FDC startup delay");
        if (I2C_ADDR != 7'h2a && I2C_ADDR != 7'h2b)
            $fatal(1, "FDC2214 ADDR supports only 0x2A/0x2B");
    end
`endif
endmodule
