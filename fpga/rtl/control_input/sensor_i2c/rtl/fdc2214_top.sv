`timescale 1ns/1ps

// Physical I/O wrapper. External SCL/SDA pull-ups and a compatible I/O bank
// voltage are required. See constraints/fdc2214_h4.cst for the supplied H4 map.
// INT is synchronized for observation; acquisition still polls STATUS/DRDY.
module fdc2214_top #(
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
    inout  wire scl_io,
    inout  wire sda_io,
    output wire fdc_sd_o,
    output wire configured_o,
    output wire fault_o,
    output wire [3:0] fault_code_o,
    output wire [27:0] raw_ch0_o,
    output wire [27:0] raw_ch1_o,
    output wire [1:0] flags_ch0_o,
    output wire [1:0] flags_ch1_o,
    output wire [15:0] status_o,
    output wire sample_valid_o,
    output wire sample_error_o,
    output wire [31:0] sample_count_o,
    output wire fdc_addr_o,
    input  wire fdc_int_ni,
    output wire fdc_int_active_o
);
    wire scl_drive_low, sda_drive_low;
    assign scl_io = scl_drive_low ? 1'b0 : 1'bz;
    assign sda_io = sda_drive_low ? 1'b0 : 1'bz;
    // Keep the physical strap and the bus address consistent, including reset.
    assign fdc_addr_o = I2C_ADDR[0];
    initial begin
        if (I2C_ADDR != 7'h2a && I2C_ADDR != 7'h2b)
            $fatal(1, "FDC2214 physical ADDR supports only 0x2A or 0x2B");
    end
    (* ASYNC_REG = "TRUE" *) logic [1:0] int_sync;
    always_ff @(posedge clk_i) begin
        if (!rst_ni || restart_i) int_sync <= 2'b11;
        else int_sync <= {int_sync[0], fdc_int_ni};
    end
    assign fdc_int_active_o = !int_sync[1];

    fdc2214_core #(
        .CLK_HZ(CLK_HZ), .I2C_HZ(I2C_HZ), .POWERUP_CYCLES(POWERUP_CYCLES),
        .POLL_CYCLES(POLL_CYCLES), .TRANSFER_TIMEOUT_CYCLES(TRANSFER_TIMEOUT_CYCLES),
        .DATA_TIMEOUT_CYCLES(DATA_TIMEOUT_CYCLES), .I2C_ADDR(I2C_ADDR),
        .RCOUNT_CH0(RCOUNT_CH0), .RCOUNT_CH1(RCOUNT_CH1),
        .SETTLECOUNT_CH0(SETTLECOUNT_CH0), .SETTLECOUNT_CH1(SETTLECOUNT_CH1),
        .CLOCK_DIVIDERS_CH0(CLOCK_DIVIDERS_CH0), .CLOCK_DIVIDERS_CH1(CLOCK_DIVIDERS_CH1),
        .IDRIVE_CH0(IDRIVE_CH0), .IDRIVE_CH1(IDRIVE_CH1),
        .USE_EXTERNAL_CLOCK(USE_EXTERNAL_CLOCK), .LOW_POWER_ACTIVATION(LOW_POWER_ACTIVATION)
    ) core (
        .clk_i(clk_i), .rst_ni(rst_ni), .restart_i(restart_i),
        .scl_i(scl_io), .sda_i(sda_io),
        .scl_drive_low_o(scl_drive_low), .sda_drive_low_o(sda_drive_low),
        .fdc_sd_o(fdc_sd_o), .configured_o(configured_o), .fault_o(fault_o),
        .fault_code_o(fault_code_o), .raw_ch0_o(raw_ch0_o), .raw_ch1_o(raw_ch1_o),
        .flags_ch0_o(flags_ch0_o), .flags_ch1_o(flags_ch1_o), .status_o(status_o),
        .sample_valid_o(sample_valid_o), .sample_error_o(sample_error_o),
        .sample_count_o(sample_count_o)
    );
endmodule
