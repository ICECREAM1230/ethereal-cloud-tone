`timescale 1ns/1ps

// One complete packet at a time over the unmodified vendor byte/bit engine.
// Byte zero occupies [7:0]; the packet adapter never reverses wire bytes.
module i2c_transaction_master #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 100_000,
    parameter integer TRANSFER_TIMEOUT_CYCLES = CLK_HZ / 50,
    parameter integer MAX_BYTES = 32,
    parameter integer LEN_W = $clog2(MAX_BYTES + 1)
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic req_valid_i,
    output logic req_ready_o,
    input  logic [6:0] req_address_i,
    input  logic [LEN_W-1:0] req_tx_len_i,
    input  logic [LEN_W-1:0] req_rx_len_i,
    input  logic [8*MAX_BYTES-1:0] req_tx_data_i,
    output logic rsp_valid_o,
    input  logic rsp_ready_i,
    output logic [8*MAX_BYTES-1:0] rsp_rx_data_o,
    output logic [1:0] rsp_error_o, // 0=OK, 1=NACK, 2=timeout, 3=bad descriptor
    input  logic scl_i,
    input  logic sda_i,
    output logic scl_drive_low_o,
    output logic sda_drive_low_o,
    output logic busy_o
);
    // Round upward: I2C_HZ is an upper bound on the generated bus clock.
    localparam integer PRESCALE = (CLK_HZ + 4*I2C_HZ - 1) / (4*I2C_HZ);
    localparam logic [15:0] PRESCALE_VALUE = 16'(PRESCALE);
    localparam logic [LEN_W-1:0] MAX_LENGTH = LEN_W'(MAX_BYTES);
    localparam integer HOLD_CYCLES = 2*PRESCALE + 4;

    typedef enum logic [3:0] {
        IDLE, BUS_FREE, WRITE_COMMAND, WRITE_BYTES, READ_COMMAND, READ_BYTE,
        WAIT_STOP, RECOVER_LOW, RECOVER_RISE, RECOVER_HIGH, RECOVER_IDLE,
        RESPONSE
    } state_t;
    state_t state;
    logic [6:0] address_q;
    logic [LEN_W-1:0] tx_len_q, rx_len_q, byte_index;
    logic [8*MAX_BYTES-1:0] tx_data_q;
    integer unsigned transaction_cycles, free_cycles, recovery_cycles;

    // Inputs belong to the selected physical bus and may have just switched.
    (* ASYNC_REG = "TRUE" *) logic [1:0] scl_sync, sda_sync;
    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            scl_sync <= 2'b11;
            sda_sync <= 2'b11;
        end else begin
            scl_sync <= {scl_sync[0], scl_i};
            sda_sync <= {sda_sync[0], sda_i};
        end
    end

    logic cmd_start, cmd_read, cmd_multiple, cmd_stop, cmd_valid, cmd_ready;
    logic [7:0] tx_data, rx_data;
    logic tx_valid, tx_ready, tx_last, rx_valid;
    logic master_scl_release, master_sda_release;
    logic master_busy, master_bus_control, master_missed_ack;
    logic unused_rx_last, unused_scl_t, unused_sda_t, unused_bus_active;
    logic recovering, executing, master_reset;
    logic length_invalid;

    // When every length encoding fits (e.g. MAX_BYTES=1, LEN_W=1), there is
    // no out-of-range encoding to compare. Keep that legal parameter case
    // free of constant-comparison lint without weakening the default bounds.
    generate
        if (MAX_LENGTH == {LEN_W{1'b1}}) begin : full_length_range
            assign length_invalid = 1'b0;
        end else begin : bounded_length_range
            assign length_invalid = (req_tx_len_i > MAX_LENGTH) || (req_rx_len_i > MAX_LENGTH);
        end
    endgenerate

    assign recovering = (state == RECOVER_LOW) || (state == RECOVER_RISE)
                      || (state == RECOVER_HIGH) || (state == RECOVER_IDLE);
    assign executing = (state != IDLE) && (state != RESPONSE) && !recovering;
    // Reset the byte engine while changing channels / checking bus idle so its
    // edge tracker cannot retain activity detected on the previous channel.
    assign master_reset = !rst_ni || recovering || (state == IDLE)
                        || (state == BUS_FREE) || (state == RESPONSE);
    assign req_ready_o = rst_ni && (state == IDLE);
    assign rsp_valid_o = rst_ni && (state == RESPONSE);
    assign busy_o = rst_ni && (state != IDLE);

    i2c_master byte_master (
        .clk(clk_i), .rst(master_reset),
        .s_axis_cmd_address(address_q), .s_axis_cmd_start(cmd_start),
        .s_axis_cmd_read(cmd_read), .s_axis_cmd_write(1'b0),
        .s_axis_cmd_write_multiple(cmd_multiple), .s_axis_cmd_stop(cmd_stop),
        .s_axis_cmd_valid(cmd_valid), .s_axis_cmd_ready(cmd_ready),
        .s_axis_data_tdata(tx_data), .s_axis_data_tvalid(tx_valid),
        .s_axis_data_tready(tx_ready), .s_axis_data_tlast(tx_last),
        .m_axis_data_tdata(rx_data), .m_axis_data_tvalid(rx_valid),
        .m_axis_data_tready(1'b1), .m_axis_data_tlast(unused_rx_last),
        .scl_i(scl_sync[1]), .scl_o(master_scl_release), .scl_t(unused_scl_t),
        .sda_i(sda_sync[1]), .sda_o(master_sda_release), .sda_t(unused_sda_t),
        .busy(master_busy), .bus_control(master_bus_control), .bus_active(unused_bus_active),
        .missed_ack(master_missed_ack),
        .prescale(PRESCALE_VALUE), .stop_on_idle(1'b0)
    );

    always_comb begin
        cmd_start = 1'b0;
        cmd_read = 1'b0;
        cmd_multiple = 1'b0;
        cmd_stop = 1'b0;
        cmd_valid = 1'b0;
        tx_data = tx_data_q[8*int'(byte_index) +: 8];
        tx_valid = 1'b0;
        tx_last = (byte_index == tx_len_q - 1'b1);
        case (state)
            WRITE_COMMAND: begin
                cmd_start = 1'b1;
                cmd_multiple = 1'b1;
                cmd_stop = (rx_len_q == 0);
                cmd_valid = 1'b1;
            end
            WRITE_BYTES: tx_valid = 1'b1;
            READ_COMMAND: begin
                // Only the first read byte emits an address. For a combined
                // packet this forces the required repeated START after TX.
                cmd_start = (byte_index == 0);
                cmd_read = 1'b1;
                cmd_stop = (byte_index == rx_len_q - 1'b1);
                cmd_valid = 1'b1;
            end
            default: ;
        endcase
        if (master_reset) begin
            cmd_valid = 1'b0;
            tx_valid = 1'b0;
        end
    end

    // A recovery sequence attempts STOP even when the byte engine was stuck.
    // Each phase is bounded, and both intents are released before RESPONSE,
    // including when a slave continues to hold either physical wire low.
    always_comb begin
        scl_drive_low_o = !master_scl_release;
        sda_drive_low_o = !master_sda_release;
        case (state)
            RECOVER_LOW: begin
                scl_drive_low_o = 1'b1;
                sda_drive_low_o = 1'b1;
            end
            RECOVER_RISE, RECOVER_HIGH: begin
                scl_drive_low_o = 1'b0;
                sda_drive_low_o = 1'b1;
            end
            IDLE, BUS_FREE, RECOVER_IDLE, RESPONSE: begin
                scl_drive_low_o = 1'b0;
                sda_drive_low_o = 1'b0;
            end
            default: ;
        endcase
        if (!rst_ni) begin
            scl_drive_low_o = 1'b0;
            sda_drive_low_o = 1'b0;
        end
    end

    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            state <= IDLE;
            address_q <= 0;
            tx_len_q <= 0;
            rx_len_q <= 0;
            tx_data_q <= 0;
            byte_index <= 0;
            transaction_cycles <= 0;
            free_cycles <= 0;
            recovery_cycles <= 0;
            rsp_rx_data_o <= 0;
            rsp_error_o <= 0;
        end else begin
            if (executing) transaction_cycles <= transaction_cycles + 1;

            // The vendor core pulses missed_ack; do not wait for a command or
            // byte handshake before capturing the error and stopping traffic.
            if (executing && master_missed_ack) begin
                rsp_error_o <= 2'd1;
                rsp_rx_data_o <= 0;
                recovery_cycles <= 0;
                state <= RECOVER_LOW;
            end else if (executing && transaction_cycles >= TRANSFER_TIMEOUT_CYCLES-1) begin
                rsp_error_o <= 2'd2;
                rsp_rx_data_o <= 0;
                recovery_cycles <= 0;
                state <= RECOVER_LOW;
            end else begin
                case (state)
                    IDLE: begin
                        if (req_valid_i) begin
                            address_q <= req_address_i;
                            tx_len_q <= req_tx_len_i;
                            rx_len_q <= req_rx_len_i;
                            tx_data_q <= req_tx_data_i;
                            byte_index <= 0;
                            transaction_cycles <= 0;
                            free_cycles <= 0;
                            rsp_rx_data_o <= 0;
                            rsp_error_o <= 0;
                            if ((req_tx_len_i == 0 && req_rx_len_i == 0)
                                || length_invalid) begin
                                rsp_error_o <= 2'd3;
                                state <= RESPONSE;
                            end else state <= BUS_FREE;
                        end
                    end
                    BUS_FREE: begin
                        if (scl_sync[1] && sda_sync[1]) begin
                            free_cycles <= free_cycles + 1;
                            if (free_cycles >= HOLD_CYCLES-1)
                                state <= tx_len_q == 0 ? READ_COMMAND : WRITE_COMMAND;
                        end else free_cycles <= 0;
                    end
                    WRITE_COMMAND: if (cmd_ready) state <= WRITE_BYTES;
                    WRITE_BYTES: begin
                        if (tx_ready) begin
                            if (byte_index == tx_len_q - 1'b1) begin
                                byte_index <= 0;
                                state <= rx_len_q == 0 ? WAIT_STOP : READ_COMMAND;
                            end else byte_index <= byte_index + 1'b1;
                        end
                    end
                    READ_COMMAND: if (cmd_ready) state <= READ_BYTE;
                    READ_BYTE: begin
                        if (rx_valid) begin
                            rsp_rx_data_o[8*int'(byte_index) +: 8] <= rx_data;
                            if (byte_index == rx_len_q - 1'b1) state <= WAIT_STOP;
                            else begin
                                byte_index <= byte_index + 1'b1;
                                state <= READ_COMMAND;
                            end
                        end
                    end
                    WAIT_STOP: begin
                        if (!master_busy && !master_bus_control && scl_sync[1] && sda_sync[1])
                            state <= RESPONSE;
                    end
                    RECOVER_LOW: begin
                        recovery_cycles <= recovery_cycles + 1;
                        if (recovery_cycles >= HOLD_CYCLES-1) begin
                            recovery_cycles <= 0;
                            state <= RECOVER_RISE;
                        end
                    end
                    RECOVER_RISE: begin
                        recovery_cycles <= recovery_cycles + 1;
                        if (scl_sync[1]) begin
                            recovery_cycles <= 0;
                            state <= RECOVER_HIGH;
                        end else if (recovery_cycles >= TRANSFER_TIMEOUT_CYCLES-1) begin
                            recovery_cycles <= 0;
                            state <= RECOVER_IDLE;
                        end
                    end
                    RECOVER_HIGH: begin
                        recovery_cycles <= recovery_cycles + 1;
                        if (recovery_cycles >= HOLD_CYCLES-1) begin
                            recovery_cycles <= 0;
                            state <= RECOVER_IDLE;
                        end
                    end
                    RECOVER_IDLE: begin
                        recovery_cycles <= recovery_cycles + 1;
                        if (recovery_cycles >= HOLD_CYCLES-1) state <= RESPONSE;
                    end
                    RESPONSE: if (rsp_ready_i) state <= IDLE;
                    default: state <= IDLE;
                endcase
            end
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (CLK_HZ <= 0 || I2C_HZ <= 0)
            $fatal(1, "Clock and I2C frequencies must be positive");
        if (PRESCALE < 4 || PRESCALE > 65535)
            $fatal(1, "I2C prescale out of range (need >=4 for synchronized pins)");
        if (TRANSFER_TIMEOUT_CYCLES < 1)
            $fatal(1, "TRANSFER_TIMEOUT_CYCLES must be positive");
        if (MAX_BYTES < 1 || LEN_W < $clog2(MAX_BYTES + 1))
            $fatal(1, "MAX_BYTES/LEN_W cannot represent every permitted packet length");
    end
`endif
endmodule
