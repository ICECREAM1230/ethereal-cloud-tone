`timescale 1ns/1ps

// Non-preemptive round-robin arbitration for one transaction master. Each
// client owns a separate response mailbox, so one stalled consumer cannot hold
// the physical bus or prevent other clients from completing transactions.
module i2c_transaction_scheduler #(
    parameter integer CLIENTS = 3,
    parameter integer BUSES = 3,
    parameter integer MAX_BYTES = 32,
    parameter integer LEN_W = $clog2(MAX_BYTES + 1),
    parameter integer BUS_W = (BUSES <= 1) ? 1 : $clog2(BUSES)
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
    output logic m_req_valid_o,
    input  logic m_req_ready_i,
    output logic [6:0] m_req_address_o,
    output logic [LEN_W-1:0] m_req_tx_len_o,
    output logic [LEN_W-1:0] m_req_rx_len_o,
    output logic [8*MAX_BYTES-1:0] m_req_tx_data_o,
    input  logic m_rsp_valid_i,
    output logic m_rsp_ready_o,
    input  logic [8*MAX_BYTES-1:0] m_rsp_rx_data_i,
    input  logic [1:0] m_rsp_error_i,
    output logic bus_active_o,
    output logic [BUS_W-1:0] bus_select_o
);
    localparam integer CLIENT_W = (CLIENTS <= 1) ? 1 : $clog2(CLIENTS);
    typedef enum logic [1:0] {IDLE, ISSUE, WAIT_RESPONSE} state_t;
    state_t state_q;
    logic [CLIENT_W-1:0] next_client_q, owner_q;
    logic [BUS_W-1:0] bus_q;
    logic [6:0] address_q;
    logic [LEN_W-1:0] tx_len_q, rx_len_q;
    logic [8*MAX_BYTES-1:0] tx_data_q;
    logic [CLIENTS-1:0] mailbox_valid_q;
    logic [CLIENTS-1:0][8*MAX_BYTES-1:0] mailbox_rx_q;
    logic [CLIENTS-1:0][1:0] mailbox_error_q;
    logic grant_valid;
    logic [CLIENT_W-1:0] grant_client;

    // The scan wraps once because offset is at most CLIENTS-1. Mailboxes that
    // are consumed this cycle become eligible on the following cycle.
    always_comb begin
        integer candidate;
        candidate = 0;
        grant_valid = 1'b0;
        grant_client = '0;
        s_req_ready_o = '0;
        for (integer offset = 0; offset < CLIENTS; offset = offset + 1) begin
            candidate = int'(next_client_q) + offset;
            if (candidate >= CLIENTS) candidate = candidate - CLIENTS;
            if (!grant_valid && s_req_valid_i[candidate] && !mailbox_valid_q[candidate]) begin
                grant_valid = 1'b1;
                grant_client = CLIENT_W'(candidate);
            end
        end
        if (rst_ni && state_q == IDLE && grant_valid) s_req_ready_o[grant_client] = 1'b1;
    end

    assign m_req_valid_o = rst_ni && state_q == ISSUE;
    assign m_req_address_o = address_q;
    assign m_req_tx_len_o = tx_len_q;
    assign m_req_rx_len_o = rx_len_q;
    assign m_req_tx_data_o = tx_data_q;
    assign m_rsp_ready_o = rst_ni && state_q == WAIT_RESPONSE;
    assign s_rsp_valid_o = mailbox_valid_q & {CLIENTS{rst_ni}};
    assign s_rsp_rx_data_o = mailbox_rx_q;
    assign s_rsp_error_o = mailbox_error_q;
    // Retain the selected channel through the entire master transaction,
    // including STOP/error recovery; only the complete response ends ownership.
    assign bus_active_o = rst_ni && (state_q == ISSUE || state_q == WAIT_RESPONSE);
    assign bus_select_o = bus_active_o ? bus_q : '0;

    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            state_q <= IDLE;
            next_client_q <= '0;
            owner_q <= '0;
            bus_q <= '0;
            address_q <= '0;
            tx_len_q <= '0;
            rx_len_q <= '0;
            tx_data_q <= '0;
            mailbox_valid_q <= '0;
            mailbox_rx_q <= '0;
            mailbox_error_q <= '0;
        end else begin
            for (integer i = 0; i < CLIENTS; i = i + 1) begin
                if (mailbox_valid_q[i] && s_rsp_ready_i[i]) mailbox_valid_q[i] <= 1'b0;
            end
            case (state_q)
                IDLE: begin
                    if (grant_valid) begin
                        if (grant_client == CLIENT_W'(CLIENTS - 1)) next_client_q <= '0;
                        else next_client_q <= grant_client + CLIENT_W'(1);
                        if (int'(s_req_bus_i[grant_client]) >= BUSES) begin
                            mailbox_valid_q[grant_client] <= 1'b1;
                            mailbox_rx_q[grant_client] <= '0;
                            mailbox_error_q[grant_client] <= 2'd3;
                        end else begin
                            owner_q <= grant_client;
                            bus_q <= s_req_bus_i[grant_client];
                            address_q <= s_req_address_i[grant_client];
                            tx_len_q <= s_req_tx_len_i[grant_client];
                            rx_len_q <= s_req_rx_len_i[grant_client];
                            tx_data_q <= s_req_tx_data_i[grant_client];
                            state_q <= ISSUE;
                        end
                    end
                end
                ISSUE: begin
                    if (m_req_ready_i) state_q <= WAIT_RESPONSE;
                end
                WAIT_RESPONSE: begin
                    if (m_rsp_valid_i) begin
                        mailbox_valid_q[owner_q] <= 1'b1;
                        mailbox_rx_q[owner_q] <= m_rsp_rx_data_i;
                        mailbox_error_q[owner_q] <= m_rsp_error_i;
                        state_q <= IDLE;
                    end
                end
                default: state_q <= IDLE;
            endcase
        end
    end
endmodule
