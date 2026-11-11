`timescale 1ns/1ps

// Reusable composition: N device clients, one byte engine, B isolated buses.
module i2c_shared_bus #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 100_000,
    parameter integer TRANSFER_TIMEOUT_CYCLES = CLK_HZ / 50,
    parameter integer CLIENTS = 3,
    parameter integer BUSES = 3,
    parameter integer MAX_BYTES = 32,
    parameter integer LEN_W = $clog2(MAX_BYTES+1),
    parameter integer BUS_W = BUSES <= 1 ? 1 : $clog2(BUSES)
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic [CLIENTS-1:0] s_req_valid_i,
    output logic [CLIENTS-1:0] s_req_ready_o,
    input  logic [CLIENTS-1:0][BUS_W-1:0] s_req_bus_i,
    input  logic [CLIENTS-1:0][6:0] s_req_address_i,
    input  logic [CLIENTS-1:0][LEN_W-1:0] s_req_tx_len_i,
    input  logic [CLIENTS-1:0][LEN_W-1:0] s_req_rx_len_i,
    input  logic [CLIENTS-1:0][8*MAX_BYTES-1:0] s_req_tx_data_i,
    output logic [CLIENTS-1:0] s_rsp_valid_o,
    input  logic [CLIENTS-1:0] s_rsp_ready_i,
    output logic [CLIENTS-1:0][8*MAX_BYTES-1:0] s_rsp_rx_data_o,
    output logic [CLIENTS-1:0][1:0] s_rsp_error_o,
    input  logic [BUSES-1:0] scl_i,
    input  logic [BUSES-1:0] sda_i,
    output logic [BUSES-1:0] scl_drive_low_o,
    output logic [BUSES-1:0] sda_drive_low_o,
    output logic bus_active_o,
    output logic [BUS_W-1:0] bus_select_o,
    output logic engine_busy_o
);
    logic req_valid, req_ready, rsp_valid, rsp_ready;
    logic [6:0] address;
    logic [LEN_W-1:0] tx_len, rx_len;
    logic [8*MAX_BYTES-1:0] tx_data, rx_data;
    logic [1:0] error;
    logic engine_scl, engine_sda, engine_scl_low, engine_sda_low;
    i2c_transaction_scheduler #(
        .CLIENTS(CLIENTS), .BUSES(BUSES), .MAX_BYTES(MAX_BYTES), .LEN_W(LEN_W), .BUS_W(BUS_W)
    ) scheduler (
        .clk_i(clk_i), .rst_ni(rst_ni),
        .s_req_valid_i(s_req_valid_i), .s_req_ready_o(s_req_ready_o), .s_req_bus_i(s_req_bus_i),
        .s_req_address_i(s_req_address_i), .s_req_tx_len_i(s_req_tx_len_i),
        .s_req_rx_len_i(s_req_rx_len_i), .s_req_tx_data_i(s_req_tx_data_i),
        .s_rsp_valid_o(s_rsp_valid_o), .s_rsp_ready_i(s_rsp_ready_i),
        .s_rsp_rx_data_o(s_rsp_rx_data_o), .s_rsp_error_o(s_rsp_error_o),
        .m_req_valid_o(req_valid), .m_req_ready_i(req_ready), .m_req_address_o(address),
        .m_req_tx_len_o(tx_len), .m_req_rx_len_o(rx_len), .m_req_tx_data_o(tx_data),
        .m_rsp_valid_i(rsp_valid), .m_rsp_ready_o(rsp_ready), .m_rsp_rx_data_i(rx_data),
        .m_rsp_error_i(error), .bus_active_o(bus_active_o), .bus_select_o(bus_select_o)
    );
    i2c_transaction_master #(
        .CLK_HZ(CLK_HZ), .I2C_HZ(I2C_HZ), .TRANSFER_TIMEOUT_CYCLES(TRANSFER_TIMEOUT_CYCLES),
        .MAX_BYTES(MAX_BYTES), .LEN_W(LEN_W)
    ) engine (
        .clk_i(clk_i), .rst_ni(rst_ni), .req_valid_i(req_valid), .req_ready_o(req_ready),
        .req_address_i(address), .req_tx_len_i(tx_len), .req_rx_len_i(rx_len), .req_tx_data_i(tx_data),
        .rsp_valid_o(rsp_valid), .rsp_ready_i(rsp_ready), .rsp_rx_data_o(rx_data), .rsp_error_o(error),
        .scl_i(engine_scl), .sda_i(engine_sda), .scl_drive_low_o(engine_scl_low),
        .sda_drive_low_o(engine_sda_low), .busy_o(engine_busy_o)
    );
    i2c_bus_mux #(.BUSES(BUSES), .BUS_W(BUS_W)) pins (
        .enable_i(bus_active_o), .select_i(bus_select_o),
        .master_scl_low_i(engine_scl_low), .master_sda_low_i(engine_sda_low),
        .scl_i(scl_i), .sda_i(sda_i), .master_scl_o(engine_scl), .master_sda_o(engine_sda),
        .scl_low_o(scl_drive_low_o), .sda_low_o(sda_drive_low_o)
    );
endmodule
