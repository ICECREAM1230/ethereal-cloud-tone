`timescale 1ns/1ps

// Three real device clients, one Master instance, original independent wiring.
// XSHUT is low/release: fit a module-compatible external pull-up, never 5V.
module sensor_i2c_top #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 100_000,
    parameter integer TRANSFER_TIMEOUT_CYCLES = CLK_HZ / 50,
    parameter integer FDC_POWERUP_CYCLES = CLK_HZ / 500,
    parameter integer FDC_POLL_CYCLES = CLK_HZ / 10_000,
    parameter integer FDC_DATA_TIMEOUT_CYCLES = CLK_HZ / 10,
    parameter logic [6:0] FDC_I2C_ADDR = 7'h2a,
    parameter logic [15:0] FDC_RCOUNT_CH0 = 16'h1000,
    parameter logic [15:0] FDC_RCOUNT_CH1 = 16'h1000,
    parameter logic [15:0] FDC_SETTLECOUNT_CH0 = 16'h0100,
    parameter logic [15:0] FDC_SETTLECOUNT_CH1 = 16'h0100,
    parameter logic [15:0] FDC_CLOCK_DIVIDERS_CH0 = 16'h2001,
    parameter logic [15:0] FDC_CLOCK_DIVIDERS_CH1 = 16'h2001,
    parameter logic [4:0] FDC_IDRIVE_CH0 = 5'd15,
    parameter logic [4:0] FDC_IDRIVE_CH1 = 5'd15,
    parameter bit FDC_USE_EXTERNAL_CLOCK = 1'b1,
    parameter bit FDC_LOW_POWER_ACTIVATION = 1'b0,
    parameter integer TOF_RESET_CYCLES = CLK_HZ / 1000,
    parameter integer TOF_BOOT_CYCLES = CLK_HZ / 500,
    parameter integer TOF_POLL_CYCLES = CLK_HZ / 1000,
    parameter integer TOF_READY_TIMEOUT_CYCLES = CLK_HZ / 2,
    parameter integer MAX_BYTES = 32,
    parameter integer LEN_W = $clog2(MAX_BYTES+1)
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic [2:0] restart_i, // [0] FDC, [1] ToF1, [2] ToF2
    inout  wire scl_io,
    inout  wire sda_io,
    output wire fdc_addr_o,
    output wire fdc_sd_o,
    input  logic fdc_int_ni,
    output logic fdc_int_active_o,
    inout  wire tof1_scl_io,
    inout  wire tof1_sda_io,
    inout  wire tof1_xshut_io,
    input  logic tof1_int_ni,
    inout  wire tof2_scl_io,
    inout  wire tof2_sda_io,
    inout  wire tof2_xshut_io,
    input  logic tof2_int_ni,
    output logic [1:0] tof_int_active_o,
    output logic [2:0] configured_o,
    output logic [2:0] fault_o,
    output logic [2:0][3:0] fault_code_o,
    output logic [27:0] fdc_raw_ch0_o,
    output logic [27:0] fdc_raw_ch1_o,
    output logic [1:0] fdc_flags_ch0_o,
    output logic [1:0] fdc_flags_ch1_o,
    output logic [15:0] fdc_status_o,
    output logic fdc_sample_valid_o,
    output logic fdc_sample_error_o,
    output logic [31:0] fdc_sample_count_o,
    output logic [1:0][15:0] tof_distance_mm_o,
    output logic [1:0][7:0] tof_range_status_o,
    output logic [1:0] tof_sample_valid_o,
    output logic [1:0] tof_sample_error_o,
    output logic [1:0][31:0] tof_sample_count_o,
    output logic bus_active_o,
    output logic [1:0] bus_select_o
);
    logic [2:0] req_valid, req_ready, rsp_valid, rsp_ready;
    logic [2:0][1:0] req_bus, rsp_error;
    logic [2:0][6:0] req_address;
    logic [2:0][LEN_W-1:0] tx_len, rx_len;
    logic [2:0][8*MAX_BYTES-1:0] tx_data, rx_data;
    wire [2:0] scl_in = {tof2_scl_io, tof1_scl_io, scl_io};
    wire [2:0] sda_in = {tof2_sda_io, tof1_sda_io, sda_io};
    logic [2:0] scl_low, sda_low;
    logic [1:0] xshut_run;
    wire [1:0] tof_int_n = {tof2_int_ni, tof1_int_ni};
    logic unused_engine_busy;
    (* ASYNC_REG = "TRUE" *) logic [1:0] fdc_int_sync;
    assign fdc_addr_o = FDC_I2C_ADDR[0];
    assign scl_io = scl_low[0] ? 1'b0 : 1'bz;
    assign sda_io = sda_low[0] ? 1'b0 : 1'bz;
    assign tof1_scl_io = scl_low[1] ? 1'b0 : 1'bz;
    assign tof1_sda_io = sda_low[1] ? 1'b0 : 1'bz;
    assign tof2_scl_io = scl_low[2] ? 1'b0 : 1'bz;
    assign tof2_sda_io = sda_low[2] ? 1'b0 : 1'bz;
    assign tof1_xshut_io = xshut_run[0] ? 1'bz : 1'b0;
    assign tof2_xshut_io = xshut_run[1] ? 1'bz : 1'b0;
    assign req_bus = {2'd2, 2'd1, 2'd0};
    assign fdc_int_active_o = !fdc_int_sync[1];
    always_ff @(posedge clk_i) begin
        if (!rst_ni || restart_i[0]) fdc_int_sync <= 2'b11;
        else fdc_int_sync <= {fdc_int_sync[0], fdc_int_ni};
    end
    fdc2214_bus_client #(
        .CLK_HZ(CLK_HZ), .POWERUP_CYCLES(FDC_POWERUP_CYCLES), .POLL_CYCLES(FDC_POLL_CYCLES),
        .DATA_TIMEOUT_CYCLES(FDC_DATA_TIMEOUT_CYCLES), .I2C_ADDR(FDC_I2C_ADDR),
        .RCOUNT_CH0(FDC_RCOUNT_CH0), .RCOUNT_CH1(FDC_RCOUNT_CH1),
        .SETTLECOUNT_CH0(FDC_SETTLECOUNT_CH0), .SETTLECOUNT_CH1(FDC_SETTLECOUNT_CH1),
        .CLOCK_DIVIDERS_CH0(FDC_CLOCK_DIVIDERS_CH0), .CLOCK_DIVIDERS_CH1(FDC_CLOCK_DIVIDERS_CH1),
        .IDRIVE_CH0(FDC_IDRIVE_CH0), .IDRIVE_CH1(FDC_IDRIVE_CH1),
        .USE_EXTERNAL_CLOCK(FDC_USE_EXTERNAL_CLOCK),
        .LOW_POWER_ACTIVATION(FDC_LOW_POWER_ACTIVATION), .MAX_BYTES(MAX_BYTES), .LEN_W(LEN_W)
    ) fdc (
        .clk_i(clk_i), .rst_ni(rst_ni), .restart_i(restart_i[0]),
        .req_valid_o(req_valid[0]), .req_ready_i(req_ready[0]), .req_address_o(req_address[0]),
        .req_tx_len_o(tx_len[0]), .req_rx_len_o(rx_len[0]), .req_tx_data_o(tx_data[0]),
        .rsp_valid_i(rsp_valid[0]), .rsp_ready_o(rsp_ready[0]), .rsp_rx_data_i(rx_data[0]),
        .rsp_error_i(rsp_error[0]), .fdc_sd_o(fdc_sd_o), .configured_o(configured_o[0]),
        .fault_o(fault_o[0]), .fault_code_o(fault_code_o[0]), .raw_ch0_o(fdc_raw_ch0_o),
        .raw_ch1_o(fdc_raw_ch1_o), .flags_ch0_o(fdc_flags_ch0_o), .flags_ch1_o(fdc_flags_ch1_o),
        .status_o(fdc_status_o), .sample_valid_o(fdc_sample_valid_o),
        .sample_error_o(fdc_sample_error_o), .sample_count_o(fdc_sample_count_o)
    );
    for (genvar channel = 0; channel < 2; channel++) begin : laser
        vl53l0x_device #(
            .CLK_HZ(CLK_HZ), .MAX_BYTES(MAX_BYTES), .LEN_W(LEN_W), .I2C_ADDR(7'h29),
            .RESET_CYCLES(TOF_RESET_CYCLES), .BOOT_CYCLES(TOF_BOOT_CYCLES),
            .POLL_CYCLES(TOF_POLL_CYCLES), .READY_TIMEOUT_CYCLES(TOF_READY_TIMEOUT_CYCLES)
        ) device (
            .clk_i(clk_i), .rst_ni(rst_ni), .restart_i(restart_i[channel+1]), .int_ni(tof_int_n[channel]),
            .xshut_o(xshut_run[channel]), .int_active_o(tof_int_active_o[channel]),
            .configured_o(configured_o[channel+1]), .fault_o(fault_o[channel+1]),
            .fault_code_o(fault_code_o[channel+1]), .distance_mm_o(tof_distance_mm_o[channel]),
            .range_status_o(tof_range_status_o[channel]), .sample_valid_o(tof_sample_valid_o[channel]),
            .sample_error_o(tof_sample_error_o[channel]), .sample_count_o(tof_sample_count_o[channel]),
            .req_valid_o(req_valid[channel+1]), .req_ready_i(req_ready[channel+1]),
            .req_address_o(req_address[channel+1]), .req_tx_len_o(tx_len[channel+1]),
            .req_rx_len_o(rx_len[channel+1]), .req_tx_data_o(tx_data[channel+1]),
            .rsp_valid_i(rsp_valid[channel+1]), .rsp_ready_o(rsp_ready[channel+1]),
            .rsp_rx_data_i(rx_data[channel+1]), .rsp_error_i(rsp_error[channel+1])
        );
    end
    i2c_shared_bus #(
        .CLK_HZ(CLK_HZ), .I2C_HZ(I2C_HZ), .TRANSFER_TIMEOUT_CYCLES(TRANSFER_TIMEOUT_CYCLES),
        .CLIENTS(3), .BUSES(3), .MAX_BYTES(MAX_BYTES), .LEN_W(LEN_W)
    ) bus (
        .clk_i(clk_i), .rst_ni(rst_ni), .s_req_valid_i(req_valid), .s_req_ready_o(req_ready),
        .s_req_bus_i(req_bus), .s_req_address_i(req_address), .s_req_tx_len_i(tx_len),
        .s_req_rx_len_i(rx_len), .s_req_tx_data_i(tx_data), .s_rsp_valid_o(rsp_valid),
        .s_rsp_ready_i(rsp_ready), .s_rsp_rx_data_o(rx_data), .s_rsp_error_o(rsp_error),
        .scl_i(scl_in), .sda_i(sda_in), .scl_drive_low_o(scl_low), .sda_drive_low_o(sda_low),
        .bus_active_o(bus_active_o), .bus_select_o(bus_select_o), .engine_busy_o(unused_engine_busy)
    );
endmodule
