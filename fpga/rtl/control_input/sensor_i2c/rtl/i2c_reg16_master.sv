`timescale 1ns/1ps

// Single-master, one request in flight. 16-bit values are transferred MSB first.
// The upstream byte/bit engine is vendored unmodified under vendor/verilog-i2c.
module i2c_reg16_master #(
    parameter integer CLK_HZ = 50_000_000,
    parameter integer I2C_HZ = 100_000,
    parameter integer TRANSFER_TIMEOUT_CYCLES = CLK_HZ / 50,
    parameter logic [6:0] I2C_ADDR = 7'h2a
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic req_valid_i,
    output logic req_ready_o,
    input  logic req_read_i,
    input  logic [7:0] req_register_i,
    input  logic [15:0] req_wdata_i,
    output logic rsp_valid_o,
    output logic [15:0] rsp_rdata_o,
    output logic [1:0] rsp_error_o, // 0=OK, 1=NACK, 2=timeout
    input  logic scl_i,
    input  logic sda_i,
    output logic scl_drive_low_o,
    output logic sda_drive_low_o
);
    // Round up so the requested bus frequency is an upper bound.
    localparam integer PRESCALE = (CLK_HZ + 4*I2C_HZ - 1) / (4*I2C_HZ);
    localparam logic [15:0] PRESCALE_VALUE = 16'(PRESCALE);
    localparam integer RECOVERY_HOLD_CYCLES = 2*PRESCALE + 4;

    typedef enum logic [4:0] {
        IDLE, WRITE_COMMAND, WRITE_BYTES, POINTER_COMMAND, POINTER_BYTE,
        READ_HIGH_COMMAND, READ_HIGH_DATA, READ_LOW_COMMAND, READ_LOW_DATA,
        WAIT_STOP, RECOVER_LOW, RECOVER_RISE, RECOVER_HIGH, RECOVER_IDLE,
        RESPONSE
    } state_t;
    state_t state;
    logic [7:0] register_q;
    logic [15:0] write_data_q;
    logic [1:0] byte_index;
    integer unsigned transaction_cycles;
    integer unsigned recovery_cycles;

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

    logic cmd_start, cmd_read, cmd_write, cmd_multiple, cmd_stop;
    logic cmd_valid, cmd_ready;
    logic [7:0] tx_data, rx_data;
    logic tx_valid, tx_ready, tx_last, rx_valid;
    logic master_scl_release, master_sda_release;
    logic master_busy, master_bus_control, master_missed_ack;
    logic unused_rx_last, unused_scl_t, unused_sda_t, unused_bus_active;
    logic recovering, master_reset;

    assign recovering = (state == RECOVER_LOW) || (state == RECOVER_RISE)
                      || (state == RECOVER_HIGH) || (state == RECOVER_IDLE);
    assign master_reset = !rst_ni || recovering;
    assign req_ready_o = rst_ni && (state == IDLE);

    i2c_master byte_master (
        .clk(clk_i), .rst(master_reset),
        .s_axis_cmd_address(I2C_ADDR), .s_axis_cmd_start(cmd_start),
        .s_axis_cmd_read(cmd_read), .s_axis_cmd_write(cmd_write),
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
        cmd_write = 1'b0;
        cmd_multiple = 1'b0;
        cmd_stop = 1'b0;
        cmd_valid = 1'b0;
        tx_valid = 1'b0;
        tx_data = register_q;
        tx_last = 1'b0;
        case (state)
            WRITE_COMMAND: begin
                cmd_start = 1'b1;
                cmd_multiple = 1'b1;
                cmd_stop = 1'b1;
                cmd_valid = 1'b1;
            end
            WRITE_BYTES: begin
                tx_valid = 1'b1;
                case (byte_index)
                    2'd0: tx_data = register_q;
                    2'd1: tx_data = write_data_q[15:8];
                    default: tx_data = write_data_q[7:0];
                endcase
                tx_last = (byte_index == 2'd2);
            end
            POINTER_COMMAND: begin
                cmd_start = 1'b1;
                cmd_write = 1'b1;
                cmd_valid = 1'b1;
            end
            POINTER_BYTE: begin
                tx_valid = 1'b1;
                tx_last = 1'b1;
            end
            READ_HIGH_COMMAND: begin
                cmd_read = 1'b1;
                cmd_valid = 1'b1;
            end
            READ_LOW_COMMAND: begin
                cmd_read = 1'b1;
                cmd_stop = 1'b1;
                cmd_valid = 1'b1;
            end
            default: begin end
        endcase
        if (!rst_ni || recovering) begin
            cmd_valid = 1'b0;
            tx_valid = 1'b0;
        end
    end

    // Open-drain intent: 1 means actively pull the wire low, never drive high.
    // A bounded STOP attempt prevents an interrupted transfer being left open.
    always_comb begin
        scl_drive_low_o = !master_scl_release;
        sda_drive_low_o = !master_sda_release;
        case (state)
            RECOVER_LOW: begin scl_drive_low_o = 1'b1; sda_drive_low_o = 1'b1; end
            RECOVER_RISE, RECOVER_HIGH: begin
                scl_drive_low_o = 1'b0;
                sda_drive_low_o = 1'b1;
            end
            RECOVER_IDLE, RESPONSE: begin
                scl_drive_low_o = 1'b0;
                sda_drive_low_o = 1'b0;
            end
            default: begin end
        endcase
        if (!rst_ni) begin
            scl_drive_low_o = 1'b0;
            sda_drive_low_o = 1'b0;
        end
    end

    always_ff @(posedge clk_i) begin
        if (!rst_ni) begin
            state <= IDLE;
            register_q <= 8'h00;
            write_data_q <= 16'h0000;
            byte_index <= 2'd0;
            transaction_cycles <= 0;
            recovery_cycles <= 0;
            rsp_valid_o <= 1'b0;
            rsp_rdata_o <= 16'h0000;
            rsp_error_o <= 2'd0;
        end else begin
            rsp_valid_o <= 1'b0;
            if (state != IDLE && state != RESPONSE && !recovering)
                transaction_cycles <= transaction_cycles + 1;

            // missed_ack is a pulse in the upstream core; latch it immediately.
            if (state != IDLE && state != RESPONSE && !recovering
                && master_missed_ack) begin
                rsp_error_o <= 2'd1;
                recovery_cycles <= 0;
                state <= RECOVER_LOW;
            end else if (state != IDLE && state != RESPONSE && !recovering
                         && transaction_cycles >= TRANSFER_TIMEOUT_CYCLES-1) begin
                rsp_error_o <= 2'd2;
                recovery_cycles <= 0;
                state <= RECOVER_LOW;
            end else begin
                case (state)
                    IDLE: begin
                        if (req_valid_i) begin
                            register_q <= req_register_i;
                            write_data_q <= req_wdata_i;
                            rsp_rdata_o <= 16'h0000;
                            rsp_error_o <= 2'd0;
                            byte_index <= 2'd0;
                            transaction_cycles <= 0;
                            state <= req_read_i ? POINTER_COMMAND : WRITE_COMMAND;
                        end
                    end
                    WRITE_COMMAND: if (cmd_ready) state <= WRITE_BYTES;
                    WRITE_BYTES: begin
                        if (tx_ready) begin
                            if (byte_index == 2'd2) state <= WAIT_STOP;
                            else byte_index <= byte_index + 1'b1;
                        end
                    end
                    POINTER_COMMAND: if (cmd_ready) state <= POINTER_BYTE;
                    POINTER_BYTE: if (tx_ready) state <= READ_HIGH_COMMAND;
                    READ_HIGH_COMMAND: if (cmd_ready) state <= READ_HIGH_DATA;
                    READ_HIGH_DATA: begin
                        if (rx_valid) begin
                            rsp_rdata_o[15:8] <= rx_data;
                            state <= READ_LOW_COMMAND;
                        end
                    end
                    READ_LOW_COMMAND: if (cmd_ready) state <= READ_LOW_DATA;
                    READ_LOW_DATA: begin
                        if (rx_valid) begin
                            rsp_rdata_o[7:0] <= rx_data;
                            state <= WAIT_STOP;
                        end
                    end
                    WAIT_STOP: begin
                        if (!master_busy && !master_bus_control) state <= RESPONSE;
                    end
                    RECOVER_LOW: begin
                        recovery_cycles <= recovery_cycles + 1;
                        if (recovery_cycles >= RECOVERY_HOLD_CYCLES-1) begin
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
                        if (recovery_cycles >= RECOVERY_HOLD_CYCLES-1) begin
                            recovery_cycles <= 0;
                            state <= RECOVER_IDLE;
                        end
                    end
                    RECOVER_IDLE: begin
                        recovery_cycles <= recovery_cycles + 1;
                        if (recovery_cycles >= RECOVERY_HOLD_CYCLES-1) state <= RESPONSE;
                    end
                    RESPONSE: begin
                        rsp_valid_o <= 1'b1;
                        state <= IDLE;
                    end
                    default: state <= IDLE;
                endcase
            end
        end
    end

`ifndef SYNTHESIS
    initial begin
        if (CLK_HZ <= 0 || I2C_HZ < 10_000 || I2C_HZ > 400_000)
            $fatal(1, "I2C frequency must be within FDC2214's 10-400kHz range");
        if (PRESCALE < 4 || PRESCALE > 65535)
            $fatal(1, "I2C prescale out of range (need >=4 for synchronized pins)");
        if (TRANSFER_TIMEOUT_CYCLES < 1)
            $fatal(1, "TRANSFER_TIMEOUT_CYCLES must be positive");
    end
`endif
endmodule
