`timescale 1ns/1ps

// Standalone dual-channel FDC2214 configuration and acquisition controller.
// No pitch/volume mapping and no dependency on the existing audio project.
module fdc2214_core #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 100_000,
    parameter integer POWERUP_CYCLES = CLK_HZ / 500,
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
    input  logic scl_i,
    input  logic sda_i,
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
    output logic sample_valid_o,
    output logic sample_error_o,
    output logic [31:0] sample_count_o
);
    // Compatibility composition: one device FSM + the original reg16 adapter.
    // Shared multi-device designs instantiate fdc2214_bus_client instead.
    logic req_valid, req_ready, req_read, rsp_valid;
    logic [7:0] req_register;
    logic [15:0] req_wdata, rsp_rdata;
    logic [1:0] rsp_error;
    wire local_rst_n = rst_ni && !restart_i;

    fdc2214_device #(
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
        .rst_ni(rst_ni),
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
        .sample_valid_o(sample_valid_o),
        .sample_error_o(sample_error_o),
        .sample_count_o(sample_count_o)
    );

    i2c_reg16_master #(
        .CLK_HZ(CLK_HZ), .I2C_HZ(I2C_HZ),
        .TRANSFER_TIMEOUT_CYCLES(TRANSFER_TIMEOUT_CYCLES), .I2C_ADDR(I2C_ADDR)
    ) register_master (
        .clk_i(clk_i), .rst_ni(local_rst_n),
        .req_valid_i(req_valid), .req_ready_o(req_ready), .req_read_i(req_read),
        .req_register_i(req_register), .req_wdata_i(req_wdata),
        .rsp_valid_o(rsp_valid), .rsp_rdata_o(rsp_rdata), .rsp_error_o(rsp_error),
        .scl_i(scl_i), .sda_i(sda_i), .scl_drive_low_o(scl_drive_low_o),
        .sda_drive_low_o(sda_drive_low_o)
    );
endmodule
