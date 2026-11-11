// USB full-speed single-transaction engine, 48 MHz, 2026-10-16.
// Original implementation for this project. CRC polynomials are specified
// by USB 2.0 sections 8.3.5 and 8.3.5.1; no upstream SIE code is copied.
//
// req_ready reserves the complete internal 64-byte receive buffer. The
// requester must likewise reserve space before submitting an IN request.
// For IN, req_len is the endpoint/status maximum (0 allows only a ZLP).
// This UTMI adapter uses line_state (including transmitted SE0/J) to observe
// the actual end of TX, since the supplied PHY has no TX-active port.
module fs_packet_engine #(
    parameter integer RESPONSE_TIMEOUT_CLKS = 80,  // >=18 FS bits + input latency
    parameter integer PACKET_WATCHDOG_CLKS = 4096
) (
    input  wire         clk,
    input  wire         rst,
    input  wire         req_valid,
    output wire         req_ready,
    input  wire [7:0]   req_pid,
    input  wire [6:0]   req_addr,
    input  wire [3:0]   req_ep,
    input  wire         req_data1,
    input  wire [6:0]   req_len,
    input  wire [511:0] req_payload,
    output reg          done,
    output reg [2:0]    result,
    output reg [6:0]    rx_len,
    output reg [511:0]  rx_payload,
    output reg [7:0]    utmi_tx_data,
    output reg          utmi_tx_valid,
    input  wire         utmi_tx_ready,
    input  wire [7:0]   utmi_rx_data,
    input  wire         utmi_rx_valid,
    input  wire         utmi_rx_active,
    input  wire         utmi_rx_error,
    input  wire [1:0]   utmi_line_state
);
    localparam [7:0] PID_OUT = 8'he1, PID_IN = 8'h69,
                     PID_SETUP = 8'h2d, PID_SOF = 8'ha5,
                     PID_DATA0 = 8'hc3, PID_DATA1 = 8'h4b,
                     PID_ACK = 8'hd2, PID_NAK = 8'h5a, PID_STALL = 8'h1e;
    localparam [2:0] RES_OK = 0, RES_NAK = 1, RES_STALL = 2,
                     RES_TIMEOUT = 3, RES_BAD = 4, RES_DUP = 5;
    localparam [3:0] S_IDLE = 0, S_TOKEN = 1, S_TX_DATA = 2,
                     S_ACK = 3, S_TX_END = 4, S_GAP = 5,
                     S_RESPONSE = 6, S_RX = 7, S_ACK_GAP = 8,
                     S_COMPLETE = 9;
    localparam [1:0] AFTER_RESPONSE = 0, AFTER_DATA = 1, AFTER_DONE = 2;
    reg [3:0] state;
    reg [1:0] after_tx;
    reg [7:0] pid_q;
    reg data1_q;
    reg [6:0] len_q;
    reg [511:0] payload_q;
    reg [15:0] token_q;
    reg [6:0] tx_index;
    reg [15:0] tx_crc;
    reg [15:0] watchdog;
    reg [15:0] response_timer;
    reg [3:0] gap_timer;
    reg eop_se0_seen;
    reg response_started;
    reg [7:0] packet_pid;
    reg [6:0] byte_count;
    reg [15:0] rx_crc;
    reg packet_bad;
    reg [511:0] packet_payload;
    reg [6:0] packet_len;
    reg [2:0] pending_result;

    function automatic [4:0] crc5(input [10:0] token_bits);
        reg [4:0] c;
        reg f;
        integer k;
        begin
            c = 5'h1f;
            for (k = 0; k < 11; k = k + 1) begin
                f = c[0] ^ token_bits[k];
                c = c >> 1;
                if (f) c = c ^ 5'h14;
            end
            crc5 = ~c;
        end
    endfunction

    function automatic [15:0] crc16_byte(input [15:0] c_in, input [7:0] d);
        reg [15:0] c;
        reg f;
        integer k;
        begin
            c = c_in;
            for (k = 0; k < 8; k = k + 1) begin
                f = c[0] ^ d[k];
                c = c >> 1;
                if (f) c = c ^ 16'ha001;
            end
            crc16_byte = c;
        end
    endfunction

    assign req_ready = !rst && (state == S_IDLE) && !utmi_rx_active &&
                       (utmi_line_state == 2'b01);

    always @* begin
        utmi_tx_valid = 1'b0;
        utmi_tx_data = 8'b0;
        case (state)
            S_TOKEN: begin
                utmi_tx_valid = 1'b1;
                case (tx_index)
                    0: utmi_tx_data = pid_q;
                    1: utmi_tx_data = token_q[7:0];
                    default: utmi_tx_data = token_q[15:8];
                endcase
            end
            S_TX_DATA: begin
                utmi_tx_valid = 1'b1;
                if (tx_index == 0)
                    utmi_tx_data = data1_q ? PID_DATA1 : PID_DATA0;
                else if (tx_index <= len_q)
                    utmi_tx_data = payload_q[(tx_index-1'b1)*8 +: 8];
                else if (tx_index == len_q + 7'd1)
                    utmi_tx_data = ~tx_crc[7:0];
                else
                    utmi_tx_data = ~tx_crc[15:8];
            end
            S_ACK: begin
                utmi_tx_valid = 1'b1;
                utmi_tx_data = PID_ACK;
            end
            default: begin end
        endcase
        if (rst) utmi_tx_valid = 1'b0;
    end

    always @(posedge clk) begin
        if (rst) begin
            state <= S_IDLE;
            after_tx <= AFTER_DONE;
            pid_q <= 0;
            data1_q <= 0;
            len_q <= 0;
            payload_q <= 0;
            token_q <= 0;
            tx_index <= 0;
            tx_crc <= 16'hffff;
            watchdog <= 0;
            response_timer <= 0;
            gap_timer <= 0;
            eop_se0_seen <= 0;
            response_started <= 0;
            packet_pid <= 0;
            byte_count <= 0;
            rx_crc <= 16'hffff;
            packet_bad <= 0;
            packet_payload <= 0;
            packet_len <= 0;
            pending_result <= RES_OK;
            done <= 0;
            result <= RES_OK;
            rx_len <= 0;
            rx_payload <= 0;
        end else begin
            done <= 1'b0;
            case (state)
                S_IDLE: begin
                    watchdog <= 0;
                    if (req_valid && req_ready) begin
                        pid_q <= req_pid;
                        data1_q <= req_data1;
                        len_q <= req_len;
                        payload_q <= req_payload;
                        token_q <= {crc5({req_ep, req_addr}), req_ep, req_addr};
                        tx_index <= 0;
                        tx_crc <= 16'hffff;
                        packet_payload <= 0;
                        packet_len <= 0;
                        rx_len <= 0;
                        rx_payload <= 0;
                        pending_result <= RES_OK;
                        if (((req_pid != PID_IN) && (req_pid != PID_OUT) &&
                             (req_pid != PID_SETUP) && (req_pid != PID_SOF)) ||
                            (req_len > 64)) begin
                            pending_result <= RES_BAD;
                            state <= S_COMPLETE;
                        end else state <= S_TOKEN;
                    end
                end
                S_TOKEN, S_TX_DATA, S_ACK: begin
                    // Covers a stalled TX-ready interface as well as babble.
                    watchdog <= watchdog + 1'b1;
                    if (watchdog == 16'(PACKET_WATCHDOG_CLKS-1)) begin
                        pending_result <= RES_BAD;
                        state <= S_COMPLETE;
                    end else if (utmi_tx_ready) begin
                        if (state == S_TOKEN) begin
                            if (tx_index == 2) begin
                                after_tx <= (pid_q == PID_SOF) ? AFTER_DONE :
                                            (pid_q == PID_IN) ? AFTER_RESPONSE : AFTER_DATA;
                                state <= S_TX_END;
                                eop_se0_seen <= 0;
                            end else tx_index <= tx_index + 1'b1;
                        end else if (state == S_ACK) begin
                            after_tx <= AFTER_DONE;
                            state <= S_TX_END;
                            eop_se0_seen <= 0;
                        end else begin
                            if ((tx_index != 0) && (tx_index <= len_q))
                                tx_crc <= crc16_byte(tx_crc, utmi_tx_data);
                            if (tx_index == len_q + 7'd2) begin
                                after_tx <= AFTER_RESPONSE;
                                state <= S_TX_END;
                                eop_se0_seen <= 0;
                            end else tx_index <= tx_index + 1'b1;
                        end
                    end
                end
                S_TX_END: begin
                    watchdog <= watchdog + 1'b1;
                    if (utmi_line_state == 2'b00) eop_se0_seen <= 1'b1;
                    if (eop_se0_seen && (utmi_line_state == 2'b01)) begin
                        watchdog <= 0;
                        if (after_tx == AFTER_RESPONSE) begin
                            state <= S_RESPONSE;
                            response_timer <= 0;
                            response_started <= 0;
                            packet_bad <= 0;
                        end else begin
                            state <= S_GAP;
                            // EOP J is one bit; a new packet must wait >=2 bits.
                            gap_timer <= (after_tx == AFTER_DATA) ? 4'd7 : 4'd3;
                        end
                    end else if (watchdog == 16'(PACKET_WATCHDOG_CLKS-1)) begin
                        pending_result <= RES_BAD;
                        state <= S_COMPLETE;
                    end
                end
                S_GAP: begin
                    if (gap_timer != 0) gap_timer <= gap_timer - 1'b1;
                    else if (after_tx == AFTER_DATA) begin
                        state <= S_TX_DATA;
                        tx_index <= 0;
                    end else state <= S_COMPLETE;
                end
                S_RESPONSE: begin
                    response_timer <= response_timer + 1'b1;
                    // SOP precedes RX-active by the SYNC field. Stop the USB
                    // response timer on observed K, not on the first byte.
                    if (utmi_line_state == 2'b10) response_started <= 1'b1;
                    if (utmi_rx_error) packet_bad <= 1'b1;
                    if (utmi_rx_active) begin
                        state <= S_RX;
                        watchdog <= 0;
                        byte_count <= utmi_rx_valid ? 7'd1 : 7'd0;
                        if (utmi_rx_valid) packet_pid <= utmi_rx_data;
                        rx_crc <= 16'hffff;
                    end else if (response_timer == 16'(RESPONSE_TIMEOUT_CLKS-1) &&
                                 !response_started && utmi_line_state != 2'b10) begin
                        pending_result <= (packet_bad || utmi_rx_error) ? RES_BAD : RES_TIMEOUT;
                        state <= S_COMPLETE;
                    end else if (response_timer == 16'(RESPONSE_TIMEOUT_CLKS+64-1)) begin
                        // A malformed SYNC cannot hold the transaction forever.
                        pending_result <= RES_BAD;
                        state <= S_COMPLETE;
                    end
                end
                S_RX: begin
                    watchdog <= watchdog + 1'b1;
                    if (utmi_rx_error) packet_bad <= 1'b1;
                    if (utmi_rx_valid) begin
                        if (byte_count == 0) packet_pid <= utmi_rx_data;
                        else begin
                            rx_crc <= crc16_byte(rx_crc, utmi_rx_data);
                            if (byte_count <= 64)
                                packet_payload[(byte_count-1'b1)*8 +: 8] <= utmi_rx_data;
                        end
                        if (byte_count != 127) byte_count <= byte_count + 1'b1;
                        if (byte_count >= 67) packet_bad <= 1'b1;
                    end
                    if (!utmi_rx_active) begin
                        if (packet_bad || utmi_rx_error || utmi_rx_valid ||
                            (packet_pid[7:4] != ~packet_pid[3:0])) begin
                            pending_result <= RES_BAD;
                            state <= S_COMPLETE;
                        end else if (byte_count == 1 && packet_pid == PID_NAK) begin
                            pending_result <= RES_NAK;
                            state <= S_COMPLETE;
                        end else if (byte_count == 1 && packet_pid == PID_STALL) begin
                            pending_result <= RES_STALL;
                            state <= S_COMPLETE;
                        end else if (pid_q != PID_IN) begin
                            pending_result <= (byte_count == 1 && packet_pid == PID_ACK) ? RES_OK : RES_BAD;
                            state <= S_COMPLETE;
                        end else if ((packet_pid == PID_DATA0 || packet_pid == PID_DATA1) &&
                                     byte_count >= 3 && byte_count <= 67 &&
                                     byte_count <= len_q + 7'd3 &&
                                     rx_crc == 16'hb001) begin
                            packet_len <= byte_count - 7'd3;
                            pending_result <= ((packet_pid == PID_DATA1) == data1_q) ? RES_OK : RES_DUP;
                            // All bytes, CRC and PHY EOP have now been checked.
                            // The PHY's RX-active includes EOP validation.
                            state <= S_ACK_GAP;
                            gap_timer <= 0;
                            watchdog <= 0;
                        end else begin
                            pending_result <= RES_BAD;
                            state <= S_COMPLETE;
                        end
                    end else if (watchdog == 16'(PACKET_WATCHDOG_CLKS-1)) begin
                        pending_result <= RES_BAD;
                        state <= S_COMPLETE;
                    end
                end
                S_ACK_GAP: begin
                    // One cycle after RX completion is sufficient with this
                    // PHY's synchronized input/EOP latency; wire tests verify
                    // the full-speed 2..7.5 bit response window.
                    state <= S_ACK;
                end
                S_COMPLETE: begin
                    result <= pending_result;
                    if (pid_q == PID_IN && pending_result == RES_OK) begin
                        rx_len <= packet_len;
                        rx_payload <= packet_payload;
                    end
                    done <= 1'b1;
                    state <= S_IDLE;
                end
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule
