`timescale 1ns/1ps

// FDC register protocol -> shared byte-packet transactions. No I2C engine here.
module fdc2214_bus_client #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer POWERUP_CYCLES = CLK_HZ / 500,
    parameter integer POLL_CYCLES = CLK_HZ / 10_000,
    parameter integer DATA_TIMEOUT_CYCLES = CLK_HZ / 10,
    parameter logic [6:0] I2C_ADDR = 7'h2a,
    parameter integer MAX_BYTES = 32,
    parameter integer LEN_W = $clog2(MAX_BYTES+1),
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
    output logic [6:0] req_address_o,
    output logic [LEN_W-1:0] req_tx_len_o,
    output logic [LEN_W-1:0] req_rx_len_o,
    output logic [8*MAX_BYTES-1:0] req_tx_data_o,
    input  logic rsp_valid_i,
    output logic rsp_ready_o,
    input  logic [8*MAX_BYTES-1:0] rsp_rx_data_i,
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
    output logic sample_valid_o,
    output logic sample_error_o,
    output logic [31:0] sample_count_o
);
    logic reg_valid, reg_ready, reg_read;
    logic [7:0] reg_address;
    logic [15:0] reg_wdata;
    logic pending, discard_response;
    logic unused_rsp_bytes;
    assign unused_rsp_bytes = ^rsp_rx_data_i[8*MAX_BYTES-1:16];
    assign req_valid_o = rst_ni && !restart_i && !pending && reg_valid;
    assign reg_ready = rst_ni && !restart_i && !pending && req_ready_i;
    assign req_address_o = I2C_ADDR;
    assign rsp_ready_o = 1'b1;
    always_comb begin
        req_tx_len_o = reg_read ? LEN_W'(1) : LEN_W'(3);
        req_rx_len_o = reg_read ? LEN_W'(2) : '0;
        req_tx_data_o = '0;
        req_tx_data_o[7:0] = reg_address;
        if (!reg_read) begin
            req_tx_data_o[15:8] = reg_wdata[15:8];
            req_tx_data_o[23:16] = reg_wdata[7:0];
        end
    end
    initial begin
        if (MAX_BYTES < 3) $fatal(1, "FDC packet client needs MAX_BYTES >= 3");
        if (I2C_ADDR != 7'h2a && I2C_ADDR != 7'h2b)
            $fatal(1, "FDC ADDR supports only 0x2A or 0x2B");
    end
    // Restart cannot reset a shared bus. Fence any old response until drained.
    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            pending <= 1'b0;
            discard_response <= 1'b0;
        end else begin
            if (req_valid_o && req_ready_i) begin
                pending <= 1'b1;
                discard_response <= 1'b0;
            end
            if (restart_i && pending) discard_response <= 1'b1;
            if (pending && rsp_valid_i) begin
                pending <= 1'b0;
                discard_response <= 1'b0;
            end
        end
    end
    fdc2214_device #(
        .CLK_HZ(CLK_HZ), .POWERUP_CYCLES(POWERUP_CYCLES),
        .POLL_CYCLES(POLL_CYCLES), .DATA_TIMEOUT_CYCLES(DATA_TIMEOUT_CYCLES),
        .RCOUNT_CH0(RCOUNT_CH0), .RCOUNT_CH1(RCOUNT_CH1),
        .SETTLECOUNT_CH0(SETTLECOUNT_CH0), .SETTLECOUNT_CH1(SETTLECOUNT_CH1),
        .CLOCK_DIVIDERS_CH0(CLOCK_DIVIDERS_CH0), .CLOCK_DIVIDERS_CH1(CLOCK_DIVIDERS_CH1),
        .IDRIVE_CH0(IDRIVE_CH0), .IDRIVE_CH1(IDRIVE_CH1),
        .USE_EXTERNAL_CLOCK(USE_EXTERNAL_CLOCK), .LOW_POWER_ACTIVATION(LOW_POWER_ACTIVATION)
    ) device (
        // Keep the device in shutdown until the old bus transaction drains.
        // Only then release SD and start the full power-up wait again.
        .clk_i(clk_i), .rst_ni(rst_ni), .restart_i(restart_i || (pending && discard_response)),
        .req_valid_o(reg_valid), .req_ready_i(reg_ready), .req_read_o(reg_read),
        .req_register_o(reg_address), .req_wdata_o(reg_wdata),
        .rsp_valid_i(pending && rsp_valid_i && !discard_response && !restart_i),
        .rsp_rdata_i({rsp_rx_data_i[7:0], rsp_rx_data_i[15:8]}), .rsp_error_i(rsp_error_i),
        .fdc_sd_o(fdc_sd_o), .configured_o(configured_o), .fault_o(fault_o),
        .fault_code_o(fault_code_o), .raw_ch0_o(raw_ch0_o), .raw_ch1_o(raw_ch1_o),
        .flags_ch0_o(flags_ch0_o), .flags_ch1_o(flags_ch1_o), .status_o(status_o),
        .sample_valid_o(sample_valid_o), .sample_error_o(sample_error_o), .sample_count_o(sample_count_o)
    );
endmodule
